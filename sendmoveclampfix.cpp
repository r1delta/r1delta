#include "sendmoveclampfix.h"
#include "cvar.h"
CLC_Move__ReadFromBufferType CLC_Move__ReadFromBufferOriginal;
CLC_Move__WriteToBufferType CLC_Move__WriteToBufferOriginal;

bool __fastcall CLC_Move__ReadFromBuffer(CLC_Move* thisptr, bf_read& buffer)
{
	int bak = buffer.GetNumBitsRead();
	if (buffer.ReadUBitLong(7) != 0) {
		buffer.Seek(bak);
		return CLC_Move__ReadFromBufferOriginal(thisptr, buffer);
	}
	thisptr->m_nNewCommands = buffer.ReadUBitLong(6);
	thisptr->m_nBackupCommands = buffer.ReadUBitLong(4);
	thisptr->m_nLength = buffer.ReadWord();
	thisptr->m_DataIn = buffer;
	return buffer.SeekRelative(thisptr->m_nLength);
}

bool __fastcall CLC_Move__WriteToBuffer(CLC_Move* thisptr, bf_write& buffer)
{
	static auto var = OriginalCCVar_FindVar(cvarinterface, "net_secure");
	if (var->m_Value.m_nValue == 1)
		return CLC_Move__WriteToBufferOriginal(thisptr, buffer);
	thisptr->m_nLength = thisptr->m_DataOut.GetNumBitsWritten();
	buffer.WriteUBitLong(0, 7);
	buffer.WriteUBitLong(thisptr->m_nNewCommands, 6);
	buffer.WriteUBitLong(thisptr->m_nBackupCommands, 4);
	buffer.WriteWord(thisptr->m_nLength);
	return buffer.WriteBits(thisptr->m_DataOut.GetData(), thisptr->m_nLength);
}