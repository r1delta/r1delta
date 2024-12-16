#pragma once

extern int Voice_GetChannel(int nEntity);
extern int Voice_AssignChannel(int nEntity, bool bProximity);
extern int Voice_AddIncomingData(int nChannel, const char* pchData, int iSequenceNumber, bool bIsCompressed);