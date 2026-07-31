#pragma once

#include <stddef.h>
#include <stdint.h>

bool R1OVPK_HasFile(const char* relativeResourcePath);
bool R1OVPK_GetFileSize(const char* relativeResourcePath, uint64_t* size);
void* R1OVPK_OpenFile(const char* relativeResourcePath);
bool R1OVPK_ReadFile(void* handle, void* output, int bytesToRead, int* bytesRead);
bool R1OVPK_CloseFile(void* handle);
bool R1OVPK_SeekFile(void* handle, int offset, int origin);
bool R1OVPK_TellFile(void* handle, uint64_t* position);
bool R1OVPK_SizeFile(void* handle, uint64_t* size);
bool R1OVPK_IsFileOk(void* handle, bool* isOk);
bool R1OVPK_IsEndOfFile(void* handle, bool* isEndOfFile);
bool R1OVPK_ReadLine(void* handle, char* output, int maxChars, char** result);
