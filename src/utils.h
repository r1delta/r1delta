#pragma once

#include "framework.h"
#include <stdint.h>

void UpdateRWXFunction(void* rwxfunc, void* real);
uintptr_t CreateFunction(void* func, void* real);

template <typename ReturnType, typename ...Args>
inline ReturnType CallVFunc(int index, void* thisPtr, Args... args) {
	return (*reinterpret_cast<ReturnType(__fastcall***)(void*, Args...)>(thisPtr))[index](thisPtr, args...);
}

template<typename ReturnType>
inline ReturnType GetVFunc(void* thisPtr, uint32_t index) {
	return (*static_cast<ReturnType**>(thisPtr))[index];
}