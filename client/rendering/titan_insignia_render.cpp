#include "titan_insignia_render.h"

#include "engine/core/ffa_targeting.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/logging/logging.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace TitanInsigniaRender {
namespace {

constexpr DWORD kClientTimeDateStamp = 0x55038DB8;
constexpr DWORD kClientImageSize = 0x39A4000;

// CMaterialProxyDict* GetMaterialProxyDict(): the function-local static every client.dll proxy
// registers with (CPlayerLogoProxy's initializer at +0x7AC460 shows the call shape).
constexpr std::uintptr_t kProxyDictRva = 0x1AE4F0;
constexpr unsigned char kProxyDictPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05, 0xFE, 0xA2, 0xDD, 0x00 };
// C_BasePlayer* UTIL_PlayerByIndex(int index): null outside 1..maxClients.
constexpr std::uintptr_t kPlayerByIndexRva = 0x280FE0;
constexpr unsigned char kPlayerByIndexPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0x83, 0xF9, 0x01, 0x7C, 0x30 };
// C_BasePlayer* GetLocalPlayer(int splitScreenSlot), -1 for the active slot.
constexpr std::uintptr_t kLocalPlayerRva = 0x7B1B0;
constexpr unsigned char kLocalPlayerPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0x83, 0xF9, 0xFF, 0x75, 0x6C };
// IMaterialSystem* materials.
constexpr std::uintptr_t kMaterialSystemRva = 0x380E688;

// CMaterialProxyDict vtable: [2] Add(const char* name, IMaterialProxy* (*factory)()).
constexpr std::size_t kProxyDictAdd = 2 * sizeof(void*);
// IMaterialSystem::FindMaterial(name, textureGroup, context, complain, complainPrefix).
constexpr std::size_t kMaterialsFindMaterial = 85 * sizeof(void*);
// IMaterial (materialsystem_dx11.dll CMaterial vtable at RVA 0x184DC8).
constexpr std::size_t kMaterialGetName = 0;
constexpr std::size_t kMaterialIsErrorMaterial = 28 * sizeof(void*);
constexpr std::size_t kMaterialFindVar = 54 * sizeof(void*);
constexpr std::size_t kMaterialGetTexture = 61 * sizeof(void*);
constexpr std::size_t kMaterialSetTexture = 73 * sizeof(void*);
constexpr std::size_t kMaterialReference = 115 * sizeof(void*);
// ITexture reference counting, as CPlayerLogoProxy::Init and Release call it (second argument 0).
constexpr std::size_t kTextureIncrementReference = 14 * sizeof(void*);
constexpr std::size_t kTextureDecrementReference = 15 * sizeof(void*);
// Insignias received from servers that the client keeps; its own and the menu's do not count.
constexpr std::size_t kMaxRemoteInsignias = 128;
// FindVar's second argument as client.dll's texture proxies pass it for $basetexture.
constexpr int kFindVarTexture = 8;
constexpr std::uint16_t kInvalidVar = 0xFFFF;
// IClientRenderable::GetIClientUnknown and IClientUnknown::GetBaseEntity, as CEntityMaterialProxy::OnBind
// (client.dll +0x22A1D0) resolves the entity it binds for.
constexpr std::size_t kRenderableGetUnknown = 0;
constexpr std::size_t kUnknownGetBaseEntity = 7 * sizeof(void*);

constexpr char kProxyName[] = "R1DeltaTitanInsignia";
constexpr std::string_view kFolder = "materials/models/r1delta/insignia/";
// Menu image panels name materials relative to materials/vgui, and the provider registry refuses paths
// with "..", so the menu material lives there.
constexpr std::string_view kMenuFolder = "materials/vgui/r1delta/insignia/";
constexpr char kMaterialFolder[] = "models/r1delta/insignia/";
constexpr char kMenuImageFolder[] = "r1delta/insignia/";

template <typename Function>
Function Virtual(void* object, std::size_t byteOffset)
{
	return reinterpret_cast<Function>((*reinterpret_cast<void***>(object))[byteOffset / sizeof(void*)]);
}

using PlayerByIndexFn = void*(__fastcall*)(int index);
using LocalPlayerFn = void*(__fastcall*)(int splitScreenSlot);
using FindMaterialFn = void*(__fastcall*)(void* materials, const char* name, const char* group, int context, char complain, const char* prefix);
using GetNameFn = const char*(__fastcall*)(void* material);
using IsErrorMaterialFn = bool(__fastcall*)(void* material);
using FindVarFn = std::uint16_t(__fastcall*)(void* material, const char* name, int type, int complain);
using GetTextureFn = void*(__fastcall*)(void* material, std::uint16_t var);
using SetTextureFn = void(__fastcall*)(void* material, std::uint16_t var, void* texture);
using ReferenceFn = void(__fastcall*)(void* material);
using GetPointerFn = void*(__fastcall*)(void* object);
using TextureReferenceFn = void(__fastcall*)(void* texture, int unused);

std::uintptr_t g_clientBase = 0;
PlayerByIndexFn g_playerByIndex = nullptr;
LocalPlayerFn g_localPlayer = nullptr;

using FileBytes = r2port::FileBytes;

FileBytes MakeBytes(const std::string& text)
{
	return std::make_shared<const std::vector<std::uint8_t>>(text.begin(), text.end());
}

struct Files {
	FileBytes vtf;
	FileBytes vmt;
	FileBytes menuVmt;
};

// Insignia files by id. The provider reads them from the filesystem's threads.
std::mutex g_filesMutex;
std::unordered_map<std::string, Files> g_files;

// Main thread only.
struct Material {
	void* material = nullptr;
	void* texture = nullptr;
	bool failed = false;
};
std::unordered_map<std::string, Material> g_materials;
std::size_t g_remoteInsignias = 0;
std::array<std::string, kMaxPlayers + 1> g_assigned;

// What OnBind reads, published by Frame(). Textures stay valid: their materials are referenced
// for the rest of the process.
std::array<std::atomic<void*>, kMaxPlayers + 1> g_players{};
std::array<std::atomic<void*>, kMaxPlayers + 1> g_textures{};

class InsigniaProvider final : public r2port::FileProvider {
public:
	bool MayContain(std::string_view path) const override
	{
		return path.starts_with(kFolder) || path.starts_with(kMenuFolder);
	}
	bool Exists(std::string_view path) override { return static_cast<bool>(Pick(path)); }
	std::optional<std::uint64_t> Size(std::string_view path) override
	{
		const FileBytes bytes = Pick(path);
		return bytes ? std::optional<std::uint64_t>(bytes->size()) : std::nullopt;
	}
	FileBytes Read(std::string_view path) override { return Pick(path); }

private:
	static FileBytes Pick(std::string_view path)
	{
		const bool menu = path.starts_with(kMenuFolder);
		if (!menu && !path.starts_with(kFolder))
			return {};
		std::string_view name = path.substr(menu ? kMenuFolder.size() : kFolder.size());
		const std::string_view id = name.substr(0, (std::min)(name.size(), TitanInsignia::kIdLength));
		const std::string_view suffix = name.substr(id.size());
		std::lock_guard<std::mutex> lock(g_filesMutex);
		const auto found = g_files.find(std::string(id));
		if (found == g_files.end())
			return {};
		if (menu)
			return suffix == ".vmt" ? found->second.menuVmt : FileBytes{};
		if (suffix == ".vtf")
			return found->second.vtf;
		if (suffix == ".vmt")
			return found->second.vmt;
		return {};
	}
};

// IMaterialProxy as client.dll's CMaterialProxyDict creates and the material system drives it.
class InsigniaProxy {
public:
	virtual bool Init(void* material, void* keyValues)
	{
		(void)keyValues;
		m_material = material;
		m_var = Virtual<FindVarFn>(material, kMaterialFindVar)(material, "$basetexture", kFindVarTexture, 0);
		return m_var != kInvalidVar;
	}

	virtual void OnBind(void* renderable)
	{
		if (!m_stock) {
			// Held while this proxy can put the texture back: setting a custom one may drop the material's
			// own reference to it.
			m_stock = Virtual<GetTextureFn>(m_material, kMaterialGetTexture)(m_material, m_var);
			if (m_stock)
				Virtual<TextureReferenceFn>(m_stock, kTextureIncrementReference)(m_stock, 0);
		}
		// Decals bind with a player index instead of a renderable (CPlayerLogoProxy::OnBind).
		const bool isRenderable = reinterpret_cast<std::uintptr_t>(renderable) > 0xFFFF;
		void* custom = isRenderable ? TextureFor(renderable) : nullptr;
		Virtual<SetTextureFn>(m_material, kMaterialSetTexture)(m_material, m_var, custom ? custom : m_stock);
	}

	virtual void Release() { delete this; }
	virtual void* GetMaterial() { return m_material; }
	virtual ~InsigniaProxy()
	{
		if (m_stock)
			Virtual<TextureReferenceFn>(m_stock, kTextureDecrementReference)(m_stock, 0);
	}

private:
	static void* TextureFor(void* renderable)
	{
		void* unknown = Virtual<GetPointerFn>(renderable, kRenderableGetUnknown)(renderable);
		void* entity = unknown ? Virtual<GetPointerFn>(unknown, kUnknownGetBaseEntity)(unknown) : nullptr;
		void* player = entity ? r1delta::ffa_targeting::ResolveClientPlayerOwner(entity) : nullptr;
		if (!player)
			return nullptr;
		for (int index = 1; index <= kMaxPlayers; ++index) {
			if (g_players[index].load(std::memory_order_acquire) == player)
				return g_textures[index].load(std::memory_order_acquire);
		}
		return nullptr;
	}

	void* m_material = nullptr;
	std::uint16_t m_var = kInvalidVar;
	// The texture the VMT names, captured before this proxy first changes it.
	void* m_stock = nullptr;
};

void* CreateProxy()
{
	return new InsigniaProxy();
}

bool IsRetailClient(std::uintptr_t base)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
	return nt->FileHeader.TimeDateStamp == kClientTimeDateStamp && nt->OptionalHeader.SizeOfImage == kClientImageSize;
}

template <std::size_t N>
bool Matches(std::uintptr_t base, std::uintptr_t rva, const unsigned char (&expected)[N], const char* what)
{
	if (std::memcmp(reinterpret_cast<const void*>(base + rva), expected, N) == 0)
		return true;
	Warning("R1Delta: custom Titan insignias unavailable: client.dll %s at +0x%llX does not match\n",
		what, static_cast<unsigned long long>(rva));
	return false;
}

void LoadMaterial(const std::string& id, Material& entry)
{
	void* materials = *reinterpret_cast<void**>(g_clientBase + kMaterialSystemRva);
	if (!materials)
		return;
	const std::string name = kMaterialFolder + id;
	void* material = Virtual<FindMaterialFn>(materials, kMaterialsFindMaterial)(materials, name.c_str(), nullptr, 10, 1, nullptr);
	if (!material || Virtual<IsErrorMaterialFn>(material, kMaterialIsErrorMaterial)(material)) {
		Warning("R1Delta: custom Titan insignia material %s did not load\n", name.c_str());
		entry.failed = true;
		return;
	}
	const std::uint16_t var = Virtual<FindVarFn>(material, kMaterialFindVar)(material, "$basetexture", kFindVarTexture, 0);
	void* texture = var != kInvalidVar ? Virtual<GetTextureFn>(material, kMaterialGetTexture)(material, var) : nullptr;
	if (!texture) {
		Warning("R1Delta: custom Titan insignia material %s has no texture\n", name.c_str());
		entry.failed = true;
		return;
	}
	Virtual<ReferenceFn>(material, kMaterialReference)(material);
	entry.material = material;
	entry.texture = texture;
}

std::string BuildVmt(const std::string& id)
{
	// The stock insignia materials' parameters.
	return "\"VertexLitGeneric\"\n{\n"
		"\t\"$decal\" \"1\"\n"
		"\t\"$decalscale\" \".25\"\n"
		"\t\"$surfaceprop\" \"metal\"\n"
		"\t\"$basetexture\" \"" + std::string(kMaterialFolder) + id + "\"\n"
		"\t\"$translucent\" \"1\"\n}\n";
}

std::string BuildMenuVmt(const std::string& id)
{
	return "\"Basic\"\n{\n"
		"\t\"$basetexture\" \"" + std::string(kMaterialFolder) + id + "\"\n"
		"\t\"$translucent\" \"1\"\n"
		"\t\"$vertexalpha\" \"1\"\n}\n";
}

}

bool Install(std::uintptr_t clientBase)
{
	if (g_clientBase)
		return true;
	if (!clientBase || !IsRetailClient(clientBase)) {
		Warning("R1Delta: custom Titan insignias unavailable: client.dll is not the R1 retail build\n");
		return false;
	}
	if (!Matches(clientBase, kProxyDictRva, kProxyDictPrologue, "material proxy dictionary")
		|| !Matches(clientBase, kPlayerByIndexRva, kPlayerByIndexPrologue, "UTIL_PlayerByIndex")
		|| !Matches(clientBase, kLocalPlayerRva, kLocalPlayerPrologue, "GetLocalPlayer"))
		return false;
	if (!r2port::RegisterProvider("materials", std::make_shared<InsigniaProvider>())) {
		Warning("R1Delta: custom Titan insignias unavailable: the file provider did not register\n");
		return false;
	}
	using GetDictFn = void*(__fastcall*)();
	using AddProxyFn = void(__fastcall*)(void* dict, const char* name, void* (*factory)());
	void* dict = reinterpret_cast<GetDictFn>(clientBase + kProxyDictRva)();
	Virtual<AddProxyFn>(dict, kProxyDictAdd)(dict, kProxyName, &CreateProxy);
	g_playerByIndex = reinterpret_cast<PlayerByIndexFn>(clientBase + kPlayerByIndexRva);
	g_localPlayer = reinterpret_cast<LocalPlayerFn>(clientBase + kLocalPlayerRva);
	g_clientBase = clientBase;
	return true;
}

bool IsInstalled()
{
	return g_clientBase != 0;
}

bool Add(const std::string& id, const TitanInsignia::TopMip& mip, bool local)
{
	if (!g_clientBase)
		return false;
	if (Has(id))
		return true;
	if (!local && g_remoteInsignias >= kMaxRemoteInsignias) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			Warning("R1Delta: this game received %zu custom Titan insignias; further ones show the stock insignia\n",
				kMaxRemoteInsignias);
		}
		return false;
	}
	std::vector<std::uint8_t> vtf = TitanInsignia::BuildVtf(mip);
	if (vtf.empty())
		return false;
	Files files{
		std::make_shared<const std::vector<std::uint8_t>>(std::move(vtf)),
		MakeBytes(BuildVmt(id)),
		MakeBytes(BuildMenuVmt(id)),
	};
	{
		std::lock_guard<std::mutex> lock(g_filesMutex);
		g_files.emplace(id, std::move(files));
	}
	g_materials.emplace(id, Material{});
	if (!local)
		++g_remoteInsignias;
	return true;
}

bool Has(const std::string& id)
{
	std::lock_guard<std::mutex> lock(g_filesMutex);
	return g_files.contains(id);
}

std::string MenuImage(const std::string& id)
{
	return kMenuImageFolder + id;
}

void Assign(int entityIndex, const std::string& id)
{
	if (entityIndex >= 1 && entityIndex <= kMaxPlayers)
		g_assigned[entityIndex] = id;
}

void ClearAssignments()
{
	for (std::string& id : g_assigned)
		id.clear();
}

void Frame(bool showOthers)
{
	if (!g_clientBase)
		return;
	for (auto& [id, entry] : g_materials) {
		if (!entry.material && !entry.failed)
			LoadMaterial(id, entry);
	}
	void* local = g_localPlayer(-1);
	for (int index = 1; index <= kMaxPlayers; ++index) {
		void* player = nullptr;
		void* texture = nullptr;
		if (!g_assigned[index].empty()) {
			player = g_playerByIndex(index);
			const auto found = g_materials.find(g_assigned[index]);
			if (player && found != g_materials.end() && (showOthers || player == local))
				texture = found->second.texture;
		}
		g_textures[index].store(texture, std::memory_order_release);
		g_players[index].store(player, std::memory_order_release);
	}
}

}
