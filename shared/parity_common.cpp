#include "parity_common.h"
#include "core.h"
#include "cvar.h"
#include "load.h"
#include "logging.h"
#include "persistentdata.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Parity {
namespace {

// svc_StringCmd is formatted into 1024 bytes and executed as one CCommand (511).
constexpr std::size_t kMaxNamesPayload = 400;

constexpr std::uintptr_t kExplainDisconnectionRva = 0x117240;
constexpr std::uintptr_t kHostDisconnectRva = 0x137160;

int HexDigit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool IsPlainNameChar(unsigned char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		|| c == '_' || c == '.' || c == '-' || c == '/';
}

bool DecodeName(const char* begin, const char* end, std::string& out) {
	out.clear();
	for (const char* c = begin; c < end; ++c) {
		if (*c != '%') {
			out.push_back(*c);
			continue;
		}
		if (end - c < 3 || HexDigit(c[1]) < 0 || HexDigit(c[2]) < 0)
			return false;
		out.push_back(static_cast<char>(HexDigit(c[1]) * 16 + HexDigit(c[2])));
		c += 2;
	}
	return !out.empty();
}

}

void AppendEncodedName(std::string& out, const char* name) {
	static const char kHex[] = "0123456789ABCDEF";
	for (const unsigned char* c = reinterpret_cast<const unsigned char*>(name); *c; ++c) {
		if (IsPlainNameChar(*c)) {
			out.push_back(static_cast<char>(*c));
			continue;
		}
		out.push_back('%');
		out.push_back(kHex[*c >> 4]);
		out.push_back(kHex[*c & 15]);
	}
}

std::vector<std::string> DecodeNameList(const char* list, std::size_t maxNames) {
	std::vector<std::string> names;
	std::string name;
	for (const char* c = list; c && *c;) {
		const char* end = strchr(c, ' ');
		if (!end)
			end = c + strlen(c);
		if (names.size() < maxNames && DecodeName(c, end, name))
			names.push_back(name);
		c = *end ? end + 1 : end;
	}
	return names;
}

void SendEncodedNames(int playerSlot, const char* commandPrefix, const std::vector<const char*>& names) {
	std::string payload;
	auto flush = [&]() {
		if (payload.empty())
			return;
		char command[CCommand::COMMAND_MAX_LENGTH];
		_snprintf_s(command, sizeof(command), _TRUNCATE, "%s \"%s\"", commandPrefix, payload.c_str());
		SendServerCommandToClient(playerSlot, command);
		payload.clear();
	};
	std::string encoded;
	for (const char* name : names) {
		encoded.clear();
		AppendEncodedName(encoded, name);
		if (!payload.empty() && payload.size() + 1 + encoded.size() > kMaxNamesPayload)
			flush();
		if (!payload.empty())
			payload.push_back(' ');
		payload += encoded;
	}
	flush();
}

std::string QuotedList(const std::vector<std::string>& names, std::size_t budget) {
	std::string out;
	std::size_t shown = 0;
	for (const std::string& name : names) {
		const std::string item = (shown ? ", \"" : "\"") + name + "\"";
		if (shown && out.size() + item.size() > budget)
			break;
		out += item;
		++shown;
	}
	if (shown < names.size())
		out += " and " + std::to_string(names.size() - shown) + " more";
	return out;
}

bool ServerConVarEnabled(const char* name) {
	// R1O registers these as R1O convars; R1 modes own the R1 convar itself.
	if (IsR1ODedicatedServer()) {
		const ConVarR1O* var = CCVar_FindVar(cvarinterface, name);
		return !var || var->m_Value.m_nValue != 0;
	}
	const ConVarR1* var = OriginalCCVar_FindVar(cvarinterface, name);
	return !var || var->m_Value.m_nValue != 0;
}

void DisconnectWithReason(const char* reason) {
	using ExplainDisconnectionFn = __int64(__fastcall*)(__int64, const char*, ...);
	using HostDisconnectFn = void(__fastcall*)(bool);
	reinterpret_cast<ExplainDisconnectionFn>(G_engine + kExplainDisconnectionRva)(1, "%s\n", reason);
	reinterpret_cast<HostDisconnectFn>(G_engine + kHostDisconnectRva)(true);
}

void Report(bool warning, const char* format, ...) {
	char line[1024];
	va_list args;
	va_start(args, format);
	vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	OutputDebugStringA(line);
	if (warning)
		Warning("%s", line);
	else
		Msg("%s", line);
}

}
