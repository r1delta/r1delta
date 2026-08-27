#pragma once

namespace r1delta::hudwarp
{
inline constexpr char kUseGpuConVarName[] = "hudwarp_use_gpu";
inline constexpr char kDisableConVarName[] = "hudwarp_disable";

template <typename ConVar>
struct RuntimeConVars
{
	ConVar* useGpu;
	ConVar* disable;
};

template <typename ConVar, typename Registrar>
RuntimeConVars<ConVar> RegisterRuntimeConVars(Registrar registrar, int archiveFlag)
{
	return {
		registrar(kUseGpuConVarName, "1", archiveFlag, "Use GPU processing for HUD warp"),
		registrar(kDisableConVarName, "0", archiveFlag, "Disable HUD warp"),
	};
}
}
