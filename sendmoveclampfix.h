#pragma once

#include "bitbuf.h"

struct CLC_Move
{
	bool m_bReliable;
	void* m_NetChannel;
	void* unk;
	void* m_pMessageHandler;
	int m_nBackupCommands;
	int m_nNewCommands;
	int m_nLength;
	bf_read m_DataIn;
	bf_write m_DataOut;
};

typedef bool (*CLC_Move__ReadFromBufferType)(CLC_Move* thisptr, bf_read& buffer);
extern CLC_Move__ReadFromBufferType CLC_Move__ReadFromBufferOriginal;

bool __fastcall CLC_Move__ReadFromBuffer(CLC_Move* thisptr, bf_read& buffer);
bool __fastcall CLC_Move__WriteToBuffer(CLC_Move* thisptr, bf_write& buffer);