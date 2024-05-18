#include "sendmoveclampfix.h"

CLC_Move__ReadFromBufferType CLC_Move__ReadFromBufferOriginal;

bool __fastcall CLC_Move__ReadFromBuffer(CLC_Move* thisptr, bf_read& buffer)
{
	int bak = buffer.GetNumBitsRead();
	if (buffer.ReadLongLong() != 0xd0032147bf50a000) {
		buffer.m_bOverflow = false;
		buffer.Seek(bak);
		buffer.m_bOverflow = false;
		return CLC_Move__ReadFromBufferOriginal(thisptr, buffer);
	}
	thisptr->m_nNewCommands = buffer.ReadUBitLong(7);
	thisptr->m_nBackupCommands = buffer.ReadUBitLong(4);
	thisptr->m_nLength = buffer.ReadWord();
	thisptr->m_DataIn = buffer;
	return buffer.SeekRelative(thisptr->m_nLength);
}

bool __fastcall CLC_Move__WriteToBuffer(CLC_Move* thisptr, bf_write& buffer)
{
	thisptr->m_nLength = thisptr->m_DataOut.GetNumBitsWritten();
	buffer.WriteLongLong(0xd0032147bf50a000);
	buffer.WriteUBitLong(thisptr->m_nNewCommands, 7);
	buffer.WriteUBitLong(thisptr->m_nBackupCommands, 4);
	buffer.WriteWord(thisptr->m_nLength);
	return buffer.WriteBits(thisptr->m_DataOut.GetData(), thisptr->m_nLength);
}