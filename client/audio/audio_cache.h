// Audio cache hooks for R1Delta
#pragma once

#include <cstdint>

// Audio cache hook for missing valkyrie sounds
typedef __int64* (*GetAcacheHk_t)(const char*);
extern GetAcacheHk_t GetAcacheOriginal;
__int64* GetAcacheHk(const char* wav_path);
