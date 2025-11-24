#include "hudwarp.h"
#include "hudwarp_hooks.h"
#include "load.h"

// ID3DUserDefinedAnnotation
#include <d3d11_1.h>
#include "tctx.hh"

ID3D11Device* pDevice = 0;
ID3D11DeviceContext** ppID3D11DeviceContext = 0;
ID3DUserDefinedAnnotation* PIX = 0;

// Thread safety and error tracking
static DWORD gRenderThreadId = 0;
bool gGPUHudwarpFailed = false;

typedef __int64(__fastcall *BeginPixEvent_type)(__int64 queuedRenderContext, unsigned long color, const char* pszName);
BeginPixEvent_type BeginPixEvent = nullptr;

void* QueueXEvent_materials = 0;
HudwarpProcess* hudwarpProcess;
bool isRenderingHud = false;
bool shouldUseGPUHudwarp = true;
bool isHudwarpDisabled = false;
void SetupHudwarp()
{
	R1DAssert(!pDevice);
	R1DAssert(!ppID3D11DeviceContext);
	R1DAssert(!hudwarpProcess);

	pDevice = *(ID3D11Device**)(G_matsystem + 0x290D88);
	ppID3D11DeviceContext = (ID3D11DeviceContext**)(G_matsystem + 0x290D90);
	hudwarpProcess = new HudwarpProcess(pDevice, ppID3D11DeviceContext);

	R1DAssert(G_engine);
	QueueXEvent_materials = *(void**)(G_engine + 0x318A688);
	R1DAssert(QueueXEvent_materials);

	auto hr = (*ppID3D11DeviceContext)->QueryInterface(IID_PPV_ARGS(&PIX));
	R1DAssert(!FAILED(hr));
	if (FAILED(hr))
	{
		PIX = 0;
	}
}


void QueueBeginEvent(const char* markerName)
{
	auto queuedRenderContext = (*(__int64(__fastcall**)(void*))(*(uintptr_t*)QueueXEvent_materials + 896i64))(QueueXEvent_materials);

	BeginPixEvent(queuedRenderContext, 111111, markerName); // hijack color field for our purposes
}

void QueueEndEvent()
{
	auto queuedRenderContext = (*(__int64(__fastcall**)(void*))(*(uintptr_t*)QueueXEvent_materials + 896i64))(QueueXEvent_materials);

	BeginPixEvent(queuedRenderContext, 222222, ""); // hijack color field for our purposes
}


typedef void(__fastcall *RenderHud_type)(__int64 a1, __int64 a2, __int64 a3);
//HookedFuncStaticWithType<RenderHud_type> RenderHud("client.dll", 0x2AE630);
RenderHud_type RenderHud;
void __fastcall RenderHud_Hook(__int64 a1, __int64 a2, __int64 a3)
{
	// Update state once per frame to prevent possible issues with one or neither applying
	static ConVarR1* hudwarp_use_gpu = OriginalCCVar_FindVar(cvarinterface, "hudwarp_use_gpu");
	shouldUseGPUHudwarp = hudwarp_use_gpu->m_Value.m_nValue == 1;


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

	// Capture render thread ID for thread safety
	gRenderThreadId = GetCurrentThreadId();

	// Init GPU Hudwarp
	SetupHudwarp();

	return ret;
}


void HudRenderStart()
{
	if (!shouldUseGPUHudwarp || gGPUHudwarpFailed)
		return;

	if (hudwarpProcess)
		hudwarpProcess->Begin();
}

void HudRenderFinish()
{
	if (!shouldUseGPUHudwarp || gGPUHudwarpFailed)
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
		// FIX: Clamp to safe range to avoid massive allocations in materialsystem_dx11
		// that can cause crashes on low-memory systems
		unsigned int targetChopsize = (screenX > screenY) ? screenX : screenY;

		// Clamp to range [64, 512] to prevent excessive geometry/texture allocations
		if (targetChopsize < 64)
			targetChopsize = 64;
		else if (targetChopsize > 512)
			targetChopsize = 512;

		hudwarp_chopsize->m_Value.m_nValue = targetChopsize;
	}

	CMatSystemSurface__ApplyHudwarpSettings_org(thisptr, &newSettings, screenX, screenY);

	hudwarp_chopsize->m_Value.m_nValue = originalChopsize;
}

// TODO(mrsteyk): figure out better flags?
#if BUILD_DEBUG
static void BeginPixEvent_(const char* name)
{
	if (PIX)
	{
		TempArena temp(tctx.get_arena_for_scratch());

		const size_t len = name ? strlen(name) : 0;
		const wchar_t* nw = L"NULL";
		if (len)
		{
			wchar_t* buf = (wchar_t*)arena_push(temp.arena, (len + 1) * 2);
			for (size_t i = 0; i < len; i++)
			{
				buf[i] = name[i];
			}
			nw = buf;
		}

		PIX->BeginEvent(nw);
	}
}
static void BeginPixEvent_(const wchar_t* name)
{
	if (PIX)
	{
		PIX->BeginEvent(name);
	}
}

static void EndPixEvent_()
{
	if (PIX)
	{
		PIX->EndEvent();
	}
}

static void SetPixMarker_(const char* name)
{
	if (PIX)
	{
		TempArena temp(tctx.get_arena_for_scratch());

		const size_t len = name ? strlen(name) : 0;
		const wchar_t* nw = L"NULL";
		if (len)
		{
			wchar_t* buf = (wchar_t*)arena_push(temp.arena, (len + 1) * 2);
			for (size_t i = 0; i < len; i++)
			{
				buf[i] = name[i];
			}
			nw = buf;
		}

		PIX->SetMarker(nw);
	}
}

void EndPixEvent_Hook(void* queueRenderContext)
{
	EndPixEvent_();
}

void SetPixMarker_Hook(void* queueRenderContext, uint32_t color, const char* pszName)
{
	SetPixMarker_(pszName);
}
#else
#define BeginPixEvent_(...)
#define EndPixEvent_(...)
#define SetPixMarker_(...)
#endif

void __fastcall BeginPixEvent_Hook(void* queuedRenderContext, unsigned long color, const char* pszName) {
	// Thread safety gate: only process on render thread
	if (gRenderThreadId != 0 && GetCurrentThreadId() != gRenderThreadId)
		return;

	static unsigned int hudEventDepth = 0;

	switch (color)
	{
	case 111111:
		if (!strcmp_static(pszName, "HUD"))
		{
			isRenderingHud = true;
			BeginPixEvent_(L"HUD");
			HudRenderStart();
		}
		else
		{
			R1DAssert(0);
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
				EndPixEvent_();
			}
		}
		else
		{
			R1DAssert(0);
		}

		break;

	default: {
#if BUILD_DEBUG
		BeginPixEvent_(pszName);
#endif
	}break;
	}
}


typedef float* (__fastcall *sub_18000BE60_type)(float* a1, float* a2, float* a3, float* a4, float a5, float* a6, float* a7, DWORD* a8);
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

typedef void** (__fastcall *sub_1800154A0_type)(__int64 a1, int a2, int a3, int a4, int a5);
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
	R1DAssert(G_client);

	// Check if -usegpuhudwarp command line argument is present
	bool useGPUHudwarp = HasEngineCommandLineFlag("-usegpuhudwarp");

	if (useGPUHudwarp) {
		MH_CreateHook((void*)(G_client + 0x2AE630), &RenderHud_Hook, reinterpret_cast<LPVOID*>(&RenderHud));
		MH_EnableHook(MH_ALL_HOOKS);
	}
}
char (*osub_180001CC0)();
char sub_180001CC0() {
	auto ret = osub_180001CC0();
	int* ptr = (int*)(G_matsystem + 0x293218);
	if (*ptr == 0)
		*ptr = 1;
	return ret;
}
void SetupHudWarpMatSystemHooks() {
	// Check if -usegpuhudwarp command line argument is present
	bool useGPUHudwarp = HasEngineCommandLineFlag("-usegpuhudwarp");

	// Always apply sub_180001CC0 hook
	MH_CreateHook((void*)(G_matsystem + 0x1CC0), &sub_180001CC0, reinterpret_cast<LPVOID*>(&osub_180001CC0));

	// Only apply other hooks if -usegpuhudwarp is set
	if (useGPUHudwarp) {
		uintptr_t sub_63D0_addr = G_matsystem + 0x63D0;
		MH_CreateHook((void*)sub_63D0_addr, &sub_63D0, reinterpret_cast<LPVOID*>(&sub_63D0_org));

		// NOTE(mrsteyk): changed to queued hooks.
		R1DAssert(MH_CreateHook((void*)(G_matsystem + 0x626D0), &BeginPixEvent_Hook, 0) == MH_OK);
#if BUILD_DEBUG
		R1DAssert(MH_CreateHook((void*)(G_matsystem + 0x626E0), &EndPixEvent_Hook, 0) == MH_OK);
		R1DAssert(MH_CreateHook((void*)(G_matsystem + 0x626F0), &SetPixMarker_Hook, 0) == MH_OK);
#endif
		BeginPixEvent = (BeginPixEvent_type)(G_matsystem + 0x5D7E0);
	}

	MH_EnableHook(MH_ALL_HOOKS);
}

void SetupHudWarpVguiHooks() {
	// Check if -usegpuhudwarp command line argument is present
	bool useGPUHudwarp = HasEngineCommandLineFlag("-usegpuhudwarp");

	if (useGPUHudwarp) {
		uintptr_t vguimatsurfacedllBaseAddress = (uintptr_t)GetModuleHandleW(L"vguimatsurface.dll");
		MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0xBAC0), &sub_18000BAC0, reinterpret_cast<LPVOID*>(&sub_18000BAC0_org));
		MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0x15A30), &CMatSystemSurface__ApplyHudwarpSettings, reinterpret_cast<LPVOID*>(&CMatSystemSurface__ApplyHudwarpSettings_org));
		MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0xBE60), &sub_18000BE60, reinterpret_cast<LPVOID*>(&sub_18000BE60_org));
		MH_CreateHook((void*)(vguimatsurfacedllBaseAddress + 0x154A0), &sub_1800154A0, reinterpret_cast<LPVOID*>(&sub_1800154A0_org));

		MH_EnableHook(MH_ALL_HOOKS);
	}
}