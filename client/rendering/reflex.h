#pragma once

#include <cstdint>

struct ID3D11Device;

void SetupReflexEngineHooks(std::uintptr_t engineBase);
void SetupReflexMaterialSystemHooks(std::uintptr_t materialSystemBase);
void RegisterReflexConVars();

void ReflexOnDeviceReady(ID3D11Device* device);
void ReflexBeginSimulation();
void ReflexEndSimulationAndBeginRenderSubmit();
void ReflexOnEngineFrameComplete();
