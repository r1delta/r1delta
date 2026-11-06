#pragma once

// The one place the Winsock hooks (EOS, overlay, punch traffic) and the delta_p2p_* layer are brought up. Every host mode runs it after
// Host_Init (client/listen, legacy dedicated, R1O fake dedicated), and -nop2p keeps all three off the p2p layer entirely (no Winsock
// hooks, no delta_p2p_* convars, no heartbeat transports), which is what the flag has always promised. It used to be honoured on the R1O
// path only; on Wine the flag was used as a bypass for the socket-hook install failure, and the other two paths ignored it.

#include "core.h"
#include "eos_network.h"
#include "logging.h"
#include "net_hooks.h"
#include "p2p/p2p.h"

inline void InitializeP2PLayer()
{
	if (HasEngineCommandLineFlag("-nop2p")) {
		Msg("p2p: disabled by -nop2p\n");
		return;
	}
	if (!eos::InitializeNetworking())
		Msg("EOS: Initialization skipped or failed\n");
	net_hooks::Initialize();
	p2p::Initialize();
}
