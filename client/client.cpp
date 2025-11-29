#include <windows.h>
#include "core.h"
#include "client.h"
#include "persistentdata.h"
#include "load.h"
#include "weaponxdebug.h"
#include "netchanwarnings.h"
#include "localchatwriter.h"
#include "localize.h"
#include "squirrel.h"

typedef void (*sub_18027F2C0Type)(__int64 a1, const char *a2, void *a3);
sub_18027F2C0Type sub_18027F2C0Original;
char* g_loadingStatusText = new char[512];

void TextMsg(bf_read *msg)
{
    TextMsgPrintType_t msg_dest = (TextMsgPrintType_t)msg->ReadByte();

    char text[256];
    msg->ReadString(text, sizeof(text));

    if (msg_dest == TextMsgPrintType_t::HUD_PRINTCONSOLE)
    {
        auto endpos = strlen(text);
        if (text[endpos - 1] == '\n')
            text[endpos - 1] = '\0'; // cut off repeated newline

        Msg("%s\n", text);
    }
}

void sub_18027F2C0(__int64 a1, const char *a2, void *a3)
{
    if (!strcmp_static(a2, "SayText"))
    {
        sub_18027F2C0Original(a1, "TextMsg", TextMsg);
    }
    OriginalCCVar_FindCommand(cvarinterface, "find")->m_pCommandCallback = (FnCommandCallback_t)Find;

    sub_18027F2C0Original(a1, a2, a3);
}

bool (*oCPortalPlayer__CreateMove)(__int64 a1, float a2, __int64 a3, char a4);
bool CPortalPlayer__CreateMove(__int64 a1, float a2, __int64 a3, char a4)
{
    static auto ref = OriginalCCVar_FindVar(cvarinterface, "host_timescale");
    a2 *= ref->m_Value.m_fValue;

    return oCPortalPlayer__CreateMove(a1, a2, a3, a4);
}
__int64 (*osub_18008E820)(__int64 a1, unsigned int a2);
__int64 __fastcall sub_18008E820(__int64 a1, unsigned int a2)
{
    if (a1)
        return osub_18008E820(a1, a2);
    return 0;
}
void *(*oKeyValues__SetString__Client)(__int64 a1, char *a2, const char *a3);
void *KeyValues__SetString__Client(__int64 a1, char *a2, const char *a3)
{
    static auto target = G_engine + 0x16FABA;
    if (uintptr_t(_ReturnAddress()) == target)
        a3 = "30";
    return oKeyValues__SetString__Client(a1, a2, a3);
}
__int64 (*oSharedVehicleViewSmoothing)(
    __int64 a1,
    float *a2,
    float *a3,
    char a4,
    char a5,
    int a6,
    unsigned int *a7,
    float *a8,
    char a9);
__int64 SharedVehicleViewSmoothing(
    __int64 a1,
    float *a2,
    float *a3,
    char a4,
    char a5,
    int a6,
    unsigned int *a7,
    float *a8,
    char a9)
{
    if (a1 == 0)
    {
        static auto getlocalplayer = reinterpret_cast<__int64 (*)(int a1)>(G_client + 0x7B120);
        a1 = getlocalplayer(-1);
    }
    return oSharedVehicleViewSmoothing(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}
typedef void *CLoadingDialog;
namespace vgui
{
    typedef void* Panel;
    typedef void* Label;
}

struct CGameUI
{
    _BYTE gap0[24];
    char m_szPreviousStatusText[128];
};
typedef void* (*CGameUI__StartProgressBarType)(CGameUI* thispt);
CGameUI__StartProgressBarType CGameUI__StartProgressBarOriginal;

typedef vgui::Panel* (*vgui__PHandle__Get_Type)(void* a1);
vgui__PHandle__Get_Type vgui__PHandle__Get_Original;
vgui::Panel* vgui__PHandle__Get_Hook(void* a1)
{
    if ((uintptr_t)a1 < 0x500)
        return 0;
    return vgui__PHandle__Get_Original(a1);
}

void CGameUI__StartProgressBar(CGameUI *thisptr)
{
    static ConVarR1 *enable = OriginalCCVar_FindVar(cvarinterface, "delta_useLegacyProgressBar");
    if (enable->m_Value.m_nValue != 1)
        return;
    static auto CLoadingDialog__ctor = (void (*)(void *thisptr, void *parent))(G_client + 0x363A10);
    static auto vgui__PHandle__Get = (vgui::Panel * (*)(void *a1))(G_client + 0x689A70);
    static auto vgui__PHandle__Set = (vgui::Panel * (*)(void *a1, vgui::Panel *pEnt))(G_client + 0x689AE0);
    static auto CLoadingDialog__SetProgressPoint = (bool (*)(CLoadingDialog *a1, float progress))(G_client + 0x363640);
    static auto CLoadingDialog__Open = (void (*)(CLoadingDialog *a1))(G_client + 0x3631C0);
    static auto BaseModPanel = (vgui::Panel * (*)())(G_client + 0x3871D0);
    static auto GetLoadingDialogHandle = (CLoadingDialog * (*)())(G_client + 0x363720);
    auto g_hLoadingDialog = (CLoadingDialog *)(G_client + 0xB151D8);
    CLoadingDialog *v2; // eax

    if (!GetLoadingDialogHandle())
    {
        v2 = (CLoadingDialog *)GlobalAllocator()->mi_malloc(0x3E0u * 2, TAG_GAME, HEAP_GAME); // just for safety
        CLoadingDialog__ctor(v2, BaseModPanel());
        vgui__PHandle__Set(g_hLoadingDialog, v2);
    }
    thisptr->m_szPreviousStatusText[0] = 0;
    CLoadingDialog__SetProgressPoint(GetLoadingDialogHandle(), 0.0);
    CLoadingDialog__Open(GetLoadingDialogHandle());
	CGameUI__StartProgressBarOriginal(thisptr);
}
bool (*oCGameUI__UpdateProgressBar)(CGameUI *thisptr, float progress, const char *statusText);
bool CGameUI__UpdateProgressBar(CGameUI *thisptr, float progress, const char *statusText)
{
    auto ret = oCGameUI__UpdateProgressBar(thisptr, progress, statusText);
    static auto CGameUI__ContinueProgressBar = (bool (*)(CGameUI *a1, float a2))(G_client + 0x360D60);
    static auto CGameUI__SetProgressBarText = (bool (*)(CGameUI *a1, const char *a2))(G_client + 0x360E40);
    CGameUI__ContinueProgressBar(thisptr, progress);
    if (statusText && strlen(statusText) > 2)
    {
        strcpy(g_loadingStatusText, statusText);
        CGameUI__SetProgressBarText(thisptr, statusText);
    }
    return ret;
}

void (*oBaseModUI__LoadingProgress__PaintBackground)(__int64* thisptr);
void BaseModUI__LoadingProgress__PaintBackground(__int64* thisptr)
{
    static bool done = false;
    if (!done) {
        done = true;
        OriginalCCVar_FindVar(cvarinterface, "mat_rimlightnearstrength")->m_nFlags = 0;
        OriginalCCVar_FindVar(cvarinterface, "mat_rimlightfarstrength")->m_nFlags = 0;
        OriginalCCVar_FindVar(cvarinterface, "model_fadeRangeFraction")->m_nFlags = 0;        
        OriginalCCVar_FindVar(cvarinterface, "r_lod_switch_scale")->m_nFlags = 0;
        OriginalCCVar_FindVar(cvarinterface, "model_defaultFadeDistMin")->m_nFlags = 0;
        OriginalCCVar_FindVar(cvarinterface, "model_defaultFadeDistScale")->m_nFlags = 0;        
        Cbuf_AddText(0, "rese;mat_rimlightnearstrength 0.5; mat_rimlightfarstrength 15;model_fadeRangeFraction 0.9;r_lod_switch_scale 5;model_defaultFadeDistMin 50000000000;model_defaultFadeDistScale 50000000\n", 0);
    }
    vgui::Panel* loadingRes = reinterpret_cast<vgui::Panel*>(thisptr);
    vgui::Label* loadingText = reinterpret_cast<vgui::Label*>(thisptr);

    static auto vgui__Label__SetText = (void (*)(vgui::Label * label, const char* value))(G_client + 0x6B9190);
    static auto vgui__Panel__FindChildByName = (vgui::Panel * (*)(vgui::Panel * panel, const char* childName, bool recurseDown))(G_client + 0x68DC50);

    loadingText = (vgui::Label*)vgui__Panel__FindChildByName((vgui::Panel*)thisptr, "ProgressLabel", false);

    // doesn't seem to localize #LoadingProgress_LoadResources properly
    if (!strcmp_static(g_loadingStatusText, "#LoadingProgress_LoadResources"))
        return oBaseModUI__LoadingProgress__PaintBackground(thisptr);

    if (loadingText)
        vgui__Label__SetText(loadingText, g_loadingStatusText);

    return oBaseModUI__LoadingProgress__PaintBackground(thisptr);
}

typedef __int64(__fastcall *tCHudChat__FormatAndDisplayMessage)(
    __int64 a1, const CHAR *a2, unsigned int a3, char a4, char a5);
typedef struct player_info_s
{
    char _0x0000[0x0008]; // 0x0000
    char szName[32];      // 0x0008
    char _0x0028[0x0228]; // 0x0028
} player_info_t;
// Matches disassembly call [vtable+0x80]
typedef bool(__fastcall *tEngineClient_GetPlayerInfo)(
    uintptr_t pEngineClient,    // RCX
    unsigned int playerIndex,   // RDX (EDX)
    player_info_s *outputBuffer // R8
);

// Matches disassembly call [vtable+0x58]
typedef const wchar_t *(__fastcall *tLocalize_Find)(
    uintptr_t pLocalize,  // RCX
    const char *tokenName // RDX
);

// --- Hook Variables ---
tCHudChat__FormatAndDisplayMessage oCHudChat__FormatAndDisplayMessage = nullptr;

// --- Helper Functions ---

// --- Helper Functions ---
inline std::wstring MultiByteToWide(const char *str, int strLen = -1)
{
    if (!str || strLen == 0 || (strLen == -1 && *str == '\0'))
    {
        return std::wstring();
    }
    // Ensure the input string length for MultiByteToWideChar is calculated correctly
    // If strLen is -1, use strlen to avoid reading past embedded nulls if they exist.
    // However, MultiByteToWideChar handles -1 itself, so this might be redundant unless
    // we suspect the input 'str' itself is not properly terminated within its buffer.
    // Let's stick to passing strLen = -1 for now, relying on the guaranteed termination
    // we added for szName.

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, strLen, NULL, 0);
    if (size_needed <= 0)
    {
        return std::wstring(); // Error or empty string
    }
    std::wstring wstrTo(size_needed, L'\0');
    int result = MultiByteToWideChar(CP_UTF8, 0, str, strLen, &wstrTo[0], size_needed);
    if (result == 0)
    {
        return std::wstring(); // Conversion failed
    }
    // Remove potential trailing null if MultiByteToWideChar included it when strLen was -1
    if (!wstrTo.empty() && wstrTo.back() == L'\0')
    {
        wstrTo.pop_back();
    }
    return wstrTo;
}

inline std::string WideToMultiByte(const std::wstring &wstr, unsigned int codePage = CP_UTF8)
{ // Default to UTF-8
    if (wstr.empty())
    {
        return std::string();
    }
    int size_needed = WideCharToMultiByte(codePage, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size_needed <= 0)
    {
        return std::string();
    }
    std::string strTo(size_needed, '\0');
    int result = WideCharToMultiByte(codePage, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    if (result == 0)
    {
        return std::string(); // Conversion failed
    }
    return strTo;
}

bool skip_valid_ansi_csi_sgr(char *&str)
{
    if (*str++ != '\x1B')
        return false;
    if (*str++ != '[') // CSI
        return false;
    for (char *c = str; *c; c++)
    {
        if (*c >= '0' && *c <= '9')
            continue;
        if (*c == ';' || *c == ':')
            continue;
        if (*c == 'm') // SGR
            break;
        return false;
    }
    return true;
}

void RemoveAsciiControlSequences(char *str, bool allow_color_codes)
{
    for (char *pc = str, c = *pc; c = *pc; pc++)
    {
        // skip UTF-8 characters
        int bytesToSkip = 0;
        if ((c & 0xE0) == 0xC0)
            bytesToSkip = 1; // skip 2-byte UTF-8 sequence
        else if ((c & 0xF0) == 0xE0)
            bytesToSkip = 2; // skip 3-byte UTF-8 sequence
        else if ((c & 0xF8) == 0xF0)
            bytesToSkip = 3; // skip 4-byte UTF-8 sequence
        else if ((c & 0xFC) == 0xF8)
            bytesToSkip = 4; // skip 5-byte UTF-8 sequence
        else if ((c & 0xFE) == 0xFC)
            bytesToSkip = 5; // skip 6-byte UTF-8 sequence

        bool invalid = false;
        char *orgpc = pc;
        for (int i = 0; i < bytesToSkip; i++)
        {
            char next = pc[1];

            // valid UTF-8 part
            if ((next & 0xC0) == 0x80)
            {
                pc++;
                continue;
            }

            // invalid UTF-8 part or encountered \0
            invalid = true;
            break;
        }
        if (invalid)
        {
            // erase the whole "UTF-8" sequence
            for (char *x = orgpc; x <= pc; x++)
                if (*x != '\0')
                    *x = ' ';
                else
                    break;
        }
        if (bytesToSkip > 0)
            continue; // this byte was already handled as UTF-8

        // an invalid control character or an UTF-8 part outside of UTF-8 sequence
        if ((iscntrl(c) && c != '\n' && c != '\r' && c != '\x1B') || (c & 0x80) != 0)
        {
            *pc = ' ';
            continue;
        }

        if (c == '\x1B')                                          // separate handling for this escape sequence...
            if (allow_color_codes && skip_valid_ansi_csi_sgr(pc)) // ...which we allow for color codes...
                pc--;
            else // ...but remove it otherwise
                *pc = ' ';
    }
}

int recurse = 0;
__int64 __fastcall CHudChat__FormatAndDisplayMessage_Hooked(
    __int64 thisptr,       // CHudChat* this
    const CHAR *message,   // message
    unsigned int senderId, // player index/entity id
    char teamChat,         // is team chat?
    char deadChat          // is dead chat?
)
{
    if (recurse != 0)
    {
        // return oCHudChat__FormatAndDisplayMessage(thisptr, message, senderId, teamChat, deadChat);
        return 0;
    }
    recurse++;

    static uintptr_t s_pEngineClient = 0;
    static bool s_globalsInitialized = false;

    constexpr uintptr_t IDA_ANALYSIS_BASE_ADDRESS = 0x180000000;
    constexpr uintptr_t IDA_ADDR_G_PENGINECLIENT = 0x180BF51E8;
    constexpr uintptr_t OFFSET_G_PENGINECLIENT = IDA_ADDR_G_PENGINECLIENT - IDA_ANALYSIS_BASE_ADDRESS;

    if (!s_globalsInitialized && G_client != 0)
    {
        uintptr_t addressOfEnginePtr = G_client + OFFSET_G_PENGINECLIENT;
        s_pEngineClient = *(uintptr_t *)addressOfEnginePtr;
        s_globalsInitialized = true;
    }

    bool isAnonymous = senderId == 0;

    std::wstring finalMessageStringW;
    bool nameRetrievedSuccessfully = false;
    player_info_t playerInfo = {};

    int myTeam = -1, theirTeam = -1;
    if (s_pEngineClient && !isAnonymous)
    {
        uintptr_t engineClientVtable = *(uintptr_t *)s_pEngineClient;
        if (engineClientVtable != 0)
        {
            uintptr_t *pGetPlayerInfoFuncAddrLocation = (uintptr_t *)(engineClientVtable + 0x80);
            uintptr_t getPlayerInfoFuncAddr = *pGetPlayerInfoFuncAddrLocation;
            tEngineClient_GetPlayerInfo pGetPlayerInfo = (tEngineClient_GetPlayerInfo)getPlayerInfoFuncAddr;
            bool funcReturnedSuccess = pGetPlayerInfo(s_pEngineClient, senderId, &playerInfo);
            playerInfo.szName[sizeof(playerInfo.szName) - 1] = '\0'; // GUARANTEE termination
            if (funcReturnedSuccess && playerInfo.szName[0] != '\0')
            {
                nameRetrievedSuccessfully = true;
            }
        }

        void *(*GetClientEntitySelf)(int idx) = (decltype(GetClientEntitySelf))(G_client + 0x7B1B0);
        void *(*GetClientEntity)(int idx) = (decltype(GetClientEntity))(G_client + 0x280FE0);

        auto me = GetClientEntitySelf(-1);
        auto them = GetClientEntity(senderId);

        if (me && them)
        {
            myTeam = (*(int(__fastcall **)(void *))(*(_QWORD *)me + 768LL))(me);
            theirTeam = (*(int(__fastcall **)(void *))(*(_QWORD *)them + 768LL))(them);
        }
    }
    const char *nameToUse = isAnonymous ? "" : (nameRetrievedSuccessfully ? playerInfo.szName : "?UNKNOWN?");

    const wchar_t *deadPrefix = L"";
    const wchar_t *teamPrefix = L"";
    if (G_localizeIface)
    {
        if (deadChat)
        {
            const wchar_t *foundPrefix = G_localizeIface->Find("#HUD_CHAT_DEAD_PREFIX");
            if (foundPrefix)
            {
                deadPrefix = foundPrefix;
            }
        }
        if (teamChat)
        {
            const wchar_t *foundPrefix = G_localizeIface->Find("#HUD_CHAT_TEAM_PREFIX");
            if (foundPrefix)
            {
                teamPrefix = foundPrefix;
            }
        }
    }

    bool doNameSeparator = !isAnonymous;

    LocalChatWriter::SwatchColor playerNameColor = LocalChatWriter::MainTextColor;
    if (theirTeam != -1)
    {
        if (theirTeam != 2 && theirTeam != 3)
        {
            playerNameColor = LocalChatWriter::MainTextColor;
            const wchar_t *foundPrefix = G_localizeIface->Find("#HUD_CHAT_SPEC_PREFIX");
            if (foundPrefix)
                teamPrefix = foundPrefix;
        }
        else
        {
            int opposite = theirTeam == 2 ? 3 : 2;
            playerNameColor = myTeam == opposite ? LocalChatWriter::EnemyTeamNameColor : LocalChatWriter::SameTeamNameColor;
        }
    }

    auto writer = LocalChatWriter();
    writer.InsertChar('\n');
    writer.InsertSwatchColorChange(playerNameColor);

    finalMessageStringW.append(deadPrefix);
    writer.InsertText(deadPrefix);
    finalMessageStringW.append(teamPrefix);
    writer.InsertText(teamPrefix);

    std::wstring wideName = MultiByteToWide(nameToUse, -1);
    finalMessageStringW.append(wideName);
    writer.InsertText(wideName.c_str());

    if (doNameSeparator)
    {
        finalMessageStringW.push_back(L':');
        finalMessageStringW.push_back(L' ');
        writer.InsertSwatchColorChange(LocalChatWriter::MainTextColor);
        writer.InsertChar(':');
        writer.InsertChar(' ');
    }

    // Append message
    if (message != nullptr && !IsBadReadPtr(message, 1))
    {
        RemoveAsciiControlSequences((char *)message, true);
        auto text = MultiByteToWide(message, -1);
        finalMessageStringW.append(text);
        writer.Write(message);
    }

    std::string finalMessageMB = WideToMultiByte(finalMessageStringW, CP_UTF8);
    Msg("*** CHAT *** %s\n", finalMessageMB.c_str());

    return 0;
}
char (*oMsgFunc__SayText)(bf_read* a1);

typedef bool(__fastcall* tReadStringFromBuffer)(bf_read* pBuffer, char* dest, int maxLength, bool* bOverflow, void* unknown);

typedef bool(__fastcall* tShouldFilterMessage)(int playerIndex);

typedef char(__fastcall* tAddGameLine)(CHudChat* pThis, const char* text, int playerSlot, bool isTeamMsg, bool isDeadMsg);

char __fastcall MsgFunc__SayText(bf_read* pBuffer)
{
	recurse = 0; 
    static auto dword_99DEE0 = (uint32_t*)(G_client + 0x99DEE0);
    static auto qword_F808C8_ptr = (CHudChat**)(G_client + 0xF808C8);
    static auto ReadStringFunc = (tReadStringFromBuffer)(G_client + 0x66AE20);
    static auto FilterMessageFunc = (tShouldFilterMessage)(G_client + 0x444490);
    static auto AddGameLineFunc = (tAddGameLine)(G_client + 0x17D440);


    unsigned int playerSlot;
    int bitsLeft = pBuffer->m_nBitsAvail;
    if (bitsLeft >= 8)
    {
        playerSlot = static_cast<uint8_t>(pBuffer->m_nInBufWord);
        pBuffer->m_nBitsAvail -= 8;

        if (pBuffer->m_nBitsAvail == 0) 
        {
            pBuffer->m_nBitsAvail = 32;
            if (pBuffer->m_pDataIn >= pBuffer->m_pBufferEnd) {
                pBuffer->m_bOverflow = true;
                pBuffer->m_nInBufWord = 0;
            }
            else {
                pBuffer->m_nInBufWord = *pBuffer->m_pDataIn;
            }
            pBuffer->m_pDataIn++;
        }
        else
        {
            pBuffer->m_nInBufWord >>= 8;
        }
    }
    else 
    {
        uint32_t lowBits = pBuffer->m_nInBufWord;
        int bitsFromNext = 8 - bitsLeft;

        if (pBuffer->m_pDataIn >= pBuffer->m_pBufferEnd) {
            pBuffer->m_bOverflow = true;
        }

        if (pBuffer->m_bOverflow) {
            playerSlot = 0;
        }
        else {
            uint32_t nextWord = *pBuffer->m_pDataIn;
            pBuffer->m_pDataIn++;

            uint32_t highBits = (nextWord & dword_99DEE0[bitsFromNext]) << bitsLeft;
            playerSlot = highBits | lowBits;

            pBuffer->m_nInBufWord = nextWord >> bitsFromNext;
            pBuffer->m_nBitsAvail = 32 - bitsFromNext;
        }
    }

    
	char* str = (char*)GlobalAllocator()->mi_malloc(256, TAG_GAME, HEAP_GAME);
    if (!ReadStringFunc(pBuffer, str, 256, nullptr, nullptr)) {
        return 1;
    }

   
    bool isTeam = (pBuffer->m_nInBufWord & 1) != 0;
    if (--pBuffer->m_nBitsAvail == 0) {
        pBuffer->m_nBitsAvail = 32;
        if (pBuffer->m_pDataIn >= pBuffer->m_pBufferEnd) {
            pBuffer->m_bOverflow = true;
            pBuffer->m_nInBufWord = 0;
        }
        else {
            pBuffer->m_nInBufWord = *pBuffer->m_pDataIn;
        }
        pBuffer->m_pDataIn++;
    }
    else {
        pBuffer->m_nInBufWord >>= 1;
    }

    bool isDead = (pBuffer->m_nInBufWord & 1) != 0;
    if (--pBuffer->m_nBitsAvail == 0) {
        pBuffer->m_nBitsAvail = 32;
        if (pBuffer->m_pDataIn >= pBuffer->m_pBufferEnd) {
            pBuffer->m_bOverflow = true;
            pBuffer->m_nInBufWord = 0;
        }
        else {
            pBuffer->m_nInBufWord = *pBuffer->m_pDataIn;
        }
        pBuffer->m_pDataIn++;
    }
    else {
        pBuffer->m_nInBufWord >>= 1;
    }

   
    if (!FilterMessageFunc(playerSlot - 1)) {
        for (CHudChat* i = *qword_F808C8_ptr; i; i = i->next) {
            AddGameLineFunc(i, str, playerSlot, isTeam, isDead);
        }
    }
    return 1;
}



// from bme
typedef _QWORD *(__fastcall *sub_18017E140_type)(_QWORD *, __int64, char *);
sub_18017E140_type sub_18017E140_org;
_QWORD *__fastcall sub_18017E140(_QWORD *a1, __int64 a2, char *a3)
{
    auto ret = sub_18017E140_org(a1, a2, a3);
    *(bool *)(a1[77] + 1011) = true; // enable Unicode input in CBaseHudChatEntry
    return ret;
}
float GetZoomFrac()
{
    // remember the last non‑zero zoom fraction
    static float lastNonZero = 1.0f;

    float rawZoom = 1.0f;
    // your existing zoom check
    if (reinterpret_cast<bool (*)()>(G_client + 0x2C23C0)() &&
        reinterpret_cast<__int64 (*)()>(G_client + 0x2C2360)())
    {
        rawZoom = 1.0f - reinterpret_cast<float (*)(__int64)>(G_client + 0x35CF0)(
                             reinterpret_cast<__int64 (*)(int)>(G_client + 0x7B120)(0));
    }

    // if it’s non‑zero, update lastNonZero; otherwise keep the old one
    if (rawZoom > 0.0f)
    {
        lastNonZero = rawZoom;
        return rawZoom;
    }
    else
    {
        // clamp to previous
        return lastNonZero;
    }
}

void (*osub_21F3F0)();
void sub_21F3F0()
{
    Cbuf_AddText(0, "retry\n", 0);
    osub_21F3F0();
    return;
}

void (*oWeaponSprayFunction1)(__int64 a1, __int64 a2, float a3, __int64 a4, _DWORD *a5, char a6, float a7, char a8);
void WeaponSprayFunction1(__int64 a1, __int64 a2, float a3, __int64 a4, _DWORD *a5, char a6, float a7, char a8)
{
    oWeaponSprayFunction1(a1, a2, a3, a4, a5, a6, a7, a8);
    // 2) grab zoom scale
    auto var = CCVar_FindVar(cvarinterface, "delta_enable_ads_sway");
    if(var && var->m_Value.m_nValue == 1)
    {
        return;
	}
    float zoom = GetZoomFrac();
    // 3) scale the three final sway components
    //    [pitch] = float at addr (a1 + 12)
    //    [yaw]   = float at addr (a1 + 16)
    //    [roll]  = float at addr (a1 + 20)
    *(float *)(a1 + 12) *= zoom;
    *(float *)(a1 + 16) *= zoom;
    *(float *)(a1 + 20) *= zoom;
}
/*void (*oWeaponSprayFunction2)(float* a1, __int64 a2, __int64 a3, _DWORD* a4, __int64 a5);
void WeaponSprayFunction2(float* a1, __int64 a2, __int64 a3, _DWORD* a4, __int64 a5) {
    if (GetZoomed())
        return;
    return oWeaponSprayFunction2(a1, a2, a3, a4, a5);
}*/
__int64 (*oLoadingProgress__SetupControlStates)(__int64 a1, const char *a2, const char *a3);
static char g_last_a3_value[256] = "";

// --- Hook Function Implementation ---
__int64 __fastcall LoadingProgress__SetupControlStates(__int64 a1, const char *a2, const char *a3)
{
    // The value we will actually pass to the original function
    const char *effective_a3 = a3;

    // Check if a3 is valid and starts with "mp_"
    if (a3 != nullptr && strncmp(a3, "mp_", 3) == 0)
    {

        // Check if we have a previously stored non-"mp_" value
        if (g_last_a3_value[0] != '\0')
        {
            effective_a3 = g_last_a3_value; // Use the stored value
        }
        else
        {
        }
    }
    else
    {
        // a3 does not start with "mp_" or is NULL.
        // Store this value as the new "last good value" if it's not NULL.
        if (a3 != nullptr)
        {
            strncpy(g_last_a3_value, a3, 256 - 1);
            // Ensure null termination
            g_last_a3_value[256 - 1] = '\0';
        }
        else
        {
            // If a3 is null, clear the stored value as it's not a valid "last value"
            g_last_a3_value[0] = '\0';
        }
    }

    return oLoadingProgress__SetupControlStates(a1, a2, effective_a3);
}

typedef void *(*CHudMenu__ThinkType)(__int64 a1);
CHudMenu__ThinkType oCHudMenu__Think = nullptr;
void *__fastcall CHudMenu__Think(__int64 a1)
{
    // Store the byte value before calling Think
    BYTE prevValue = *(_BYTE *)(a1 + 648);

    // Call the original Think function
    void *result = oCHudMenu__Think(a1);

    // Check if the byte went from non-zero to 0 (menu timed out)
    BYTE newValue = *(_BYTE *)(a1 + 648);
    if (prevValue != 0 && newValue == 0) {
        // Menu timed out, send menuselect -1 to cancel on server
        Cbuf_AddText(0, "menuselect -1\n", 0);
    }

    return result;
}

void InitClient()
{
    auto client = G_client;
    auto engine = G_engine;
    // Initialize HUD function pointers
    GetHud = (GetHudType)(client + 0x173390);
    CHudFindElement = (CHudFindElementType)(client + 0x175850);
    CHudMenuSelectMenuItem = (CHudMenuSelectMenuItemType)(client + 0x1B3850);
    MH_CreateHook((LPVOID)(client + 0x1B3740), &CHudMenu__Think, reinterpret_cast<LPVOID *>(&oCHudMenu__Think));
    MH_CreateHook((LPVOID)(client + 0x21FE50), &PredictionErrorFn, reinterpret_cast<LPVOID *>(NULL));
    // MH_CreateHook((LPVOID)(client + 0x029840), &C_BaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(NULL));
    MH_CreateHook((LPVOID)(client + 0x27F2C0), &sub_18027F2C0, reinterpret_cast<LPVOID *>(&sub_18027F2C0Original));
    // MH_CreateHook((LPVOID)(engine + 0x56A450), &vsnprintf_l_hk, NULL);
    // MH_CreateHook((LPVOID)(client + 0x744864), &vsnprintf_l_hk, NULL);
    MH_CreateHook((LPVOID)(engine + 0x102D50), &Cbuf_AddText, reinterpret_cast<LPVOID *>(&Cbuf_AddTextOriginal));
    MH_CreateHook((LPVOID)(engine + 0x4801B0), &ConVar_PrintDescription, reinterpret_cast<LPVOID *>(&ConVar_PrintDescriptionOriginal));
    MH_CreateHook((LPVOID)(engine + 0x47FB00), &CConVar__GetSplitScreenPlayerSlot, NULL);
    MH_CreateHook((LPVOID)(engine + 0x4722E0), &sub_1804722E0, 0);
    MH_CreateHook((LPVOID)(engine + 0x21F3F0), &sub_21F3F0, reinterpret_cast<LPVOID *>(&osub_21F3F0));
    // MH_CreateHook((LPVOID)(client + 0x4A6150), &WeaponXRegisterClient, reinterpret_cast<LPVOID*>(&oWeaponXRegisterClient));
    MH_CreateHook((LPVOID)(client + 0x959F0), &CPortalPlayer__CreateMove, reinterpret_cast<LPVOID *>(&oCPortalPlayer__CreateMove));
    MH_CreateHook((LPVOID)(client + 0x8E820), &sub_18008E820, reinterpret_cast<LPVOID *>(&osub_18008E820));
    MH_CreateHook((LPVOID)(engine + 0x47A410), &KeyValues__SetString__Client, reinterpret_cast<LPVOID *>(&oKeyValues__SetString__Client));
    MH_CreateHook((LPVOID)(client + 0x286F50), &SharedVehicleViewSmoothing, reinterpret_cast<LPVOID *>(&oSharedVehicleViewSmoothing));
    MH_CreateHook((LPVOID)(client + 0x360210), &CGameUI__StartProgressBar, reinterpret_cast<LPVOID*>(&CGameUI__StartProgressBarOriginal));
    MH_CreateHook((LPVOID)(client + 0x3601C0), &CGameUI__UpdateProgressBar, reinterpret_cast<LPVOID *>(&oCGameUI__UpdateProgressBar));
    MH_CreateHook((LPVOID)(client + 0x3C34D0), &BaseModUI__LoadingProgress__PaintBackground, reinterpret_cast<LPVOID*>(&oBaseModUI__LoadingProgress__PaintBackground));
    MH_CreateHook((LPVOID)(client + 0x17D440), &CHudChat__FormatAndDisplayMessage_Hooked, reinterpret_cast<LPVOID *>(&oCHudChat__FormatAndDisplayMessage));
    MH_CreateHook((LPVOID)(client + 0x17DAA0), &MsgFunc__SayText, reinterpret_cast<LPVOID *>(&oMsgFunc__SayText));
    MH_CreateHook((LPVOID)(client + 0x17E140), &sub_18017E140, reinterpret_cast<LPVOID *>(&sub_18017E140_org));

    MH_CreateHook((LPVOID)(client + 0x47F1E0), &WeaponSprayFunction1, reinterpret_cast<LPVOID *>(&oWeaponSprayFunction1));
    // MH_CreateHook((LPVOID)(client + 0x47FED0), &WeaponSprayFunction2, reinterpret_cast<LPVOID*>(&oWeaponSprayFunction2));
    MH_CreateHook((LPVOID)(client + 0x3C5830), &LoadingProgress__SetupControlStates, reinterpret_cast<LPVOID *>(&oLoadingProgress__SetupControlStates));
    MH_CreateHook((LPVOID)(client + 0x689A70), &vgui__PHandle__Get_Hook, reinterpret_cast<LPVOID *>(&vgui__PHandle__Get_Original));

    if (IsNoOrigin())

#if BUILD_DEBUG
    // if (!InitNetChanWarningHooks())
    //	MessageBoxA(NULL, "Failed to initialize warning hooks", "ERROR", 16);
#endif
    MH_EnableHook(MH_ALL_HOOKS);
}
