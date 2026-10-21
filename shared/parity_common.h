#pragma once
#include <cstddef>
#include <string>
#include <vector>

// Helpers shared by the client/server parity checks (sound scripts, script
// sync): names travel server -> client as svc_StringCmd console commands, and
// the client explains a mismatch the way retail's own checks disconnect.
namespace Parity {

// COM_ExplainDisconnection formats into a 256-byte buffer.
constexpr std::size_t kMaxReason = 250;

// Percent-encodes everything but [A-Za-z0-9_./-] so a name list can travel as
// one quoted, space-separated command argument.
void AppendEncodedName(std::string& out, const char* name);
// Decodes a space-separated list of encoded names, keeping at most maxNames.
std::vector<std::string> DecodeNameList(const char* list, std::size_t maxNames);

// Server: sends `<commandPrefix> "<encoded names>"` commands to one client,
// splitting the list so every command stays a single CCommand.
void SendEncodedNames(int playerSlot, const char* commandPrefix, const std::vector<const char*>& names);

// `"a", "b" and N more`, cut at roughly `budget` bytes.
std::string QuotedList(const std::vector<std::string>& names, std::size_t budget);

// Server: a parity convar is enforced unless set to 0 (or absent).
bool ServerConVarEnabled(const char* name);

// Client: COM_ExplainDisconnection + Host_Disconnect (engine.dll 2015), the
// pair retail's own sound-script check uses.
void DisconnectWithReason(const char* reason);

// Console line (Msg, or Warning when `warning`) mirrored to the debugger
// output, so a parity verdict is visible in dedicated/CDB logs too.
void Report(bool warning, const char* format, ...);

}
