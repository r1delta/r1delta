// Audio cache hooks for R1Delta
// Handles missing sound file fallbacks

#include "audio_cache.h"
#include "load.h"
#include <cstring>

// Audio cache hook for missing valkyrie sounds
GetAcacheHk_t GetAcacheOriginal = nullptr;

__int64* GetAcacheHk(const char* wav_path) {
    auto ret = GetAcacheOriginal(wav_path);
    if (!ret) {
        if (strstr(wav_path, "wpn_valkyrie_1p_wpnfire_reduced_2ch_01") != nullptr) {
            return (__int64*)(G_engine + 0x1fcdae8);
        }
        else if (strstr(wav_path, "wpn_valkyrie_1p_wpnfire_reduced_2ch_02") != nullptr) {
            return (__int64*)(G_engine + 0x1fcdc10);
        }
        return ret;
    }
    return ret;
}
