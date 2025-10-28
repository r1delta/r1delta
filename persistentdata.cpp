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
#include "logging.h"
#include "squirrel.h"
#include "keyvalues.h"
#include "factory.h"
// Network message handling
#include <unordered_map>
#include <cstdint>
#include <unordered_set>
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
#include "load.h"
#include "tctx.hh"

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
		if (value.find("mp_weapon") != std::string_view::npos) {
			return false;
		}
	}
	
	if (key.find("pilotLoadouts") != std::string_view::npos) {
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
bool NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer) {
	if (g_bNoSendConVar) {
		// Write 0 ConVars
		buffer.WriteByte(0);
		return !buffer.IsOverflowed();
	}
	if (!IsDedicatedServer()) {
		auto var = OriginalCCVar_FindVar(cvarinterface, "net_secure");
		bool bVanilla = var->m_Value.m_nValue == 1;
		if (bVanilla) {
			for (int i = thisptr->m_ConVars.Count() - 1; i >= 0; --i) {
				if (thisptr->m_ConVars[i].name[0] == '_') {
					thisptr->m_ConVars.Remove(i);
				}
			}
		}
	}

	uint32_t numvars = thisptr->m_ConVars.Count();
	if (numvars < 255) {
		buffer.WriteByte(numvars);
	}
	else {
		buffer.WriteByte(static_cast<uint8_t>(-1));
		buffer.WriteUBitVar(numvars);
	}

	for (uint32_t i = 0; i < numvars; i++) {
		NetMessageCvar_t* var = &thisptr->m_ConVars[i];

		// Check if this is a persistent data convar
		if (strncmp(var->name, PERSIST_COMMAND" ", 3) == 0) {
			// Create a modified name without the prefix
			char modifiedName[sizeof(var->name)];
			// Set high bit on first character to mark it as persistent data
			modifiedName[0] = var->name[3] | 0x80;
			strcpy(modifiedName + 1, var->name + 4);
			buffer.WriteString(modifiedName);
		}
		else {
			buffer.WriteString(var->name);
		}
		buffer.WriteString(var->value);
	}
	return !buffer.IsOverflowed();
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
	thisptr->m_ConVars.RemoveAll();
	for (uint32_t i = 0; i < numvars; i++) {
		NetMessageCvar_t var;
		if (!buffer.ReadString(var.name, sizeof(var.name)) ||
			!buffer.ReadString(var.value, sizeof(var.value))) {
			Warning("Failed to read convar %d/%d\n", i, numvars);
			return false;
		}

		// Check if this is a persistent data convar by checking the high bit
		if (var.name[0] & 0x80) {
			// Clear the high bit for validation
			var.name[0] &= 0x7F;

			// Create string views for validation without the prefix
			std::string nameStr(var.name);
			std::string valueStr(var.value);

			if (!PDef::IsValidKeyAndValue(nameStr, valueStr)) {
				Warning("Invalid persistent data convar: key=%s value=%s\n", var.name, var.value);
				continue;
			}

			if (!SafePrefixConVarName(var.name, sizeof(var.name), PERSIST_COMMAND" ")) {
				Warning("Failed to prefix persistent data convar\n");
				continue;
			}
		}
		else {
			// Skip networkid_force CVar case-insensitively
			if (::_stricmp(var.name, "networkid_force") == 0) {
				continue; // Skip this CVar
			}

			// Check if convar exists and has FCVAR_USERINFO flag
			auto cvar = OriginalCCVar_FindVar(cvarinterface, var.name);
			if (!cvar || !(cvar->m_nFlags & (FCVAR_USERINFO | FCVAR_REPLICATED))) {
				Warning("Invalid userinfo convar (doesn't exist or missing FCVAR_USERINFO or FCVAR_REPLICATED flag): %s\n", var.name);
				continue;
			}
		}

		thisptr->m_ConVars.AddToTail(var);
	}

	return !buffer.IsOverflowed();
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
	if (IsDedicatedServer())
		return g_pClientArrayDS[index].m_ConVars;
	else
		return g_pClientArray[index].m_ConVars;
}

void Script_XPChanged_Rebuild(void* pPlayer) {

	
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
static constexpr double SAVE_DELAY = 5.0; // 5 second delay
static bool g_bRecursive = false;
static bool g_bSavePending = false;


// Command handling
void setinfopersist_cmd(const CCommand& args) {
	auto engine = G_engine;
	auto setinfo_cmd = decltype(&setinfopersist_cmd)(engine + 0x5B520);
	auto setinfo_cmd_flags = (int*)(engine + 0x05B5FF);
	void(*ccommand_constructor)(CCommand * thisptr, int nArgC, const char** ppArgV) = decltype(ccommand_constructor)(engine + 0x4806F0);

	static bool bUnprotectedFlags = false;
	if (!bUnprotectedFlags) {
		bUnprotectedFlags = true;
		DWORD out;
		VirtualProtect(setinfo_cmd_flags, sizeof(int), PAGE_EXECUTE_READWRITE, &out);
	}

	*setinfo_cmd_flags = FCVAR_PERSIST_MASK;

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

		g_bNoSendConVar = noSend;
		setinfo_cmd(*pCommand);
		g_bNoSendConVar = false;

		// Only start/reset timer if value actually changed
		if (valueChanged && !g_bTimerActive) {
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
	*setinfo_cmd_flags = FCVAR_USERINFO;
	
}

char ExecuteConfigFile(int configType) {
	if (OriginalCCVar_FindVar && OriginalCCVar_FindVar(cvarinterface, "cl_fovScale"))
		OriginalCCVar_FindVar(cvarinterface, "cl_fovScale")->m_fMaxVal = 1.7f;
	constexpr size_t MAX_PATH_LENGTH = 1024;
	constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024; // 1 MB

	char pathBuffer[MAX_PATH_LENGTH];
	if (!GetConfigPath(pathBuffer, MAX_PATH_LENGTH, configType)) {
		return 0; // Failed to get config path
	}

	std::filesystem::path configPath(pathBuffer);

	if (!std::filesystem::exists(configPath)) {
		return 0; // Config file doesn't exist
	}

	uintmax_t fileSize = std::filesystem::file_size(configPath);
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

	std::ifstream file(configPath, std::ios::binary);
	if (!file.read(buffer, fileSize)) {
		return 0; // Failed to read file
	}

	g_bRecursive = true;
	Exec_CmdGuts(buffer, 1);
	g_bRecursive = false;
	return 1; // Success
}



void PData_OnConsoleCommand(const char* str) {
	// Skip if we're in a recursive call
	if (g_bRecursive) {
		return;
	}

	// Check if we need to execute a pending save
	if (g_bSavePending) {
		g_bSavePending = false;
		Cbuf_AddTextOriginal(0, "savePlayerConfig\n", 0);
		return;
	}

	// Check if timer has expired
	if (g_bTimerActive) {
		double currentTime = Plat_FloatTime();
		if (currentTime - g_flLastCommandTime >= SAVE_DELAY) {
			g_bTimerActive = false;
			g_bSavePending = true;
		}
	}
}
