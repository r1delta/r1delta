#include "filesystem.h"
#include <iostream>
#include <unordered_set>
#include <string>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <Windows.h>
#include <intrin.h>
#include "logging.h"
#include "load.h"

__int64 (*oWeaponXRegisterClient)(__int64 a1, const char** a2);
__int64 (*oWeaponXRegisterServer)(__int64 a1, const char** a2);

__int64 __fastcall WeaponXRegisterClient(__int64 a1, const char** a2) {
	if (a2 && *a2 && strlen(*a2) > 0)
		std::cout << "CLIENT: " << *a2 << std::endl;
	return oWeaponXRegisterClient(a1, a2);
}
__int64 __fastcall WeaponXRegisterServer(__int64 a1, const char** a2) {
	if (a2 && *a2 && strlen(*a2) > 0)
		std::cout << "SERVER: " << *a2 << std::endl;
	return oWeaponXRegisterServer(a1, a2);
}