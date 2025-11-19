#pragma once

#include <mutex>

namespace eos
{

// Provides a global mutex that must be held while invoking any EOS SDK API.
std::recursive_mutex& GetSdkMutex();

using SdkLock = std::lock_guard<std::recursive_mutex>;

} // namespace eos

