#include "hudwarp.h"
#include "hudwarp_hooks.h"
ID3D11Device* pDevice;
ID3D11DeviceContext** ppID3D11DeviceContext;

typedef __int64(*__fastcall SetPixMarker_type)(__int64 queuedRenderContext, unsigned long color, const char* pszName);
SetPixMarker_type SetPixMarker = nullptr;


HudwarpProcess* hudwarpProcess;
bool isRenderingHud = false;
bool shouldUseGPUHudwarp = true;
bool isHudwarpDisabled = false;
void SetupHudwarp()
{
	DWORD64 materialSystemBase = (DWORD64)GetModuleHandle(L"materialsystem_dx11.dll");
	pDevice = *(ID3D11Device**)(materialSystemBase + 0x290D88);
	ppID3D11DeviceContext = (ID3D11DeviceContext**)(materialSystemBase + 0x290D90);
	hudwarpProcess = new HudwarpProcess(pDevice, ppID3D11DeviceContext);
}



void QueueBeginEvent(const char* markerName)
{
	static DWORD64 enginedllBaseAddress = (DWORD64)(GetModuleHandle(L"engine.dll"));
	static auto materials = *(__int64*)(enginedllBaseAddress + 0x318A688);
	auto queuedRenderContext = (*(__int64(__fastcall**)(__int64))(*(DWORD64*)materials + 896i64))(materials);

	SetPixMarker(queuedRenderContext, 111111, markerName); // hijack color field for our purposes
}

void QueueEndEvent()
{
	static DWORD64 enginedllBaseAddress = (DWORD64)(GetModuleHandle(L"engine.dll"));
	static auto materials = *(__int64*)(enginedllBaseAddress + 0x318A688);
	auto queuedRenderContext = (*(__int64(__fastcall**)(__int64))(*(DWORD64*)materials + 896i64))(materials);

	SetPixMarker(queuedRenderContext, 222222, ""); // hijack color field for our purposes
}


typedef void(*__fastcall RenderHud_type)(__int64 a1, __int64 a2, __int64 a3);
//HookedFuncStaticWithType<RenderHud_type> RenderHud("client.dll", 0x2AE630);
RenderHud_type RenderHud;
void __fastcall RenderHud_Hook(__int64 a1, __int64 a2, __int64 a3)
{
	// Update state once per frame to prevent possible issues with one or neither applying
	//static ConVarR1 hudwarp_use_gpu{ "hudwarp_use_gpu" };
	static ConVarR1* hudwarp_use_gpu = OriginalCCVar_FindVar(cvarinterface, "hudwarp_use_gpu");
	shouldUseGPUHudwarp = hudwarp_use_gpu->m_Value.m_nValue == 1;

	//static ConVarR1 hudwarp_disable{ "hudwarp_disable" };
//	isHudwarpDisabled = hudwarp_disable.m_Value.m_nValue;
	static ConVarR1* hudwarp_disable = OriginalCCVar_FindVar(cvarinterface, "hudwarp_disable");
	isHudwarpDisabled = hudwarp_disable->m_Value.m_nValue == 1;
	QueueBeginEvent("HUD");
	RenderHud(a1, a2, a3);
	QueueEndEvent();
}

bool shouldDoublePumpHudwarpVerts = false;
int hudwarpVertsProcessed = 0;

typedef bool(__fastcall* sub_63D0_type)(HWND, unsigned int, __int64);
sub_63D0_type sub_63D0_org;
char __fastcall sub_63D0(HWND a1, unsigned int a2, __int64 a3)
{
	// Initialization of DirectX swapchain and related device/context setup
	auto ret = sub_63D0_org(a1, a2, a3);

	// Init GPU Hudwarp
	SetupHudwarp();

	return ret;
}


void HudRenderStart()
{
	if (!shouldUseGPUHudwarp)
		return;

	if (hudwarpProcess)
		hudwarpProcess->Begin();
}

void HudRenderFinish()
{
	if (!shouldUseGPUHudwarp)
		return;

	if (hudwarpProcess)
		hudwarpProcess->Finish();
}



typedef void(__fastcall* sub_18000BAC0_type)(float*, float*, float*);
sub_18000BAC0_type sub_18000BAC0_org = nullptr;
void __fastcall sub_18000BAC0(float* a1, float* a2, float* a3)
{
	// Ported from TFORevive by Barnaby

	if (!shouldUseGPUHudwarp)
	{
		// If hudwarp is disabled and running on CPU do just the scaling
		if (isHudwarpDisabled)
		{
			// Still perform scaling for HUD when warping is disabled
			float viewWidth = a1[2];
			float viewHeight = a1[3];

			float xScale = a1[7];
			float yScale = a1[9];

			a3[0] = (a2[0] - 0.5f * viewWidth) * xScale + 0.5f * viewWidth;
			a3[1] = (a2[1] - 0.5f * viewHeight) * yScale + 0.5f * viewHeight;
			a3[2] = a2[2];
			return;
		}

		return sub_18000BAC0_org(a1, a2, a3);
	}

	// We prevent the hud from reaching bounds of render texture by adding a border of 0.025 * width/height to the texture
	// We need to offset the verts to account for that here
	const float hudOffset = HUD_TEX_BORDER_SIZE;
	const float hudScale = 1.0f - 2.0f * hudOffset;

	float viewWidth = a1[2];
	float viewHeight = a1[3];

	a3[0] = a2[0] * hudScale + hudOffset * viewWidth;
	a3[1] = a2[1] * hudScale + hudOffset * viewHeight;
	a3[2] = a2[2];
}

typedef void(__fastcall* CMatSystemSurface__ApplyHudwarpSettings_type)(void*, HudwarpSettings*, unsigned int, unsigned int);
CMatSystemSurface__ApplyHudwarpSettings_type CMatSystemSurface__ApplyHudwarpSettings_org = nullptr;
void __fastcall CMatSystemSurface__ApplyHudwarpSettings(void* thisptr, HudwarpSettings* hudwarpSettings, unsigned int screenX, unsigned int screenY)
{
	// Ported from TFORevive by Barnaby
	static ConVarR1* hudwarp_chopsize = OriginalCCVar_FindVar(cvarinterface, "hudwarp_chopsize");
	unsigned int originalChopsize = hudwarp_chopsize->m_Value.m_nValue;

	static ConVarR1* hudwarp_disable = OriginalCCVar_FindVar(cvarinterface, "hudwarp_disable");
	HudwarpSettings newSettings = *hudwarpSettings;
	if (hudwarp_disable->m_Value.m_nValue == 1)
	{
		// Override hudwarp settings if hudwarp_disable is 1.
		// NOTE: Comment below refers to original CPU version, we can set them to 0 when using our shader.
		// Stuff breaks if you set the warp values to 0.
		// Respawn set them to a min of 1deg in rads (0.017453292), we can do that too because it'll result in so little distortion you won't notice it :)
		newSettings.xWarp = 0.0f;
		if (newSettings.xScale > 1.0f) newSettings.xScale = 1.0f;
		newSettings.yWarp = 0.0f;
		if (newSettings.yScale > 1.0f) newSettings.yScale = 1.0f;
	}

	if (hudwarpProcess)
		hudwarpProcess->UpdateSettings(&newSettings);

	static ConVarR1* hudwarp_use_gpu = OriginalCCVar_FindVar(cvarinterface, "hudwarp_use_gpu");
	// If using GPU hudwarp or hudwarp is disabled do this
	// Replace chopsize, it gets set from the cvar in CMatSystemSurface__ApplyHudwarpSettings
	if (hudwarp_use_gpu->m_Value.m_nValue || hudwarp_disable->m_Value.m_nValue == 1)
	{
		if (screenX > screenY) [[likely]]
			{
				hudwarp_chopsize->m_Value.m_nValue = screenX;
			}
		else
		{
			hudwarp_chopsize->m_Value.m_nValue = screenY;
		}
	}

	CMatSystemSurface__ApplyHudwarpSettings_org(thisptr, &newSettings, screenX, screenY);

	hudwarp_chopsize->m_Value.m_nValue = originalChopsize;
}
typedef __int64(__fastcall* sub_5ADC0_type)(__int64 queuedRenderContext, unsigned long color, const char* pszName);
sub_5ADC0_type sub_5ADC0 = nullptr;
__int64 __fastcall sub_5ADC0_Hook(__int64 queuedRenderContext, unsigned long color, const char* pszName) {
	static unsigned int hudEventDepth = 0;

	switch (color)
	{
	case 111111:
		if (!strcmp(pszName, "HUD"))
		{
			isRenderingHud = true;
			HudRenderStart();
		}

		if (isRenderingHud)
			hudEventDepth++;
		break;
	case 222222:
		if (isRenderingHud)
		{
			hudEventDepth--;
			if (hudEventDepth == 0)
			{
				isRenderingHud = false;
				HudRenderFinish();
			}
		}

		break;
	}

	return sub_5ADC0(queuedRenderContext, color, pszName);
}




typedef float* (*__fastcall sub_18000BE60_type)(float* a1, float* a2, float* a3, float* a4, float a5, float* a6, float* a7, DWORD* a8);
//HookedFuncStaticWithType<sub_18000BE60_type> sub_18000BE60_org("vguimatsurface.dll", 0xBE60);
sub_18000BE60_type sub_18000BE60_org;
float* __fastcall sub_18000BE60(float* a1, float* a2, float* a3, float* a4, float a5, float* a6, float* a7, DWORD* a8)
{
	shouldDoublePumpHudwarpVerts = shouldUseGPUHudwarp || isHudwarpDisabled;
	hudwarpVertsProcessed = 0;

	float* ret = sub_18000BE60_org(a1, a2, a3, a4, a5, a6, a7, a8);

	shouldDoublePumpHudwarpVerts = false;

	return ret;
}

typedef void** (*__fastcall sub_1800154A0_type)(__int64 a1, int a2, int a3, int a4, int a5);
//HookedFuncStaticWithType<sub_1800154A0_type> sub_1800154A0_org("vguimatsurface.dll", 0x154A0);
sub_1800154A0_type sub_1800154A0_org;
void** __fastcall sub_1800154A0(__int64 a1, int a2, int a3, int a4, int a5)
{
	shouldDoublePumpHudwarpVerts = shouldUseGPUHudwarp || isHudwarpDisabled;
	hudwarpVertsProcessed = 0;

	void** ret = sub_1800154A0_org(a1, a2, a3, a4, a5);

	shouldDoublePumpHudwarpVerts = false;

	return ret;
}

typedef void(__fastcall* OnWindowSizeChanged_type)(unsigned int, unsigned int, bool);
OnWindowSizeChanged_type OnWindowSizeChanged_org = nullptr;
void __fastcall OnWindowSizeChanged(unsigned int w, unsigned int h, bool isInGame)
{
	OnWindowSizeChanged_org(w, h, isInGame);

	if (hudwarpProcess)
		hudwarpProcess->Resize(w, h);
}


void SetupHudWarpHooks() {
	DWORD64 clientBaseAddress = (DWORD64)GetModuleHandle(L"client.dll");
	MH_CreateHook((void*)(clientBaseAddress + 0x2AE630), &RenderHud_Hook, reinterpret_cast<LPVOID*>(&RenderHud));
	MH_EnableHook(MH_ALL_HOOKS);
}

void SetupHudWarpMatSystemHooks() {
	DWORD64 materialSystemBase = (DWORD64)GetModuleHandle(L"materialsystem_dx11.dll");
	DWORD64 sub_63D0_addr = materialSystemBase + 0x63D0;
	MH_CreateHook((void*)sub_63D0_addr, &sub_63D0, reinterpret_cast<LPVOID*>(&sub_63D0_org));
	MH_CreateHook((void*)(materialSystemBase + 0x5ADC0), &sub_5ADC0_Hook, reinterpret_cast<LPVOID*>(&sub_5ADC0));
	SetPixMarker = (SetPixMarker_type)(materialSystemBase + 0x5D7E0);
}

void SetupHudWarpVguiHooks() {
	DWORD64 vguimatsurfacedllBaseAddress = (DWORD64)GetModuleHandle(L"vguimatsurface.dll");
	MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0xBAC0), &sub_18000BAC0, reinterpret_cast<LPVOID*>(&sub_18000BAC0_org));
	MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0x15A30), &CMatSystemSurface__ApplyHudwarpSettings, reinterpret_cast<LPVOID*>(&CMatSystemSurface__ApplyHudwarpSettings_org));
	MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0xBE60), &sub_18000BE60, reinterpret_cast<LPVOID*>(&sub_18000BE60_org));
	MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0x154A0), &sub_1800154A0, reinterpret_cast<LPVOID*>(&sub_1800154A0_org));
}