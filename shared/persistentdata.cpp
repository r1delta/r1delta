// Persistent data ("pdata") for R1Delta.
//
// Ownership model
// ---------------
// * The CLIENT owns a player's persistent data. It keeps every value in an
//   in-memory store (ClientStore below), mirrors valid entries into "__ <key>"
//   FCVAR_USERINFO convars so the engine replicates them to servers, and writes
//   the store to its own file (profile/persistent_data.txt) atomically with a
//   checksum trailer and a .bak of the previous generation. The file is read
//   exactly once per process; nothing ever re-applies older on-disk values over
//   newer in-memory ones, so map changes and profile.cfg reloads cannot roll
//   progression back. profile.cfg still receives a copy of the data so older
//   builds keep working after a downgrade, but this build never reads it back
//   except to pick up changes an older build made.
//
// * SERVERS only write by sending "__ key value" string commands. Each write
//   stays pending (see persistentdata_state.h) until the client echoes it back,
//   which makes writes survive the reliable-stream clear that happens when the
//   engine reconnects clients on changelevel.
//
// * The wire format is unchanged from earlier builds (packed "_r1dp1" chunks or
//   legacy high-bit names) plus an optional "_r1dpfull" marker that tells the
//   server a message is a complete snapshot rather than a delta.

#include "core.h"
#include "filesystem.h"

class PDef;

#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include "bitbuf.h"
#include "cvar.h"
#include "persistentdata.h"
#include "persistentdata_codec.h"
#include "persistentdata_slots.h"
#include "persistentdata_state.h"
#include "persistentdata_store.h"
#include "logging.h"
#include "squirrel.h"
#include "keyvalues.h"
#include "factory.h"
#include "load.h"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <charconv>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <map>
#include <variant>
#include <optional>
#include <sstream>
#include <cctype>
#include <regex>
#include <limits>
#include <zstd.h>
#include "tctx.h"

namespace {
constexpr unsigned long long kSyntheticPlatformUserIdBase = 9000000000000000000ULL;
constexpr unsigned long long kSyntheticPlatformUserIdSpan = 10000000000000000ULL;
static_assert(
	kSyntheticPlatformUserIdBase + kSyntheticPlatformUserIdSpan - 1
		<= 9223372036854775807ULL);
}

unsigned long long GenerateSyntheticPlatformUserId()
{
	const unsigned long long processEntropy =
		(static_cast<unsigned long long>(GetCurrentProcessId()) << 32)
		^ GetTickCount64();
	return kSyntheticPlatformUserIdBase
		+ processEntropy % kSyntheticPlatformUserIdSpan;
}

// Constants
constexpr size_t MAX_LENGTH = 254;
constexpr char kPersistPrefix[] = PERSIST_COMMAND" ";
constexpr size_t kPersistPrefixLength = sizeof(kPersistPrefix) - 1;
// Marker entry a client appends when a NET_SetConVar carries its complete
// persistent data set. Names starting with '_' are stripped for vanilla
// servers, and older R1Delta servers ignore it as an unknown userinfo convar.
constexpr char kFullSnapshotMarker[] = "_r1dpfull";
bool g_bNoSendConVar = false;

namespace {
constexpr const char* kPersistentDataDiagnosticFlag = "-r1delta_pdata_diag";

bool PersistentDataDiagnosticsEnabled()
{
	return HasEngineCommandLineFlag(kPersistentDataDiagnosticFlag)
		|| AreR1OFakeDediVerboseLogsEnabled();
}

bool IsPersistentDataDiagnosticKey(const char* name)
{
	return name
		&& (strcmp(name, PERSIST_COMMAND" xp") == 0
			|| strcmp(name, PERSIST_COMMAND" previousXP") == 0
			|| strcmp(name, PERSIST_COMMAND" gen") == 0
			|| strcmp(name, PERSIST_COMMAND" bc.uiActiveBurnCardIndex") == 0);
}

void LogPersistentDataDiagnostic(
	const char* stage,
	int playerSlot,
	const char* name,
	const char* value)
{
	if (!PersistentDataDiagnosticsEnabled() || !IsPersistentDataDiagnosticKey(name))
		return;

	char message[512];
	_snprintf_s(
		message,
		sizeof(message),
		_TRUNCATE,
		"R1Delta: pdata-diag stage=%s playerSlot=%d name=\"%s\" value=\"%s\"\n",
		stage ? stage : "unknown",
		playerSlot,
		name ? name : "",
		value ? value : "");
	OutputDebugStringA(message);
}

bool IsPersistentConVarName(const char* name)
{
	return name && strncmp(name, kPersistPrefix, kPersistPrefixLength) == 0;
}

// Builds "__ <key>" into `out`. Returns false for keys that are too long.
bool MakePersistConVarName(const char* key, char* out, size_t outSize)
{
	const size_t keyLength = strlen(key);
	if (keyLength == 0 || kPersistPrefixLength + keyLength >= outSize
		|| kPersistPrefixLength + keyLength > MAX_LENGTH)
		return false;
	memcpy(out, kPersistPrefix, kPersistPrefixLength);
	memcpy(out + kPersistPrefixLength, key, keyLength + 1);
	return true;
}
}

bool IsValidUserInfo(const char* value, int length)
{
	if (!value)
		return false;
	const size_t len = (length == -1) ? strlen(value) : static_cast<size_t>(length);
	return PersistentDataStore::IsSafeToken(std::string_view(value, len), MAX_LENGTH);
}

bool IsPDataFullSnapshotMarker(const char* name)
{
	return name && strcmp(name, kFullSnapshotMarker) == 0;
}

// ConVar handling
__int64 CConVar__GetSplitScreenPlayerSlot(char* fakethisptr) {
	ConVarR1* thisptr = reinterpret_cast<ConVarR1*>(fakethisptr - 48);
	return (thisptr->m_nFlags & FCVAR_PERSIST) ? -1 : 0;
}
// Forward declarations
class SchemaParser;
class PDataValidator;

// Represents a schema type which can be either a primitive type or an enum name
struct SchemaType {
	enum class Type {
		Bool,
		Int,
		Float,
		String,
		Enum,

		COUNT,
		INVALID = COUNT,
	};

	Type type;
	std::string enumName; // Only valid if type == Enum

	bool operator==(const SchemaType& other) const {
		if (type == Type::INVALID || other.type == Type::INVALID) return false;
		if (type != other.type) return false;
		if (type == Type::Enum) return enumName == other.enumName;
		return true;
	}

	bool valid() const
	{
		return type < Type::COUNT;
	}
};

// Represents an array definition in the schema
struct ArrayDef {
	std::variant<int, std::string> size; // Either a fixed size or enum name
};

class PDataValidator {
public:
	bool processSegmentForArrays(std::string& currentBase, const std::string& segment) const;
	// Main validation function
	bool isValid(const std::string_view& key, const std::string_view& value) const;
	bool validateArrayIndices(const std::string_view& key) const;

private:
	friend class SchemaParser;

	// Schema storage
	std::unordered_map<std::string, SchemaType, HashStrings, std::equal_to<>> keys;
	std::unordered_map<std::string, ArrayDef, HashStrings, std::equal_to<>> arrays;
	std::unordered_map<std::string, std::map<std::string, int, std::less<>>, HashStrings, std::equal_to<>> enums;

	// Helper functions
	bool isValidEnumValue(const std::string_view& enumName, const std::string_view& value) const;
	SchemaType getKeyType(const std::string_view& key) const;
	bool validateArrayAccess(const std::string_view& arrayName, const std::string_view& index) const;
#if 0
	std::vector<std::string> splitKey(const std::string_view& key) const;
#endif
};


class SchemaParser {
public:
	static PDataValidator parse(const std::string& squirrelCode) {
		PDataValidator validator;
		parseArrays(squirrelCode, validator);
		parseEnums(squirrelCode, validator);
		parseKeys(squirrelCode, validator);
		return validator;
	}

private:
	static void parseArrays(const std::string& code, PDataValidator& validator) {
		std::regex arrayPattern(R"foo(AddPersistenceArray\("([^"]+)",\s*(?:"([^"]+)"|(\d+))\))foo");
		std::smatch matches;
		std::string::const_iterator searchStart(code.cbegin());

		while (std::regex_search(searchStart, code.cend(), matches, arrayPattern)) {
			const std::string& arrayName = matches[1];

			// Check if size is enum name or number
			if (matches[2].matched) {
				// Enum name
				validator.arrays[arrayName] = ArrayDef{ std::string(matches[2]) };
			}
			else {
				// Numeric size
				validator.arrays[arrayName] = ArrayDef{ std::stoi(matches[3]) };
			}

			searchStart = matches.suffix().first;
		}
	}

	static void parseEnums(const std::string& code, PDataValidator& validator) {
		// First find enum blocks
		std::regex enumBlockPattern(R"foo(::(\w+)\s*<-\s*\{([^}]+)\})foo");
		std::smatch blockMatches;
		std::string::const_iterator searchStart(code.cbegin());

		while (std::regex_search(searchStart, code.cend(), blockMatches, enumBlockPattern)) {
			const std::string& enumName = blockMatches[1];
			const std::string& enumBody = blockMatches[2];

			// Parse enum values
			std::regex valuePattern(R"foo((?:\["([^"]+)"\]|(\w+))\s*=\s*(\d+))foo");
			std::smatch valueMatches;
			std::string::const_iterator valueStart(enumBody.cbegin());

			std::map<std::string, int, std::less<>> enumValues;
			while (std::regex_search(valueStart, enumBody.cend(), valueMatches, valuePattern)) {
				// If group 1 matched, it was a ["name"] format
				// If group 2 matched, it was a bare identifier
				const std::string& enumValue = valueMatches[1].matched ? valueMatches[1].str() : valueMatches[2].str();
				enumValues[enumValue] = std::stoi(valueMatches[3]);
				valueStart = valueMatches.suffix().first;
			}

			if (!enumValues.empty()) {
				validator.enums[enumName] = std::move(enumValues);
			}

			searchStart = blockMatches.suffix().first;
		}

		// Process AddPersistenceEnum calls
		std::regex addEnumPattern(R"foo(AddPersistenceEnum\("([^"]+)",\s*(\w+)\))foo");
		std::smatch addEnumMatches;
		searchStart = code.cbegin();

		while (std::regex_search(searchStart, code.cend(), addEnumMatches, addEnumPattern)) {
			const std::string& enumName = addEnumMatches[1];
			const std::string& enumRef = addEnumMatches[2];

			// Copy enum definition if it exists
			auto it = validator.enums.find(enumRef);
			if (it != validator.enums.end()) {
				validator.enums[enumName] = it->second;
			}

			searchStart = addEnumMatches.suffix().first;
		}
	}

	static void parseKeys(const std::string& code, PDataValidator& validator) {
		std::regex keyPattern(R"foo(AddPersistenceKey\("([^"]+)",\s*"([^"]+)"\))foo");
		std::smatch matches;
		std::string::const_iterator searchStart(code.cbegin());

		while (std::regex_search(searchStart, code.cend(), matches, keyPattern)) {
			const std::string& keyName = matches[1];
			const std::string& typeName = matches[2];

			// Convert type string to SchemaType
			SchemaType type;
			if (typeName == "bool") {
				type = { SchemaType::Type::Bool };
			}
			else if (typeName == "int") {
				type = { SchemaType::Type::Int };
			}
			else if (typeName == "float") {
				type = { SchemaType::Type::Float };
			}
			else if (typeName == "string") {
				type = { SchemaType::Type::String };
			}
			else {
				// Assume it's an enum type
				type = { SchemaType::Type::Enum, typeName };
			}

			validator.keys[keyName] = type;

			searchStart = matches.suffix().first;
		}
	}

	static std::string stripComments(const std::string& code) {
		std::stringstream result;
		bool inLineComment = false;
		bool inBlockComment = false;

		for (size_t i = 0; i < code.length(); ++i) {
			if (inLineComment) {
				if (code[i] == '\n') {
					inLineComment = false;
					result << '\n';
				}
				continue;
			}

			if (inBlockComment) {
				if (i + 1 < code.length() && code[i] == '*' && code[i + 1] == '/') {
					inBlockComment = false;
					++i;
				}
				continue;
			}

			if (i + 1 < code.length()) {
				if (code[i] == '/' && code[i + 1] == '/') {
					inLineComment = true;
					++i;
					continue;
				}
				if (code[i] == '/' && code[i + 1] == '*') {
					inBlockComment = true;
					++i;
					continue;
				}
			}

			result << code[i];
		}

		return result.str();
	}
};

// Implementation

static std::vector<std::string> splitOnDot(const std::string_view& key)
{
	std::vector<std::string> parts;
	size_t start = 0;
	while (true)
	{
		size_t dotPos = key.find('.', start);
		if (dotPos == std::string::npos)
		{
			parts.emplace_back(key.substr(start));
			break;
		}
		parts.emplace_back(key.substr(start, dotPos - start));
		start = dotPos + 1;
	}
	return parts;
}

// Takes a single segment (e.g. "weaponKillStats[mp_weapon_lmg][x]")
// and iterates over all bracket references. Each bracket reference
// is validated as arrayName[index].
bool PDataValidator::processSegmentForArrays(std::string& currentBase, const std::string& segment) const
{
	// This function appends the bracket‐free part of each segment to 'currentBase'.
	// Then, for every [index] found, it calls validateArrayAccess(...) on the base + that index.
	//
	// Example 1: segment = "gen"
	//   No brackets => remainder = "gen".
	//   If currentBase is empty, currentBase becomes "gen".
	//   If currentBase was "something", it becomes "something.gen".
	//
	// Example 2: segment = "npcTitans[titan_atlas]"
	//   arrayName = "npcTitans", index = "titan_atlas"
	//   validateArrayAccess("npcTitans", "titan_atlas")
	//   We do NOT append "[titan_atlas]" to currentBase. Instead, we keep "npcTitans" in currentBase.
	//   The next bracket or next segment will pick up from there.

	size_t offset = 0;
	while (true)
	{
		// Find the next bracket in 'segment'
		size_t bracketStart = segment.find('[', offset);
		if (bracketStart == std::string::npos)
		{
			// No more brackets. The remainder is a plain identifier (e.g. "gen", or "npcTitans" if no bracket).
			std::string remainder = segment.substr(offset);
			if (!remainder.empty())
			{
				// If currentBase was non-empty, insert a dot, e.g. "foo" + "." + "bar"
				if (!currentBase.empty())
					currentBase.push_back('.');
				currentBase.append(remainder);
			}
			break;
		}

		// The portion before '[' is our array name
		std::string arrayName = segment.substr(offset, bracketStart - offset);

		if (!arrayName.empty())
		{
			// e.g. from "npcTitans[something]", arrayName = "npcTitans"
			if (!currentBase.empty())
				currentBase.push_back('.');
			currentBase.append(arrayName);
		}

		// Find the matching ']' 
		size_t bracketEnd = segment.find(']', bracketStart);
		if (bracketEnd == std::string::npos)
			return false; // malformed bracket usage

		// The bracket content is the array index
		std::string index = segment.substr(bracketStart + 1, bracketEnd - (bracketStart + 1));

		// Validate arrayName -> index
		if (!validateArrayAccess(currentBase, index))
			return false;

		// We leave 'currentBase' alone here (it stays "npcTitans", for instance),
		// because the bracket was validated. We do not store "[index]" in 'currentBase'.
		// The next bracket or next segment will pick up from the same base name.

		offset = bracketEnd + 1; // move past the ']'
	}

	return true;
}

bool PDataValidator::validateArrayIndices(const std::string_view& key) const {
	ZoneScoped;

	const size_t firstBracketPos = key.find('[');
	if (firstBracketPos == std::string_view::npos)
		return true;

	std::array<char, MAX_LENGTH> currentValidationBase = {};
	size_t currentValidationBaseLength = 0;
	const auto appendValidationBase = [&](std::string_view component) {
		const size_t separatorLength = currentValidationBaseLength == 0 ? 0 : 1;
		if (currentValidationBaseLength + separatorLength + component.size() >= currentValidationBase.size())
			return false;
		if (separatorLength)
			currentValidationBase[currentValidationBaseLength++] = '.';
		memcpy(currentValidationBase.data() + currentValidationBaseLength, component.data(), component.size());
		currentValidationBaseLength += component.size();
		currentValidationBase[currentValidationBaseLength] = '\0';
		return true;
	};

	size_t segmentStart = 0;
	while (segmentStart < key.size()) {
		const size_t nextDot = key.find('.', segmentStart);
		const size_t segmentEnd = nextDot == std::string_view::npos ? key.size() : nextDot;
		const size_t bracketPos = key.find('[', segmentStart);
		const bool hasArrayIndex =
			bracketPos != std::string_view::npos && bracketPos < segmentEnd;

		if (segmentStart == segmentEnd)
			return false;

		if (!hasArrayIndex) {
			if (!appendValidationBase(key.substr(segmentStart, segmentEnd - segmentStart)))
				return false;
		}
		else {
			if (bracketPos == segmentStart)
				return false;
			if (!appendValidationBase(key.substr(segmentStart, bracketPos - segmentStart)))
				return false;

			size_t cursor = bracketPos;
			while (cursor < segmentEnd) {
				if (key[cursor] != '[')
					return false;
				const size_t bracketEnd = key.find(']', cursor);
				if (bracketEnd == std::string_view::npos || bracketEnd >= segmentEnd)
					return false;

				std::string_view index = key.substr(cursor + 1, bracketEnd - cursor - 1);
				const std::string_view validationBase(
					currentValidationBase.data(), currentValidationBaseLength);
				if (!validateArrayAccess(validationBase, index)) {
					Warning(__FUNCTION__ ": out of bound pdata array index %.*s for %s!\n",
						static_cast<int>(index.size()), index.data(), currentValidationBase.data());
					return false;
				}
				cursor = bracketEnd + 1;
			}
		}

		if (segmentEnd == key.size())
			return true;
		segmentStart = segmentEnd + 1;
	}

	return false;
}

bool PDataValidator::isValid(const std::string_view& key, const std::string_view& value) const
{
	if (!validateArrayIndices(key)) return false;
	std::vector<std::string> segments = splitOnDot(key);
    std::string baseKey;
    baseKey.reserve(key.size()); // rough

    for (const auto& segment : segments) {
        if (!processSegmentForArrays(baseKey, segment)) {
            return false;
        }
    }

	// Now that all bracket references were validated, check if baseKey is in schema
	SchemaType type = getKeyType(baseKey);
	if (!type.valid())
		return false;

	// Additional validation for "gen" key
	if (key == "gen" && type.type == SchemaType::Type::Int) {
		int genValue;
		auto res = std::from_chars(value.data(), value.data() + value.size(), genValue);
		if (res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) {
			return false;
		}
		if (genValue < 0 || genValue > 9) {
			return false;
		}
	}

	// Check for invalid weapon strings in loadouts
	if (key.find("titanLoadouts") != std::string_view::npos) {
		if (value.find("mp_weapon_mega") != std::string_view::npos) {
			return true;
		}
		if (value.find("mp_weapon") != std::string_view::npos) {
			return false;
		}
	}
	
	if (key.find("pilotLoadouts") != std::string_view::npos) {
		if (value.find("mp_weapon_mega3") != std::string_view::npos) {
			return false;
		}
		if (value.find("mp_weapon_mega4") != std::string_view::npos) {
			return false;
		}
		if (value.find("mp_titanweapon") != std::string_view::npos) {
			return false;
		}
	}


	// Validate value
	switch (type.type)
	{
	case SchemaType::Type::Bool:
		return (value == "0" || value == "1" ||
			value == "true" || value == "false");

	case SchemaType::Type::Int:
	{
		if (value.empty()) return false;
		size_t start = (value[0] == '-') ? 1 : 0;
		if (start == value.size()) return false; // just "-"
		for (size_t i = start; i < value.size(); i++)
			if (!std::isdigit(static_cast<unsigned char>(value[i])))
				return false;
		return true;
	}

	case SchemaType::Type::Float:
	{
		float tmp;
		auto res = std::from_chars(value.data(), value.data() + value.size(), tmp);
		return (res.ec != std::errc::invalid_argument && res.ec != std::errc::result_out_of_range);
	}

	case SchemaType::Type::String:
		// allow any string (subject to your IsValidUserInfo / length checks)
		return true;

	case SchemaType::Type::Enum:
		return isValidEnumValue(type.enumName, value);

	default:
		return false;
	}

	return false;
}
bool PDataValidator::isValidEnumValue(const std::string_view& enumName, const std::string_view& value) const {
	auto enumIt = enums.find(enumName);
	if (enumIt == enums.end()) return false;

	// Special case for pdata_null which is valid for any enum
	if (value == "pdata_null") return true;

	// NOTE(mrsteyk): you already have everything you would ever want to do lowercase comparison without allocating...
	//                it's not THAT expensive to always convert to lowercase you know. Feel free to prove me wrong.
	auto vl = value.length();

	for (const auto& [ev, _] : enumIt->second) {
		auto evl = ev.length();
		if (evl != vl) continue;

		bool equal = true;
		for (size_t i = 0; i < vl; ++i)
		{
			if (std::tolower(value[i]) != std::tolower(ev[i]))
			{
				equal = false;
				break;
			}
		}
		if (!equal)
		{
			continue;
		}

		return true;
	}

	return false;
}
SchemaType PDataValidator::getKeyType(const std::string_view& key) const {
	// Strip out array indices to get base key format
	std::string baseKey;
	size_t pos = 0;

	while (pos < key.length()) {
		size_t bracketStart = key.find('[', pos);
		if (bracketStart == std::string::npos) {
			// No more brackets, append rest of string
			baseKey += key.substr(pos);
			break;
		}

		// Append everything before the bracket
		baseKey += key.substr(pos, bracketStart - pos);

		// Skip to after closing bracket
		size_t bracketEnd = key.find(']', bracketStart);
		if (bracketEnd == std::string::npos) return SchemaType{ .type = SchemaType::Type::INVALID }; // Malformed

		pos = bracketEnd + 1;

		// If there's a following character and it's not a dot, add a dot
		if (pos < key.length() && key[pos] != '.') {
			baseKey += '.';
		}
	}

	auto it = keys.find(baseKey);
	if (it != keys.end()) return it->second;
	return SchemaType{ .type = SchemaType::Type::INVALID };
}

bool PDataValidator::validateArrayAccess(const std::string_view& arrayName,
	const std::string_view& index) const {
	auto it = arrays.find(arrayName);
	if (it == arrays.end()) return false;

	const auto& arrayDef = it->second;

	// Helper to check if a string is all digits
	auto isNumeric = [](std::string_view str) {
		return !str.empty() &&
			std::all_of(str.begin(), str.end(), [](unsigned char c) {
			return std::isdigit(c);
				});
	};

	if (std::holds_alternative<std::string>(arrayDef.size)) {
		const std::string& enumName = std::get<std::string>(arrayDef.size);
		auto enumIt = enums.find(enumName);
		if (enumIt == enums.end()) return false;

		if (isNumeric(index)) {
			// Convert numeric index to integer
			int idx;
			auto res = std::from_chars(index.data(), index.data() + index.size(), idx);
			if (res.ec != std::errc() || res.ptr != index.data() + index.size())
				return false;

			// Find maximum value in enum
			int maxVal = -1;
			for (const auto& [_, value] : enumIt->second) {
				maxVal = std::max(maxVal, value);
			}
			return idx >= 0 && idx <= maxVal;
		}

		// Non-numeric index, validate as enum value name
		return isValidEnumValue(enumName, index);
	}

	// Handle fixed-size array
	int size = std::get<int>(arrayDef.size);
	int idx;
	auto res = std::from_chars(index.data(), index.data() + index.size(), idx);
	if (res.ec != std::errc() || res.ptr != index.data() + index.size())
		return false;
	return idx >= 0 && idx < size;
}

std::string readFile(const std::string& filename) {
	std::ifstream file(filename);
	if (!file.is_open()) {
		Error("Could not open %s", filename.c_str());
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}


static bool g_pdef_use_gamefs = true;
//#define PDATA_DEBUG false;
static bool TryReadPDefWithGameFS(std::string& outText)
{
	if (!g_CBaseFileSystemInterface)
		return false;

	constexpr const char* kPdefRelPath = "scripts/vscripts/_pdef.nut";
	const char* pid = "GAME";

	typedef FileHandle_t(__thiscall* OpenFunc)(void*, const char*, const char*, const char*);
	OpenFunc openFunc = (OpenFunc)g_CBaseFileSystem->Open;
	typedef int64_t(__thiscall* SizeFunc)(void*, FileHandle_t);
	SizeFunc sizeFunc = (SizeFunc)g_CBaseFileSystem->Size2;
	typedef void(__thiscall* CloseFunc)(void*, FileHandle_t);
	CloseFunc closeFunc = (CloseFunc)g_CBaseFileSystem->Close;
	typedef int(__thiscall* ReadFunc)(void*, void*, int, FileHandle_t);
	ReadFunc readFunc = (ReadFunc)g_CBaseFileSystem->Read;

	auto fh = openFunc(g_CBaseFileSystemInterface, kPdefRelPath, "rb", pid);
	if (!fh)
		return false;
	int len = sizeFunc(g_CBaseFileSystemInterface, fh);
#ifdef PDATA_DEBUG
	Msg("Pdata Size %d\n", len);
#endif // PDATA_DEBUG
	if (len <= 0) {
		closeFunc(g_CBaseFileSystemInterface, fh);
		return false;
	}
	std::string buf;
	buf.resize(static_cast<size_t>(len));

	int rd = readFunc(g_CBaseFileSystemInterface, buf.data(), len, fh);
#ifdef PDATA_DEBUG
	Msg("Pdata READ Size %d\n", rd);
#endif // PDATA_DEBUG

	if (rd < 0) {
		closeFunc(g_CBaseFileSystemInterface, fh);
		return false;
	}
	// Close the file
	closeFunc(g_CBaseFileSystemInterface, fh);
	outText = std::move(buf);
	return true;
}

void PDef::InitValidator() {
		try {
			bool useGameFS = g_pdef_use_gamefs;
			if (OriginalCCVar_FindVar) {
				if (auto* cv = OriginalCCVar_FindVar(cvarinterface, "delta_pdef_use_gamefs")) {
					useGameFS = (cv->m_Value.m_nValue != 0);
				}
			}

			std::string schemaCode;

			if (useGameFS) {
				if (!TryReadPDefWithGameFS(schemaCode)) {
					// If FS read failed (e.g., too early or not mounted), fallback to raw path
					useGameFS = false;
					Msg("No modded _pdef.nut found in GameFS, falling back to r1delta path.\n");
				}
			}

			if (!useGameFS) {
				// Raw path fallback (loose file in r1delta)
				auto exeDir = GetExecutableDirectory();
				auto schemaPath = std::filesystem::absolute(
					exeDir / std::filesystem::path("r1delta") / "scripts" / "vscripts" / "_pdef.nut"
				).lexically_normal();

				if (!std::filesystem::exists(schemaPath)) {
					Error("FATAL: Could not find _pdef.nut. Expected location: %s",
						schemaPath.string().c_str());
				}
				schemaCode = readFile(schemaPath.string());
			}

			s_validator = std::make_unique<PDataValidator>(SchemaParser::parse(schemaCode));
		}
		catch (const std::exception& e) {
			Error("FATAL: Failed to initialize PData validator: %s", e.what());
		}
		catch (...) {
			Error("FATAL: An unknown error occurred during PData validator initialization.");
		}
	}
	bool PDef::IsValidKeyAndValue(const std::string& key, const std::string& value) {
		std::call_once(s_initFlag, InitValidator);
		return s_validator->isValid(key, value);
	}

	bool PDef::ValidateKeyIndices(const std::string_view& key) {
		// Initialize validator on first use
		std::call_once(s_initFlag, InitValidator);

		if (!s_validator) {
			Error("PData validator failed to initialize");
		}
		
		return s_validator->validateArrayIndices(key);
	}


std::unique_ptr<PDataValidator> PDef::s_validator;
std::once_flag PDef::s_initFlag;

//-----------------------------------------------------------------------------
// Wire format
//-----------------------------------------------------------------------------

namespace {
constexpr uint32_t kPackedPDataMinEntries = 32;

bool IsSchemaValidPersistentConVar(const NetMessageCvar_t& var)
{
	const char* key = var.name + kPersistPrefixLength;
	return IsValidUserInfo(key) && IsValidUserInfo(var.value)
		&& PDef::IsValidKeyAndValue(key, var.value);
}

void WarnDroppedPersistentEntry(const char* where, const char* key, const char* value)
{
	static int budget = 32;
	if (budget <= 0)
		return;
	--budget;
	Warning("R1Delta: %s dropped persistent data entry that fails the active schema: key=%s value=%s\n",
		where, key ? key : "", value ? value : "");
}

struct PackedPDataPayload {
	std::string encoded;
	uint32_t entryCount = 0;
};

bool BuildPackedPDataPayload(const NET_SetConVar* message, PackedPDataPayload& payload)
{
	if (!message || HasEngineCommandLineFlag("-r1delta_legacy_pdata_wire"))
		return false;

	std::vector<PersistentDataCodec::Entry> entries;
	entries.reserve(message->m_ConVars.Count());
	for (int i = 0; i < message->m_ConVars.Count(); ++i) {
		const NetMessageCvar_t& var = message->m_ConVars[i];
		if (!IsPersistentConVarName(var.name))
			continue;
		LogPersistentDataDiagnostic("client-pack", -1, var.name, var.value);
		entries.push_back({ var.name + kPersistPrefixLength, var.value });
	}

	if (entries.size() < kPackedPDataMinEntries
		|| !PersistentDataCodec::Encode(entries, payload.encoded))
		return false;
	payload.entryCount = static_cast<uint32_t>(entries.size());
	return true;
}
}

bool IsPackedPDataWireName(const char* name)
{
	return name && strcmp(name, PersistentDataCodec::WireName) == 0;
}

bool DecodePackedPDataWire(const std::string& encoded, std::vector<NetMessageCvar_t>& output)
{
	std::vector<PersistentDataCodec::Entry> entries;
	if (!PersistentDataCodec::Decode(encoded, entries))
		return false;

	std::vector<NetMessageCvar_t> decoded;
	decoded.reserve(entries.size());
	for (const PersistentDataCodec::Entry& entry : entries) {
		NetMessageCvar_t var = {};
		if (entry.key.size() + kPersistPrefixLength >= sizeof(var.name)
			|| entry.value.size() >= sizeof(var.value))
			return false;
		memcpy(var.name, kPersistPrefix, kPersistPrefixLength);
		memcpy(var.name + kPersistPrefixLength, entry.key.c_str(), entry.key.size() + 1);
		memcpy(var.value, entry.value.c_str(), entry.value.size() + 1);
		// One entry the server's schema does not know (e.g. a client-side mod)
		// must not throw away the rest of the player's data.
		if (!IsSchemaValidPersistentConVar(var)) {
			WarnDroppedPersistentEntry("packed decode", entry.key.c_str(), entry.value.c_str());
			continue;
		}
		decoded.push_back(var);
	}
	output = std::move(decoded);
	return true;
}

static size_t ClientPersistentConVarCount();

bool NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer) {
	const int startBit = buffer.GetNumBitsWritten();
	if (g_bNoSendConVar) {
		buffer.WriteByte(0);
		return !buffer.IsOverflowed();
	}
	bool vanilla = false;
	if (!IsDedicatedServer()) {
		auto var = OriginalCCVar_FindVar(cvarinterface, "net_secure");
		vanilla = var && var->m_Value.m_nValue == 1;
		if (vanilla) {
			for (int i = thisptr->m_ConVars.Count() - 1; i >= 0; --i) {
				if (thisptr->m_ConVars[i].name[0] == '_')
					thisptr->m_ConVars.Remove(i);
			}
		}
	}

	// Entries that fail the active schema (data kept for a mod that is not
	// loaded right now) stay on disk but are not sent; a server would reject
	// them.
	size_t persistentCount = 0;
	bool hasMarker = false;
	for (int i = thisptr->m_ConVars.Count() - 1; i >= 0; --i) {
		NetMessageCvar_t& var = thisptr->m_ConVars[i];
		if (IsPDataFullSnapshotMarker(var.name)) {
			hasMarker = true;
			continue;
		}
		if (!IsPersistentConVarName(var.name))
			continue;
		++persistentCount;
		if (!IsSchemaValidPersistentConVar(var))
			thisptr->m_ConVars.Remove(i);
	}
	if (!vanilla && !hasMarker && !IsDedicatedServer() && persistentCount > 0
		&& persistentCount >= ClientPersistentConVarCount()) {
		NetMessageCvar_t marker = {};
		strcpy_s(marker.name, sizeof(marker.name), kFullSnapshotMarker);
		strcpy_s(marker.value, sizeof(marker.value), "1");
		thisptr->m_ConVars.AddToTail(marker);
	}

	PackedPDataPayload packed;
	const bool usePackedPData = BuildPackedPDataPayload(thisptr, packed);
	uint32_t nonPersistentCount = 0;
	for (int i = 0; i < thisptr->m_ConVars.Count(); ++i) {
		if (!IsPersistentConVarName(thisptr->m_ConVars[i].name))
			++nonPersistentCount;
	}
	const uint32_t chunkCount = usePackedPData
		? static_cast<uint32_t>((packed.encoded.size() + PersistentDataCodec::ChunkSize - 1) / PersistentDataCodec::ChunkSize)
		: 0;
	const uint32_t numvars = usePackedPData
		? nonPersistentCount + chunkCount
		: static_cast<uint32_t>(thisptr->m_ConVars.Count());

	if (numvars < 255) {
		buffer.WriteByte(numvars);
	}
	else {
		buffer.WriteByte(static_cast<uint8_t>(-1));
		buffer.WriteUBitVar(numvars);
	}

	auto writeConVar = [&](NetMessageCvar_t& var) {
		if (!IsDedicatedServer() && _stricmp(var.name, "platform_user_id") == 0 && var.value[0] == 0) {
			ConVarR1* platformUserId = OriginalCCVar_FindVar
				? OriginalCCVar_FindVar(cvarinterface, "platform_user_id")
				: nullptr;
			char fallback[32] = {};
			if (platformUserId && platformUserId->m_Value.m_pszString && platformUserId->m_Value.m_pszString[0]) {
				strncpy_s(fallback, sizeof(fallback), platformUserId->m_Value.m_pszString, _TRUNCATE);
			}
			else {
				const unsigned long long generated = GenerateSyntheticPlatformUserId();
				_snprintf_s(fallback, sizeof(fallback), _TRUNCATE, "%llu", generated);
				if (platformUserId) {
					if (SetConvarStringOriginal)
						SetConvarStringOriginal(platformUserId, fallback);
					platformUserId->m_Value.m_nValue = static_cast<int>(generated & 0x7FFFFFFF);
				}
			}
			strncpy_s(var.value, sizeof(var.value), fallback, _TRUNCATE);
		}

		if (IsPersistentConVarName(var.name)) {
			char modifiedName[sizeof(var.name)] = {};
			modifiedName[0] = static_cast<char>(static_cast<unsigned char>(var.name[kPersistPrefixLength]) | 0x80);
			strcpy_s(modifiedName + 1, sizeof(modifiedName) - 1, var.name + kPersistPrefixLength + 1);
			buffer.WriteString(modifiedName);
		}
		else {
			buffer.WriteString(var.name);
		}
		buffer.WriteString(var.value);
	};

	for (int i = 0; i < thisptr->m_ConVars.Count(); ++i) {
		NetMessageCvar_t& var = thisptr->m_ConVars[i];
		if (usePackedPData && IsPersistentConVarName(var.name))
			continue;
		writeConVar(var);
	}

	if (usePackedPData) {
		for (size_t offset = 0; offset < packed.encoded.size(); offset += PersistentDataCodec::ChunkSize) {
			const size_t length = (std::min)(PersistentDataCodec::ChunkSize, packed.encoded.size() - offset);
			buffer.WriteString(PersistentDataCodec::WireName);
			char chunk[PersistentDataCodec::ChunkSize + 1] = {};
			memcpy(chunk, packed.encoded.data() + offset, length);
			buffer.WriteString(chunk);
		}
	}

	const bool result = !buffer.IsOverflowed();
	static int writeLogBudget = 32;
	if (writeLogBudget > 0 && (AreR1OFakeDediVerboseLogsEnabled() || usePackedPData)) {
		--writeLogBudget;
		char msg[512];
		_snprintf_s(
			msg,
			sizeof(msg),
			_TRUNCATE,
			"R1Delta: NET_SetConVar write count=%u original=%d persistent=%zu packedEntries=%u packedChunks=%u startBit=%d endBit=%d result=%d\n",
			numvars,
			thisptr->m_ConVars.Count(),
			persistentCount,
			packed.entryCount,
			chunkCount,
			startBit,
			buffer.GetNumBitsWritten(),
			static_cast<int>(result));
		OutputDebugStringA(msg);
	}
	return result;
}

bool SafePrefixConVarName(char* name, size_t nameBufferSize, const char* prefix) {
	const size_t prefixLen = strlen(prefix);
	const size_t nameLen = strlen(name);

	// Check if there's enough space for prefix + original name + null terminator
	if (nameLen + prefixLen >= nameBufferSize) {
		Warning("ConVar name too long for prefixing: %s\n", name);
		return false;
	}

	memmove(name + prefixLen, name, nameLen + 1);
	memcpy(name, prefix, prefixLen);
	return true;
}

static int NativeServerSlotFromMessageHandler(const void* handler);

bool NET_SetConVar__ReadFromBuffer(NET_SetConVar* thisptr, bf_read& buffer) {
	uint32_t numvars;
	uint8_t byteCount = buffer.ReadByte();

	if (byteCount == static_cast<uint8_t>(-1)) {
		numvars = buffer.ReadUBitVar();
	}
	else {
		numvars = byteCount;
	}
	if (numvars > 4096*4) {
		Warning("Client sent too many ConVars %d\n", numvars);
		return false;
	}
	std::vector<NetMessageCvar_t> staged;
	staged.reserve(numvars);
	std::string packedPData;
	bool sawPackedPData = false;
	bool sawLegacyPData = false;
	bool sawFullSnapshotMarker = false;
	size_t decodedPersistentCount = 0;
	for (uint32_t i = 0; i < numvars; i++) {
		NetMessageCvar_t var;
		if (!buffer.ReadString(var.name, sizeof(var.name)) ||
			!buffer.ReadString(var.value, sizeof(var.value))) {
			Warning("Failed to read convar %d/%d\n", i, numvars);
			return false;
		}

		if (IsPackedPDataWireName(var.name)) {
			const size_t chunkLength = strlen(var.value);
			if (!chunkLength || packedPData.size() > PersistentDataCodec::MaxEncodedSize
				|| PersistentDataCodec::MaxEncodedSize - packedPData.size() < chunkLength) {
				Warning("Invalid packed persistent data chunk\n");
				return false;
			}
			sawPackedPData = true;
			packedPData.append(var.value, chunkLength);
			continue;
		}

		if (IsPDataFullSnapshotMarker(var.name)) {
			sawFullSnapshotMarker = true;
			continue;
		}

		// Persistent data convars are sent with the high bit set on the first character.
		if (static_cast<unsigned char>(var.name[0]) & 0x80) {
			sawLegacyPData = true;
			var.name[0] &= 0x7F;

			if (!SafePrefixConVarName(var.name, sizeof(var.name), PERSIST_COMMAND" ")) {
				Warning("Failed to prefix persistent data convar\n");
				return false;
			}
			if (!IsSchemaValidPersistentConVar(var)) {
				WarnDroppedPersistentEntry("legacy read", var.name + kPersistPrefixLength, var.value);
				continue;
			}
		}
		else {
			// Skip networkid_force CVar case-insensitively
			if (::_stricmp(var.name, "networkid_force") == 0) {
				continue;
			}

			int flags = 0;
			if (OriginalCCVar_FindVar) {
				if (auto* cvar = OriginalCCVar_FindVar(cvarinterface, var.name))
					flags = cvar->m_nFlags;
			}
			if (!(flags & (FCVAR_USERINFO | FCVAR_REPLICATED))) {
				Warning("Invalid userinfo convar (doesn't exist or missing FCVAR_USERINFO or FCVAR_REPLICATED flag): %s\n", var.name);
				continue;
			}
		}

		staged.push_back(var);
	}

	if (sawPackedPData) {
		if (sawLegacyPData) {
			Warning("Mixed packed and legacy persistent data payload\n");
			return false;
		}
		std::vector<NetMessageCvar_t> decoded;
		if (!DecodePackedPDataWire(packedPData, decoded)) {
			Warning("Failed to decode packed persistent data payload\n");
			return false;
		}
		decodedPersistentCount = decoded.size();
		staged.insert(staged.end(), decoded.begin(), decoded.end());
	}

	if (buffer.IsOverflowed())
		return false;

	// Server side: reconcile against writes the client has not acknowledged yet.
	const int playerSlot = NativeServerSlotFromMessageHandler(thisptr->m_pMessageHandler);
	if (playerSlot >= 0)
		PData_ServerReconcileIncoming(playerSlot, nullptr, staged, sawFullSnapshotMarker);

	thisptr->m_ConVars.RemoveAll();
	thisptr->m_ConVars.EnsureCapacity(static_cast<int>(staged.size()));
	for (const NetMessageCvar_t& var : staged)
		thisptr->m_ConVars.AddToTail(var);

	if (sawPackedPData) {
		static int packedDecodeLogBudget = 32;
		if (packedDecodeLogBudget-- > 0) {
			char message[256];
			_snprintf_s(
				message,
				sizeof(message),
				_TRUNCATE,
				"R1Delta: NET_SetConVar decoded packedEntries=%zu total=%zu encodedBytes=%zu full=%d slot=%d\n",
				decodedPersistentCount,
				staged.size(),
				packedPData.size(),
				static_cast<int>(sawFullSnapshotMarker),
				playerSlot);
			OutputDebugStringA(message);
		}
	}
	return true;
}

// Squirrel VM functions
SQInteger Script_ClientGetPersistentData(HSQUIRRELVM v) {
	if (sq_gettop(nullptr, v) != 3) {
		return sq_throwerror(v, "Expected 2 parameters");
	}

	const SQChar* key;
	if (SQ_FAILED(sq_getstring(v, 2, &key))) {
		return sq_throwerror(v, "Parameter 1 must be a string");
	}
	const SQChar* defaultValue;
	if (SQ_FAILED(sq_getstring(v, 3, &defaultValue))) {
		return sq_throwerror(v, "Parameter 2 must be a string");
	}

	if (!IsValidUserInfo(key) || !IsValidUserInfo(defaultValue)) {
		return sq_throwerror(v, "Invalid user info key or default value.");
	}

	char name[CCommand::COMMAND_MAX_LENGTH];
	ConVarR1* var = PDef::ValidateKeyIndices(key) && MakePersistConVarName(key, name, sizeof(name))
		? OriginalCCVar_FindVar(cvarinterface, name)
		: nullptr;
	sq_pushstring(v, var && var->m_Value.m_pszString ? var->m_Value.m_pszString : defaultValue, -1);
	return 1;
}

struct CBaseClient
{
	_BYTE gap0[1040];
	KeyValues* m_ConVars;
	char pad[284392];
};
static_assert(sizeof(CBaseClient) == 285440);
struct CBaseClientDS
{
	_BYTE gap0[920];
	KeyValues* m_ConVars;
	char pad[215712];
};
static_assert(sizeof(CBaseClientDS) == 216640);

//-----------------------------------------------------------------------------
// Server side
//-----------------------------------------------------------------------------

CBaseClient* g_pClientArray;
CBaseClientDS* g_pClientArrayDS;

namespace {

std::array<PersistentDataState::PlayerState, PersistentDataSlots::kMaximumSupportedClients> g_serverPlayers;

// R1O fake dedicated servers have no engine-side userinfo table for clients,
// so they keep a full mirror of every player's data.
bool ServerKeepsMirror()
{
	return IsR1ODedicatedServer();
}

bool IsServerPlayerSlot(int playerSlot)
{
	return pGlobalVarsServer
		&& PersistentDataSlots::IsValidPlayerSlot(playerSlot, pGlobalVarsServer->maxClients);
}

uintptr_t NativeClientBase(int playerSlot)
{
	if (playerSlot < 0 || playerSlot >= PersistentDataSlots::kMaximumSupportedClients
		|| IsR1ODedicatedServer())
		return 0;
	if (IsDedicatedServer())
		return g_pClientArrayDS ? reinterpret_cast<uintptr_t>(&g_pClientArrayDS[playerSlot]) : 0;
	return g_pClientArray ? reinterpret_cast<uintptr_t>(&g_pClientArray[playerSlot]) : 0;
}

// IClient lives at +8 in CBaseClient on both engine builds (see sv_filter.h).
constexpr size_t kNativeIClientOffset = 8;
constexpr size_t kIClientGetUserIdIndex = 120 / sizeof(void*);
constexpr size_t kIClientGetNetChannelIndex = 144 / sizeof(void*);

bool ReadNativeClientSession(uintptr_t clientBase, uintptr_t& netChannel, int& userId)
{
	__try {
		void* client = reinterpret_cast<void*>(clientBase + kNativeIClientOffset);
		const uintptr_t* vtable = *reinterpret_cast<uintptr_t* const*>(client);
		if (!vtable)
			return false;
		using GetNetChannelFn = void* (__fastcall*)(void*);
		using GetUserIdFn = int(__fastcall*)(void*);
		netChannel = reinterpret_cast<uintptr_t>(
			reinterpret_cast<GetNetChannelFn>(vtable[kIClientGetNetChannelIndex])(client));
		userId = netChannel
			? reinterpret_cast<GetUserIdFn>(vtable[kIClientGetUserIdIndex])(client)
			: -1;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ResolveServerSession(int playerSlot, PersistentDataState::SessionKey& session)
{
	session = {};
	if (!IsServerPlayerSlot(playerSlot))
		return false;
	if (IsR1ODedicatedServer())
		return R1OResolvePersistenceSessionForSlot(playerSlot, session) && session.IsValid();

	const uintptr_t clientBase = NativeClientBase(playerSlot);
	uintptr_t netChannel = 0;
	int userId = -1;
	if (!clientBase || !ReadNativeClientSession(clientBase, netChannel, userId))
		return false;
	session.netChannel = netChannel;
	session.userId = userId;
	return session.IsValid();
}

// Returns the player's state bound to its current connection, or nullptr for
// slots without a real client (bots, empty slots).
PersistentDataState::PlayerState* BindServerPlayer(int playerSlot)
{
	PersistentDataState::SessionKey session;
	if (!ResolveServerSession(playerSlot, session))
		return nullptr;
	PersistentDataState::PlayerState& state = g_serverPlayers[playerSlot];
	if (!PersistentDataState::BeginSession(state, session))
		return nullptr;
	return &state;
}

uintptr_t EdictForPlayerSlot(int playerSlot)
{
	if (!pGlobalVarsServer || !pGlobalVarsServer->pEdicts)
		return 0;
	return reinterpret_cast<uintptr_t>(pGlobalVarsServer->pEdicts)
		+ static_cast<uintptr_t>(playerSlot + 1) * 56;
}

using ClientCommandFn = void(__fastcall*)(void*, uintptr_t, const char*, ...);

bool SendPersistentCommandToClient(int playerSlot, const char* key, const char* value)
{
	const uintptr_t edict = EdictForPlayerSlot(playerSlot);
	if (!edict || !key || !value)
		return false;

	if (IsR1ODedicatedServer()) {
		// Use the exact native interface that R1OFactory handed to server_local.dll.
		// dedicated.dll's app-system factory does not expose this interface in fake
		// dedicated mode. ClientCommand is slot 37 in VEngineServer022.
		void* engineServer = GetR1ONativeEngineServer022();
		if (!engineServer)
			return false;
		const auto vtable = *reinterpret_cast<uintptr_t* const*>(engineServer);
		if (!vtable || !vtable[37])
			return false;
		reinterpret_cast<ClientCommandFn>(vtable[37])(
			engineServer, edict, PERSIST_COMMAND" \"%s\" \"%s\"", key, value);
		return true;
	}

	static ClientCommandFn clientCommand = nullptr;
	if (!clientCommand) {
		clientCommand = IsDedicatedServer()
			? reinterpret_cast<ClientCommandFn>(G_engine_ds + 0x6F030)
			: reinterpret_cast<ClientCommandFn>(G_engine + 0xFE7F0);
	}
	clientCommand(nullptr, edict, PERSIST_COMMAND" \"%s\" \"%s\"", key, value);
	return true;
}

void SendResends(int playerSlot, PersistentDataState::PlayerState& state, double now)
{
	if (state.pending.empty())
		return;
	const PersistentDataState::EntryList due = PersistentDataState::CollectResends(state, now);
	for (const auto& [name, value] : due) {
		if (!IsPersistentConVarName(name.c_str()))
			continue;
		LogPersistentDataDiagnostic("server-resend", playerSlot, name.c_str(), value.c_str());
		SendPersistentCommandToClient(playerSlot, name.c_str() + kPersistPrefixLength, value.c_str());
	}
}

void ServiceServerPlayers(bool throttle)
{
	if (!pGlobalVarsServer)
		return;
	const double now = Plat_FloatTime();
	static double lastService = 0.0;
	if (throttle && now - lastService < 0.25)
		return;
	lastService = now;

	const int maxClients = (std::min)(
		pGlobalVarsServer->maxClients, PersistentDataSlots::kMaximumSupportedClients);
	for (int slot = 0; slot < maxClients; ++slot) {
		PersistentDataState::PlayerState& state = g_serverPlayers[slot];
		if (state.pending.empty())
			continue;
		// Never deliver a write to whoever occupies the slot now unless it is
		// the same connection the write was made for.
		// A slot that cannot be resolved right now (e.g. mid-changelevel) keeps
		// its pending writes; a different connection wipes them in BeginSession.
		PersistentDataState::SessionKey session;
		if (!ResolveServerSession(slot, session)
			|| !PersistentDataState::BeginSession(state, session))
			continue;
		SendResends(slot, state, now);
	}
}

KeyValues* GetClientConVarsKV(int index)
{
	if (index < 0 || index >= PersistentDataSlots::kMaximumSupportedClients || IsR1ODedicatedServer())
		return nullptr;
	if (IsDedicatedServer())
		return g_pClientArrayDS ? g_pClientArrayDS[index].m_ConVars : nullptr;
	return g_pClientArray ? g_pClientArray[index].m_ConVars : nullptr;
}

int NativePlayerSlotFromEntity(const void* player)
{
	if (!player || !pGlobalVarsServer || !pGlobalVarsServer->pEdicts)
		return -1;
	const auto edict = *reinterpret_cast<const __int64*>(reinterpret_cast<uintptr_t>(player) + 64);
	return static_cast<int>(((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1);
}

struct ServerPlayerRef
{
	int playerSlot = -1;
	bool replay = false;
};

bool ResolveScriptPlayer(HSQUIRRELVM v, ServerPlayerRef& player, const char*& error)
{
	void* entity = sq_getentity(v, 2);
	if (!entity) {
		error = "player is null";
		return false;
	}
	if (!IsR1ODedicatedServer()) {
		player.playerSlot = NativePlayerSlotFromEntity(entity);
		player.replay = PersistentDataSlots::IsReplayPlayerSlot(player.playerSlot);
		return true;
	}

	if (!pGlobalVarsServer || !pGlobalVarsServer->pEdicts) {
		error = "player is not backed by a valid edict";
		return false;
	}
	__try {
		const uintptr_t edict = *reinterpret_cast<const uintptr_t*>(
			reinterpret_cast<uintptr_t>(entity) + 0x40);
		const uintptr_t firstEdict = reinterpret_cast<uintptr_t>(pGlobalVarsServer->pEdicts);
		if (edict < firstEdict + 56 || (edict - firstEdict) % 56 != 0) {
			error = "player is not backed by a valid edict";
			return false;
		}
		player.playerSlot = static_cast<int>((edict - firstEdict) / 56) - 1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		error = "player is not backed by a valid edict";
		return false;
	}
	player.replay = PersistentDataSlots::IsReplayPlayerSlot(player.playerSlot);
	if (!player.replay && !IsServerPlayerSlot(player.playerSlot)) {
		error = "player is not an active client";
		return false;
	}
	return true;
}

// Reads what the server should consider the player's current value.
bool ServerReadPersistent(int playerSlot, const char* name, std::string& value)
{
	if (!IsServerPlayerSlot(playerSlot) || PersistentDataSlots::IsReplayPlayerSlot(playerSlot))
		return false;

	PersistentDataState::PlayerState* state = BindServerPlayer(playerSlot);
	if (!state && ServerKeepsMirror() && !g_serverPlayers[playerSlot].session.IsValid())
		state = &g_serverPlayers[playerSlot]; // a bot's unbound mirror, never a previous connection
	if (state) {
		if (const std::string* found = PersistentDataState::Find(*state, name)) {
			value = *found;
			LogPersistentDataDiagnostic("server-read", playerSlot, name, value.c_str());
			return true;
		}
	}
	if (ServerKeepsMirror())
		return false;

	KeyValues* vars = GetClientConVarsKV(playerSlot);
	if (!vars)
		return false;
	static constexpr char kMissing[] = "\x01";
	const char* result = vars->GetString(name, kMissing);
	if (!result || strcmp(result, kMissing) == 0)
		return false;
	value = result;
	LogPersistentDataDiagnostic("server-read", playerSlot, name, result);
	return true;
}

void ServerWritePersistent(int playerSlot, const char* key, const char* name, const char* value)
{
	if (!IsServerPlayerSlot(playerSlot) || PersistentDataSlots::IsReplayPlayerSlot(playerSlot))
		return;

	KeyValues* vars = ServerKeepsMirror() ? nullptr : GetClientConVarsKV(playerSlot);
	if (!ServerKeepsMirror() && !vars)
		return;

	std::string clientValue;
	bool haveClientValue = false;
	if (vars) {
		static constexpr char kMissing[] = "\x01";
		const char* current = vars->GetString(name, kMissing);
		if (current && strcmp(current, kMissing) != 0) {
			clientValue = current;
			haveClientValue = true;
		}
	}

	const double now = Plat_FloatTime();
	PersistentDataState::PlayerState* state = BindServerPlayer(playerSlot);
	bool mustSend;
	if (state) {
		mustSend = PersistentDataState::RecordServerWrite(
			*state, name, value, haveClientValue ? &clientValue : nullptr, now, ServerKeepsMirror());
	}
	else {
		// No real connection behind the slot (bot). Keep the old best-effort
		// behaviour without delivery tracking.
		PersistentDataState::PlayerState& unbound = g_serverPlayers[playerSlot];
		// An unresolved previously bound slot can be between maps or already
		// occupied by a bot. Preserve pending writes, but do not read or alter
		// the previous connection's profile through an unbound player.
		if (unbound.session.IsValid())
			return;
		if (ServerKeepsMirror()) {
			auto& slotValue = unbound.values[name];
			mustSend = slotValue != value;
			slotValue = value;
		}
		else {
			mustSend = !haveClientValue || clientValue != value;
		}
	}

	LogPersistentDataDiagnostic("server-write", playerSlot, name, value);
	if (!mustSend)
		return;
	if (vars)
		vars->SetString(name, value);
	if (!SendPersistentCommandToClient(playerSlot, key, value))
		Warning("R1Delta: failed to send persistent data update to player slot %d\n", playerSlot);
}

// Builds the "__ key" name for a script-supplied key. Keys with out-of-range
// array indices are rejected here instead of producing a bogus name.
bool BuildScriptPersistName(const char* key, char* out, size_t outSize)
{
	if (!PDef::ValidateKeyIndices(key))
		return false;
	return MakePersistConVarName(key, out, outSize);
}

}

bool PData_ServerReconcileIncoming(
	int playerSlot,
	const PersistentDataState::SessionKey* sessionOverride,
	std::vector<NetMessageCvar_t>& staged,
	bool fullSnapshot)
{
	if (!IsServerPlayerSlot(playerSlot))
		return false;

	PersistentDataState::SessionKey session;
	if (sessionOverride)
		session = *sessionOverride;
	else if (!ResolveServerSession(playerSlot, session))
		return false;

	PersistentDataState::PlayerState& state = g_serverPlayers[playerSlot];
	if (!PersistentDataState::BeginSession(state, session))
		return false;

	std::vector<size_t> indices;
	PersistentDataState::EntryList entries;
	for (size_t i = 0; i < staged.size(); ++i) {
		if (!IsPersistentConVarName(staged[i].name))
			continue;
		indices.push_back(i);
		entries.emplace_back(staged[i].name, staged[i].value);
	}
	if (entries.empty() && state.pending.empty() && !(fullSnapshot && ServerKeepsMirror()))
		return true;

	PersistentDataState::ApplyClientUpdate(state, entries, fullSnapshot, ServerKeepsMirror());

	for (size_t i = 0; i < entries.size(); ++i) {
		const std::string& name = entries[i].first;
		const std::string& value = entries[i].second;
		if (i < indices.size()) {
			NetMessageCvar_t& var = staged[indices[i]];
			if (value != var.value)
				strncpy_s(var.value, sizeof(var.value), value.c_str(), _TRUNCATE);
			LogPersistentDataDiagnostic(fullSnapshot ? "server-snapshot" : "server-delta",
				playerSlot, var.name, var.value);
			continue;
		}
		NetMessageCvar_t var = {};
		if (name.size() >= sizeof(var.name) || value.size() >= sizeof(var.value))
			continue;
		memcpy(var.name, name.c_str(), name.size() + 1);
		memcpy(var.value, value.c_str(), value.size() + 1);
		staged.push_back(var);
	}

	SendResends(playerSlot, state, Plat_FloatTime());
	return true;
}

void R1OClearPersistentUserDataForPlayer(int playerSlot)
{
	if (playerSlot >= 0 && playerSlot < PersistentDataSlots::kMaximumSupportedClients)
		PersistentDataState::Reset(g_serverPlayers[playerSlot]);
}

bool R1OGetPersistentUserDataConVar(int playerSlot, const char* name, std::string& value)
{
	return IsR1ODedicatedServer() && IsPersistentConVarName(name)
		&& ServerReadPersistent(playerSlot, name, value);
}

void PData_ServerRunFrame()
{
	ServiceServerPlayers(false);
}

static bool ParsePersistentInteger(const std::string& value, int& result)
{
	if (value.empty())
		return false;
	const char* begin = value.data();
	const char* end = begin + value.size();
	const auto parsed = std::from_chars(begin, end, result);
	return parsed.ec == std::errc() && parsed.ptr == end;
}

static void RebuildNetworkedPersistentInt(void* pPlayer, const char* name, uintptr_t offset, int minValue, int maxValue)
{
	if (IsR1ODedicatedServer()) {
		std::string value;
		int parsed = 0;
		if (!ServerReadPersistent(NativePlayerSlotFromEntity(pPlayer), name, value)
			|| !ParsePersistentInteger(value, parsed))
			return;
		parsed = (std::max)(minValue, (std::min)(maxValue, parsed));
		if (!R1OMarkTFOPlayerNetworkStateChanged(pPlayer)) {
			Warning("Failed to mark R1O player %s for replication\n", name);
			return;
		}
		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pPlayer) + offset) = parsed;
		return;
	}

	std::string value;
	int parsed = 0;
	if (!ServerReadPersistent(NativePlayerSlotFromEntity(pPlayer), name, value)
		|| !ParsePersistentInteger(value, parsed) || parsed == 0)
		return;
	parsed = (std::max)(minValue, (std::min)(maxValue, parsed));
	int& networked = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pPlayer) + offset);
	if (networked != parsed)
		networked = parsed;
}

void Script_XPChanged_Rebuild(void* pPlayer) {
	RebuildNetworkedPersistentInt(pPlayer, PERSIST_COMMAND" xp", 0x1834,
		(std::numeric_limits<int>::min)(), (std::numeric_limits<int>::max)());
}

void Script_GenChanged_Rebuild(void* pPlayer) {
	RebuildNetworkedPersistentInt(pPlayer, PERSIST_COMMAND" gen", 0x183C, 0, 9);
}

SQInteger Script_ServerGetPersistentUserDataKVString(HSQUIRRELVM v) {
	ServerPlayerRef player;
	const char* error = nullptr;
	if (!ResolveScriptPlayer(v, player, error))
		return sq_throwerror(v, error);

	const char* pKey = nullptr;
	const char* pDefaultValue = nullptr;
	if (SQ_FAILED(sq_getstring(v, 3, &pKey)) || SQ_FAILED(sq_getstring(v, 4, &pDefaultValue)))
		return sq_throwerror(v, "Expected key and default string parameters");
	if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pDefaultValue))
		return sq_throwerror(v, "Invalid user info key or default value.");

	ServiceServerPlayers(true);

	char name[CCommand::COMMAND_MAX_LENGTH];
	std::string value;
	if (!player.replay
		&& BuildScriptPersistName(pKey, name, sizeof(name))
		&& ServerReadPersistent(player.playerSlot, name, value)) {
		sq_pushstring(v, value.c_str(), -1);
		return 1;
	}
	sq_pushstring(v, pDefaultValue, -1);
	return 1;
}

SQInteger Script_ServerSetPersistentUserDataKVString(HSQUIRRELVM v) {
	ServerPlayerRef player;
	const char* error = nullptr;
	if (!ResolveScriptPlayer(v, player, error))
		return sq_throwerror(v, error);

	const char* pKey = nullptr;
	const char* pValue = nullptr;
	if (SQ_FAILED(sq_getstring(v, 3, &pKey)) || SQ_FAILED(sq_getstring(v, 4, &pValue)))
		return sq_throwerror(v, "Expected key and value string parameters");
	if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pValue))
		return sq_throwerror(v, "Invalid user info key or value.");

	ServiceServerPlayers(true);

	char name[CCommand::COMMAND_MAX_LENGTH];
	if (player.replay) {
		// Replay entity: nothing to persist.
	}
	else if (!BuildScriptPersistName(pKey, name, sizeof(name))) {
		static int budget = 16;
		if (budget-- > 0)
			Warning("R1Delta: ignoring persistent data write with invalid key %s\n", pKey);
	}
	else {
		ServerWritePersistent(player.playerSlot, pKey, name, pValue);
	}

	sq_pushstring(v, pValue, -1);
	return 1;
}

static int NativeServerSlotFromMessageHandler(const void* handler)
{
	if (!handler || IsR1ODedicatedServer() || !pGlobalVarsServer)
		return -1;
	const uintptr_t address = reinterpret_cast<uintptr_t>(handler);
	uintptr_t base = 0;
	size_t stride = 0;
	if (IsDedicatedServer()) {
		base = reinterpret_cast<uintptr_t>(g_pClientArrayDS);
		stride = sizeof(CBaseClientDS);
	}
	else {
		base = reinterpret_cast<uintptr_t>(g_pClientArray);
		stride = sizeof(CBaseClient);
	}
	const int maxClients = (std::min)(
		pGlobalVarsServer->maxClients, PersistentDataSlots::kMaximumSupportedClients);
	if (!base || maxClients <= 0 || address < base
		|| address >= base + stride * static_cast<size_t>(maxClients))
		return -1;
	return static_cast<int>((address - base) / stride);
}
bool IsValidServerCommand(const char* cmd)
{
	bool in_string = false;
	size_t cmdlen = strlen(cmd);
	for (size_t i = 0; i < cmdlen; i++)
	{
		if (i+1 < cmdlen && cmd[i] == '\\' && cmd[i + 1] == '"') {
			i++;
			continue;
		};

		if (cmd[i] == '"') in_string = !in_string;
		if ((cmd[i] == ';' || cmd[i] == '\n') && !in_string)
			return false;
	}

	return
		!memcmp(cmd, PERSIST_COMMAND, sizeof(PERSIST_COMMAND) - 1) // Persistent data set
		|| !strcmp_static(cmd + 1, "remote_view"); // [-+]remote_view
}

typedef char (*CBaseClientState__InternalProcessStringCmdType)(void* thisptr, void* msg, bool bIsHLTV);
CBaseClientState__InternalProcessStringCmdType CBaseClientState__InternalProcessStringCmdOriginal;
char CBaseClientState__InternalProcessStringCmd(void* thisptr, void* msg, bool bIsHLTV) {
	const char* cmd = *(const char**)((uintptr_t)msg + 32);
	if (!IsValidServerCommand(cmd))
	{
		// Not a valid command, send back to server.

		static uintptr_t clientstate = (uintptr_t)(G_engine + 0x797070);
		static void(__fastcall * oClState_SendStringCmd)(uintptr_t, const char*) = (decltype(oClState_SendStringCmd))(G_engine + 0x25590);
		oClState_SendStringCmd(clientstate, cmd);
		
		return true;
	}

	auto engine = G_engine;
	void(*Cbuf_Execute)() = decltype(Cbuf_Execute)(engine + 0x1057C0);
	char ret = CBaseClientState__InternalProcessStringCmdOriginal(thisptr, msg, bIsHLTV);
	Cbuf_Execute(); // fix cbuf overflow on too many stringcmds
	return ret;
}
char __fastcall GetConfigPath(char* outPath, size_t outPathSize, int configType)
{
	CHAR folderPath[MAX_PATH];

	// Get the user's Documents folder path
	if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, folderPath) < 0)
	{
		return 0;
	}

	// Determine the subfolder based on configType
	const char* subFolder = (configType == 1) ? "/profile" : "/local";

	// Construct the base path
	char tempPath[512];
	auto size = snprintf(tempPath, sizeof(tempPath), "%s%s%s", folderPath, "/Respawn/R1Delta", subFolder);

	if (size >= 511)
	{
		return 0;
	}

	// Determine the config file name based on configType
	const char* configFile;
	switch (configType)
	{
	case 0:
		configFile = "settings.cfg";
		break;
	case 1:
		configFile = "profile.cfg";
		break;
	case 2:
		configFile = "videoconfig.txt";
		break;
	default:
		configFile = "error.cfg";
		break;
	}

	// Construct the final path
	snprintf(outPath, outPathSize, "%s/%s", tempPath, configFile);

	return 1;
}

//-----------------------------------------------------------------------------
// Client side: the persistent data store
//-----------------------------------------------------------------------------

namespace {

// "__ <key>" convars are replicated to servers (USERINFO) and archived to
// profile.cfg (FCVAR_ARCHIVE_PLAYERPROFILE). The profile.cfg copy is write-only
// from this build's point of view: it exists so an older build (rollback or
// downgrade) still finds current data. It is stripped whenever profile.cfg is
// executed and only imported when an older build changed it (see
// EnsureStoreLoaded).
constexpr int kPersistentConVarFlags = FCVAR_PERSIST_MASK;

constexpr double kSaveDebounceSeconds = 1.0;
constexpr double kSaveMaxDelaySeconds = 5.0;
constexpr double kSaveRetrySeconds = 2.0;

struct ClientStore
{
	bool loaded = false;
	bool disabled = false;
	PersistentDataStore::Entries entries;
	// Names of the "__ key" convars created so far. Used to recognise a
	// NET_SetConVar that carries the complete data set.
	std::unordered_set<std::string> conVarNames;

	bool dirty = false;
	double firstDirtyTime = 0.0;
	double lastChangeTime = 0.0;
	double retryTime = 0.0;
	bool reportedSaveFailure = false;

	std::filesystem::path path;
};

// Intentionally leaked: the final flush runs from atexit, after which static
// destructors may already have run.
ClientStore& Store()
{
	static ClientStore* store = new ClientStore();
	return *store;
}

std::filesystem::path WithSuffix(const std::filesystem::path& path, const wchar_t* suffix)
{
	std::filesystem::path result = path;
	result += suffix;
	return result;
}

bool ReadWholeFile(const std::filesystem::path& path, std::string& contents, size_t maxSize)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error) || error)
		return false;
	const uintmax_t size = std::filesystem::file_size(path, error);
	if (error || size > maxSize)
		return false;
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	contents.assign(static_cast<size_t>(size), '\0');
	if (size && !file.read(contents.data(), static_cast<std::streamsize>(size)))
		return false;
	return true;
}

bool WriteFileDurably(const std::filesystem::path& path, std::string_view contents)
{
	HANDLE file = CreateFileW(
		path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	const bool ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) != FALSE
		&& written == contents.size()
		&& FlushFileBuffers(file) != FALSE;
	CloseHandle(file);
	if (!ok)
		DeleteFileW(path.c_str());
	return ok;
}

bool GetProfileDirectory(std::filesystem::path& directory)
{
	char path[MAX_PATH * 4] = {};
	if (!GetConfigPath(path, sizeof(path), 1))
		return false;
	directory = std::filesystem::path(path).parent_path();
	return true;
}

// Atomically replaces the store file, preserving the previous generation as
// .bak in the same operation. A failed replacement keeps the complete .tmp
// for recovery; it must not bypass a failed backup by overwriting the primary.
bool SaveStoreNow(bool quiet)
{
	ClientStore& store = Store();
	if (!store.loaded || store.disabled || store.path.empty())
		return false;

	const std::string contents = PersistentDataStore::Serialize(store.entries);
	const std::filesystem::path temporary = WithSuffix(store.path, L".tmp");
	const std::filesystem::path backup = WithSuffix(store.path, L".bak");

	bool ok = WriteFileDurably(temporary, contents);
	if (ok) {
		std::error_code error;
		const bool primaryExists = std::filesystem::exists(store.path, error);
		if (error)
			ok = false;
		else if (primaryExists)
			ok = ReplaceFileW(store.path.c_str(), temporary.c_str(), backup.c_str(),
				0, nullptr, nullptr) != FALSE;
		else
			ok = MoveFileExW(temporary.c_str(), store.path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
	}

	if (ok) {
		store.dirty = false;
		store.reportedSaveFailure = false;
		return true;
	}

	store.retryTime = Plat_FloatTime() + kSaveRetrySeconds;
	if (!quiet && !store.reportedSaveFailure) {
		store.reportedSaveFailure = true;
		Warning("R1Delta: failed to save persistent data to %s (error %lu); will keep retrying\n",
			store.path.string().c_str(), GetLastError());
	}
	return false;
}

enum class LoadResult { Missing, Loaded, Recovered };

// Picks the committed trustworthy copy: the primary file, else a .tmp left by
// an interrupted replacement, else the previous generation.
// If none is intact, salvages the damaged copy with the most entries.
LoadResult LoadStoreFile(const std::filesystem::path& path, PersistentDataStore::Entries& entries,
	std::filesystem::path& loadedFrom)
{
	const std::filesystem::path candidates[] = {
		path, WithSuffix(path, L".tmp"), WithSuffix(path, L".bak"),
	};

	PersistentDataStore::Entries salvage;
	std::filesystem::path salvageFrom;
	bool anyPresent = false;
	for (const auto& candidate : candidates) {
		std::string contents;
		if (!ReadWholeFile(candidate, contents, PersistentDataStore::MaxFileSize))
			continue;
		anyPresent = true;
		PersistentDataStore::Entries parsed;
		const auto status = PersistentDataStore::Parse(contents, parsed);
		if (status == PersistentDataStore::ParseStatus::Valid) {
			entries = std::move(parsed);
			loadedFrom = candidate;
			return candidate == path ? LoadResult::Loaded : LoadResult::Recovered;
		}
		Warning("R1Delta: persistent data file %s is damaged (%u entries readable)\n",
			candidate.string().c_str(), static_cast<unsigned>(parsed.size()));
		if (parsed.size() > salvage.size()) {
			salvage = std::move(parsed);
			salvageFrom = candidate;
		}
	}

	if (!salvage.empty()) {
		entries = std::move(salvage);
		loadedFrom = salvageFrom;
		return LoadResult::Recovered;
	}
	if (anyPresent)
		Warning("R1Delta: no readable persistent data file found; starting from the legacy profile\n");
	return LoadResult::Missing;
}

struct LegacyProfile
{
	PersistentDataStore::Entries entries;
	std::filesystem::file_time_type modified{};
	bool ownerMarker = false;
};

bool ReadLegacyProfile(const std::filesystem::path& profile, LegacyProfile& legacy)
{
	std::string contents;
	if (!ReadWholeFile(profile, contents, PersistentDataStore::MaxFileSize))
		return false;
	PersistentDataStore::ExtractLegacyProfileEntries(contents, legacy.entries);
	if (legacy.entries.empty())
		return false;
	legacy.ownerMarker = PersistentDataStore::ProfileHasOwnerMarker(contents);
	std::error_code error;
	legacy.modified = std::filesystem::last_write_time(profile, error);
	if (error)
		legacy.modified = std::filesystem::file_time_type::min();
	return true;
}

// Sets the archived marker convar so the next profile.cfg write records that
// its "__" lines are this build's mirror.
void MarkProfileOwner()
{
	ConVarR1* owner = OriginalCCVar_FindVar
		? OriginalCCVar_FindVar(cvarinterface, PersistentDataStore::ProfileOwnerConVar)
		: nullptr;
	if (owner && SetConvarStringOriginal)
		SetConvarStringOriginal(owner, "1");
}

// Creates or updates the "__ key" userinfo convar through the engine's setinfo
// implementation, with persistent flags patched in for the call.
bool SetPersistentConVar(const char* key, const char* value)
{
	auto engine = G_engine;
	auto setinfo_cmd = reinterpret_cast<void(*)(const CCommand&)>(engine + 0x5B520);
	auto setinfo_cmd_flags = reinterpret_cast<int*>(engine + 0x05B5FF);
	void(*ccommand_constructor)(CCommand* thisptr, int nArgC, const char** ppArgV) =
		decltype(ccommand_constructor)(engine + 0x4806F0);

	char name[CCommand::COMMAND_MAX_LENGTH];
	if (!MakePersistConVarName(key, name, sizeof(name)))
		return false;

	static bool setInfoFlagsWritable = false;
	if (!setInfoFlagsWritable) {
		DWORD oldProtection = 0;
		setInfoFlagsWritable = VirtualProtect(
			setinfo_cmd_flags, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtection) != FALSE;
	}
	if (!setInfoFlagsWritable) {
		Warning("Failed to enable persistent setinfo flags\n");
		return false;
	}

	const char* argv[] = { "setinfo", name, value };
	char commandMemory[sizeof(CCommand)];
	CCommand* command = reinterpret_cast<CCommand*>(commandMemory);
	ccommand_constructor(command, 3, argv);
	*setinfo_cmd_flags = kPersistentConVarFlags;
	setinfo_cmd(*command);
	*setinfo_cmd_flags = FCVAR_USERINFO;
	command->~CCommand();

	Store().conVarNames.insert(name);
	return true;
}

bool IsSchemaValidEntry(const std::string& key, const std::string& value)
{
	return IsValidUserInfo(key.c_str()) && IsValidUserInfo(value.c_str())
		&& PDef::IsValidKeyAndValue(key, value);
}

// Mirrors store entries into convars. Entries the active schema rejects (e.g.
// data for a mod that is not loaded) stay dormant in the store and on disk.
size_t ApplyStoreToConVars()
{
	size_t dormant = 0;
	for (const auto& [key, value] : Store().entries) {
		if (!IsSchemaValidEntry(key, value)) {
			++dormant;
			continue;
		}
		char name[CCommand::COMMAND_MAX_LENGTH];
		ConVarR1* existing = MakePersistConVarName(key.c_str(), name, sizeof(name))
			? OriginalCCVar_FindVar(cvarinterface, name)
			: nullptr;
		if (existing && existing->m_Value.m_pszString && value == existing->m_Value.m_pszString) {
			Store().conVarNames.insert(name);
			continue;
		}
		SetPersistentConVar(key.c_str(), value.c_str());
	}
	return dormant;
}

void MarkStoreDirty()
{
	ClientStore& store = Store();
	const double now = Plat_FloatTime();
	if (!store.dirty)
		store.firstDirtyTime = now;
	store.dirty = true;
	store.lastChangeTime = now;
}

bool EnsureStoreLoaded()
{
	ClientStore& store = Store();
	if (store.loaded)
		return !store.disabled;
	store.loaded = true;
	if (IsDedicatedServer() || IsR1ODedicatedServer()) {
		store.disabled = true;
		return false;
	}

	std::filesystem::path directory;
	if (!GetProfileDirectory(directory)) {
		Warning("R1Delta: could not resolve the profile directory; persistent data will not be saved\n");
		store.disabled = true;
		return false;
	}
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	store.path = directory / "persistent_data.txt";
	const std::filesystem::path profile = directory / "profile.cfg";

	std::filesystem::path loadedFrom;
	const LoadResult result = LoadStoreFile(store.path, store.entries, loadedFrom);
	bool changed = false;
	if (result == LoadResult::Recovered) {
		Warning("R1Delta: recovered persistent data from %s\n", loadedFrom.string().c_str());
		// Keep whatever is at the primary path for manual recovery before it
		// gets replaced.
		if (std::filesystem::exists(store.path, error) && loadedFrom != store.path)
			CopyFileW(store.path.c_str(), WithSuffix(store.path, L".corrupt").c_str(), FALSE);
		changed = true;
	}

	// profile.cfg also carries a write-only mirror of the data for older
	// builds. Import from it on the first run after upgrading (no store yet),
	// or when an older build ran in between: the file then lacks the owner
	// marker, is newer than the store, and differs from it.
	LegacyProfile legacy;
	const bool haveLegacy = ReadLegacyProfile(profile, legacy)
		|| (result == LoadResult::Missing && ReadLegacyProfile(WithSuffix(profile, L".bak"), legacy));
	if (haveLegacy) {
		bool import = result == LoadResult::Missing;
		if (!import && !legacy.ownerMarker) {
			const auto storeTime = std::filesystem::last_write_time(loadedFrom, error);
			import = !error && legacy.modified > storeTime;
		}
		if (import) {
			PersistentDataStore::Entries merged = store.entries;
			const size_t imported = PersistentDataStore::MergeLegacyEntries(merged, legacy.entries);
			if (imported) {
				if (result != LoadResult::Missing) {
					// Keep the store as it was, in case the older build changed
					// something it should not have.
					CopyFileW(loadedFrom.c_str(), WithSuffix(store.path, L".pre-import").c_str(), FALSE);
				}
				store.entries = std::move(merged);
				changed = true;
				Msg("R1Delta: imported %u persistent data entries from profile.cfg\n", static_cast<unsigned>(imported));
			}
		}
	}
	MarkProfileOwner();

	const size_t dormant = ApplyStoreToConVars();
	Msg("R1Delta: loaded %u persistent data entries (%u inactive under the current schema)\n",
		static_cast<unsigned>(store.entries.size()), static_cast<unsigned>(dormant));

	std::atexit([] { PData_Flush(true); });

	if (changed) {
		MarkStoreDirty();
		SaveStoreNow(false);
	}
	return true;
}

}

static size_t ClientPersistentConVarCount()
{
	const ClientStore& store = Store();
	return store.loaded ? store.conVarNames.size() : 0;
}

void PData_Flush(bool quiet)
{
	ClientStore& store = Store();
	if (store.loaded && !store.disabled && store.dirty)
		SaveStoreNow(quiet);
}

void PData_OnSchemaReloaded()
{
	ClientStore& store = Store();
	if (!store.loaded || store.disabled)
		return;
	// Entries for a mod that just became active get their convars back.
	ApplyStoreToConVars();
}

// Command handling
void setinfopersist_cmd(const CCommand& args) {
	if (args.ArgC() >= 3) {
		const char* key = args.Arg(1);
		const char* value = args.Arg(2);
		if (!IsValidUserInfo(key)) {
			Warning("Invalid user info key %s. Only certain characters are allowed.\n", key);
			return;
		}
		if (!IsValidUserInfo(value)) {
			Warning("Invalid user info value %s. Only certain characters are allowed.\n", value);
			return;
		}
		if (!PDef::IsValidKeyAndValue(key, value)) {
			Warning("PData key %s, value %s failed validation.\n", key, value);
			return;
		}

		EnsureStoreLoaded();

		// "nosend" updates the local value without replicating it right away.
		const bool noSend = args.ArgC() >= 4
			&& (strcmp_static(args.Arg(3), "nosend") == 0 || strcmp_static(args.Arg(3), "forcehash") == 0);
		const bool previousNoSend = g_bNoSendConVar;
		g_bNoSendConVar = noSend;
		const bool applied = SetPersistentConVar(key, value);
		g_bNoSendConVar = previousNoSend;
		if (!applied)
			return;

		ClientStore& store = Store();
		if (!store.disabled) {
			auto [it, inserted] = store.entries.try_emplace(key, value);
			if (inserted || it->second != value) {
				it->second = value;
				MarkStoreDirty();
			}
		}
	}
	else if (args.ArgC() == 2) {
		char name[CCommand::COMMAND_MAX_LENGTH];
		ConVarR1* var = MakePersistConVarName(args.Arg(1), name, sizeof(name))
			? OriginalCCVar_FindVar(cvarinterface, name)
			: nullptr;
		if (!var)
			var = OriginalCCVar_FindVar(cvarinterface, args.GetCommandString());
		if (var)
			ConVar_PrintDescription(var);
	}
	else {
		Msg("Usage: " PERSIST_COMMAND " <key> <value> [nosend]\n");
	}
}

char ExecuteConfigFile(int configType) {
	if (OriginalCCVar_FindVar && OriginalCCVar_FindVar(cvarinterface, "cl_fovScale"))
		OriginalCCVar_FindVar(cvarinterface, "cl_fovScale")->m_fMaxVal = 1.7f;

	char pathBuffer[1024];
	if (!GetConfigPath(pathBuffer, sizeof(pathBuffer), configType))
		return 0;

	// Persistent data comes from the store, never from profile.cfg. Loading it
	// here keeps it available at the same point in startup as before.
	if (configType == 1)
		EnsureStoreLoaded();

	std::string contents;
	if (!ReadWholeFile(std::filesystem::path(pathBuffer), contents, PersistentDataStore::MaxFileSize)
		|| contents.empty())
		return 0;
	if (configType == 1)
		contents = PersistentDataStore::StripPersistentLines(contents);

	auto engine = G_engine;
	void* (*Exec_CmdGuts)(const char* commands, char bUseExecuteCommand) = decltype(Exec_CmdGuts)(engine + 0x01059A0);
	Exec_CmdGuts(contents.c_str(), 1);
	return 1;
}

void PData_RunFrame()
{
	PData_ServerRunFrame();

	ClientStore& store = Store();
	if (!store.dirty || store.disabled)
		return;
	const double now = Plat_FloatTime();
	if (now < store.retryTime)
		return;
	if (now - store.lastChangeTime >= kSaveDebounceSeconds
		|| now - store.firstDirtyTime >= kSaveMaxDelaySeconds)
		SaveStoreNow(false);
}

using NativeProfileWriterFn = char(__fastcall*)(unsigned int configType);
static NativeProfileWriterFn g_NativeProfileWriterOriginal = nullptr;

// The engine writes profile.cfg on shutdown and when settings change; use it
// as an extra flush point for the store.
static char __fastcall NativeProfileWriterHook(unsigned int configType)
{
	if (configType == 1 && Store().loaded && !Store().disabled)
		MarkProfileOwner();
	const char result = g_NativeProfileWriterOriginal
		? g_NativeProfileWriterOriginal(configType)
		: 0;
	if (configType == 1)
		PData_Flush(false);
	return result;
}

void InstallPersistentProfileWriterHook(uintptr_t engineBase)
{
	if (!engineBase || IsDedicatedServer() || IsR1ODedicatedServer()
		|| g_NativeProfileWriterOriginal)
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0x134850);
	constexpr unsigned char expectedPrologue[] = {
		0x40, 0x53, 0x48, 0x81, 0xEC, 0xA0, 0x04, 0x00, 0x00, 0x8B, 0xD9
	};
	if (memcmp(target, expectedPrologue, sizeof(expectedPrologue)) != 0) {
		Warning("Persistent-data profile writer prologue mismatch at %p; hook not installed\n", target);
		return;
	}

	const MH_STATUS createStatus = MH_CreateHook(
		target,
		reinterpret_cast<void*>(&NativeProfileWriterHook),
		reinterpret_cast<void**>(&g_NativeProfileWriterOriginal));
	if (createStatus != MH_OK) {
		g_NativeProfileWriterOriginal = nullptr;
		Warning("Failed to create persistent-data profile writer hook (%d)\n",
			static_cast<int>(createStatus));
		return;
	}
	const MH_STATUS enableStatus = MH_EnableHook(target);
	if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
		const MH_STATUS removeStatus = MH_RemoveHook(target);
		if (removeStatus == MH_OK || removeStatus == MH_ERROR_NOT_CREATED)
			g_NativeProfileWriterOriginal = nullptr;
		Warning("Failed to enable persistent-data profile writer hook (%d); remove status=%d\n",
			static_cast<int>(enableStatus), static_cast<int>(removeStatus));
	}
}

static bool IsCommandWord(const char* command, const char* word)
{
	const size_t length = strlen(word);
	return _strnicmp(command, word, length) == 0
		&& (command[length] == '\0' || isspace(static_cast<unsigned char>(command[length])) || command[length] == ';');
}

void PData_OnConsoleCommand(const char* str)
{
	if (!str)
		return;
	const char* command = str;
	while (*command == ' ' || *command == '\t' || *command == '\r' || *command == '\n')
		++command;
	// Save before anything that can end the session or the process.
	if (IsCommandWord(command, "quit") || IsCommandWord(command, "exit")
		|| IsCommandWord(command, "disconnect"))
		PData_Flush(false);
}
