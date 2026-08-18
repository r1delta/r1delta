#include "../engine/core/materialsystem_dx11_material_guard.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
using namespace r1delta::materialsystem_dx11;

static_assert(sizeof(kExpectedMaterialSetupErrorShaderPrologue) == 29);
static_assert(sizeof(kExpectedMaterialInitializePrologue) == 28);
static_assert(sizeof(kExpectedMaterialProperty0Prologue) == 33);
static_assert(sizeof(kExpectedMaterialIsErrorPrologue) == 33);
static_assert(sizeof(kExpectedMaterialProperty1Prologue) == 33);
static_assert(sizeof(kExpectedMaterialProperty2Prologue) == 33);
static_assert(sizeof(kExpectedMaterialProperty3Prologue) == 33);
static_assert(sizeof(kExpectedMaterialAlphaModulationPrologue) == 33);
static_assert(sizeof(kExpectedMaterialReflectivityPrologue) == 40);

int g_initializeCalls;
bool g_sawInitializedBit;
int g_setupErrorCalls;
bool g_setupSawInitializationDepth;
int g_alphaCalls;
int g_reflectivityCalls;

enum class InitializeMode
{
	publishComplete,
	publishCompleteWithZeroResult,
	failWithoutPublishedBit,
	failWithPublishedBit,
	returnSuccessWithoutShader,
	returnSuccessWithPrimaryOnly
};
InitializeMode g_initializeMode;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

struct MaterialStorage
{
	alignas(std::uintptr_t) std::array<std::uint8_t,
		kMaterialProxyOwnerOffset + sizeof(std::uintptr_t)> bytes{};

	std::uintptr_t Address() noexcept
	{
		return reinterpret_cast<std::uintptr_t>(bytes.data());
	}

	std::uintptr_t& Shader() noexcept
	{
		return *reinterpret_cast<std::uintptr_t*>(
			bytes.data() + kMaterialShaderOffset);
	}

	std::uint16_t& Flags() noexcept
	{
		return *reinterpret_cast<std::uint16_t*>(
			bytes.data() + kMaterialFlagsOffset);
	}

	std::uint32_t& ProxyCount() noexcept
	{
		return *reinterpret_cast<std::uint32_t*>(
			bytes.data() + kMaterialProxyCountOffset);
	}

	std::uintptr_t& RenderStateOwner() noexcept
	{
		return *reinterpret_cast<std::uintptr_t*>(
			bytes.data() + kMaterialRenderStateOwnerOffset);
	}

	std::uintptr_t& PrimaryContext() noexcept
	{
		return *reinterpret_cast<std::uintptr_t*>(
			bytes.data() + kMaterialPrimaryContextOffset);
	}

	std::uintptr_t& SecondaryContext() noexcept
	{
		return *reinterpret_cast<std::uintptr_t*>(
			bytes.data() + kMaterialSecondaryContextOffset);
	}

	std::uintptr_t& ProxyOwner() noexcept
	{
		return *reinterpret_cast<std::uintptr_t*>(
			bytes.data() + kMaterialProxyOwnerOffset);
	}

	void PublishComplete() noexcept
	{
		Flags() |= kMaterialInitializedFlag;
		Shader() = 0x30000;
		PrimaryContext() = 0x10000;
		SecondaryContext() = 0x20000;
	}
};

static_assert(offsetof(MaterialStorage, bytes) == 0);

MaterialStorage* Storage(std::int64_t material) noexcept
{
	return reinterpret_cast<MaterialStorage*>(
		static_cast<std::uintptr_t>(material));
}

std::int64_t __fastcall Initialize(
	std::int64_t material,
	std::int64_t,
	std::int64_t,
	std::int64_t)
{
	MaterialInitializationScope initializationScope;
	++g_initializeCalls;
	MaterialStorage* const storage = Storage(material);
	g_sawInitializedBit =
		(storage->Flags() & kMaterialInitializedFlag) != 0;

	switch (g_initializeMode) {
	case InitializeMode::publishComplete:
		storage->PublishComplete();
		return 1;
	case InitializeMode::publishCompleteWithZeroResult:
		storage->PublishComplete();
		return 0;
	case InitializeMode::failWithoutPublishedBit:
		return 0;
	case InitializeMode::failWithPublishedBit:
		storage->Flags() |= kMaterialInitializedFlag;
		return 0;
	case InitializeMode::returnSuccessWithoutShader:
		storage->Flags() |= kMaterialInitializedFlag;
		storage->PrimaryContext() = 0x10000;
		storage->SecondaryContext() = 0x20000;
		return 1;
	case InitializeMode::returnSuccessWithPrimaryOnly:
		storage->Flags() |= kMaterialInitializedFlag;
		storage->Shader() = 0x30000;
		storage->PrimaryContext() = 0x10000;
		return 1;
	}
	return 0;
}

void __fastcall SetupErrorShader(std::int64_t material)
{
	g_setupSawInitializationDepth =
		IsMaterialInitializationInProgress();
	++g_setupErrorCalls;
	Storage(material)->PublishComplete();
}

void __fastcall SetupIncompleteErrorShader(std::int64_t material)
{
	g_setupSawInitializationDepth =
		IsMaterialInitializationInProgress();
	++g_setupErrorCalls;
	Storage(material)->Shader() = 0x30000;
}

float __fastcall AlphaModulation(std::int64_t)
{
	++g_alphaCalls;
	return 0.625f;
}

void __fastcall Reflectivity(std::int64_t, float* output)
{
	++g_reflectivityCalls;
	output[0] = 0.25f;
	output[1] = 0.5f;
	output[2] = 0.75f;
}

void Reset(InitializeMode mode = InitializeMode::publishComplete)
{
	g_initializeCalls = 0;
	g_sawInitializedBit = false;
	g_setupSawInitializationDepth = false;
	g_setupErrorCalls = 0;
	g_alphaCalls = 0;
	g_reflectivityCalls = 0;
	g_initializeMode = mode;
}

bool TestCompleteStateSkipsInitializationAndInvokesAccessors()
{
	Reset();
	MaterialStorage material;
	material.PublishComplete();
	float alpha = -1.0f;
	float reflectivity[3] = { -1.0f, -1.0f, -1.0f };

	bool passed = Check(EnsureMaterialContext(material.Address(), nullptr),
		"complete state required an initializer");
	passed &= Check(InvokeMaterialAlphaModulation(
			material.Address(),
			&Initialize,
			&AlphaModulation,
			&alpha),
		"complete alpha access was rejected");
	passed &= Check(InvokeMaterialReflectivity(
			material.Address(),
			&Initialize,
			&Reflectivity,
			reflectivity),
		"complete reflectivity access was rejected");
	passed &= Check(g_initializeCalls == 0,
		"complete state was initialized again");
	passed &= Check(g_alphaCalls == 1 && g_reflectivityCalls == 1,
		"complete state did not call each accessor once");
	passed &= Check(alpha == 0.625f,
		"alpha result changed");
	passed &= Check(reflectivity[0] == 0.25f
			&& reflectivity[1] == 0.5f
			&& reflectivity[2] == 0.75f,
		"reflectivity result changed");
	return passed;
}

bool TestPristineInitializationRetriesOnce()
{
	Reset();
	MaterialStorage material;

	return Check(EnsureMaterialContext(material.Address(), &Initialize),
			"pristine material did not initialize")
		&& Check(g_initializeCalls == 1,
			"pristine material did not initialize exactly once")
		&& Check(!g_sawInitializedBit,
			"pristine material was marked before initialization")
		&& Check(ReadMaterialContextState(material.Address()).IsUsable(),
			"initializer did not publish a complete state");
}

bool TestZeroResultWithCompleteStateIsAccepted()
{
	Reset(InitializeMode::publishCompleteWithZeroResult);
	MaterialStorage material;

	return Check(EnsureMaterialContext(material.Address(), &Initialize),
			"complete state with zero initializer result was rejected")
		&& Check(g_initializeCalls == 1,
			"zero-result initializer was not bounded to one call");
}

bool TestShaderIsRequiredForUsableState()
{
	Reset(InitializeMode::returnSuccessWithoutShader);
	MaterialStorage material;

	return Check(!EnsureMaterialContext(material.Address(), &Initialize),
			"contexts without a shader were accepted")
		&& Check(g_initializeCalls == 1,
			"incomplete initializer was retried")
		&& Check(!ReadMaterialContextState(material.Address()).IsUsable(),
			"contexts without a shader reported usable");
}

bool TestFailedInitializationPreservesPublishedBit()
{
	Reset(InitializeMode::failWithPublishedBit);
	MaterialStorage material;

	return Check(!EnsureMaterialContext(material.Address(), &Initialize),
			"failed initialization reported usable")
		&& Check(g_initializeCalls == 1,
			"failed initializer was retried")
		&& Check((material.Flags() & kMaterialInitializedFlag) != 0,
			"failed initialization mutated the published bit");
}

bool TestIncompleteSuccessFailsClosed()
{
	Reset(InitializeMode::returnSuccessWithPrimaryOnly);
	MaterialStorage material;

	return Check(!EnsureMaterialContext(material.Address(), &Initialize),
			"one material context was accepted")
		&& Check(g_initializeCalls == 1,
			"incomplete success was retried")
		&& Check((material.Flags() & kMaterialInitializedFlag) != 0,
			"incomplete success mutated the published bit");
}

bool OwnerBlocksInitialization(MaterialStorage& material, const char* message)
{
	Reset();
	const bool result = EnsureMaterialContext(
		material.Address(),
		&Initialize,
		&SetupErrorShader);
	return Check(!result, message)
		&& Check(g_initializeCalls == 0,
			"partial owner state called Initialize")
		&& Check(g_setupErrorCalls == 0,
			"partial owner state called SetupErrorShader")
		&& Check((material.Flags() & kMaterialInitializedFlag) == 0,
			"partial owner state published the initialized bit");
}

bool TestEveryPartialOwnerBlocksInitialization()
{
	bool passed = true;
	{
		MaterialStorage material;
		material.Shader() = 1;
		passed &= OwnerBlocksInitialization(material,
			"shader-only state retried initialization");
	}
	{
		MaterialStorage material;
		material.ProxyCount() = 1;
		passed &= OwnerBlocksInitialization(material,
			"proxy-count state retried initialization");
	}
	{
		MaterialStorage material;
		material.RenderStateOwner() = 1;
		passed &= OwnerBlocksInitialization(material,
			"render-state owner retried initialization");
	}
	{
		MaterialStorage material;
		material.PrimaryContext() = 1;
		passed &= OwnerBlocksInitialization(material,
			"primary-context state retried initialization");
	}
	{
		MaterialStorage material;
		material.SecondaryContext() = 1;
		passed &= OwnerBlocksInitialization(material,
			"secondary-context state retried initialization");
	}
	{
		MaterialStorage material;
		material.ProxyOwner() = 1;
		passed &= OwnerBlocksInitialization(material,
			"proxy-owner state retried initialization");
	}
	return passed;
}

bool TestPristinePublishedFailureBuildsErrorState()
{
	Reset();
	MaterialStorage material;
	material.Flags() = kMaterialInitializedFlag;

	return Check(FinalizeMaterialInitialization(
			material.Address(),
			&SetupErrorShader),
			"pristine published failure did not build an error state")
		&& Check(g_setupErrorCalls == 1,
			"error state was not built exactly once")
		&& Check(g_setupSawInitializationDepth,
			"error builder ran without initialization depth")
		&& Check(ReadMaterialContextState(material.Address()).IsUsable(),
			"error builder did not publish a complete state");
}

bool TestPublishedPristineFailureRecoversAtAccessor()
{
	Reset();
	MaterialStorage material;
	material.Flags() = kMaterialInitializedFlag;
	float alpha = -1.0f;

	return Check(InvokeMaterialAlphaModulation(
			material.Address(),
			&Initialize,
			&AlphaModulation,
			&alpha,
			&SetupErrorShader),
			"published pristine failure did not recover")
		&& Check(g_initializeCalls == 0,
			"published pristine failure retried Initialize")
		&& Check(g_setupErrorCalls == 1,
			"published pristine failure did not build one error state")
		&& Check(g_setupSawInitializationDepth,
			"published recovery ran without initialization depth")
		&& Check(g_alphaCalls == 1 && alpha == 0.625f,
			"published recovery did not invoke the real alpha getter");
}

bool TestUnpublishedPristineFailureRecoversAtAccessor()
{
	Reset(InitializeMode::failWithoutPublishedBit);
	MaterialStorage material;
	float reflectivity[3] = { -1.0f, -1.0f, -1.0f };

	return Check(InvokeMaterialReflectivity(
			material.Address(),
			&Initialize,
			&Reflectivity,
			reflectivity,
			&SetupErrorShader),
			"unpublished pristine failure did not recover")
		&& Check(g_initializeCalls == 1,
			"unpublished pristine failure did not initialize exactly once")
		&& Check(!g_sawInitializedBit,
			"unpublished pristine failure was marked before Initialize")
		&& Check(g_setupErrorCalls == 1,
			"unpublished pristine failure did not build one error state")
		&& Check(g_setupSawInitializationDepth,
			"unpublished recovery ran without initialization depth")
		&& Check((material.Flags() & kMaterialInitializedFlag) != 0,
			"accessor recovery did not publish the initialized bit")
		&& Check(g_reflectivityCalls == 1
			&& reflectivity[0] == 0.25f
			&& reflectivity[2] == 0.75f,
			"unpublished recovery did not invoke real reflectivity");
}

bool OwnerBlocksErrorBuilder(MaterialStorage& material, const char* message)
{
	Reset();
	material.Flags() |= kMaterialInitializedFlag;
	const bool result = FinalizeMaterialInitialization(
		material.Address(),
		&SetupErrorShader);
	return Check(!result, message)
		&& Check(g_setupErrorCalls == 0,
			"partial owner state called SetupErrorShader");
}

bool TestEveryPartialOwnerBlocksErrorBuilder()
{
	Reset();
	bool passed = true;
	{
		MaterialStorage material;
		passed &= Check(!FinalizeMaterialInitialization(
				material.Address(),
				&SetupErrorShader),
			"unpublished pristine state called SetupErrorShader");
		passed &= Check(g_setupErrorCalls == 0,
			"unpublished state reached SetupErrorShader");
	}
	{
		MaterialStorage material;
		material.Shader() = 1;
		passed &= OwnerBlocksErrorBuilder(material,
			"shader-only state built an error shader");
	}
	{
		MaterialStorage material;
		material.ProxyCount() = 1;
		passed &= OwnerBlocksErrorBuilder(material,
			"proxy-count state built an error shader");
	}
	{
		MaterialStorage material;
		material.RenderStateOwner() = 1;
		passed &= OwnerBlocksErrorBuilder(material,
			"render-state owner built an error shader");
	}
	{
		MaterialStorage material;
		material.PrimaryContext() = 1;
		passed &= OwnerBlocksErrorBuilder(material,
			"primary-context state built an error shader");
	}
	{
		MaterialStorage material;
		material.SecondaryContext() = 1;
		passed &= OwnerBlocksErrorBuilder(material,
			"secondary-context state built an error shader");
	}
	{
		MaterialStorage material;
		material.ProxyOwner() = 1;
		passed &= OwnerBlocksErrorBuilder(material,
			"proxy-owner state built an error shader");
	}
	return passed;
}

bool TestIncompleteErrorStateFailsClosed()
{
	Reset();
	MaterialStorage material;
	material.Flags() = kMaterialInitializedFlag;

	return Check(!FinalizeMaterialInitialization(
			material.Address(),
			&SetupIncompleteErrorShader),
			"incomplete error state was accepted")
		&& Check(g_setupErrorCalls == 1,
			"error builder call count changed")
		&& Check(g_setupSawInitializationDepth,
			"incomplete error builder ran without initialization depth")
		&& Check(!ReadMaterialContextState(material.Address()).IsUsable(),
			"incomplete error state reported usable");
}

bool TestInitializationDepthBlocksRecovery()
{
	Reset();
	MaterialStorage uninitialized;
	bool initializeResult = true;
	{
		MaterialInitializationScope scope;
		initializeResult = EnsureMaterialContext(
			uninitialized.Address(),
			&Initialize,
			&SetupErrorShader);
	}

	MaterialStorage failed;
	failed.Flags() = kMaterialInitializedFlag;
	bool errorResult = true;
	{
		MaterialInitializationScope scope;
		errorResult = FinalizeMaterialInitialization(
			failed.Address(),
			&SetupErrorShader);
	}

	return Check(!initializeResult,
			"same-thread initialization recursed")
		&& Check(g_initializeCalls == 0,
			"same-thread initialization called Initialize")
		&& Check(!errorResult,
			"same-thread initialization built an error shader")
		&& Check(g_setupErrorCalls == 0,
			"same-thread initialization called SetupErrorShader");
}

int g_reentrantInitializeCalls;
int g_reentrantGetterCalls;
bool g_reentrantAccessorResult;

void __fastcall ReentrantReflectivity(std::int64_t, float*)
{
	++g_reentrantGetterCalls;
}

std::int64_t __fastcall ReentrantInitialize(
	std::int64_t material,
	std::int64_t,
	std::int64_t,
	std::int64_t)
{
	MaterialInitializationScope initializationScope;
	++g_reentrantInitializeCalls;
	Storage(material)->Flags() |= kMaterialInitializedFlag;
	float output[3] = { 4.0f, 5.0f, 6.0f };
	g_reentrantAccessorResult = InvokeMaterialReflectivity(
		static_cast<std::uintptr_t>(material),
		&ReentrantInitialize,
		&ReentrantReflectivity,
		output,
		&SetupErrorShader);
	return 0;
}

bool TestSameThreadReentryIsBounded()
{
	Reset();
	g_reentrantInitializeCalls = 0;
	g_reentrantGetterCalls = 0;
	g_reentrantAccessorResult = true;
	MaterialStorage material;

	return Check(EnsureMaterialContext(
			material.Address(),
			&ReentrantInitialize,
			&SetupErrorShader),
			"outer accessor did not recover after blocked reentry")
		&& Check(g_reentrantInitializeCalls == 1,
			"reentrant initialization was not bounded")
		&& Check(!g_reentrantAccessorResult,
			"accessor ran during partial initialization")
		&& Check(g_reentrantGetterCalls == 0,
			"original reentrant accessor ran during partial initialization")
		&& Check(g_setupErrorCalls == 1,
			"outer accessor did not build exactly one error state")
		&& Check(g_setupSawInitializationDepth,
			"outer error recovery ran without initialization depth")
		&& Check(ReadMaterialContextState(material.Address()).IsUsable(),
			"outer recovery did not publish a usable state");
}

bool TestFailedAccessorsDoNotFabricateValues()
{
	Reset();
	MaterialStorage material;
	material.Flags() = kMaterialInitializedFlag;
	material.Shader() = 1;
	float alpha = 7.0f;
	float reflectivity[3] = { 4.0f, 5.0f, 6.0f };

	bool passed = Check(!InvokeMaterialAlphaModulation(
			material.Address(),
			&Initialize,
			&AlphaModulation,
			&alpha,
			&SetupErrorShader),
		"invalid alpha state reached the original");
	passed &= Check(!InvokeMaterialReflectivity(
			material.Address(),
			&Initialize,
			&Reflectivity,
			reflectivity,
			&SetupErrorShader),
		"invalid reflectivity state reached the original");
	passed &= Check(g_alphaCalls == 0 && g_reflectivityCalls == 0,
		"failed access called an original getter");
	passed &= Check(g_setupErrorCalls == 0,
		"partial accessor state called SetupErrorShader");
	passed &= Check(alpha == 7.0f,
		"failed alpha access fabricated a value");
	passed &= Check(reflectivity[0] == 4.0f
			&& reflectivity[1] == 5.0f
			&& reflectivity[2] == 6.0f,
		"failed reflectivity access fabricated values");
	return passed;
}

std::mutex g_raceMutex;
std::condition_variable g_racePublished;
std::condition_variable g_raceContinue;
std::condition_variable g_raceAttempted;
bool g_racePublishedBit;
bool g_raceMayFinish;
std::atomic<int> g_raceInitializeCalls;
std::atomic<int> g_raceGetterCalls;
bool g_raceSecondAttempted;
bool g_raceSecondBlocked;

std::int64_t __fastcall RaceInitialize(
	std::int64_t material,
	std::int64_t,
	std::int64_t,
	std::int64_t)
{
	MaterialInitializationScope initializationScope;
	++g_raceInitializeCalls;
	MaterialStorage* const storage = Storage(material);
	storage->Flags() |= kMaterialInitializedFlag;
	{
		std::unique_lock<std::mutex> lock(g_raceMutex);
		g_racePublishedBit = true;
		g_racePublished.notify_one();
		g_raceContinue.wait(lock, []() { return g_raceMayFinish; });
	}
	storage->Shader() = 0x30000;
	storage->PrimaryContext() = 0x10000;
	storage->SecondaryContext() = 0x20000;
	return 1;
}

void __fastcall RaceReflectivity(std::int64_t, float* output)
{
	++g_raceGetterCalls;
	output[0] = 0.125f;
	output[1] = 0.25f;
	output[2] = 0.5f;
}

bool TestTwoThreadEarlyPublicationIsSerialized()
{
	MaterialStorage material;
	g_racePublishedBit = false;
	g_raceMayFinish = false;
	g_raceInitializeCalls = 0;
	g_raceGetterCalls = 0;
	g_raceSecondAttempted = false;
	g_raceSecondBlocked = false;
	bool firstResult = false;
	bool secondResult = false;
	float firstOutput[3] = {};
	float secondOutput[3] = {};

	std::thread first([&]() {
		firstResult = InvokeMaterialReflectivity(
			material.Address(),
			&RaceInitialize,
			&RaceReflectivity,
			firstOutput);
	});
	{
		std::unique_lock<std::mutex> lock(g_raceMutex);
		g_racePublished.wait(lock, []() { return g_racePublishedBit; });
	}
	std::thread second([&]() {
		const bool acquiredMaterialMutex =
			MaterialInitializationMutex().try_lock();
		if (acquiredMaterialMutex)
			MaterialInitializationMutex().unlock();
		{
			std::lock_guard<std::mutex> lock(g_raceMutex);
			g_raceSecondAttempted = true;
			g_raceSecondBlocked = !acquiredMaterialMutex;
		}
		g_raceAttempted.notify_one();
		secondResult = InvokeMaterialReflectivity(
			material.Address(),
			&RaceInitialize,
			&RaceReflectivity,
			secondOutput);
	});
	bool secondWasBlocked = false;
	{
		std::unique_lock<std::mutex> lock(g_raceMutex);
		g_raceAttempted.wait(lock, []() {
			return g_raceSecondAttempted;
		});
		secondWasBlocked =
			g_raceSecondBlocked && g_raceGetterCalls.load() == 0;
		g_raceMayFinish = true;
	}
	g_raceContinue.notify_one();
	first.join();
	second.join();

	return Check(secondWasBlocked,
			"second thread observed the early published bit")
		&& Check(firstResult && secondResult,
			"serialized accessor was rejected")
		&& Check(g_raceInitializeCalls.load() == 1,
			"early publication caused duplicate initialization")
		&& Check(g_raceGetterCalls.load() == 2,
			"serialized accessors did not each run once")
		&& Check(firstOutput[0] == 0.125f
			&& secondOutput[2] == 0.5f,
			"serialized accessor output changed");
}

bool TestInvalidInputsFailClosed()
{
	Reset();
	MaterialStorage material;
	float value[3] = {};
	return Check(!EnsureMaterialContext(0, &Initialize),
			"null material was accepted")
		&& Check(!EnsureMaterialContext(material.Address(), nullptr),
			"null initializer was accepted for an incomplete state")
		&& Check(!FinalizeMaterialInitialization(0, &SetupErrorShader),
			"null material reached the error builder")
		&& Check(!InvokeMaterialReflectivity(
				material.Address(),
				&Initialize,
				nullptr,
				value),
			"null reflectivity getter was accepted")
		&& Check(!InvokeMaterialReflectivity(
				material.Address(),
				&Initialize,
				&Reflectivity,
				nullptr),
			"null reflectivity output was accepted");
}
}

int main()
{
	bool passed = true;
	passed &= TestCompleteStateSkipsInitializationAndInvokesAccessors();
	passed &= TestPristineInitializationRetriesOnce();
	passed &= TestZeroResultWithCompleteStateIsAccepted();
	passed &= TestShaderIsRequiredForUsableState();
	passed &= TestFailedInitializationPreservesPublishedBit();
	passed &= TestIncompleteSuccessFailsClosed();
	passed &= TestEveryPartialOwnerBlocksInitialization();
	passed &= TestPristinePublishedFailureBuildsErrorState();
	passed &= TestPublishedPristineFailureRecoversAtAccessor();
	passed &= TestUnpublishedPristineFailureRecoversAtAccessor();
	passed &= TestEveryPartialOwnerBlocksErrorBuilder();
	passed &= TestIncompleteErrorStateFailsClosed();
	passed &= TestInitializationDepthBlocksRecovery();
	passed &= TestSameThreadReentryIsBounded();
	passed &= TestFailedAccessorsDoNotFabricateValues();
	passed &= TestTwoThreadEarlyPublicationIsSerialized();
	passed &= TestInvalidInputsFailClosed();
	if (!passed)
		return 1;
	std::cout << "materialsystem DX11 material guard tests passed\n";
	return 0;
}
