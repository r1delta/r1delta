#include "titan_insignia.h"
#include "titan_insignia_image.h"
#include "titan_insignia_wire.h"

#include "core.h"
#include "cvar.h"
#include "load.h"
#include "logging.h"
#include "persistentdata.h"
#include "client/rendering/titan_insignia_render.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Wire = TitanInsignia::Wire;

namespace {

constexpr char kSelectionConVar[] = "delta_titan_insignia";
constexpr char kShowConVar[] = "delta_titan_insignias_show";
constexpr char kFolderName[] = "insignias";
constexpr std::size_t kMaxFileNameLength = 128;
// Partly received insignias a server can make a client hold at once.
constexpr std::size_t kMaxIncoming = 64;

void ReportWarning(int& budget, const char* format, ...)
{
	if (budget <= 0)
		return;
	--budget;
	char text[512];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
	va_end(args);
	Warning("%s", text);
	OutputDebugStringA(text);
}

// --- Client ------------------------------------------------------------------

std::filesystem::path Folder()
{
	std::error_code error;
	return std::filesystem::current_path(error) / "r1" / kFolderName;
}

std::filesystem::path PathFromUtf8(std::string_view text)
{
	return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string Utf8FromPath(const std::filesystem::path& path)
{
	const std::u8string text = path.u8string();
	return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

bool IsSupportedFileName(std::string_view name)
{
	if (name.empty() || name.size() > kMaxFileNameLength || name.front() == '.'
		|| name.find_first_of("/\\:*?\"<>|") != std::string_view::npos)
		return false;
	const std::size_t dot = name.rfind('.');
	if (dot == std::string_view::npos)
		return false;
	std::string extension(name.substr(dot + 1));
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	static constexpr std::string_view kExtensions[] = { "png", "jpg", "jpeg", "tga", "bmp", "gif", "psd" };
	return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions);
}

struct EncodedFile {
	std::uintmax_t size = 0;
	std::filesystem::file_time_type modified{};
	TitanInsignia::TopMip mip;
	std::string id;
	std::string error;
};

struct Incoming {
	std::vector<std::string> chunks;
	std::size_t received = 0;
};

struct ClientState {
	ConVarR1* marker = nullptr;
	ConVarR1* selection = nullptr;
	ConVarR1* show = nullptr;
	std::uint64_t instance = 0;

	bool applied = false;
	std::string appliedSelection;
	std::string error;
	// The error last printed, so a broken selection warns once and not on every menu refresh.
	std::string warnedError;
	std::string ownId;
	std::string ownPacked;

	// Encoded images of the insignia folder by file name, reused while a file is unchanged.
	std::map<std::string, EncodedFile> files;
	std::unordered_map<std::string, Incoming> incoming;
};
ClientState g_client;
int g_clientWarnings = 32;

std::uint64_t DrawInstance()
{
	std::uint64_t value = 0;
	if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
		value = (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32) ^ GetTickCount64();
	return value;
}

// The encoded image of one file of the insignia folder; `error` is set when it is not usable.
const EncodedFile& EncodeFile(const std::string& name)
{
	EncodedFile& entry = g_client.files[name];
	const std::filesystem::path path = Folder() / PathFromUtf8(name);
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	const std::filesystem::file_time_type modified = error ? std::filesystem::file_time_type{}
		: std::filesystem::last_write_time(path, error);
	if (error) {
		entry = {};
		entry.error = "the file cannot be read";
		return entry;
	}
	if ((!entry.id.empty() || !entry.error.empty()) && entry.size == size && entry.modified == modified)
		return entry;

	entry = {};
	entry.size = size;
	entry.modified = modified;
	if (size > TitanInsignia::kMaxSourceFileBytes) {
		entry.error = "the file is larger than 16 MB";
		return entry;
	}
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
	std::ifstream stream(path, std::ios::binary);
	if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
		entry.error = "the file cannot be read";
		return entry;
	}
	if (!TitanInsignia::EncodeImageFile(bytes, entry.mip, entry.error))
		return entry;
	entry.id = TitanInsignia::ComputeId(entry.mip);
	if (entry.id.empty())
		entry.error = "the image could not be hashed";
	return entry;
}

void PublishMarker()
{
	if (!g_client.marker)
		return;
	Wire::Marker marker;
	marker.instance = g_client.instance;
	marker.id = g_client.ownId;
	const std::string value = Wire::FormatMarker(marker);
	// A userinfo change sends a NET_SetConVar, and with it the insignia.
	const char* current = g_client.marker->m_Value.m_pszString;
	if (!current || value != current)
		SetConvarStringOriginal(g_client.marker, value.c_str());
}

void ApplySelection(const std::string& name)
{
	g_client.applied = true;
	g_client.appliedSelection = name;
	g_client.error.clear();
	g_client.ownId.clear();
	g_client.ownPacked.clear();
	if (!name.empty()) {
		const EncodedFile* encoded = nullptr;
		if (!IsSupportedFileName(name))
			g_client.error = "not a supported image file name";
		else {
			encoded = &EncodeFile(name);
			g_client.error = encoded->error;
		}
		std::string packed;
		if (g_client.error.empty() && !Wire::Pack(encoded->mip, packed))
			g_client.error = "the image could not be packed";
		if (g_client.error.empty()) {
			g_client.ownId = encoded->id;
			g_client.ownPacked = std::move(packed);
			TitanInsigniaRender::Add(encoded->id, encoded->mip, true);
		}
	}
	if (!g_client.error.empty() && g_client.error != g_client.warnedError)
		Warning("R1Delta: custom Titan insignia %s: %s\n", name.c_str(), g_client.error.c_str());
	g_client.warnedError = g_client.error;
	PublishMarker();
}

// Encodes the selected file again when the selection changed; `force` also re-reads an unchanged
// selection, for a file that was added or edited since.
void ApplyCurrentSelection(bool force)
{
	if (!g_client.selection)
		return;
	const char* current = g_client.selection->m_Value.m_pszString ? g_client.selection->m_Value.m_pszString : "";
	if (force || !g_client.applied || g_client.appliedSelection != current)
		ApplySelection(current);
}

void ReceiveData(const CCommand& args)
{
	const std::string id = args.Arg(2);
	const long part = strtol(args.Arg(3), nullptr, 10);
	const long parts = strtol(args.Arg(4), nullptr, 10);
	const std::string_view chunk = args.Arg(5);
	if (!TitanInsignia::IsValidId(id) || parts < 1 || static_cast<std::size_t>(parts) > Wire::kMaxParts
		|| part < 0 || part >= parts || chunk.empty() || chunk.size() > Wire::kCommandChunkChars) {
		ReportWarning(g_clientWarnings, "R1Delta: the server sent a malformed custom Titan insignia part\n");
		return;
	}
	if (TitanInsigniaRender::Has(id))
		return;
	if (!g_client.incoming.contains(id) && g_client.incoming.size() >= kMaxIncoming)
		g_client.incoming.clear();
	Incoming& incoming = g_client.incoming[id];
	if (incoming.chunks.size() != static_cast<std::size_t>(parts)) {
		incoming.chunks.assign(static_cast<std::size_t>(parts), {});
		incoming.received = 0;
	}
	std::string& slot = incoming.chunks[static_cast<std::size_t>(part)];
	if (slot.empty())
		++incoming.received;
	slot.assign(chunk);
	if (incoming.received != incoming.chunks.size())
		return;

	std::string packed;
	for (const std::string& piece : incoming.chunks)
		packed += piece;
	g_client.incoming.erase(id);
	TitanInsignia::TopMip mip;
	if (!Wire::Unpack(packed, mip) || TitanInsignia::ComputeId(mip) != id) {
		ReportWarning(g_clientWarnings, "R1Delta: the server sent custom Titan insignia %s with data that does not match it\n", id.c_str());
		return;
	}
	TitanInsigniaRender::Add(id, mip, false);
}

void ReceiveAssignment(const CCommand& args)
{
	const long index = strtol(args.Arg(2), nullptr, 10);
	const std::string id = args.Arg(3);
	if (index < 1 || index > TitanInsigniaRender::kMaxPlayers
		|| (id != Wire::kNoInsignia && !TitanInsignia::IsValidId(id))) {
		ReportWarning(g_clientWarnings, "R1Delta: the server sent a malformed custom Titan insignia assignment\n");
		return;
	}
	TitanInsigniaRender::Assign(static_cast<int>(index), id == Wire::kNoInsignia ? std::string() : id);
}

void InsigniaCommand(const CCommand& args)
{
	if (args.ArgC() < 2)
		return;
	const char* verb = args.Arg(1);
	if (strcmp(verb, "data") == 0 && args.ArgC() == 6)
		ReceiveData(args);
	else if (strcmp(verb, "set") == 0 && args.ArgC() == 4)
		ReceiveAssignment(args);
	else
		ReportWarning(g_clientWarnings, "R1Delta: the server sent an unknown custom Titan insignia command\n");
}

}

// --- Client ------------------------------------------------------------------

void TitanInsignia_RegisterClient()
{
	g_client.instance = DrawInstance();
	// FCVAR_PERSIST puts the marker into the signon NET_SetConVar (see SoundScriptParity_RegisterClient).
	g_client.marker = RegisterConVar(Wire::kMarkerConVar, "", FCVAR_USERINFO | FCVAR_PERSIST | FCVAR_DONTRECORD,
		"R1Delta custom Titan insignia marker sent to servers");
	g_client.selection = RegisterConVar(kSelectionConVar, "", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE,
		"Image file in r1/insignias drawn on your Titans in place of the loadout's insignia (empty: none)");
	g_client.show = RegisterConVar(kShowConVar, "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE,
		"Draw other players' custom Titan insignias (0: only your own)");
	RegisterConCommand(Wire::kCommand, InsigniaCommand,
		"Server message in the R1Delta custom Titan insignia exchange", FCVAR_SERVER_CAN_EXECUTE);
}

void TitanInsignia_ClientConnecting()
{
	ApplyCurrentSelection(true);
	TitanInsigniaRender::ClearAssignments();
}

void TitanInsignia_ClientWriteUpload(NET_SetConVar* message, bool signon)
{
	// The server names every insignia again after a signon. Parts already received stay: the server may
	// have finished sending that insignia before the signon.
	if (signon)
		TitanInsigniaRender::ClearAssignments();
	const char* markerValue = nullptr;
	for (int i = 0; i < message->m_ConVars.Count(); ++i) {
		if (strcmp(message->m_ConVars[i].name, Wire::kMarkerConVar) == 0)
			markerValue = message->m_ConVars[i].value;
	}
	Wire::Marker marker;
	if (!markerValue || !Wire::ParseMarker(markerValue, marker) || marker.id.empty())
		return;
	// A marker for an insignia that is no longer selected; the current one follows in its own message.
	if (marker.id != g_client.ownId)
		return;
	for (std::size_t offset = 0; offset < g_client.ownPacked.size(); offset += Wire::kUploadChunkChars) {
		NetMessageCvar_t entry = {};
		strcpy_s(entry.name, sizeof(entry.name), Wire::kUploadChunkConVar);
		const std::string chunk = g_client.ownPacked.substr(offset, Wire::kUploadChunkChars);
		strcpy_s(entry.value, sizeof(entry.value), chunk.c_str());
		message->m_ConVars.AddToTail(entry);
	}
}

void TitanInsignia_ClientFrame()
{
	if (!g_client.selection)
		return;
	// client.dll's static initializers (which build its material proxy dictionary) have run by the
	// first host frame; its DLL load notification comes before them.
	static bool installAttempted = false;
	if (!installAttempted) {
		installAttempted = true;
		TitanInsigniaRender::Install(G_client);
	}
	ApplyCurrentSelection(false);
	TitanInsigniaRender::Frame(!g_client.show || g_client.show->m_Value.m_nValue != 0);
}

SQInteger Script_TitanInsigniaGetFiles(HSQUIRRELVM v)
{
	std::vector<std::string> names;
	std::error_code error;
	// Folder reads fail quietly: a file that cannot be listed or named in UTF-8 is not offered.
	std::filesystem::directory_iterator item(Folder(), error);
	for (; !error && item != std::filesystem::directory_iterator(); item.increment(error)) {
		std::error_code entryError;
		if (!item->is_regular_file(entryError))
			continue;
		std::string name;
		try {
			name = Utf8FromPath(item->path().filename());
		}
		catch (const std::exception&) {
			continue;
		}
		if (IsSupportedFileName(name))
			names.push_back(name);
	}
	std::sort(names.begin(), names.end());

	sq_newarray(v, 0);
	for (const std::string& name : names) {
		const EncodedFile& encoded = EncodeFile(name);
		std::string image;
		if (encoded.error.empty()) {
			TitanInsigniaRender::Add(encoded.id, encoded.mip, true);
			image = TitanInsigniaRender::MenuImage(encoded.id);
		}
		sq_newtable(v);
		sq_pushstring_lit(v, "file"); sq_pushstring_std(v, name); sq_newslot(v, -3, 0);
		sq_pushstring_lit(v, "image"); sq_pushstring_std(v, image); sq_newslot(v, -3, 0);
		sq_pushstring_lit(v, "error"); sq_pushstring_std(v, encoded.error); sq_newslot(v, -3, 0);
		sq_arrayappend(v, -2);
	}
	return 1;
}

SQInteger Script_TitanInsigniaGetSelected(HSQUIRRELVM v)
{
	ApplyCurrentSelection(true);
	const std::string image = g_client.ownId.empty() ? std::string() : TitanInsigniaRender::MenuImage(g_client.ownId);
	sq_newtable(v);
	sq_pushstring_lit(v, "file"); sq_pushstring_std(v, g_client.appliedSelection); sq_newslot(v, -3, 0);
	sq_pushstring_lit(v, "image"); sq_pushstring_std(v, image); sq_newslot(v, -3, 0);
	sq_pushstring_lit(v, "error"); sq_pushstring_std(v, g_client.error); sq_newslot(v, -3, 0);
	return 1;
}

SQInteger Script_TitanInsigniaSelect(HSQUIRRELVM v)
{
	const SQChar* name = nullptr;
	if (SQ_FAILED(sq_getstring(v, 2, &name)) || !name)
		return sq_throwerror(v, "Expected a file name string");
	if (*name && !IsSupportedFileName(name))
		return sq_throwerror(v, "Not a supported image file name in r1/insignias");
	if (!g_client.selection)
		return sq_throwerror(v, "Custom Titan insignias are not available");
	SetConvarStringOriginal(g_client.selection, name);
	ApplyCurrentSelection(true);
	sq_pushstring_std(v, g_client.error);
	return 1;
}

SQInteger Script_TitanInsigniaOpenFolder(HSQUIRRELVM v)
{
	const std::filesystem::path folder = Folder();
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	if (error)
		return sq_throwerror(v, "Cannot create r1/insignias");
	ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return 0;
}

