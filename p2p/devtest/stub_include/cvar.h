#pragma once
// devtest stand-in for engine/cvar.h
#include <cstdint>
#include <string>
#include <vector>
#define FCVAR_NONE 0
#define FCVAR_ARCHIVE (1 << 7)
struct CVValue_t
{
    char* m_pszString;
    int64_t m_StringLength;
    float m_fValue;
    int m_nValue;
};
struct ConVarR1
{
    CVValue_t m_Value;
};
class CCommand
{
public:
    std::vector<std::string> args;
    int64_t ArgC() const { return static_cast<int64_t>(args.size()); }
    const char* Arg(int i) const { return i < static_cast<int>(args.size()) ? args[i].c_str() : ""; }
};
struct ConCommandR1;
ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* help);
ConCommandR1* RegisterConCommand(const char* name, void (*callback)(const CCommand&), const char* help, int flags);
extern uintptr_t cvarinterface;
extern ConVarR1* (*OriginalCCVar_FindVar)(uintptr_t, const char*);
