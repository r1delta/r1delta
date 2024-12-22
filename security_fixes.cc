#include "core.h"
#include "engine.h"
#include "netadr.h"

#include "logging.h"
#include "load.h"

//ConVarR1* cvar_net_chan_limit_msec = nullptr;

//-

class INetChannelHandler {
public:
	virtual void destructor() = 0;
	virtual void ConnectionStart(CNetChan*);
	virtual void ConnectionClosing(const char*);
	virtual void ConnectionCrashed(const char*);
	virtual void PacketStart(int, int);
	virtual void PacketEnd();
	virtual void FileRequested(const char*, unsigned int, bool);
	virtual void FileReceived(const char*, unsigned int, bool);
	virtual void FileDenied(const char*, unsigned int, bool);
	virtual void FileSent(const char*, unsigned int, bool);
};

void(__fastcall* oCNetChan__ProcessPacket)(CNetChan*, netpacket_s*, bool);
bool(__fastcall* oCNetChan___ProcessMessages)(CNetChan*, bf_read*);

bool __fastcall CNetChan___ProcessMessages(CNetChan* thisptr, bf_read* buf) {
	if (!thisptr || !buf)
		return oCNetChan___ProcessMessages(thisptr, buf);

	if (buf->GetNumBitsRead() < 6 || buf->IsOverflowed()) // idfk tbh just move this to whenever a net message processes.
		return oCNetChan___ProcessMessages(thisptr, buf);

	static auto net_chan_limit_msec_ptr = OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	auto net_chan_limit_msec = net_chan_limit_msec_ptr->m_Value.m_fValue;

	if (net_chan_limit_msec == 0.0f)
		return oCNetChan___ProcessMessages(thisptr, buf);

	// TODO(mrsteyk): m_flLastProcessingTime and m_flFinalProcessingTime for an object (probably unordered_map...).

#if 0
	float flStartTime = Plat_FloatTime();

	if ((flStartTime - m_flLastProcessingTime) > 1.0f) {
		m_flLastProcessingTime = flStartTime;
		m_flFinalProcessingTime = 0.0f;
	}
#endif

	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);

	const auto original = oCNetChan___ProcessMessages(thisptr, buf);

	QueryPerformanceCounter(&end);

	auto time_ticks = end.QuadPart - start.QuadPart;
	auto time_msec = time_ticks * 1000 / g_PerformanceFrequency;

	bool bIsProcessingTimeReached = (time_msec >= 1);

	const auto pMessageHandler = *reinterpret_cast<INetChannelHandler**>(reinterpret_cast<uintptr_t>(thisptr) + 0x3ED0);

	if (pMessageHandler && bIsProcessingTimeReached && time_msec >= net_chan_limit_msec) {
		// TODO(mrsteyk): move that name thing out.
#ifdef _DEBUG
		Msg("R1Delta: CNetChan::_ProcessMessages: Max processing time reached for client \"%s\" (%dms)\n", (const char*)(uintptr_t(thisptr) + 0x3EA8), time_msec);
#endif
		pMessageHandler->ConnectionCrashed("Max processing time reached");
		return false;
	}

	return original;
}

void __fastcall CNetChan__ProcessPacket(CNetChan* thisptr, netpacket_s* packet, bool bHasHeader) {
	if (!thisptr)
		return oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	bool bReceivingPacket = *(bool*)((uintptr_t)thisptr + 0x3F80) && !*(int*)((uintptr_t)thisptr + 0xD8);

	if (!bReceivingPacket)
		return oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	static auto net_chan_limit_msec_ptr = OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	auto net_chan_limit_msec = net_chan_limit_msec_ptr->m_Value.m_fValue;

	if (net_chan_limit_msec == 0.0f)
		return oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

#if 0
	float flStartTime = Plat_FloatTime();

	if ((flStartTime - m_flLastProcessingTime) > 1.0f) {
		m_flLastProcessingTime = flStartTime;
		m_flFinalProcessingTime = 0.0f;
	}
#endif

	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);

	oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	const auto pMessageHandler = *reinterpret_cast<INetChannelHandler**>(reinterpret_cast<uintptr_t>(thisptr) + 0x3ED0);

	QueryPerformanceCounter(&end);

	auto time_ticks = end.QuadPart - start.QuadPart;
	auto time_msec = time_ticks * 1000 / g_PerformanceFrequency;

	bool bIsProcessingTimeReached = (time_msec >= 1);

	if (pMessageHandler && bIsProcessingTimeReached && time_msec >= net_chan_limit_msec) {
		// TODO(mrsteyk): move that name thing out.
#ifdef _DEBUG
		Msg("CNetChan::ProcessPacket: Max processing time reached for client \"%s\" (%dms)\n", (const char*)(uintptr_t(thisptr) + 0x3EA8), time_msec);
#endif
		pMessageHandler->ConnectionCrashed("Max processing time reached");
	}
}

struct CLC_VoiceData {
	char gap0[24];
	void* m_pMessageHandler;
	int m_nLength;
	CBitRead m_DataIn;
	CBitWrite m_DataOut;
	__declspec(align(16)) char byte30;
	char gap31[39];
	uint64 qword88;
};

bool(__fastcall* oCGameClient__ProcessVoiceData)(void*, CLC_VoiceData*);

bool __fastcall CGameClient__ProcessVoiceData(void* thisptr, CLC_VoiceData* msg) {
	char voiceDataBuffer[4096];

	void* thisptr_shifted = reinterpret_cast<uintptr_t>(thisptr) == 16 ? nullptr : reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(thisptr) - 8);
	int bitsRead = msg->m_DataIn.ReadBitsClamped(voiceDataBuffer, msg->m_nLength);

	if (msg->m_DataIn.IsOverflowed())
		return false;

	auto SV_BroadcastVoiceData = reinterpret_cast<void(__cdecl*)(void*, int, char*, uint64)>(G_engine + 0xEE4D0);

	if (thisptr_shifted)
		SV_BroadcastVoiceData(thisptr_shifted, (bitsRead + 7) / 8, voiceDataBuffer, *reinterpret_cast<uint64*>(reinterpret_cast<uintptr_t>(msg) + 0x88));

	return true;
}

//-

#if 0
#define MAX_USER_MSG_DATA 255

struct SVC_UserMessage {
	unsigned char gap0[24];
	void* m_nMessageHandler;
	int m_nMsgType;
	int m_nLength;
	CBitRead m_DataIn;
	CBitWrite m_DataOut;
};

bool(__fastcall* oCClientState__ProcessUserMessage)(void*, SVC_UserMessage*);

bool __fastcall CClientState__ProcessUserMessage(void* thisptr, SVC_UserMessage* msg) {
	ALIGN4 byte userdata[MAX_USER_MSG_DATA] = { 0 };

	bf_read userMsg("UserMessage(read)", userdata, sizeof(userdata));

	int bitsRead = msg->m_DataIn.ReadBitsClamped(userdata, msg->m_nLength);
	userMsg.StartReading(userdata, (bitsRead + 7) / 8);

	const auto& res = g_ClientDLL->DispatchUserMessage(msg->m_nMsgType, &userMsg);

	if (!res) {
		Msg("Couldn't dispatch user message (%i)\n", msg->m_nMsgType);

		return res;
	}

	return res;
}
#endif

//-

constexpr int NET_FILE = 2;

bool(__fastcall* oCNetChan__ProcessControlMessage)(CNetChan*, int, CBitRead*);

bool __fastcall CNetChan__ProcessControlMessage(CNetChan* thisptr, int cmd, CBitRead* buf) {
	if (buf) {
		unsigned char _cmd = buf->ReadUBitLong(6);

		if (_cmd >= NET_FILE)
			return false;
	}

	return oCNetChan__ProcessControlMessage(thisptr, cmd, buf);
}

//-

#if 0
bool(__fastcall* oCClientState__ProcessVoiceData)(void*, SVC_VoiceMessage*);

bool __fastcall CClientState__ProcessVoiceData(void* thisptr, SVC_VoiceMessage* msg) {
	//char chReceived[4104];

	//unsigned int dwLength = msg->m_nLength;

	//if (dwLength >= 0x1000)
	//	dwLength = 4096;

	//int bitsRead = msg->m_DataIn.ReadBitsClamped(chReceived, msg->m_nLength);

	//if (msg->m_bFromClient)
	//	return false;

	//int iEntity = (msg->m_bFromClient + 1);
	//int nChannel = Voice_GetChannel(iEntity);

	//if (nChannel != -1)
	//	return Voice_AddIncomingData(nChannel, chReceived, *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(thisptr) + 0x84), true);

	//nChannel = Voice_AssignChannel(iEntity, false);

	//if (nChannel != -1)
	//	return Voice_AddIncomingData(nChannel, chReceived, *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(thisptr) + 0x84), true);

	//return nChannel;
	return oCClientState__ProcessVoiceData(thisptr, msg);
}
#endif

//-

struct CPostDataUpdateCall {
	int m_iEnt;
	int m_UpdateType;
};

struct CEntityInfo {
	void* __vftable;
	void* m_pFrom;
	void* m_pTo;
	char pad[0x4];
	bool m_bAsDelta;
	int m_UpdateType;
	int m_nOldEntity;
	int m_nNewEntity;
	int m_nHeaderBase;
	int m_nHeaderCount;
};

struct CEntityReadInfo : CEntityInfo {
	CBitRead* m_pBuf;
	int m_UpdateFlags;
	bool m_bIsEntity;
	int m_nBaseline;
	bool m_bUpdateBaselines;
	int m_nLocalPlayerBits;
	int m_nOtherPlayerBits;
	CPostDataUpdateCall m_PostDataUpdateCalls[2048];
	int m_nPostDataUpdateCalls;
};

void(__cdecl* oCL_CopyExistingEntity)(CEntityReadInfo&);

void CL_CopyExistingEntity(CEntityReadInfo& u) {
	if (u.m_nNewEntity >= 2048 || u.m_nNewEntity < 0) {
#if BUILD_DEBUG
		Warning("R1Delta: CL_CopyExistingEntity: m_nNewEntity (%d) out of bounds!! (exceeds >= 2048)\n", u.m_nNewEntity);
#endif
		Host_Error("CL_CopyExistingEntity: u.m_nNewEntity out of bounds.");
		return;
	}

	return oCL_CopyExistingEntity(u);
}

//-

bool __fastcall CBaseClientState__ProcessGetCvarValue(void* thisptr, void* msg) {
	return true;
}

//-

#if 0
struct __declspec(align(8)) SVC_SplitScreen {
	enum ESplitScreenMessageType : int {
		MSG_ADDUSER = 0x0,
		MSG_REMOVEUSER = 0x1,
		MSG_TYPE_BITS = 0x1,
	};

	void* _vftable;
	char pad[16];
	void* m_pMessageHandler;
	int m_Type;
	int m_nSlot;
	int m_nPlayerIndex;
};

class ISplitScreen {
public:
	bool RemoveSplitScreenPlayer(int type) {
		auto vtbl = *(uintptr_t**)this;
		return ((bool(__fastcall*)(void*, int))(vtbl[7]))(this, type);
	}

	bool AddSplitScreenPlayer(int type) {
		auto vtbl = *(uintptr_t**)this;
		return ((bool(__fastcall*)(void*, int))(vtbl[6]))(this, type);
	}
};

bool __fastcall CBaseClientState__ProcessSplitScreenUser(void* thisptr, SVC_SplitScreen* msg) {
	static auto splitscreen = (ISplitScreen*)G_engine + 0x797060;

	if (msg->m_nSlot < 0 || msg->m_nSlot > 1) {
#if BUILD_DEBUG
		Warning("CBaseClientState::ProcessSplitScreenUser: failed to process \"(msg->m_nSlot < 0 || msg->m_nSlot > 1)\".\n");
#endif
		return false;
	}

	if (splitscreen->RemoveSplitScreenPlayer(msg->m_Type))
		splitscreen->AddSplitScreenPlayer(msg->m_Type);

	return true;
}
#endif

bool __fastcall CBaseClient__IsSplitScreenUser(void* thisptr) {
	return false;
}

//-

void
security_fixes_engine(uintptr_t engine_base)
{
	void RegisterConVar(const char* name, const char* value, int flags, const char* helpString);
	//RegisterConVar("net_chan_limit_msec", "225", FCVAR_GAMEDLL | FCVAR_CHEAT, "Netchannel processing is limited to so many milliseconds, abort connection if exceeding budget");

	// TODO(mrsteyk): why is there const qualifier in the first place?
	//cvar_net_chan_limit_msec = (ConVarR1*)OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	//R1DAssert(cvar_net_chan_limit_msec);

	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E1230), &CNetChan__ProcessControlMessage, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessControlMessage)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x27EA0), &CBaseClientState__ProcessGetCvarValue, NULL) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x536F0), &CL_CopyExistingEntity, reinterpret_cast<LPVOID*>(&oCL_CopyExistingEntity)) == MH_OK);

	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E96E0), &CNetChan__ProcessPacket, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessPacket)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E51D0), &CNetChan___ProcessMessages, reinterpret_cast<LPVOID*>(&oCNetChan___ProcessMessages)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0xDA330), &CGameClient__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCGameClient__ProcessVoiceData)) == MH_OK);
#if 0
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x17D400), &CClientState__ProcessUserMessage, reinterpret_cast<LPVOID*>(&oCClientState__ProcessUserMessage)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x17D600), &CClientState__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCClientState__ProcessVoiceData)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E0C80), &CNetChan__FlowNewPacket, NULL) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x237F0), &CBaseClientState__ProcessSplitScreenUser, NULL) == MH_OK);
#endif
}

void
security_fixes_server(uintptr_t engine_base)
{
	//MH_CreateHook((LPVOID)(engine_base + 0xD4E30), &CBaseClient__ProcessSignonState, reinterpret_cast<LPVOID*>(&oCBaseClient__ProcessSignonState));
	MH_CreateHook((LPVOID)(engine_base + 0xD1EC0), &CBaseClient__IsSplitScreenUser, reinterpret_cast<LPVOID*>(NULL));
}
