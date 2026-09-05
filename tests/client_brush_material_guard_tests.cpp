#include "../engine/core/client_brush_material_guard.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
using namespace r1delta::client_brush_material;

int g_originalCalls;
const void* g_originalModel;
int g_originalResult;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

template <typename T, std::size_t Size>
void Store(std::array<std::uint8_t, Size>& storage, std::size_t offset, T value)
{
	std::memcpy(storage.data() + offset, &value, sizeof(value));
}

int __fastcall OriginalMaterialCount(const void* model)
{
	++g_originalCalls;
	g_originalModel = model;
	return g_originalResult;
}

struct BrushModelFixture
{
	std::array<std::uint8_t, kModelSurfaceCountOffset + sizeof(std::int32_t)> model{};
	std::array<std::uint8_t, kSharedSurfaceTableOffset + sizeof(void*)> shared{};
	std::array<std::uint8_t, 64> surfaces{};

	BrushModelFixture()
	{
		Store(model, kModelTypeOffset, kBrushModelType);
		Store(model, kModelSharedDataOffset, static_cast<void*>(shared.data()));
		Store(model, kModelFirstSurfaceOffset, std::int32_t{0x1BA5});
		Store(model, kModelSurfaceCountOffset, std::int32_t{1});
	}

	void SetSurfaceTable(void* value)
	{
		Store(shared, kSharedSurfaceTableOffset, value);
	}
};

void ResetOriginal(int result)
{
	g_originalCalls = 0;
	g_originalModel = nullptr;
	g_originalResult = result;
}

bool TestReportedCrashFixtureFailsClosed()
{
	BrushModelFixture fixture;
	ResetOriginal(7);

	const std::uintptr_t reportedFaultAddress =
		static_cast<std::uintptr_t>(0x1BA5) * kSurfaceStride
		+ kSurfaceMaterialOffset;
	const int result = CountMaterialsWithGuard(
		fixture.model.data(), &OriginalMaterialCount);

	return Check(reportedFaultAddress == 0x374B8,
		"fixture no longer reproduces the report's null-table fault address")
		&& Check(result == 0,
			"incomplete brush material state did not report zero materials")
		&& Check(g_originalCalls == 0,
			"incomplete brush material state reached the crashing stock lookup");
}

bool TestNullSharedDataFailsClosed()
{
	BrushModelFixture fixture;
	Store(fixture.model, kModelSharedDataOffset, static_cast<void*>(nullptr));
	ResetOriginal(9);

	const int result = CountMaterialsWithGuard(
		fixture.model.data(), &OriginalMaterialCount);
	return Check(result == 0, "null brush shared data did not fail closed")
		&& Check(g_originalCalls == 0,
			"null brush shared data reached the stock lookup");
}

bool TestReadyBrushPreservesOriginalLookup()
{
	BrushModelFixture fixture;
	fixture.SetSurfaceTable(fixture.surfaces.data());
	ResetOriginal(3);

	const int result = CountMaterialsWithGuard(
		fixture.model.data(), &OriginalMaterialCount);
	return Check(result == 3, "ready brush material count changed")
		&& Check(g_originalCalls == 1,
			"ready brush did not call the stock lookup exactly once")
		&& Check(g_originalModel == fixture.model.data(),
			"ready brush changed the stock model argument");
}

bool TestZeroSurfaceBrushPreservesOriginalLookup()
{
	BrushModelFixture fixture;
	Store(fixture.model, kModelSurfaceCountOffset, std::int32_t{0});
	ResetOriginal(0);

	const int result = CountMaterialsWithGuard(
		fixture.model.data(), &OriginalMaterialCount);
	return Check(result == 0, "zero-surface brush result changed")
		&& Check(g_originalCalls == 1,
			"safe zero-surface brush did not preserve the stock lookup");
}

bool TestNonBrushPreservesOriginalLookup()
{
	BrushModelFixture fixture;
	Store(fixture.model, kModelTypeOffset, std::int32_t{2});
	ResetOriginal(11);

	const int result = CountMaterialsWithGuard(
		fixture.model.data(), &OriginalMaterialCount);
	return Check(result == 11, "non-brush material count changed")
		&& Check(g_originalCalls == 1,
			"non-brush model did not call the stock lookup exactly once");
}

bool TestInvalidArgumentsFailClosed()
{
	BrushModelFixture fixture;
	ResetOriginal(5);
	bool passed = true;
	passed &= Check(CountMaterialsWithGuard(nullptr, &OriginalMaterialCount) == 0,
		"null model did not fail closed");
	passed &= Check(CountMaterialsWithGuard(fixture.model.data(), nullptr) == 0,
		"null original function did not fail closed");
	passed &= Check(g_originalCalls == 0,
		"invalid arguments called the stock lookup");
	return passed;
}

bool TestInstallationScope()
{
	bool passed = true;
	passed &= Check(ShouldInstall(
		true,
		L"C:\\whatever\\bin\\x64_retail\\engine.dll"),
		"stock client engine path was rejected");
	passed &= Check(ShouldInstall(
		true,
		L"D:\\TITANFALL\\BIN\\X64_RETAIL\\ENGINE.DLL"),
		"stock engine path matching was not case-insensitive");
	passed &= Check(!ShouldInstall(
		false,
		L"C:\\whatever\\bin\\x64_retail\\engine.dll"),
		"non-client runtime accepted the hook");
	passed &= Check(!ShouldInstall(
		true,
		L"C:\\whatever\\r1delta\\bin\\engine_r1o.dll"),
		"R1O engine path was accepted");
	passed &= Check(!ShouldInstall(true, nullptr),
		"null module path was accepted");
	return passed;
}
}

int main()
{
	bool passed = true;
	passed &= TestReportedCrashFixtureFailsClosed();
	passed &= TestNullSharedDataFailsClosed();
	passed &= TestReadyBrushPreservesOriginalLookup();
	passed &= TestZeroSurfaceBrushPreservesOriginalLookup();
	passed &= TestNonBrushPreservesOriginalLookup();
	passed &= TestInvalidArgumentsFailClosed();
	passed &= TestInstallationScope();
	if (!passed)
		return 1;
	std::cout << "client brush-material guard tests passed\n";
	return 0;
}
