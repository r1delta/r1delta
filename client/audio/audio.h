#pragma once
#include <cstdint>

bool InstallR1AudioReadHooks(std::uintptr_t filesystemBase);
bool R1AudioReadHooksInstalled();
void AudioReportMemory();
