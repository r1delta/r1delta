#include "voice.h"
#include "load.h"

int Voice_GetChannel(int nEntity) {
	return reinterpret_cast<int(*)(int)>(G_engine + 0x178F0)(nEntity);
}

int Voice_AssignChannel(int nEntity, bool bProximity) {
	return reinterpret_cast<int(*)(int, bool)>(G_engine + 0x177C0)(nEntity, bProximity);
}

int Voice_AddIncomingData(int nChannel, const char* pchData, int iSequenceNumber, bool bIsCompressed) {
	return reinterpret_cast<int(*)(int, const char*, int, bool)>(G_engine + 0x17A80)(nChannel, pchData, iSequenceNumber, bIsCompressed);
}