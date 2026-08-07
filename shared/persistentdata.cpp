#include "core.h"
#include "filesystem.h" 


// Helper to extract base name from a segment with possible array index
static std::string getBaseArrayName(const std::string_view& segment) {
    size_t bracketPos = segment.find('[');
    return std::string(bracketPos == std::string::npos ? segment : segment.substr(0, bracketPos));
}

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
#include "persistentdata_transaction.h"
#include "logging.h"
#include "squirrel.h"
#include "keyvalues.h"
#include "factory.h"
#include "load.h"
// Network message handling
#include <unordered_map>
#include <cstdint>
#include <charconv>

static std::unordered_map<int, PersistentDataState::PlayerState> s_R1OPersistentUserDataByPlayer;

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
	const char* value,
	bool found,
	size_t entryCount)
{
	if (!PersistentDataDiagnosticsEnabled() || !IsPersistentDataDiagnosticKey(name))
		return;

	char message[512];
	_snprintf_s(
		message,
		sizeof(message),
		_TRUNCATE,
		"R1Delta: pdata-diag stage=%s playerSlot=%d found=%d entries=%zu name=\"%s\" value=\"%s\"\n",
		stage ? stage : "unknown",
		playerSlot,
		found ? 1 : 0,
		entryCount,
		name ? name : "",
		value ? value : "");
	OutputDebugStringA(message);
}
}

static bool IsR1OPersistentPlayerSlot(int playerSlot)
{
	return IsR1ODedicatedServer()
		&& pGlobalVarsServer
		&& PersistentDataSlots::IsValidPlayerSlot(playerSlot, pGlobalVarsServer->maxClients);
}

static const char* R1OPersistKeyPrefix()
{
	return PERSIST_COMMAND" ";
}

static bool IsR1OPersistentUserDataName(const char* name)
{
	return name && strncmp(name, R1OPersistKeyPrefix(), strlen(R1OPersistKeyPrefix())) == 0;
}

static PersistentDataState::Values R1OCollectPersistentUserData(
	const std::vector<NetMessageCvar_t>& values)
{
	PersistentDataState::Values collected;
	for (const NetMessageCvar_t& var : values) {
		if (IsR1OPersistentUserDataName(var.name))
			collected.insert_or_assign(var.name, var.value);
	}
	return collected;
}

bool R1OReplacePersistentUserDataForPlayer(
	int playerSlot,
	PersistentDataState::SessionKey session,
	const std::vector<NetMessageCvar_t>& values)
{
	if (!IsR1OPersistentPlayerSlot(playerSlot))
		return false;

	PersistentDataState::Values replacement = R1OCollectPersistentUserData(values);
	const size_t entryCount = replacement.size();
	for (const auto& entry : replacement) {
			LogPersistentDataDiagnostic(
				"server-snapshot",
				playerSlot,
				entry.first.c_str(),
				entry.second.c_str(),
				true,
				entryCount);
	}
	return PersistentDataState::Replace(
		s_R1OPersistentUserDataByPlayer[playerSlot], session, std::move(replacement));
}

bool R1OMergePersistentUserDataForPlayer(
	int playerSlot,
	PersistentDataState::SessionKey session,
	const std::vector<NetMessageCvar_t>& values)
{
	if (!IsR1OPersistentPlayerSlot(playerSlot))
		return false;

	PersistentDataState::Values updates = R1OCollectPersistentUserData(values);
	auto& state = s_R1OPersistentUserDataByPlayer[playerSlot];
	if (!PersistentDataState::Merge(state, session, std::move(updates)))
		return false;

	for (const NetMessageCvar_t& var : values) {
		if (IsR1OPersistentUserDataName(var.name)) {
			LogPersistentDataDiagnostic(
				"server-delta",
				playerSlot,
				var.name,
				var.value,
				true,
				state.values.size());
		}
	}
	return true;
}

void R1OClearPersistentUserDataForPlayer(int playerSlot)
{
	if (playerSlot >= 0 && playerSlot < PersistentDataSlots::kMaximumSupportedClients)
		s_R1OPersistentUserDataByPlayer.erase(playerSlot);
}

bool R1OStorePersistentUserDataConVar(int playerSlot, const char* name, const char* value)
{
	if (!IsR1OPersistentPlayerSlot(playerSlot)
		|| !IsR1OPersistentUserDataName(name) || !value)
		return false;
	s_R1OPersistentUserDataByPlayer[playerSlot].values[name] = value;
	return true;
}

bool R1OGetPersistentUserDataConVar(int playerSlot, const char* name, std::string& value)
{
	if (!IsR1OPersistentPlayerSlot(playerSlot)
		|| !IsR1OPersistentUserDataName(name))
		return false;
	const auto player = s_R1OPersistentUserDataByPlayer.find(playerSlot);
	if (player == s_R1OPersistentUserDataByPlayer.end())
		return false;
	const auto found = player->second.values.find(name);
	if (found == player->second.values.end()) {
		LogPersistentDataDiagnostic(
			"server-lookup",
			playerSlot,
			name,
			nullptr,
			false,
			player->second.values.size());
		return false;
	}
	value = found->second;
	LogPersistentDataDiagnostic(
		"server-lookup",
		playerSlot,
		name,
		value.c_str(),
		true,
		player->second.values.size());
	return true;
}

static const char* R1OFindPersistentUserDataConVar(int playerSlot, const char* name, const char* defaultValue)
{
	static thread_local std::string value;
	return R1OGetPersistentUserDataConVar(playerSlot, name, value) ? value.c_str() : defaultValue;
}

static void* R1OGetEntityFromScriptArgument(HSQUIRRELVM vm, SQInteger index)
{
	return sq_getentity(vm, index);
}

struct R1OPersistentPlayerContext
{
	int playerSlot = -1;
	uintptr_t edict = 0;
};

static bool R1OResolvePersistentPlayer(void* entity, R1OPersistentPlayerContext& context)
{
	if (!entity || !pGlobalVarsServer || !pGlobalVarsServer->pEdicts)
		return false;

	__try {
		const uintptr_t edict = *reinterpret_cast<const uintptr_t*>(
			reinterpret_cast<uintptr_t>(entity) + 0x40);
		const uintptr_t firstEdict = reinterpret_cast<uintptr_t>(pGlobalVarsServer->pEdicts);
		if (edict < firstEdict + 56)
			return false;
		const uintptr_t delta = edict - firstEdict;
		if (delta % 56 != 0)
			return false;
		const int playerSlot = static_cast<int>(delta / 56) - 1;
		if (playerSlot < 0 || playerSlot >= PersistentDataSlots::kMaximumSupportedClients)
			return false;
		context.playerSlot = playerSlot;
		context.edict = edict;
		static int ownerLogBudget = 8;
		if (IsR1OPersistentPlayerSlot(playerSlot)
			&& ownerLogBudget > 0
			&& AreR1OFakeDediVerboseLogsEnabled()) {
			--ownerLogBudget;
			char message[192];
			_snprintf_s(
				message,
				sizeof(message),
				_TRUNCATE,
				"R1Delta: R1O persistence script owner playerSlot=%d entity=%p\n",
				playerSlot,
				entity);
			OutputDebugStringA(message);
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static int R1OPlayerSlotFromEntity(void* entity)
{
	R1OPersistentPlayerContext context;
	return R1OResolvePersistentPlayer(entity, context)
		&& IsR1OPersistentPlayerSlot(context.playerSlot)
		? context.playerSlot
		: -1;
}

static bool R1OSendPersistentUserDataCommand(
	const R1OPersistentPlayerContext& player,
	const char* hashedKey,
	const char* value)
{
	if (!player.edict || !hashedKey || !value)
		return false;

	// Use the exact native interface that R1OFactory handed to server_local.dll.
	// dedicated.dll's app-system factory does not expose this interface in fake
	// dedicated mode. ClientCommand is slot 37 in VEngineServer022.
	void* engineServer = GetR1ONativeEngineServer022();
	if (!engineServer)
		return false;
	const auto vtable = *reinterpret_cast<uintptr_t* const*>(engineServer);
	if (!vtable || !vtable[37])
		return false;

	using ClientCommandFn = void(__fastcall*)(void*, uintptr_t, const char*, ...);
	const auto clientCommand = reinterpret_cast<ClientCommandFn>(vtable[37]);
	clientCommand(
		engineServer,
		player.edict,
		PERSIST_COMMAND" \"%s\" \"%s\"",
		hashedKey,
		value);
	static int deliveryLogBudget = 16;
	if (deliveryLogBudget > 0 && AreR1OFakeDediVerboseLogsEnabled()) {
		--deliveryLogBudget;
		char message[256];
		_snprintf_s(
			message,
			sizeof(message),
			_TRUNCATE,
			"R1Delta: R1O persistence client update playerSlot=%d key=%s\n",
			player.playerSlot,
			hashedKey);
		OutputDebugStringA(message);
	}
	return true;
}

static bool ParseR1OPersistentInteger(const std::string& value, int& result)
{
	if (value.empty())
		return false;
	const char* begin = value.data();
	const char* end = begin + value.size();
	const auto parsed = std::from_chars(begin, end, result);
	return parsed.ec == std::errc() && parsed.ptr == end;
}

#include <shlobj.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <variant>
#include <optional>
#include <fstream>
#include <sstream>
#include <cctype>
#include <iostream>
#include <regex>
#include <limits>
#include <zstd.h>
#include "load.h"
#include "tctx.h"

//#define HASH_USERINFO_KEYS
// Constants
constexpr size_t MAX_LENGTH = 254;
constexpr const char* INVALID_CHARS = "{}()':;`\"\n";
bool g_bNoSendConVar = false;

// TODO(mrsteyk): this shit must be checked in validator too, no?
// Utility functions
bool IsValidUserInfo(const char* value, int length) {
	if (!value || !*value) return false; // Null or empty check

	size_t len = (length == -1) ? strlen(value) : length;
	if (len > MAX_LENGTH) return false;

	// For values: Only allow 0-9, -, ., and a-zA-Z for pdata_null
	// For keys: Only allow a-z, A-Z, 0-9, _, ., and [] for array indices
	for (size_t i = 0; i < len; i++) {
		char c = value[i];

		// Basic ASCII printable range
		if (c < 32 || c > 126) return false;

		// Explicitly denied characters that could cause problems:
		switch (c) {
		case '"':  // String termination
		case '\\': // Escapes
		case '{':  // Code blocks/JSON
		case '}':
		case '\'': // String delimiters
		case '`':
		case ';':  // Command separators
		case '/':
		case '*':
		case '<':  // XML/HTML
		case '>':
		case '&':  // Shell
		case '|':
		case '$':
		case '!':
		case '?':
		case '+':  // URL encoding
		case '%':
		case '\n': // Any whitespace except regular space
		case '\r':
		case '\t':
		case '\v':
		case '\f':
			return false;
	}
}

	return true;
}
std::string hashUserInfoKey(const std::string& key) {
#ifdef HASH_USERINFO_KEYS
	// Hash the key
	std::size_t hash = std::hash<std::string>{}(key);

	// Convert to base36
	const char base36Chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	std::string result;

	do {
		result.push_back(base36Chars[hash % 36]);
		hash /= 36;
	} while (hash > 0);

	// Reverse the string to get the correct order
	std::reverse(result.begin(), result.end());

	// Truncate to maximum allowed length if necessary
	constexpr size_t MAX_KEY_LENGTH = 254 - sizeof(PERSIST_COMMAND);
	if (result.length() > MAX_KEY_LENGTH) {
		result = result.substr(0, MAX_KEY_LENGTH);
	}

	return result;
#else
	return key;
#endif
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
	std::string resolveArrayIndices(const std::string_view& key) const;

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

std::string PDataValidator::resolveArrayIndices(const std::string_view& key) const {
	ZoneScoped;

    std::vector<std::string> segments = splitOnDot(key);
    std::string resolvedKey;
    std::string currentValidationBase;  // Tracks the base name for validation
    
    for (const auto& segment : segments) {
        // Get base name without any array indices for validation
        std::string baseName = getBaseArrayName(segment);
        
        // Build the validation base with dot separators
        if (!currentValidationBase.empty()) {
            currentValidationBase += ".";
        }
        currentValidationBase += baseName;

        // Process array indices if present
        size_t bracketPos = segment.find('[');
        if (bracketPos != std::string::npos) {
            std::string indexStr = segment.substr(bracketPos+1, segment.find(']')-bracketPos-1);
            
            // Validate using the accumulated base name without indices
            if (!validateArrayAccess(currentValidationBase, indexStr)) {
                Warning(__FUNCTION__ ": out of bound pdata array index %s for %s!\n", 
                    indexStr.c_str(), currentValidationBase.c_str());
                return "";
            }
        }

        // Build resolved key with indices
        if (!resolvedKey.empty()) {
            resolvedKey += ".";
        }
        resolvedKey += segment;
    }
    
    return resolvedKey;
}

bool PDataValidator::isValid(const std::string_view& key, const std::string_view& value) const
{
    std::string resolvedKey = resolveArrayIndices(key);
    if (resolvedKey.empty()) return false; // Invalid indices
    std::vector<std::string> segments = splitOnDot(resolvedKey);
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

	std::string PDef::ResolveKeyIndices(const std::string_view& key) {
		// Initialize validator on first use
		std::call_once(s_initFlag, InitValidator);

		if (!s_validator) {
			Error("PData validator failed to initialize");
		}
		
		return s_validator->resolveArrayIndices(key);
	}


std::unique_ptr<PDataValidator> PDef::s_validator;
std::once_flag PDef::s_initFlag;

namespace {
constexpr uint32_t kPackedPDataMinEntries = 32;

bool IsPersistentConVarName(const char* name)
{
	constexpr char prefix[] = PERSIST_COMMAND" ";
	return name && strncmp(name, prefix, sizeof(prefix) - 1) == 0;
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
	const size_t prefixLength = sizeof(PERSIST_COMMAND" ") - 1;
	for (int i = 0; i < message->m_ConVars.Count(); ++i) {
		const NetMessageCvar_t& var = message->m_ConVars[i];
		if (!IsPersistentConVarName(var.name))
			continue;
		LogPersistentDataDiagnostic(
			"client-pack",
			-1,
			var.name,
			var.value,
			true,
			entries.size() + 1);

		const char* key = var.name + prefixLength;
		if (!PDef::IsValidKeyAndValue(key, var.value))
			return false;
		entries.push_back({ key, var.value });
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
		if (entry.key.size() >= sizeof(var.name) || entry.value.size() >= sizeof(var.value))
			return false;
		memcpy(var.name, entry.key.c_str(), entry.key.size() + 1);
		if (!SafePrefixConVarName(var.name, sizeof(var.name), PERSIST_COMMAND" "))
			return false;
		memcpy(var.value, entry.value.c_str(), entry.value.size() + 1);
		if (!PDef::IsValidKeyAndValue(var.name + sizeof(PERSIST_COMMAND" ") - 1, var.value))
			return false;
		decoded.push_back(var);
	}
	output = std::move(decoded);
	return true;
}
bool NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer) {
	const int startBit = buffer.GetNumBitsWritten();
	if (g_bNoSendConVar) {
		buffer.WriteByte(0);
		return !buffer.IsOverflowed();
	}
	if (!IsDedicatedServer()) {
		auto var = OriginalCCVar_FindVar(cvarinterface, "net_secure");
		const bool vanilla = var && var->m_Value.m_nValue == 1;
		if (vanilla) {
			for (int i = thisptr->m_ConVars.Count() - 1; i >= 0; --i) {
				if (thisptr->m_ConVars[i].name[0] == '_')
					thisptr->m_ConVars.Remove(i);
			}
		}
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
				const unsigned long long generated =
					100000000000000000ULL
					+ ((static_cast<unsigned long long>(GetCurrentProcessId()) << 32) ^ GetTickCount64());
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
			constexpr size_t prefixLength = sizeof(PERSIST_COMMAND" ") - 1;
			char modifiedName[sizeof(var.name)] = {};
			modifiedName[0] = static_cast<char>(static_cast<unsigned char>(var.name[prefixLength]) | 0x80);
			strcpy_s(modifiedName + 1, sizeof(modifiedName) - 1, var.name + prefixLength + 1);
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
			"R1Delta: NET_SetConVar write count=%u original=%d packedEntries=%u packedChunks=%u startBit=%d endBit=%d result=%d\n",
			numvars,
			thisptr->m_ConVars.Count(),
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

	// Move the existing name to make room for prefix (including null terminator)
	memmove(name + prefixLen, name, nameLen + 1);

	// Copy the prefix
	memcpy(name, prefix, prefixLen);

	return true;
}

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

		// Check if this is a persistent data convar by checking the high bit
		if (static_cast<unsigned char>(var.name[0]) & 0x80) {
			sawLegacyPData = true;
			// Clear the high bit for validation
			var.name[0] &= 0x7F;

			// Create string views for validation without the prefix
			std::string nameStr(var.name);
			std::string valueStr(var.value);

			if (!PDef::IsValidKeyAndValue(nameStr, valueStr)) {
				Warning("Invalid persistent data convar: key=%s value=%s\n", var.name, var.value);
				return false;
			}

			if (!SafePrefixConVarName(var.name, sizeof(var.name), PERSIST_COMMAND" ")) {
				Warning("Failed to prefix persistent data convar\n");
				return false;
			}
		}
		else {
			// Skip networkid_force CVar case-insensitively
			if (::_stricmp(var.name, "networkid_force") == 0) {
				continue; // Skip this CVar
			}

			// Check if convar exists and has FCVAR_USERINFO flag
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
				"R1Delta: NET_SetConVar decoded packedEntries=%zu total=%zu encodedBytes=%zu\n",
				decodedPersistentCount,
				staged.size(),
				packedPData.size());
			OutputDebugStringA(message);
		}
	}
	return true;
}
const char* hashUserInfoKeyArena(Arena* arena, const char* key)
{
	ZoneScoped;

#ifdef HASH_USERINFO_KEYS
# error NOT IMPLEMENTED
#else
	// First resolve array indices
	std::string resolvedKey = PDef::ResolveKeyIndices(key);

	// NOTE(mrsteyk): guarantee key length validity.
	constexpr size_t MAX_LENGTH_DUP = MAX_LENGTH - sizeof(PERSIST_COMMAND);
	auto len = resolvedKey.length();
	if (len > MAX_LENGTH_DUP)
	{
		R1DAssert(!"Bad stuff happened!");
		len = MAX_LENGTH_DUP;
	}

	auto ret = (char*)arena_push(arena, len + 1);
	memcpy(ret, key, len);
	// TODO(mrsteyk): debug only check?
	//if (std::string(key) != std::string(ret))
	if (key[len] != 0 || !!memcmp(ret, key, len))
	{
		R1DAssert(!"in != out");
		Msg("hashUserInfoKeyArena: in: %s out %s\n", key, ret);
	}
	return ret;
#endif
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

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	auto hashedKey = hashUserInfoKeyArena(arena, key);
	auto hashedKey_len = strlen(hashedKey);
	size_t varName_size = hashedKey_len + sizeof(PERSIST_COMMAND) + 1;
	auto varName = (char*)arena_push(arena, varName_size);
	memcpy(varName, PERSIST_COMMAND" ", sizeof(PERSIST_COMMAND));
	memcpy(varName + sizeof(PERSIST_COMMAND), hashedKey, hashedKey_len);
	varName[sizeof(PERSIST_COMMAND) + hashedKey_len] = '\0';
	
	// NOTE(mrsteyk): hashed key can't be invalid, that must be a guarantee of hashUserInfoKey(Arena) given a valid key.
	//                -1 cuz null terminator.
	R1DAssert(IsValidUserInfo(varName, varName_size - 1));

	auto var = OriginalCCVar_FindVar(cvarinterface, varName);

	if (!var) {
		//Warning("Client couldn't find persistent value: key=%s, hashedKey=%s, hashed=%s\n",
		//    key, hashedKey.c_str(), "true");

		sq_pushstring(v, defaultValue, -1);
	}
	else {
		//Msg("Client accessing persistent value: key=%s, hashedKey=%s, value=%s, hashed=%s\n",
		//    key, hashedKey.c_str(), var->m_Value.m_pszString, "true");
		sq_pushstring(v, var->m_Value.m_pszString, -1);
	}

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
CBaseClient* g_pClientArray;
CBaseClientDS* g_pClientArrayDS;




KeyValues* GetClientConVarsKV(short index) {
	if (index < 0 || IsR1ODedicatedServer())
		return nullptr;
	if (IsDedicatedServer()) {
		if (!g_pClientArrayDS)
			return nullptr;
		return g_pClientArrayDS[index].m_ConVars;
	}
	else {
		if (!g_pClientArray)
			return nullptr;
		return g_pClientArray[index].m_ConVars;
	}
}

void Script_XPChanged_Rebuild(void* pPlayer) {
	if (IsR1ODedicatedServer()) {
		const int playerSlot = R1OPlayerSlotFromEntity(pPlayer);
		std::string value;
		int xp = 0;
		if (playerSlot < 0
			|| !R1OGetPersistentUserDataConVar(playerSlot, PERSIST_COMMAND" xp", value)
			|| !ParseR1OPersistentInteger(value, xp))
			return;
		if (!R1OMarkTFOPlayerNetworkStateChanged(pPlayer)) {
			Warning("Failed to mark R1O player XP for replication\n");
			return;
		}
		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pPlayer) + 0x1834) = xp;
		return;
	}

	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

	auto vars = GetClientConVarsKV(index);

	if (!vars) {
		return;
	}

	auto var = vars->GetInt(PERSIST_COMMAND" xp",0);
	auto netValue = *reinterpret_cast<int*>(reinterpret_cast<__int64>(pPlayer) + 0x1834);
	if (var == 0)
		return;

	if (var == netValue)
		return;

	*reinterpret_cast<int*>(reinterpret_cast<__int64>(pPlayer) + 0x1834) = var;
}


void Script_GenChanged_Rebuild(void* pPlayer) {
	if (IsR1ODedicatedServer()) {
		const int playerSlot = R1OPlayerSlotFromEntity(pPlayer);
		std::string value;
		int generation = 0;
		if (playerSlot < 0
			|| !R1OGetPersistentUserDataConVar(playerSlot, PERSIST_COMMAND" gen", value)
			|| !ParseR1OPersistentInteger(value, generation))
			return;
		generation = (std::max)(0, (std::min)(9, generation));
		if (!R1OMarkTFOPlayerNetworkStateChanged(pPlayer)) {
			Warning("Failed to mark R1O player generation for replication\n");
			return;
		}
		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pPlayer) + 0x183C) = generation;
		return;
	}

	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

	auto vars = GetClientConVarsKV(index);

	if (!vars) {
		return;
	}

	auto var = vars->GetInt(PERSIST_COMMAND" gen", 0);

	auto netValue = *reinterpret_cast<int*>(reinterpret_cast<__int64>(pPlayer) + 0x183C);

	if (var == 0)
		return;

	if (var == netValue)
		return;

	*reinterpret_cast<int*>(reinterpret_cast<__int64>(pPlayer) + 0x183C) = var;
	auto netValueAfter = *reinterpret_cast<int*>(reinterpret_cast<__int64>(pPlayer) + 0x183C);

}



SQInteger Script_ServerGetPersistentUserDataKVString(HSQUIRRELVM v) {
	if (IsR1ODedicatedServer()) {
		void* entity = R1OGetEntityFromScriptArgument(v, 2);
		if (!entity)
			return sq_throwerror(v, "player is null");
		R1OPersistentPlayerContext player;
		if (!R1OResolvePersistentPlayer(entity, player))
			return sq_throwerror(v, "player is not backed by a valid edict");
		const char* pKey, * pDefaultValue;
		if (SQ_FAILED(sq_getstring(v, 3, &pKey)) || SQ_FAILED(sq_getstring(v, 4, &pDefaultValue)))
			return sq_throwerror(v, "Expected key and default string parameters");
		if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pDefaultValue))
			return sq_throwerror(v, "Invalid user info key or default value.");
		if (PersistentDataSlots::IsReplayPlayerSlot(player.playerSlot)) {
			sq_pushstring(v, pDefaultValue, -1);
			return 1;
		}
		if (!IsR1OPersistentPlayerSlot(player.playerSlot))
			return sq_throwerror(v, "player is not an active client");

		auto arena = tctx.get_arena_for_scratch();
		auto temp = TempArena(arena);
		auto hashedKey = hashUserInfoKeyArena(arena, pKey);
		auto hashedKey_len = strlen(hashedKey);
		size_t modifiedKey_size = hashedKey_len + sizeof(PERSIST_COMMAND) + 1;
		auto modifiedKey = (char*)arena_push(arena, modifiedKey_size);
		memcpy(modifiedKey, PERSIST_COMMAND" ", sizeof(PERSIST_COMMAND));
		memcpy(modifiedKey + sizeof(PERSIST_COMMAND), hashedKey, hashedKey_len);
		modifiedKey[sizeof(PERSIST_COMMAND) + hashedKey_len] = '\0';
		sq_pushstring(v, R1OFindPersistentUserDataConVar(player.playerSlot, modifiedKey, pDefaultValue), -1);
		return 1;
	}
	const void* pPlayer = sq_getentity(v, 2);
	if (!pPlayer) {
		return sq_throwerror(v, "player is null");
	}

	const char* pKey, * pDefaultValue;
	sq_getstring(v, 3, &pKey);
	sq_getstring(v, 4, &pDefaultValue);
	if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pDefaultValue)) {
		return sq_throwerror(v, "Invalid user info key or default value.");
	}

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	auto hashedKey = hashUserInfoKeyArena(arena, pKey);
	auto hashedKey_len = strlen(hashedKey);
	size_t modifiedKey_size = hashedKey_len + sizeof(PERSIST_COMMAND) + 1;
	auto modifiedKey = (char*)arena_push(arena, modifiedKey_size);
	memcpy(modifiedKey, PERSIST_COMMAND" ", sizeof(PERSIST_COMMAND));
	memcpy(modifiedKey + sizeof(PERSIST_COMMAND), hashedKey, hashedKey_len);
	modifiedKey[sizeof(PERSIST_COMMAND) + hashedKey_len] = '\0';

	R1DAssert(IsValidUserInfo(modifiedKey));

	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

	if (index == 18 || !GetClientConVarsKV(index)) {
		//return sq_throwerror(v, "Client has NULL m_ConVars.");
		//Msg("REPLAY on server tried to access persistent value: key=%s, hashedKey=%s, hashed=%s\n",
		//	pKey, hashedKey.c_str(), "true");

		sq_pushstring(v, pDefaultValue, -1); // I HATE REPLAY
		return 1;
	}

	const char* pResult = GetClientConVarsKV(index)->GetString(modifiedKey, pDefaultValue);
	//Msg("Server accessing persistent value: key=%s, hashedKey=%s, value=%s, hashed=%s\n",
	//	pKey, hashedKey.c_str(), pResult, "true");

	sq_pushstring(v, pResult, -1);
	return 1;
}

SQInteger Script_ServerSetPersistentUserDataKVString(HSQUIRRELVM v) {
	if (IsR1ODedicatedServer()) {
		void* entity = R1OGetEntityFromScriptArgument(v, 2);
		if (!entity)
			return sq_throwerror(v, "player is null");
		R1OPersistentPlayerContext player;
		if (!R1OResolvePersistentPlayer(entity, player))
			return sq_throwerror(v, "player is not backed by a valid edict");
		const char* pKey, * pValue;
		if (SQ_FAILED(sq_getstring(v, 3, &pKey)) || SQ_FAILED(sq_getstring(v, 4, &pValue)))
			return sq_throwerror(v, "Expected key and value string parameters");
		if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pValue))
			return sq_throwerror(v, "Invalid user info key or value.");
		if (PersistentDataSlots::IsReplayPlayerSlot(player.playerSlot)) {
			sq_pushstring(v, pValue, -1);
			return 1;
		}
		if (!IsR1OPersistentPlayerSlot(player.playerSlot))
			return sq_throwerror(v, "player is not an active client");

		auto arena = tctx.get_arena_for_scratch();
		auto temp = TempArena(arena);
		auto hashedKey = hashUserInfoKeyArena(arena, pKey);
		auto hashedKey_len = strlen(hashedKey);
		size_t modifiedKey_size = hashedKey_len + sizeof(PERSIST_COMMAND) + 1;
		auto modifiedKey = (char*)arena_push(arena, modifiedKey_size);
		memcpy(modifiedKey, PERSIST_COMMAND" ", sizeof(PERSIST_COMMAND));
		memcpy(modifiedKey + sizeof(PERSIST_COMMAND), hashedKey, hashedKey_len);
		modifiedKey[sizeof(PERSIST_COMMAND) + hashedKey_len] = '\0';
		if (!R1OSendPersistentUserDataCommand(player, hashedKey, pValue))
			return sq_throwerror(v, "failed to send persistent data update to client");
		if (!R1OStorePersistentUserDataConVar(player.playerSlot, modifiedKey, pValue))
			return sq_throwerror(v, "failed to store persistent data");
		sq_pushstring(v, pValue, -1);
		return 1;
	}
	static void (*CVEngineServer_ClientCommand)(__int64 a1, __int64 a2, const char* a3, ...) = 0;
	if (!CVEngineServer_ClientCommand && !IsDedicatedServer())
		CVEngineServer_ClientCommand = decltype(CVEngineServer_ClientCommand)(G_engine + 0xFE7F0);
	else if (!CVEngineServer_ClientCommand)
		CVEngineServer_ClientCommand = decltype(CVEngineServer_ClientCommand)(G_engine_ds + 0x6F030);
	const void* pPlayer = sq_getentity(v, 2);
	if (!pPlayer) {
		return sq_throwerror(v, "player is null");
	}

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	const char* pKey, * pValue;
	sq_getstring(v, 3, &pKey);
	sq_getstring(v, 4, &pValue);
	if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pValue)) {
		return sq_throwerror(v, "Invalid user info key or value.");
	}
	
	auto hashedKey = hashUserInfoKeyArena(arena, pKey);
	auto hashedKey_len = strlen(hashedKey);
	size_t modifiedKey_size = hashedKey_len + sizeof(PERSIST_COMMAND) + 1;
	auto modifiedKey = (char*)arena_push(arena, modifiedKey_size);
	memcpy(modifiedKey, PERSIST_COMMAND" ", sizeof(PERSIST_COMMAND));
	memcpy(modifiedKey + sizeof(PERSIST_COMMAND), hashedKey, hashedKey_len);
	modifiedKey[sizeof(PERSIST_COMMAND) + hashedKey_len] = '\0';

	R1DAssert(IsValidUserInfo(modifiedKey));

	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);

	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

	if (!(!GetClientConVarsKV(index) || index == 18)) {
		//return sq_throwerror(v, "Client has NULL m_ConVars.");
		CVEngineServer_ClientCommand(0, edict, PERSIST_COMMAND" \"%s\" \"%s\"", hashedKey, pValue);
		GetClientConVarsKV(index)->SetString(modifiedKey, pValue);
		//Msg("Server setting persistent value: key=%s, value=%s, hashed=%s\n",
		//	pKey, pValue, "true");
	}
	else {
		//Msg("Trying to set persistent value on REPLAY on server: key=%s, hashedKey=%s, value=%s, hashed=%s\n",
		//	pKey, hashedKey.c_str(), pValue, "true");
	}

	sq_pushstring(v, pValue, -1);
	return 1;
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


static bool g_bTimerActive = false;
static double g_flLastCommandTime = 0.0;
static constexpr double SAVE_DELAY = 5.0;
static bool g_bRecursive = false;
static bool g_bSaveWritePending = false;
static bool g_bSaveQueuedThisFrame = false;
static bool g_bFinishSaveBeforeQuit = false;
static bool g_bSchemaReloadPersistenceSafe = true;
static bool g_bProfileReplayComplete = false;
using NativeProfileWriterFn = char(__fastcall*)(unsigned int configType);
static NativeProfileWriterFn g_NativeProfileWriterOriginal = nullptr;

namespace {
constexpr int kPersistentSchemaFlags =
	FCVAR_PERSIST | FCVAR_ARCHIVE_PLAYERPROFILE | FCVAR_USERINFO;

struct PersistentConVarBinding {
	std::string logicalKey;
	int conVarFlags = 0;
	int parentFlags = 0;
};

std::unordered_map<std::string, PersistentConVarBinding> g_persistentConVarBindings;
using PersistentValueSnapshot = std::unordered_map<
	std::string, std::string, HashStrings, std::equal_to<>>;
PersistentValueSnapshot g_pendingProfileValues;

void ApplyPersistentSchemaFlags(
	ConVarR1* conVar,
	const PersistentConVarBinding& binding,
	bool enabled)
{
	if (!conVar)
		return;
	conVar->m_nFlags = (conVar->m_nFlags & ~kPersistentSchemaFlags)
		| (enabled ? binding.conVarFlags : 0);
	ConVarR1* parent = conVar->m_pParent;
	if (parent && parent != conVar) {
		parent->m_nFlags = (parent->m_nFlags & ~kPersistentSchemaFlags)
			| (enabled ? binding.parentFlags : 0);
	}
}

void RememberPersistentConVar(
	const char* conVarName,
	std::string_view logicalKey,
	ConVarR1* conVar)
{
	if (!conVarName || !*conVarName || !conVar)
		return;
	auto [bindingIt, inserted] = g_persistentConVarBindings.try_emplace(conVarName);
	PersistentConVarBinding& binding = bindingIt->second;
	binding.logicalKey.assign(logicalKey);
	const int conVarFlags = conVar->m_nFlags & kPersistentSchemaFlags;
	ConVarR1* parent = conVar->m_pParent;
	const int parentFlags = parent && parent != conVar
		? parent->m_nFlags & kPersistentSchemaFlags
		: 0;
	if (inserted || conVarFlags)
		binding.conVarFlags = conVarFlags;
	if (inserted || parentFlags)
		binding.parentFlags = parentFlags;
	ApplyPersistentSchemaFlags(conVar, binding, true);
}
}

static bool ValidateProfileContents(
	std::string_view contents,
	PersistentDataCodec::ProfileEntryValidator persistentValidator = nullptr,
	void* validatorContext = nullptr)
{
	return PersistentDataCodec::ValidateProfile(contents, persistentValidator, validatorContext);
}

static bool ReadValidProfileFile(
	const std::filesystem::path& path,
	std::string* contents = nullptr,
	PersistentDataCodec::ProfileEntryValidator persistentValidator = nullptr,
	void* validatorContext = nullptr)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error) || error)
		return false;
	const uintmax_t fileSize = std::filesystem::file_size(path, error);
	if (error || !fileSize || fileSize > PersistentDataCodec::MaxRawSize)
		return false;

	std::string loaded(static_cast<size_t>(fileSize), '\0');
	std::ifstream file(path, std::ios::binary);
	if (!file.read(loaded.data(), static_cast<std::streamsize>(loaded.size()))
		|| !ValidateProfileContents(loaded, persistentValidator, validatorContext))
		return false;
	if (contents)
		*contents = std::move(loaded);
	return true;
}

static bool ReplaceFileWithCopy(const std::filesystem::path& source, const std::filesystem::path& destination)
{
	std::filesystem::path temporary = destination;
	temporary += ".tmp";
	DeleteFileW(temporary.c_str());
	if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE))
		return false;
	if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(temporary.c_str());
		return false;
	}
	return true;
}

static bool ReplaceFileWithContents(
	const std::filesystem::path& destination,
	std::string_view contents)
{
	if (contents.empty() || contents.size() > PersistentDataCodec::MaxRawSize)
		return false;
	std::filesystem::path temporary = destination;
	temporary += ".tmp";
	DeleteFileW(temporary.c_str());
	HANDLE file = CreateFileW(
		temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	const bool writeSucceeded = WriteFile(
		file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) != FALSE
		&& written == contents.size()
		&& FlushFileBuffers(file) != FALSE;
	CloseHandle(file);
	if (!writeSucceeded
		|| !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(temporary.c_str());
		return false;
	}
	return true;
}

static bool PreservePreviousPersistentEntries(
	const std::filesystem::path& profile,
	std::string_view current,
	std::string_view previous)
{
	std::string merged;
	if (!PersistentDataCodec::PreserveMissingPersistentEntries(current, previous, merged))
		return false;
	return merged == current || ReplaceFileWithContents(profile, merged);
}

static bool GetProfileTransactionPaths(
	std::filesystem::path& profile,
	std::filesystem::path& backup,
	std::filesystem::path& marker)
{
	char path[MAX_PATH * 4] = {};
	if (!GetConfigPath(path, sizeof(path), 1))
		return false;
	profile = std::filesystem::path(path);
	// Ensure the profile directory exists before the transaction creates its
	// save marker there. On fresh installs nothing else creates this folder —
	// the native profile writer would create it implicitly on write, but the
	// transaction gates the native writer with a marker file first. Without
	// this, every save attempt fails for first-time users and progression
	// silently never persists.
	std::error_code directoryError;
	std::filesystem::create_directories(profile.parent_path(), directoryError);
	if (directoryError) {
		Warning("Could not create persistent-data profile directory '%s': %s\n",
			profile.parent_path().string().c_str(), directoryError.message().c_str());
		return false;
	}
	backup = profile;
	backup += ".bak";
	marker = profile;
	marker += ".saving";
	return true;
}

static bool CaptureCurrentPersistentValues(PersistentValueSnapshot& snapshot)
{
	snapshot.clear();
	if (!OriginalCCVar_FindVar && !g_persistentConVarBindings.empty())
		return false;
	constexpr std::string_view prefix = PERSIST_COMMAND" ";
	for (const auto& entry : g_persistentConVarBindings) {
		const std::string& conVarName = entry.first;
		ConVarR1* conVar = OriginalCCVar_FindVar
			? OriginalCCVar_FindVar(cvarinterface, conVarName.c_str())
			: nullptr;
		if (!conVar || !(conVar->m_nFlags & kPersistentSchemaFlags))
			continue;
		if (conVarName.compare(0, prefix.size(), prefix) != 0)
			return false;
		const char* value = conVar->m_Value.m_pszString;
		if (!value)
			return false;
		auto [it, inserted] = snapshot.try_emplace(
			conVarName.substr(prefix.size()), value);
		if (!inserted && it->second != value)
			return false;
	}
	return true;
}

static bool MatchRequiredPersistentValue(
	std::string_view key,
	std::string_view value,
	void* context)
{
	auto& remaining = *static_cast<PersistentValueSnapshot*>(context);
	auto it = remaining.find(key);
	if (it == remaining.end())
		return true;
	if (it->second != value)
		return false;
	remaining.erase(it);
	return true;
}

static bool ProfileContainsPersistentValues(
	const std::filesystem::path& profile,
	const PersistentValueSnapshot& required)
{
	PersistentValueSnapshot remaining = required;
	return ReadValidProfileFile(
		profile, nullptr, MatchRequiredPersistentValue, &remaining)
		&& remaining.empty();
}

void PData_ReconcilePersistentConVars()
{
	if (!OriginalCCVar_FindVar)
		return;

	size_t disabled = 0;
	size_t reenabled = 0;
	for (const auto& [conVarName, binding] : g_persistentConVarBindings) {
		ConVarR1* conVar = OriginalCCVar_FindVar(cvarinterface, conVarName.c_str());
		if (!conVar)
			continue;
		const bool wasEnabled = (conVar->m_nFlags & kPersistentSchemaFlags) != 0;
		const char* value = conVar->m_Value.m_pszString;
		const bool schemaEnabled = value
			&& IsValidUserInfo(binding.logicalKey.c_str())
			&& IsValidUserInfo(value)
			&& PDef::IsValidKeyAndValue(binding.logicalKey, value);
		const bool enabled = schemaEnabled
			|| (!g_bSchemaReloadPersistenceSafe && wasEnabled);
		ApplyPersistentSchemaFlags(conVar, binding, enabled);
		if (wasEnabled && !enabled)
			++disabled;
		else if (!wasEnabled && enabled)
			++reenabled;
	}
	if (disabled || reenabled) {
		Msg("Reconciled persistent ConVars with active schema: disabled=%zu reenabled=%zu.\n",
			disabled, reenabled);
	}
}

static bool BeginProfileSaveTransaction(
	const PersistentValueSnapshot* requiredValues = nullptr)
{
	PersistentValueSnapshot snapshot;
	if (requiredValues)
		snapshot = *requiredValues;
	PData_ReconcilePersistentConVars();
	if (!requiredValues && !CaptureCurrentPersistentValues(snapshot)) {
		Warning("Could not capture current persistent values before profile save\n");
		return false;
	}

	std::filesystem::path profile;
	std::filesystem::path backup;
	std::filesystem::path marker;
	if (!GetProfileTransactionPaths(profile, backup, marker))
		return false;

	std::error_code error;
	const bool profileExists = std::filesystem::exists(profile, error) && !error;
	if (profileExists && !ReadValidProfileFile(profile)) {
		Warning("Refusing to overwrite invalid persistent-data profile\n");
		return false;
	}
	if (profileExists && !ReplaceFileWithCopy(profile, backup)) {
		Warning("Failed to create persistent-data backup before save\n");
		return false;
	}

	HANDLE markerHandle = CreateFileW(
		marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (markerHandle == INVALID_HANDLE_VALUE) {
		Warning("Failed to create persistent-data save marker\n");
		return false;
	}
	const char markerContents[] = "R1Delta persistent-data save in progress\n";
	DWORD written = 0;
	const bool markerWritten = WriteFile(
		markerHandle, markerContents, sizeof(markerContents) - 1, &written, nullptr) != FALSE
		&& written == sizeof(markerContents) - 1
		&& FlushFileBuffers(markerHandle) != FALSE;
	CloseHandle(markerHandle);
	if (!markerWritten) {
		DeleteFileW(marker.c_str());
		Warning("Failed to persist persistent-data save marker\n");
		return false;
	}
	g_pendingProfileValues = std::move(snapshot);
	return true;
}

static void RecoverInterruptedProfileSave()
{
	std::filesystem::path profile;
	std::filesystem::path backup;
	std::filesystem::path marker;
	if (!GetProfileTransactionPaths(profile, backup, marker))
		return;

	std::error_code error;
	const bool interrupted = std::filesystem::exists(marker, error) && !error;
	std::string primaryContents;
	std::string backupContents;
	const bool primaryValid = ReadValidProfileFile(profile, &primaryContents);
	const bool backupValid = ReadValidProfileFile(backup, &backupContents);
	if (interrupted && primaryValid && backupValid
		&& !PreservePreviousPersistentEntries(profile, primaryContents, backupContents)) {
		if (ReplaceFileWithCopy(backup, profile)) {
			DeleteFileW(marker.c_str());
			Warning("Recovered the previous persistent-data profile after dormant-entry merge failed\n");
		}
		else {
			Warning("Failed to preserve dormant persistent data or restore the previous profile\n");
		}
		return;
	}
	switch (PersistentDataTransaction::SelectRecoveryAction(primaryValid, backupValid)) {
	case PersistentDataTransaction::RecoveryAction::CommitPrimary:
		if (interrupted)
			DeleteFileW(marker.c_str());
		return;
	case PersistentDataTransaction::RecoveryAction::RestoreBackup:
		if (ReplaceFileWithCopy(backup, profile)) {
			DeleteFileW(marker.c_str());
			Warning("Recovered persistent data from the last completed profile save\n");
		}
		else {
			Warning("Failed to recover interrupted persistent-data save\n");
		}
		return;
	case PersistentDataTransaction::RecoveryAction::PreserveForRecovery:
		if (interrupted)
			Warning("Interrupted persistent-data save has no valid recovery copy; preserving all files\n");
		return;
	}
}


// Command handling
void setinfopersist_cmd(const CCommand& args) {
	auto engine = G_engine;
	auto setinfo_cmd = decltype(&setinfopersist_cmd)(engine + 0x5B520);
	auto setinfo_cmd_flags = (int*)(engine + 0x05B5FF);
	void(*ccommand_constructor)(CCommand * thisptr, int nArgC, const char** ppArgV) = decltype(ccommand_constructor)(engine + 0x4806F0);

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	if (args.ArgC() >= 3) {
		if (!IsValidUserInfo(args.Arg(1))) {
			Warning("Invalid user info key %s. Only certain characters are allowed.\n", args.Arg(1));
			return;
		}
		if (!IsValidUserInfo(args.Arg(2))) {
			Warning("Invalid user info value %s. Only certain characters are allowed.\n", args.Arg(1));
			return;
		}
		if (!PDef::IsValidKeyAndValue(args.Arg(1), args.Arg(2))) {
			Warning("PData key %s, value %s failed validation.\n", args.Arg(1), args.Arg(2));
			return;
		}

		// Check current value before setting
		const char* hashedKey = hashUserInfoKeyArena(arena, args.Arg(1));
		auto hashedKey_len = strlen(hashedKey);
		// NOTE(mrsteyk): null terminator included by sizeof
		size_t fullVarName_size = sizeof(PERSIST_COMMAND) + 1 + hashedKey_len;
		auto fullVarName = (char*)arena_push(arena, fullVarName_size);
		memcpy(fullVarName, PERSIST_COMMAND" ", sizeof(PERSIST_COMMAND));
		memcpy(fullVarName + sizeof(PERSIST_COMMAND), hashedKey, hashedKey_len);
		auto existingVar = OriginalCCVar_FindVar(cvarinterface, fullVarName);
		bool valueChanged = true;  // Default to true if var doesn't exist

		if (existingVar) {
			valueChanged = (strcmp(existingVar->m_Value.m_pszString, args.Arg(2)) != 0);
		}

		// Check for "nosend" argument, or if the convar does not exist
		bool noSend = (args.ArgC() >= 4 && strcmp_static(args.Arg(3), "nosend") == 0);
		bool shouldHash = !noSend && (existingVar == nullptr);
		if (args.ArgC() >= 4 && strcmp_static(args.Arg(3), "forcehash") == 0)
			noSend = shouldHash = true;

		size_t newArgv_size = noSend ? args.ArgC() - 1 : args.ArgC();
		auto newArgv = (const char**)arena_push(arena, sizeof(const char*) * newArgv_size);
		newArgv[0] = args.Arg(0);
		char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
		snprintf(modifiedKey, sizeof(modifiedKey), "%s %s", PERSIST_COMMAND, shouldHash ? hashUserInfoKey(args.Arg(1)).c_str() : args.Arg(1));
		newArgv[1] = modifiedKey;

		std::copy(args.ArgV() + 2, args.ArgV() + newArgv_size, newArgv + 2);

		char commandMemory[sizeof(CCommand)];
		CCommand* pCommand = reinterpret_cast<CCommand*>(commandMemory);
		ccommand_constructor(pCommand, newArgv_size, newArgv);

		static bool setInfoFlagsWritable = false;
		if (!setInfoFlagsWritable) {
			DWORD oldProtection = 0;
			setInfoFlagsWritable = VirtualProtect(
				setinfo_cmd_flags, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtection) != FALSE;
		}
		if (!setInfoFlagsWritable) {
			Warning("Failed to enable persistent setinfo flags\n");
			pCommand->~CCommand();
			return;
		}

		*setinfo_cmd_flags = FCVAR_PERSIST_MASK;
		const bool previousNoSend = g_bNoSendConVar;
		g_bNoSendConVar = noSend;
		setinfo_cmd(*pCommand);
		g_bNoSendConVar = previousNoSend;
		*setinfo_cmd_flags = FCVAR_USERINFO;

		RememberPersistentConVar(
			modifiedKey,
			args.Arg(1),
			OriginalCCVar_FindVar(cvarinterface, modifiedKey));

		if (valueChanged && !g_bRecursive) {
			g_flLastCommandTime = Plat_FloatTime();
			g_bTimerActive = true;
		}

		pCommand->~CCommand();
	}
	else if (args.ArgC() == 2) {
		auto hashedKey = hashUserInfoKeyArena(arena, args.Arg(1));
		char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
		snprintf(modifiedKey, sizeof(modifiedKey), "%s %s", PERSIST_COMMAND, hashedKey);
		auto hVar = OriginalCCVar_FindVar(cvarinterface, modifiedKey);
		if (hVar)
			ConVar_PrintDescription(hVar);
		else {
			auto result = OriginalCCVar_FindVar(cvarinterface, args.GetCommandString());
			if (result)
				ConVar_PrintDescription(result);
		}
	}
	else {
		setinfo_cmd(args);
	}
}

char ExecuteConfigFile(int configType) {
	if (OriginalCCVar_FindVar && OriginalCCVar_FindVar(cvarinterface, "cl_fovScale"))
		OriginalCCVar_FindVar(cvarinterface, "cl_fovScale")->m_fMaxVal = 1.7f;
	constexpr size_t MAX_PATH_LENGTH = 1024;
	constexpr size_t MAX_BUFFER_SIZE = PersistentDataCodec::MaxRawSize;

	char pathBuffer[MAX_PATH_LENGTH];
	if (!GetConfigPath(pathBuffer, MAX_PATH_LENGTH, configType)) {
		return 0; // Failed to get config path
	}

	std::filesystem::path configPath(pathBuffer);
	if (configType == 1)
		RecoverInterruptedProfileSave();

	if (!std::filesystem::exists(configPath)) {
		return 0; // Config file doesn't exist
	}

	std::string validatedProfile;
	if (configType == 1 && !ReadValidProfileFile(configPath, &validatedProfile)) {
		Warning("Persistent-data profile failed structural validation\n");
		return 0;
	}
	const uintmax_t fileSize = configType == 1
		? validatedProfile.size()
		: std::filesystem::file_size(configPath);
	if (fileSize == 0 || fileSize > MAX_BUFFER_SIZE) {
		return 0; // File is empty or too large
	}

	auto arena = tctx.get_arena_for_scratch();
	auto temp = TempArena(arena);

	// NOTE(mrsteyk): buffer is already ZeroMemory'd
	char* buffer = static_cast<char*>(arena_push(arena, fileSize + 1)); // +1 for null terminator
	if (!buffer) {
		return 0; // Memory allocation failed
	}
	auto engine = G_engine;
	void* (*Exec_CmdGuts)(const char* commands, char bUseExecuteCommand) = decltype(Exec_CmdGuts)(engine + 0x01059A0);

	if (configType == 1) {
		memcpy(buffer, validatedProfile.data(), validatedProfile.size());
	}
	else {
		std::ifstream file(configPath, std::ios::binary);
		if (!file.read(buffer, static_cast<std::streamsize>(fileSize)))
			return 0;
	}
	buffer[fileSize] = '\0';

	g_bRecursive = true;
	Exec_CmdGuts(buffer, 1);
	g_bRecursive = false;
	if (configType == 1)
		g_bProfileReplayComplete = true;
	return 1; // Success
}



void PData_RunFrame()
{
	if (g_bRecursive || g_bSaveWritePending || !g_bTimerActive || !Cbuf_AddTextOriginal)
		return;

	const double currentTime = Plat_FloatTime();
	if (currentTime - g_flLastCommandTime < SAVE_DELAY)
		return;
	if (!BeginProfileSaveTransaction()) {
		g_flLastCommandTime = currentTime;
		return;
	}

	Cbuf_AddTextOriginal(0, "savePlayerConfig\n", 0);
	g_bTimerActive = false;
	g_bSaveWritePending = true;
	g_bSaveQueuedThisFrame = true;
}

static bool FinishPendingProfileSave()
{
	if (!g_bSaveWritePending || !g_CVFileSystem || !g_CVFileSystemInterface
		|| !g_CVFileSystem->AsyncFinishAllWrites)
		return false;
	if (g_bSaveQueuedThisFrame && !g_bFinishSaveBeforeQuit) {
		g_bSaveQueuedThisFrame = false;
		return false;
	}
	g_bSaveQueuedThisFrame = false;

	using AsyncFinishAllWritesFn = void(__fastcall*)(void* fileSystem);
	reinterpret_cast<AsyncFinishAllWritesFn>(g_CVFileSystem->AsyncFinishAllWrites)(
		reinterpret_cast<void*>(g_CVFileSystemInterface));

	std::filesystem::path profile;
	std::filesystem::path backup;
	std::filesystem::path marker;
	bool profileResolved = false;
	if (GetProfileTransactionPaths(profile, backup, marker)) {
		std::string primaryContents;
		std::string backupContents;
		bool primaryValid = ReadValidProfileFile(profile, &primaryContents);
		const bool backupValid = ReadValidProfileFile(backup, &backupContents);
		if (primaryValid && backupValid
			&& !PreservePreviousPersistentEntries(profile, primaryContents, backupContents)) {
			Warning("Persistent-data save could not preserve dormant addon entries; restoring the previous profile\n");
			primaryValid = false;
		}
		switch (PersistentDataTransaction::SelectRecoveryAction(primaryValid, backupValid)) {
		case PersistentDataTransaction::RecoveryAction::CommitPrimary:
			DeleteFileW(marker.c_str());
			profileResolved = true;
			break;
		case PersistentDataTransaction::RecoveryAction::RestoreBackup:
			if (ReplaceFileWithCopy(backup, profile)) {
				DeleteFileW(marker.c_str());
				Warning("Restored persistent data after a failed profile save\n");
				profileResolved = true;
				break;
			}
			Warning("Persistent-data backup restore failed; preserving all transaction files and scheduling a retry\n");
			g_bTimerActive = true;
			g_flLastCommandTime = Plat_FloatTime();
			break;
		case PersistentDataTransaction::RecoveryAction::PreserveForRecovery:
			Warning("Persistent-data profile save produced no valid recovery copy; preserving all transaction files and scheduling a retry\n");
			g_bTimerActive = true;
			g_flLastCommandTime = Plat_FloatTime();
			break;
		}
	}
	const bool valuesDurable = profileResolved
		&& ProfileContainsPersistentValues(profile, g_pendingProfileValues);
	g_pendingProfileValues.clear();
	g_bSaveWritePending = false;
	g_bFinishSaveBeforeQuit = false;
	if (valuesDurable) {
		const bool reconcileRetainedFlags = !g_bSchemaReloadPersistenceSafe;
		g_bSchemaReloadPersistenceSafe = true;
		g_bProfileReplayComplete = true;
		g_bTimerActive = false;
		if (reconcileRetainedFlags)
			PData_ReconcilePersistentConVars();
	}
	else {
		g_bTimerActive = true;
		g_flLastCommandTime = Plat_FloatTime();
	}
	return valuesDurable;
}

void PData_FinishPendingSave()
{
	FinishPendingProfileSave();
}

static char __fastcall NativeProfileWriterHook(unsigned int configType)
{
	if (!g_NativeProfileWriterOriginal)
		return 0;
	if (configType != 1)
		return g_NativeProfileWriterOriginal(configType);

	const bool ownsTransaction = !g_bSaveWritePending;
	if (ownsTransaction) {
		if (!BeginProfileSaveTransaction()) {
			Warning("Refusing an unprotected native persistent-data profile write\n");
			return 0;
		}
		g_bSaveWritePending = true;
		g_bSaveQueuedThisFrame = false;
	}

	const char result = g_NativeProfileWriterOriginal(configType);
	if (ownsTransaction || g_bFinishSaveBeforeQuit) {
		g_bFinishSaveBeforeQuit = true;
		FinishPendingProfileSave();
	}
	return result;
}

bool PData_PrepareForSchemaReload()
{
	if (!g_bProfileReplayComplete && g_persistentConVarBindings.empty()) {
		g_bSchemaReloadPersistenceSafe = true;
		return true;
	}
	if (!g_NativeProfileWriterOriginal) {
		Warning("Persistent-data profile writer hook is unavailable; retaining live schema flags\n");
		g_bSchemaReloadPersistenceSafe = false;
		return false;
	}

	PersistentValueSnapshot requiredValues;
	if (!CaptureCurrentPersistentValues(requiredValues)) {
		Warning("Could not capture persistent values before schema reload; retaining live schema flags\n");
		g_bSchemaReloadPersistenceSafe = false;
		return false;
	}
	if (!g_bSaveWritePending) {
		if (!BeginProfileSaveTransaction(&requiredValues)) {
			Warning("Could not prepare persistent data before schema reload; retaining live schema flags\n");
			g_bSchemaReloadPersistenceSafe = false;
			return false;
		}
		g_bSaveWritePending = true;
	}
	else {
		g_pendingProfileValues = std::move(requiredValues);
	}
	g_bSaveQueuedThisFrame = false;
	g_bFinishSaveBeforeQuit = true;
	g_NativeProfileWriterOriginal(1);
	g_bSchemaReloadPersistenceSafe = FinishPendingProfileSave();
	if (g_bSchemaReloadPersistenceSafe) {
		g_bTimerActive = false;
		return true;
	}

	g_bTimerActive = true;
	g_flLastCommandTime = Plat_FloatTime();
	Warning("Persistent-data pre-schema save did not complete; retaining live schema flags\n");
	return false;
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

void PData_OnConsoleCommand(const char* str)
{
	if (g_bRecursive)
		return;

	const char* command = str;
	while (command && (*command == ' ' || *command == '\t' || *command == '\r' || *command == '\n'))
		++command;
	if (command && g_bTimerActive) {
		const bool quitCommand =
			(_strnicmp(command, "quit", 4) == 0 && (command[4] == '\0' || isspace(static_cast<unsigned char>(command[4]))))
			|| (_strnicmp(command, "exit", 4) == 0 && (command[4] == '\0' || isspace(static_cast<unsigned char>(command[4]))));
		if (quitCommand) {
			g_flLastCommandTime = Plat_FloatTime() - SAVE_DELAY;
			g_bFinishSaveBeforeQuit = true;
		}
	}
	PData_RunFrame();
}
