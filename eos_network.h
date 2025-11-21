#pragma once

#include <cstdint>

#include "eos_sdk.h"

#include "fake_ip_layer.h"

namespace eos
{
    bool EnsureEosInitialized();
bool InitializeNetworking();
void ShutdownNetworking();
bool IsReady();

bool RegisterPeerByString(const char* remoteProductUserId,
                          const char* socketName,
                          uint8_t channel,
                          FakeEndpoint* outEndpoint);

EOS_ProductUserId GetLocalProductUserId();
FakeEndpoint GetLocalFakeEndpoint();

} // namespace eos
