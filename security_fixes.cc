#include "core.h"
#include "engine.h"
#include "netadr.h"

#include "logging.h"
#include "load.h"
#include "factory.h"

ConVarR1* cvar_net_chan_limit_msec = nullptr;
ConVarR1* cvar_net_maxpacketdrop = nullptr;


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
int(__fastcall* oCNetChan__ProcessPacketHeader)(CNetChan*, netpacket_s*);
bool g_bInProcessMessageClient = false;
static int g_recursiveBroadcastDepth = 0; // Counter for recursive calls
constexpr int MAX_RECURSIVE_BROADCAST_DEPTH = 3;

class ProcessMessageScope {
public:
	ProcessMessageScope() { g_bInProcessMessageClient = true; }
	~ProcessMessageScope() { g_bInProcessMessageClient = false; }
};
char (*oSV_BroadcastMessageWithFilterLISTEN)(__int64 a1, __int64 a2, __int64 a3);
char SV_BroadcastMessageWithFilterLISTEN(__int64 a1, __int64 a2, __int64 a3) {
	if (g_bInProcessMessageClient) {
		g_recursiveBroadcastDepth++;
		if (g_recursiveBroadcastDepth > MAX_RECURSIVE_BROADCAST_DEPTH) {
			Warning("Recursive message broadcast depth limit (%d) exceeded. Blocking call.\n", MAX_RECURSIVE_BROADCAST_DEPTH);
			// We decrement here because the call is blocked, effectively ending this recursive path.
			g_recursiveBroadcastDepth--;
			return true; // Block the call
		}
		// Call allowed, decrement after it returns
		char result = oSV_BroadcastMessageWithFilterLISTEN(a1, a2, a3);
		g_recursiveBroadcastDepth--;
		return result;
	} else {
		// Reset counter if the call is not from within ProcessMessages
		g_recursiveBroadcastDepth = 0;
		return oSV_BroadcastMessageWithFilterLISTEN(a1, a2, a3);
	}
}
void* lastDeletedNetChan = NULL;
void* (*oCNetChan___dtor)(CNetChan* a1, __int64 a2, __int64 a3);
void* __fastcall CNetChan___dtor(CNetChan* a1, __int64 a2, __int64 a3) {
    lastDeletedNetChan = a1;
    return oCNetChan___dtor(a1, a2, a3);
}
bool __fastcall CNetChan___ProcessMessages(CNetChan* thisptr, bf_read* buf) {
	std::unique_ptr<ProcessMessageScope> scope;
	if ((*(uint8_t*)(((uintptr_t)thisptr) + 216) <= 0)) {
		scope = std::make_unique<ProcessMessageScope>();
	}

	if (!thisptr || !buf)
		return oCNetChan___ProcessMessages(thisptr, buf);

	if (buf->GetNumBitsRead() < 6 || buf->IsOverflowed()) // idfk tbh just move this to whenever a net message processes.
		return oCNetChan___ProcessMessages(thisptr, buf);

	//static auto net_chan_limit_msec_ptr = OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	auto net_chan_limit_msec = cvar_net_chan_limit_msec->m_Value.m_fValue;

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
    lastDeletedNetChan = NULL;
	g_recursiveBroadcastDepth = 0; // Reset recursion counter before processing messages
	const auto original = oCNetChan___ProcessMessages(thisptr, buf);
    // wndrr: we were deleted, bail out!
    if (lastDeletedNetChan == thisptr) {
        lastDeletedNetChan = NULL;
        return false;
    }

	QueryPerformanceCounter(&end);

	auto time_ticks = end.QuadPart - start.QuadPart;
	auto time_msec = time_ticks * 1000 / g_PerformanceFrequency;

	bool bIsProcessingTimeReached = (time_msec >= 1);

	const auto pMessageHandler = *reinterpret_cast<INetChannelHandler**>(reinterpret_cast<uintptr_t>(thisptr) + 0x3ED0);

	if (pMessageHandler && bIsProcessingTimeReached && time_msec >= net_chan_limit_msec && *(uint8_t*)(((uintptr_t)thisptr) + 216) > 0) {
		// TODO(mrsteyk): move that name thing out.
#ifdef BUILD_DEBUG
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

	//static auto net_chan_limit_msec_ptr = OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	auto net_chan_limit_msec = cvar_net_chan_limit_msec->m_Value.m_fValue;

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
    lastDeletedNetChan = NULL;
	oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);
    // wndrr: we were deleted, bail out!
    if (lastDeletedNetChan == thisptr) {
        lastDeletedNetChan = NULL;
        return;
    }

	const auto pMessageHandler = *reinterpret_cast<INetChannelHandler**>(reinterpret_cast<uintptr_t>(thisptr) + 0x3ED0);

	QueryPerformanceCounter(&end);

	auto time_ticks = end.QuadPart - start.QuadPart;
	auto time_msec = time_ticks * 1000 / g_PerformanceFrequency;

	bool bIsProcessingTimeReached = (time_msec >= 1);

	if (pMessageHandler && bIsProcessingTimeReached && time_msec >= net_chan_limit_msec && *(uint8_t*)(((uintptr_t)thisptr) + 216) > 0) {
		// TODO(mrsteyk): move that name thing out.
#ifdef BUILD_DEBUG
		Msg("CNetChan::ProcessPacket: Max processing time reached for client \"%s\" (%dms)\n", (const char*)(uintptr_t(thisptr) + 0x3EA8), time_msec);
#endif
		pMessageHandler->ConnectionCrashed("Max processing time reached");
	}
}


int __fastcall CNetChan__ProcessPacketHeader(CNetChan* thisptr, netpacket_s* packet) {
    if (!thisptr)
        return oCNetChan__ProcessPacketHeader(thisptr, packet);

    if(!packet) {
        return -1;
	}

    int seqNum = *(int*)((uintptr_t)packet + 84);
    
    int m_PacketDrop = seqNum - (thisptr->m_nInSequenceNr + thisptr->m_nChokedPackets + 1);

    if (cvar_net_maxpacketdrop->m_Value.m_nValue > 0 && m_PacketDrop > cvar_net_maxpacketdrop->m_Value.m_nValue) {
        Msg("CNetChan::ProcessPacketHeader: Packet drop (%d) exceeded limit (%d)\n", m_PacketDrop, cvar_net_maxpacketdrop->m_Value.m_nValue);
        return -1;
    }
    return oCNetChan__ProcessPacketHeader(thisptr, packet);
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
	char voiceDataBuffer[32767];

	void* thisptr_shifted = reinterpret_cast<uintptr_t>(thisptr) == 16 ? nullptr : reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(thisptr) - 8);
	int bitsRead = msg->m_DataIn.ReadBitsClamped(voiceDataBuffer, msg->m_nLength);

	if (msg->m_DataIn.IsOverflowed())
		return false;

	auto SV_BroadcastVoiceData = reinterpret_cast<void(__cdecl*)(void*, int, char*, uint64)>(G_engine + 0xEE4D0);
    if (IsDedicatedServer())
        auto SV_BroadcastVoiceData = reinterpret_cast<void(__cdecl*)(void*, int, char*, uint64)>(G_engine_ds + 0x5FB80);
	if (thisptr_shifted)
		SV_BroadcastVoiceData(thisptr_shifted, (bitsRead + 7) / 8, voiceDataBuffer, *reinterpret_cast<uint64*>(reinterpret_cast<uintptr_t>(msg) + 0x88));

	return true;
}

//-

class IBaseClientDLL {
public:
	bool DispatchUserMessage(int msg_type, bf_read* msg_data) {
		return CallVFunc<bool>(43, this, msg_type, msg_data);
	}
};

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

	static auto g_ClientDLL = (IBaseClientDLL*)(G_client + 0xAEA760);
	const auto res = g_ClientDLL->DispatchUserMessage(msg->m_nMsgType, &userMsg);

	if (!res) {
		Msg("Couldn't dispatch user message (%i)\n", msg->m_nMsgType);

		return res;
	}

	return res;
}

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

struct SVC_VoiceData {
	unsigned char gap0[32];
	unsigned int m_nFromClient;
	int m_nLength;
	uint64 m_nXUID;
	CBitRead m_DataIn;
	CBitWrite m_DataOut;
};
static_assert(offsetof(SVC_VoiceData, m_DataOut) == 0x70);

int Voice_GetChannel(int nEntity) {
	return reinterpret_cast<int(*)(int)>(G_engine + 0x178F0)(nEntity);
}

int Voice_AssignChannel(int nEntity, bool bProximity) {
	return reinterpret_cast<int(*)(int, bool)>(G_engine + 0x177C0)(nEntity, bProximity);
}

int Voice_AddIncomingData(int iEntity, int nChannel, const char* pchData, int nDataSize, int iSequenceNumber, bool bIsCompressed) {
	return reinterpret_cast<int(*)(int, int, const char*, int, int, bool)>(G_engine + 0x17A80)(iEntity, nChannel, pchData, nDataSize, iSequenceNumber, bIsCompressed);
}

#define VOICE_CHANNEL_ERROR -1

bool(__fastcall* oCClientState__ProcessVoiceData)(void*, SVC_VoiceData*);



class IVoiceCodec
{
protected:
	virtual            ~IVoiceCodec() {}

public:
	// Initialize the object. The uncompressed format is always 8-bit signed mono.
	virtual bool    Init(int quality, unsigned int nSamplesPerSec) = 0;

	// Use this to delete the object.
	virtual void    Release() = 0;


	// Compress the voice data.
	// pUncompressed        -    16-bit signed mono voice data.
	// maxCompressedBytes    -    The length of the pCompressed buffer. Don't exceed this.
	// bFinal                -    Set to true on the last call to Compress (the user stopped talking).
	//                            Some codecs like big block sizes and will hang onto data you give them in Compress calls.
	//                            When you call with bFinal, the codec will give you compressed data no matter what.
	// Return the number of bytes you filled into pCompressed.
	virtual int        Compress(const char* pUncompressed, int nSamples, char* pCompressed, int maxCompressedBytes/*, bool bFinal*/) = 0;

	// Decompress voice data. pUncompressed is 16-bit signed mono.
	virtual int        Decompress(const char* pCompressed, int compressedBytes, char* pUncompressed, int maxUncompressedBytes) = 0;

	// Some codecs maintain state between Compress and Decompress calls. This should clear that state.
	virtual bool    ResetState() = 0;
};
char g_VoiceBuffer[20][65536] = { 0 }; // Or whatever size you want
int g_VoiceBufferPos[20] = { 0 };  // Track position in each buffer
float GetVolumeCurve(float volume, int curveType) {
    float result = 0.0f;
    if (volume <= 0.0f)
        return 0.0f;
    if (volume >= 1.0f)
        return 1.0f;

    switch (curveType) {
    case 0:
        return volume;
    case 1: {
        float* curveParam = (float*)(G_engine + 0x1A0DC2C); // Where dword_181A0DC2C was
        return logf(1.0f - (volume * 0.9f)) * (*curveParam * -1.0f);
    }
          // ... other cases as needed
    }
    return result;
}

void SubmitVoiceAudio(const void* data, int len, int entityIndex) {
    // Keep original basic validation
   // if (!*(uint64_t*)(G_engine + 0x200DF78) || // qword_18200DF78 
   //     !*(_DWORD*)(voice_enable + 92) ||
   //     len > sizeof(g_VoiceBuffer[0]) ||
   //     (*(unsigned __int8(__fastcall**)(int64_t, uint32_t))(*(_QWORD*)g_ClientDLL + 456i64))(g_ClientDLL, entityIndex))
   // {
   //     return;
   // }

    // Get voice channel object base
    char* voiceChannelBase = (char*)(G_engine + 0x2017B60); // unk_182017B60
    char* voiceChannel = voiceChannelBase + 200 * entityIndex;

    // Setup our audio buffer descriptor
    struct AudioBufferDesc {
        int64_t field0;
        const char* buffer;
        int64_t field16;
        int64_t field24;
        int64_t field32;
        int field40;
    } desc = {};

    desc.buffer = (const char*)data;
    *(int*)((char*)&desc + 12) = 2 * len;

    // Handle volume scaling
    float volume = 0.0f;
    float rawVolume = 1.f;//*(float*)(sound_volume_voice + 88);
    if (rawVolume >= 0.0f) {
        volume = 1.0f;
        if (rawVolume <= 1.0f)
            volume = rawVolume;
    }

    // Apply volume curve using our new function
    float scaledVolume = GetVolumeCurve(volume, 0);

    // Get audio interface from voice channel
    auto audioInterface = *(int64_t**)(voiceChannel + 24 * 8);

    // Submit audio data
    char tempBuffer[8];
    (*(void(__fastcall**)(int64_t*, int64_t, int64_t))((*audioInterface) + 96))(audioInterface, 0, 0);
    (*(void(__fastcall**)(int64_t*, char*))((*audioInterface) + 200))(audioInterface, tempBuffer);

    if (*(int*)(tempBuffer + 8) == 64)
        (*(void (**)(void))((*audioInterface) + 176))();
    else
        (*(void(__fastcall**)(int64_t*, AudioBufferDesc*, int64_t))((*audioInterface) + 168))(audioInterface, &desc, 0);
}
bool __fastcall CClientState__ProcessVoiceData(void* thisptr, SVC_VoiceData* msg) {
    char chReceived[0x4000];
    unsigned int dwLength = msg->m_nLength;
    if (dwLength >= 0x4000)
        dwLength = 0x4000;
    unsigned int dwLengthBytes = dwLength * 8;
    int bitsRead = msg->m_DataIn.ReadBitsClamped(chReceived, dwLengthBytes);
    if (bitsRead == 0 || msg->m_DataIn.IsOverflowed() || msg->m_nFromClient > 21 || msg->m_nFromClient < 0)
        return false;

    //typedef IVoiceCodec* (*CreateInterfaceFn)(const char* name, int* returnCode);
    //CreateInterfaceFn CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(GetModuleHandleA("vaudio_speex.dll"), "CreateInterface"));
    //static auto codec = CreateInterface("vaudio_speex", 0);
    //static bool bDone = false;
    //if (!bDone) {
    //    bDone = true;
    //    codec->Init(3, 48000);
    //}
    //
    //char chDecoded[8192];
    //unsigned int decodedLen = codec->Decompress(chReceived, dwLength, chDecoded, sizeof(chDecoded));
    //if (decodedLen == 0)
    //    return false;
    static auto CL_ProcessVoiceData = reinterpret_cast<void(*)(unsigned __int64, unsigned int, char*, unsigned int)>(G_engine + 0x17310);
    CL_ProcessVoiceData(msg->m_nXUID, msg->m_nFromClient, chReceived, dwLength);
    return true;



}
HMODULE __fastcall sub_180126B40(char* a1, __int64 a2, __int64 a3, __int64 a4) {
    if (strcmp(a1, "vaudio_speex"))
        return 0;
    return LoadLibraryA("r1delta\\bin_delta\\vaudio_speex.dll");
}
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

bool __fastcall CBaseClient__IsSplitScreenUser(void* thisptr) {
	return false;
}

//-

void CNetChan___FlowNewPacket(CNetChan* pChan, int flow, int outSeqNr, int inSeqNr, int nChoked, int nDropped, int nSize)
{
    float netTime; // xmm4_8 (was double)
    int v8; // r13d
    int v9; // r14d
    int v12; // r12d
    int currentindex; // eax
    int nextIndex; // r15d
    int v17; // r8d
    int v18; // ebp
    unsigned int v19; // eax
    int v20; // r9 (was char)
    int v21; // r8d
    __int64 v22; // r14
    float time; // xmm0_4
    __int64 v24; // rdx
    __int64 v25; // rcx
    __int64 v26; // rdx
    __int64 v27; // rcx
    __int64 v28; // rdx
    __int64 v29; // rcx
    int v30; // edx
    int v31; // r8 (was char)
    float v32; // xmm0_4
    __int64 v33; // r9
    __int64 v34; // rax
    __int64 v35; // rdx
    int v36; // r8d
    float v37; // xmm3_4
    __int64 result; // rax
    float v39; // xmm1_4
    float v40; // xmm0_4
    float v41; // xmm1_4
    netframe_header_t* v42; // rdx
    float v43; // xmm0_4
    float v44; // xmm2_4
    float v45; // xmm0_4

    netTime = *reinterpret_cast<double*>(G_engine + 0x30EF1C0);
    v8 = flow;
    v9 = inSeqNr;
    netflow_t* pFlow = &pChan->m_DataFlow[flow];
    v12 = outSeqNr;

    netframe_header_t* pFrameHeader = nullptr;
    netframe_t* pFrame = nullptr;

    currentindex = pFlow->currentindex;
    if (outSeqNr > currentindex)
    {
        nextIndex = currentindex + 1;
        if (currentindex + 1 <= outSeqNr)
        {
            // This variable makes sure the loops below do not execute more
            // than NET_FRAMES_BACKUP times. This has to be done as the
            // headers and frame arrays in the netflow_t structure is as
            // large as NET_FRAMES_BACKUP. Any execution past it is futile
            // and only wastes CPU time. Sending an outSeqNr that is higher
            // than the current index by something like a million or more will
            // hang the engine for several milliseconds to several seconds.
            int numPacketFrames = 0;

            v17 = outSeqNr - nextIndex;

            if (v17 + 1 >= 4)
            {
                v18 = nChoked + nDropped;
                v19 = ((unsigned int)(v12 - nextIndex - 3) >> 2) + 1;
                v20 = nextIndex + 2;
                v21 = v17 - 2;
                v22 = v19;
                time = netTime;
                nextIndex += 4 * v19;

                do
                {
                    v24 = (v20 - 2) & NET_FRAMES_MASK;
                    v25 = v24;
                    pFlow->frame_headers[v25].time = time;
                    pFlow->frame_headers[v25].valid = 0;
                    pFlow->frame_headers[v25].size = 0;
                    pFlow->frame_headers[v25].latency = -1.0;
                    pFlow->frames[v24].avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
                    pFlow->frame_headers[v25].choked = 0;
                    pFlow->frames[v24].dropped = 0;
                    if (v21 + 2 < v18)
                    {
                        if (v21 + 2 >= nChoked)
                            pFlow->frames[v24].dropped = 1;
                        else
                            pFlow->frame_headers[(v20 - 2) & NET_FRAMES_MASK].choked = 1;
                    }
                    v26 = (v20 - 1) & NET_FRAMES_MASK;
                    v27 = v26;
                    pFlow->frame_headers[v27].time = time;
                    pFlow->frame_headers[v27].valid = 0;
                    pFlow->frame_headers[v27].size = 0;
                    pFlow->frame_headers[v27].latency = -1.0;
                    pFlow->frames[v26].avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
                    pFlow->frame_headers[v27].choked = 0;
                    pFlow->frames[v26].dropped = 0;
                    if (v21 + 1 < v18)
                    {
                        if (v21 + 1 >= nChoked)
                            pFlow->frames[v26].dropped = 1;
                        else
                            pFlow->frame_headers[(v20 - 1) & NET_FRAMES_MASK].choked = 1;
                    }
                    v28 = v20 & NET_FRAMES_MASK;
                    v29 = v28;
                    pFlow->frame_headers[v29].time = time;
                    pFlow->frame_headers[v29].valid = 0;
                    pFlow->frame_headers[v29].size = 0;
                    pFlow->frame_headers[v29].latency = -1.0;
                    pFlow->frames[v28].avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
                    pFlow->frame_headers[v29].choked = 0;
                    pFlow->frames[v28].dropped = 0;
                    if (v21 < v18)
                    {
                        if (v21 >= nChoked)
                            pFlow->frames[v28].dropped = 1;
                        else
                            pFlow->frame_headers[v20 & NET_FRAMES_MASK].choked = 1;
                    }
                    pFrame = &pFlow->frames[(v20 + 1) & NET_FRAMES_MASK];
                    pFrameHeader = &pFlow->frame_headers[(v20 + 1) & NET_FRAMES_MASK];
                    pFrameHeader->time = time;
                    pFrameHeader->valid = 0;
                    pFrameHeader->size = 0;
                    pFrameHeader->latency = -1.0;
                    pFrame->avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
                    pFrameHeader->choked = 0;
                    pFrame->dropped = 0;
                    if (v21 - 1 < v18)
                    {
                        if (v21 - 1 >= nChoked)
                            pFrame->dropped = 1;
                        else
                            pFrameHeader->choked = 1;
                    }

                    // Incremented by four since this loop does four frames
                    // per iteration.
                    numPacketFrames += 4;
                    v21 -= 4;
                    v20 += 4;
                    --v22;
                } while (v22 && numPacketFrames < NET_FRAMES_BACKUP);
                v12 = outSeqNr;
                v8 = flow;
                v9 = inSeqNr;
            }

            // Check if we did not reach NET_FRAMES_BACKUP, else we will
            // execute the 129'th iteration as well. Also check if the next
            // index doesn't exceed the outSeqNr.
            if (numPacketFrames < NET_FRAMES_BACKUP && nextIndex <= v12)
            {
                v30 = v12 - nextIndex;
                v31 = nextIndex;
                v33 = v12 - nextIndex + 1;
                do
                {
                    pFrame = &pFlow->frames[v31 & NET_FRAMES_MASK];
                    pFrameHeader = &pFlow->frame_headers[v31 & NET_FRAMES_MASK];
                    v32 = netTime;
                    pFrameHeader->time = v32;
                    pFrameHeader->valid = 0;
                    pFrameHeader->size = 0;
                    pFrameHeader->latency = -1.0;
                    pFrame->avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
                    pFrameHeader->choked = 0;
                    pFrame->dropped = 0;
                    if (v30 < nChoked + nDropped)
                    {
                        if (v30 >= nChoked)
                            pFrame->dropped = 1;
                        else
                            pFrameHeader->choked = 1;
                    }
                    --v30;
                    ++v31;
                    --v33;
                    ++numPacketFrames;
                } while (v33 && numPacketFrames < NET_FRAMES_BACKUP);
                v9 = inSeqNr;
            }
        }
        pFrame->dropped = nDropped;
        pFrameHeader->choked = (short)nChoked;
        pFrameHeader->size = nSize;
        pFrameHeader->valid = 1;
        pFrame->avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
    }
    ++pFlow->totalpackets;
    pFlow->currentindex = v12;
    v34 = 544i64;

    if (!v8)
        v34 = 3688i64;

    pFlow->current_frame = pFrame;
    v35 = 548i64;
    v36 = *(uint32_t*)(&pChan->m_bProcessingMessages + v34);
    if (v9 > v36 - NET_FRAMES_BACKUP)
    {
        if (!v8)
            v35 = 3692i64;
        result = (__int64)pChan + 16 * (v9 & NET_FRAMES_MASK);
        v42 = (netframe_header_t*)(result + v35);
        if (v42->valid && v42->latency == -1.0)
        {
            v43 = 0.0;
            v44 = fmax(0.0f, netTime - v42->time);
            v42->latency = v44;
            if (v44 >= 0.0)
                v43 = v44;
            else
                v42->latency = 0.0;
            v45 = v43 + pFlow->latency;
            ++pFlow->totalupdates;
            pFlow->latency = v45;
            pFlow->maxlatency = fmaxf(pFlow->maxlatency, v42->latency);
        }
    }
    else
    {
        if (!v8)
            v35 = 3692i64;

        v37 = *(float*)(&pChan->m_bProcessingMessages + 16 * (v36 & NET_FRAMES_MASK) + v35);
        result = v35 + 16i64 * (((uint8_t)v36 + 1) & NET_FRAMES_MASK);
        v39 = v37 - *(float*)(&pChan->m_bProcessingMessages + result);
        ++pFlow->totalupdates;
        v40 = (float)((float)(v39 / 127.0) * (float)(v36 - v9)) + netTime - v37;
        v41 = fmaxf(pFlow->maxlatency, v40);
        pFlow->latency = v40 + pFlow->latency;
        pFlow->maxlatency = v41;
    }
}

//-

int* net_error = nullptr;

void NET_ClearLastError()
{
	*net_error = 0;
}

bool(__fastcall *oNET_ReceiveDatagram)(const int sock, netpacket_t* packet, bool encrypted);

bool __fastcall NET_ReceiveDatagram(const int sock, netpacket_t* packet, bool encrypted)
{
	// Failsafe: never call recvfrom more than a fixed number of times per frame.
	// We don't like the potential for infinite loops. Yes this means that 66000
	// invalid packets per frame will effectively DOS the server, but at that point
	// you're basically flooding the network and you need to solve this at a higher
	// firewall or router level instead which is beyond the scope of our netcode.
	// --henryg 10/12/2011
	for (int i = 1000; i > 0; --i)
	{
		// Attempt to receive a valid packet.
		NET_ClearLastError();
		if (oNET_ReceiveDatagram(sock, packet, encrypted))
		{
			// Received a valid packet.
			return true;
		}
		// NET_ReceiveDatagram calls Net_GetLastError() in case of socket errors
		// or a would-have-blocked-because-there-is-no-data-to-read condition.
		if (*net_error)
		{
			break;
		}
	}
	return false;
}




void
security_fixes_init()
{
	ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString);
	cvar_net_chan_limit_msec = RegisterConVar("net_chan_limit_msec", "225", FCVAR_GAMEDLL | FCVAR_CHEAT, "Netchannel processing is limited to so many milliseconds, abort connection if exceeding budget");
	R1DAssert(cvar_net_chan_limit_msec);
    cvar_net_maxpacketdrop = RegisterConVar("net_maxpacketdrop", "net_maxpacketdrop", 0, "Ignore any packets with the sequence number more than this ahead (0 == no limit)");
}

void
security_fixes_engine(uintptr_t engine_base)
{
	//R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E1230), &CNetChan__ProcessControlMessage, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessControlMessage)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x27EA0), &CBaseClientState__ProcessGetCvarValue, NULL) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x536F0), &CL_CopyExistingEntity, reinterpret_cast<LPVOID*>(&oCL_CopyExistingEntity)) == MH_OK);

	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E96E0), &CNetChan__ProcessPacket, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessPacket)) == MH_OK);
    R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E73C0), &CNetChan__ProcessPacketHeader, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessPacketHeader)) == MH_OK);
    R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E51D0), &CNetChan___ProcessMessages, reinterpret_cast<LPVOID*>(&oCNetChan___ProcessMessages)) == MH_OK);
    R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E9EA0), &CNetChan___dtor, reinterpret_cast<LPVOID*>(&oCNetChan___dtor)) == MH_OK);
    if (!IsDedicatedServer())
	    R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0xDA330), &CGameClient__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCGameClient__ProcessVoiceData)) == MH_OK);
    else
        R1DAssert(MH_CreateHook((LPVOID)(G_engine_ds + 0x4BA20), &CGameClient__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCGameClient__ProcessVoiceData)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x17D400), &CClientState__ProcessUserMessage, reinterpret_cast<LPVOID*>(&oCClientState__ProcessUserMessage)) == MH_OK);
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x17D600), &CClientState__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCClientState__ProcessVoiceData)) == MH_OK);
	//R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1E0C80), &CNetChan___FlowNewPacket, NULL) == MH_OK); // my fucking god STOP PASTING SHIT FROM APEX
    R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x1F23C0), &NET_ReceiveDatagram, reinterpret_cast<LPVOID*>(&oNET_ReceiveDatagram)) == MH_OK);
    R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x126B40), &sub_180126B40, reinterpret_cast<LPVOID*>(NULL)) == MH_OK);
	// TODO(dogecore): ip bans
	R1DAssert(MH_CreateHook((LPVOID)(engine_base + 0x237F0), &CBaseClientState__ProcessSplitScreenUser, NULL) == MH_OK);

	net_error = (int*)(G_engine + 0x30EF1D0);
}

void
security_fixes_server(uintptr_t engine_base, uintptr_t server_base)
{
	//MH_CreateHook((LPVOID)(engine_base + 0xD4E30), &CBaseClient__ProcessSignonState, reinterpret_cast<LPVOID*>(&oCBaseClient__ProcessSignonState));
	MH_CreateHook((LPVOID)(engine_base + 0xD1EC0), &CBaseClient__IsSplitScreenUser, reinterpret_cast<LPVOID*>(NULL));
	if (!IsDedicatedServer())
		MH_CreateHook((LPVOID)(engine_base + 0xcb6f0), &SV_BroadcastMessageWithFilterLISTEN, reinterpret_cast<LPVOID*>(&oSV_BroadcastMessageWithFilterLISTEN));
	// make listen server host party lead
	MH_CreateHook((LPVOID)(server_base + 0x510010), (LPVOID)(server_base + 0x25BD30), reinterpret_cast<LPVOID*>(NULL));
	
}
