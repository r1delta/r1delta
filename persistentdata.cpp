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

//#define HASH_USERINFO_KEYS
// Constants
int m_nSignonState = 0;
constexpr size_t MAX_LENGTH = 254;
constexpr const char* INVALID_CHARS = "{}()':;`\"\n";
bool g_bNoSendConVar = false;

// Utility functions
bool IsValidUserInfo(const char* value) {
	if (!value || !*value) return false; // Null or empty check

	size_t len = strlen(value);
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
		case '(':  // Code execution/commands  
		case ')':
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
		Enum
	};

	Type type;
	std::string enumName; // Only valid if type == Enum

	bool operator==(const SchemaType& other) const {
		if (type != other.type) return false;
		if (type == Type::Enum) return enumName == other.enumName;
		return true;
	}
};

// Represents an array definition in the schema
struct ArrayDef {
	std::variant<int, std::string> size; // Either a fixed size or enum name
};

class PDataValidator {
public:
	// Main validation function
	bool isValid(const std::string& key, const std::string& value) const;

private:
	friend class SchemaParser;

	// Schema storage
	std::unordered_map<std::string, SchemaType> keys;
	std::unordered_map<std::string, ArrayDef> arrays;
	std::unordered_map<std::string, std::map<std::string, int>> enums;

	// Helper functions
	bool isValidEnumValue(const std::string& enumName, const std::string& value) const;
	std::optional<SchemaType> getKeyType(const std::string& key) const;
	bool validateArrayAccess(const std::string& arrayName, const std::string& index) const;
	std::vector<std::string> splitKey(const std::string& key) const;
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
			std::string arrayName = matches[1];

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
			std::string enumName = blockMatches[1];
			std::string enumBody = blockMatches[2];

			// Parse enum values
			std::regex valuePattern(R"foo((?:\["([^"]+)"\]|(\w+))\s*=\s*(\d+))foo");
			std::smatch valueMatches;
			std::string::const_iterator valueStart(enumBody.cbegin());

			std::map<std::string, int> enumValues;
			while (std::regex_search(valueStart, enumBody.cend(), valueMatches, valuePattern)) {
				// If group 1 matched, it was a ["name"] format
				// If group 2 matched, it was a bare identifier
				std::string enumValue = valueMatches[1].matched ? valueMatches[1].str() : valueMatches[2].str();
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
			std::string enumName = addEnumMatches[1];
			std::string enumRef = addEnumMatches[2];

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
			std::string keyName = matches[1];
			std::string typeName = matches[2];

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

bool PDataValidator::isValid(const std::string& key, const std::string& value) const {
	// Split the key into parts (handling array indexing)
	auto parts = splitKey(key);
	if (parts.empty()) return false;

	std::string currentKey;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i > 0) currentKey += ".";

		// Handle array indexing
		size_t bracketPos = parts[i].find('[');
		if (bracketPos != std::string::npos) {
			std::string arrayName = parts[i].substr(0, bracketPos);
			currentKey += arrayName;

			// Extract index
			size_t closeBracket = parts[i].find(']', bracketPos);
			if (closeBracket == std::string::npos) return false;

			std::string index = parts[i].substr(bracketPos + 1, closeBracket - bracketPos - 1);
			if (!validateArrayAccess(currentKey, index)) return false;

			currentKey += "[" + index + "]";
		}
		else {
			currentKey += parts[i];
		}
	}

	// Get the type for this key
	auto type = getKeyType(currentKey);
	if (!type) return false;

	// Validate value against type
	switch (type->type) {
	case SchemaType::Type::Bool:
		return value == "0" || value == "1" ||
			value == "true" || value == "false";

	case SchemaType::Type::Int: {
		if (value.empty()) return false;

		size_t start = 0;
		if (value[0] == '-') {
			if (value.length() == 1) return false; // Just a minus sign
			start = 1;
		}

		for (size_t i = start; i < value.length(); i++) {
			if (!std::isdigit(value[i])) return false;
		}
		return true;
	}

	case SchemaType::Type::Float:
		try {
			std::stof(value);
			return true;
		}
		catch (...) {
			return false;
		}

	case SchemaType::Type::String:
		return true; // All strings are valid

	case SchemaType::Type::Enum:
		return isValidEnumValue(type->enumName, value);
	}

	return false;
}
bool PDataValidator::isValidEnumValue(const std::string& enumName, const std::string& value) const {
	auto enumIt = enums.find(enumName);
	if (enumIt == enums.end()) return false;

	// Special case for pdata_null which is valid for any enum
	if (value == "pdata_null") return true;

	// Convert value to lowercase for case-insensitive comparison
	std::string lowerValue;
	lowerValue.reserve(value.length());
	for (char c : value) {
		lowerValue += std::tolower(c);
	}

	for (const auto& [enumValue, _] : enumIt->second) {
		std::string lowerEnumValue;
		lowerEnumValue.reserve(enumValue.length());
		for (char c : enumValue) {
			lowerEnumValue += std::tolower(c);
		}
		if (lowerEnumValue == lowerValue) return true;
	}

	return false;
}
std::optional<SchemaType> PDataValidator::getKeyType(const std::string& key) const {
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
		if (bracketEnd == std::string::npos) return std::nullopt; // Malformed

		pos = bracketEnd + 1;

		// If there's a following character and it's not a dot, add a dot
		if (pos < key.length() && key[pos] != '.') {
			baseKey += '.';
		}
	}

	auto it = keys.find(baseKey);
	if (it != keys.end()) return it->second;
	return std::nullopt;
}

bool PDataValidator::validateArrayAccess(const std::string& arrayName,
	const std::string& index) const {
	auto it = arrays.find(arrayName);
	if (it == arrays.end()) return false;

	const auto& arrayDef = it->second;

	// Check if array size is defined by an enum
	if (std::holds_alternative<std::string>(arrayDef.size)) {
		const std::string& enumName = std::get<std::string>(arrayDef.size);
		return isValidEnumValue(enumName, index);
	}

	// Otherwise it's a fixed size array
	try {
		int idx = std::stoi(index);
		return idx >= 0 && idx < std::get<int>(arrayDef.size);
	}
	catch (...) {
		return false;
	}
}

std::vector<std::string> PDataValidator::splitKey(const std::string& key) const {
	std::vector<std::string> result;
	std::stringstream ss(key);
	std::string item;

	while (std::getline(ss, item, '.')) {
		result.push_back(item);
	}

	return result;
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


class PDef {
private:
	static std::unique_ptr<PDataValidator> s_validator;
	static std::once_flag s_initFlag;

	static void InitValidator() {
		try {
			// Construct path to _pdef.nut relative to game directory
			std::filesystem::path schemaPath = "r1delta/scripts/vscripts/_pdef.nut";
			if (!std::filesystem::exists(schemaPath)) {
				Error("Could not find _pdef.nut at: %s", schemaPath.string().c_str());
			}

			std::string schemaCode = readFile(schemaPath.string());
			s_validator = std::make_unique<PDataValidator>(SchemaParser::parse(schemaCode));
		}
		catch (const std::exception& e) {
			Error("Failed to initialize PData validator: %s", std::string(e.what()).c_str());
		}
	}

public:
	static bool IsValidKeyAndValue(const std::string& key, const std::string& value) {
		// Initialize validator on first use
		std::call_once(s_initFlag, InitValidator);

		if (!s_validator) {
			Error("PData validator failed to initialize");
		}

		return s_validator->isValid(key, value);
	}
};

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
		if (strncmp(var->name, "__ ", 3) == 0) {
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
	if (numvars > 4096) {
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

			if (!SafePrefixConVarName(var.name, sizeof(var.name), "__ ")) {
				Warning("Failed to prefix persistent data convar\n");
				continue;
			}
		}
		else {
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
	auto hashedKey = hashUserInfoKey(key);
	std::string varName = std::string(PERSIST_COMMAND) + " " + hashedKey;

	if (!IsValidUserInfo(key) || !IsValidUserInfo(varName.c_str()) || !IsValidUserInfo(defaultValue)) {
		return sq_throwerror(v, "Invalid user info key or default value.");
	}

	auto var = OriginalCCVar_FindVar(cvarinterface, varName.c_str());

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
struct CBaseClient {
	_BYTE gap0[1040];
	KeyValues* m_ConVars;
	char pad[284392];
};
static_assert(sizeof(CBaseClient) == 285440);
struct CBaseClientDS {
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

	auto var = vars->GetInt("__ xp", 0);
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

	auto var = vars->GetInt("__ gen", 0);

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
	std::string hashedKey = hashUserInfoKey(pKey);
	std::string modifiedKey = PERSIST_COMMAND" ";
	modifiedKey += hashedKey;

	if (!IsValidUserInfo(pKey) || !IsValidUserInfo(modifiedKey.c_str()) || !IsValidUserInfo(pDefaultValue)) {
		return sq_throwerror(v, "Invalid user info key or default value.");
	}

	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

	if (!GetClientConVarsKV(index) || index == 18) {
		//return sq_throwerror(v, "Client has NULL m_ConVars.");
		//Msg("REPLAY on server tried to access persistent value: key=%s, hashedKey=%s, hashed=%s\n",
		//	pKey, hashedKey.c_str(), "true");

		sq_pushstring(v, pDefaultValue, -1); // I HATE REPLAY
		return 1;
	}

	const char* pResult = GetClientConVarsKV(index)->GetString(modifiedKey.c_str(), pDefaultValue);
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

	const char* pKey, * pValue;
	sq_getstring(v, 3, &pKey);
	sq_getstring(v, 4, &pValue);
	std::string hashedKey = hashUserInfoKey(pKey);
	std::string modifiedKey = PERSIST_COMMAND" ";
	modifiedKey += hashedKey;

	if (!IsValidUserInfo(pKey) || !IsValidUserInfo(modifiedKey.c_str()) || !IsValidUserInfo(pValue)) {
		return sq_throwerror(v, "Invalid user info key or value.");
	}
	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);

	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

	if (!(!GetClientConVarsKV(index) || index == 18)) {
		//return sq_throwerror(v, "Client has NULL m_ConVars.");
		CVEngineServer_ClientCommand(0, edict, PERSIST_COMMAND" \"%s\" \"%s\" nosend", hashedKey.c_str(), pValue);
		GetClientConVarsKV(index)->SetString(modifiedKey.c_str(), pValue);
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

typedef char (*CBaseClientState__InternalProcessStringCmdType)(void* thisptr, void* msg, bool bIsHLTV);

CBaseClientState__InternalProcessStringCmdType CBaseClientState__InternalProcessStringCmdOriginal;

char CBaseClientState__InternalProcessStringCmd(void* thisptr, void* msg, bool bIsHLTV) {
	auto engine = G_engine;
	void(*Cbuf_Execute)() = decltype(Cbuf_Execute)(engine + 0x1057C0);
	char ret = CBaseClientState__InternalProcessStringCmdOriginal(thisptr, msg, bIsHLTV);
	Cbuf_Execute(); // fix cbuf overflow on too many stringcmds
	return ret;
}

bool (*__fastcall oCBaseClient__ProcessSignonState)(void* thisptr, void* msg);

bool __fastcall CBaseClient__ProcessSignonState(void* thisptr, void* msg) {
	m_nSignonState = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(msg) + 0x20);

	return oCBaseClient__ProcessSignonState(thisptr, msg);
}

char __fastcall GetConfigPath(char* outPath, size_t outPathSize, int configType) {
	CHAR folderPath[MAX_PATH];

	// Get the user's Documents folder path
	if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, folderPath) < 0) {
		return 0;
	}

	// Determine the subfolder based on configType
	const char* subFolder = (configType == 1) ? "/profile" : "/local";

	// Construct the base path
	char tempPath[512];
	auto size = snprintf(tempPath, sizeof(tempPath), "%s%s%s", folderPath, "/Respawn/Titanfall", subFolder);

	if (size >= 511) {
		return 0;
	}

	// Determine the config file name based on configType
	const char* configFile;
	switch (configType) {
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
		std::string hashedKey = hashUserInfoKey(args.Arg(1));
		std::string fullVarName = std::string(PERSIST_COMMAND) + " " + hashedKey;
		auto existingVar = OriginalCCVar_FindVar(cvarinterface, fullVarName.c_str());
		bool valueChanged = true;  // Default to true if var doesn't exist

		if (existingVar) {
			valueChanged = (strcmp(existingVar->m_Value.m_pszString, args.Arg(2)) != 0);
		}

		// Check for "nosend" argument, or if the convar does not exist
		bool noSend = (args.ArgC() >= 4 && strcmp(args.Arg(3), "nosend") == 0);
		bool shouldHash = !noSend && (existingVar == nullptr);
		if (args.ArgC() >= 4 && strcmp(args.Arg(3), "forcehash") == 0)
			noSend = shouldHash = true;

		std::vector<const char*> newArgv(noSend ? args.ArgC() - 1 : args.ArgC());
		newArgv[0] = args.Arg(0);
		char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
		snprintf(modifiedKey, sizeof(modifiedKey), "%s %s", PERSIST_COMMAND, shouldHash ? hashUserInfoKey(args.Arg(1)).c_str() : args.Arg(1));
		newArgv[1] = modifiedKey;

		std::copy(args.ArgV() + 2, args.ArgV() + (noSend ? args.ArgC() - 1 : args.ArgC()), newArgv.begin() + 2);

		char commandMemory[sizeof(CCommand)];
		CCommand* pCommand = reinterpret_cast<CCommand*>(commandMemory);
		ccommand_constructor(pCommand, noSend ? args.ArgC() - 1 : args.ArgC(), newArgv.data());

		g_bNoSendConVar = noSend;
		setinfo_cmd(*pCommand);
		g_bNoSendConVar = false;

		// Only start/reset timer if value actually changed
		if (valueChanged) {
			g_flLastCommandTime = Plat_FloatTime();
			g_bTimerActive = true;
		}

		pCommand->~CCommand();
	}
	else if (args.ArgC() == 2) {
		std::string hashedKey = hashUserInfoKey(args.Arg(1));
		char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
		snprintf(modifiedKey, sizeof(modifiedKey), "%s %s", PERSIST_COMMAND, hashedKey.c_str());
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

	char* buffer = static_cast<char*>(malloc(fileSize + 1)); // +1 for null terminator
	if (!buffer) {
		return 0; // Memory allocation failed
	}
	auto engine = G_engine;
	void* (*Exec_CmdGuts)(const char* commands, char bUseExecuteCommand) = decltype(Exec_CmdGuts)(engine + 0x01059A0);

	std::ifstream file(configPath, std::ios::binary);
	if (!file.read(buffer, fileSize)) {
		free(buffer);
		return 0; // Failed to read file
	}

	buffer[fileSize] = '\0'; // Null terminate the buffer
	g_bRecursive = true;
	Exec_CmdGuts(buffer, 1);
	g_bRecursive = false;
	free(buffer);
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