#pragma once
#include "engine.h"
bool InitNetChanWarningHooks();

class CNetChan;

struct /*VFT*/ INetMessage {
	virtual (~INetMessage)();
	virtual void(SetNetChannel)(void*);
	virtual void(SetReliable)(bool);
	virtual bool(Process)();
	virtual bool(ReadFromBuffer)(bf_read*);
	virtual bool(WriteToBuffer)(bf_write*);
	virtual bool(IsUnreliable)();
	virtual bool(IsReliable)();
	virtual int(GetType)();
	virtual int(GetGroup)();
	virtual const char* (GetName)();
	virtual CNetChan* (GetNetChannel)();
	virtual const char* (ToString)();
	virtual unsigned int(GetSize)();
};

bool InitNetChanWarningHooks();