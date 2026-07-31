#pragma once

#include <string>

namespace r1delta::r1o
{
std::string ResolveTFOBinDirectoryA();
std::wstring ResolveTFOBinDirectoryW();
std::string ResolveTFOModulePathA(const char* moduleName);
std::wstring ResolveTFOModulePathW(const wchar_t* moduleName);
std::string TFORuntimeValidationErrorA();
std::wstring TFORuntimeValidationErrorW();
bool IsTFORuntimeModulePathA(const char* modulePath);
bool IsTFORuntimeModulePathW(const wchar_t* modulePath);
}
