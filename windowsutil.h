#pragma once

#include <string>

#include "framework.h"

std::string ConvertToWinPath(const std::string& svInput);
BOOL FileExists(LPCSTR szPath);