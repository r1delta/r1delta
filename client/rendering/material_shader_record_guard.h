#pragma once

#include <cstdint>

// Exact R1 Client2015 materialsystem_dx11 only (timestamp 0x54874238, image size 0x3E0000). Keeps a CMaterial's 24-byte shader
// record (CMaterial+104) from carrying an inline texture handle of one shader class into a shader class that reads the same
// qword as a heap pointer. See engine/core/materialsystem_dx11_shader_record.h.
void InstallMaterialShaderRecordGuard(std::uintptr_t materialSystemBase);
