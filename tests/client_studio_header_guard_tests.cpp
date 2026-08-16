#include "../engine/core/client_studio_header_guard.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace
{
using namespace r1delta::client_studio_header;

int g_readinessCalls;
int g_initCalls;
int g_lookupCalls;
void* g_readinessResult;
void* g_recoveredHeader;
void* g_lookupHeader;
const char* g_lookupName;
int g_lookupResult;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

void Reset()
{
	g_readinessCalls = 0;
	g_initCalls = 0;
	g_lookupCalls = 0;
	g_readinessResult = nullptr;
	g_recoveredHeader = nullptr;
	g_lookupHeader = nullptr;
	g_lookupName = nullptr;
	g_lookupResult = 0;
}

void* __fastcall Readiness(void*)
{
	++g_readinessCalls;
	return g_readinessResult;
}

void __fastcall LazyInit(void* entity)
{
	++g_initCalls;
	if (g_recoveredHeader) {
		*reinterpret_cast<void**>(
			reinterpret_cast<std::uint8_t*>(entity) + kStudioHeaderOffset) =
			g_recoveredHeader;
	}
}

int __fastcall Lookup(void* studioHeader, const char* name)
{
	++g_lookupCalls;
	g_lookupHeader = studioHeader;
	g_lookupName = name;
	return g_lookupResult;
}

struct EntityStorage
{
	alignas(void*) std::array<std::uint8_t,
		kStudioHeaderOffset + sizeof(void*)> bytes{};
	std::array<void*, kReadinessVtableSlot + 1> renderableVtable{};

	EntityStorage()
	{
		renderableVtable[kReadinessVtableSlot] =
			reinterpret_cast<void*>(&Readiness);
		*reinterpret_cast<void***>(bytes.data() + kRenderableOffset) =
			renderableVtable.data();
	}

	void SetStudioHeader(void* studioHeader)
	{
		*reinterpret_cast<void**>(bytes.data() + kStudioHeaderOffset) =
			studioHeader;
	}
};

bool TestNonNullRoutesDirectlyToLookup()
{
	Reset();
	EntityStorage entity;
	void* const studioHeader = reinterpret_cast<void*>(0x12345678);
	entity.SetStudioHeader(studioHeader);
	g_lookupResult = 37;
	const char* const name = "spine_2";

	const int result = LookupWithNullGuard(
		entity.bytes.data(), name, &LazyInit, &Lookup);
	return Check(result == 37, "non-null lookup result changed")
		&& Check(g_readinessCalls == 0,
			"non-null header called renderable readiness")
		&& Check(g_initCalls == 0, "non-null header triggered lazy init")
		&& Check(g_lookupCalls == 1, "non-null header did not call lookup once")
		&& Check(g_lookupHeader == studioHeader,
			"lookup did not receive the studio header")
		&& Check(g_lookupName == name, "lookup name argument changed");
}

bool TestNullReadinessSkipsLazyInit()
{
	Reset();
	EntityStorage entity;

	const int result = LookupWithNullGuard(
		entity.bytes.data(), "missing", &LazyInit, &Lookup);
	return Check(result == -1, "null readiness did not return -1")
		&& Check(g_readinessCalls == 1,
			"null header did not call renderable readiness once")
		&& Check(g_initCalls == 0, "null readiness called lazy init")
		&& Check(g_lookupCalls == 0, "null readiness called lookup");
}

bool TestLazyInitRecoveryRoutesToLookup()
{
	Reset();
	EntityStorage entity;
	g_readinessResult = reinterpret_cast<void*>(0x1);
	g_recoveredHeader = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x87654321u));
	g_lookupResult = 12;

	const int result = LookupWithNullGuard(
		entity.bytes.data(), "head", &LazyInit, &Lookup);
	return Check(result == 12, "lazy-init recovery result changed")
		&& Check(g_readinessCalls == 1,
			"lazy-init recovery did not call renderable readiness once")
		&& Check(g_initCalls == 1, "ready header did not lazy-init exactly once")
		&& Check(g_lookupCalls == 1, "recovered header did not call lookup")
		&& Check(g_lookupHeader == g_recoveredHeader,
			"recovered header was not re-read before lookup");
}

bool TestPersistentNullReturnsIntSentinel()
{
	static_assert(sizeof(int) == 4);
	Reset();
	EntityStorage entity;
	g_readinessResult = reinterpret_cast<void*>(0x1);

	const int result = LookupWithNullGuard(
		entity.bytes.data(), "missing", &LazyInit, &Lookup);
	return Check(result == -1, "persistent null did not return int -1")
		&& Check(static_cast<std::uint32_t>(result) == UINT32_C(0xFFFFFFFF),
			"persistent-null sentinel was not 32-bit all-ones")
		&& Check(g_readinessCalls == 1,
			"persistent null did not call renderable readiness once")
		&& Check(g_initCalls == 1, "ready persistent null did not run lazy init")
		&& Check(g_lookupCalls == 0,
			"persistent null dereferenced the lookup target");
}

bool TestInvalidDependenciesFailClosed()
{
	Reset();
	EntityStorage entity;
	entity.SetStudioHeader(reinterpret_cast<void*>(0x1234));
	bool passed = true;
	passed &= Check(LookupWithNullGuard(nullptr, "x", &LazyInit, &Lookup) == -1,
		"null entity did not fail closed");
	passed &= Check(LookupWithNullGuard(
		entity.bytes.data(), "x", nullptr, &Lookup) == -1,
		"missing lazy-init routine did not fail closed");
	passed &= Check(LookupWithNullGuard(
		entity.bytes.data(), "x", &LazyInit, nullptr) == -1,
		"missing lookup routine did not fail closed");
	passed &= Check(
		g_readinessCalls == 0 && g_initCalls == 0 && g_lookupCalls == 0,
		"invalid dependency path called engine code");
	return passed;
}

bool TestInstallationScope()
{
	bool passed = true;
	passed &= Check(ShouldInstall(
		true,
		L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Titanfall\\r1\\bin\\x64_retail\\client.dll"),
		"proven stock client path was rejected");
	passed &= Check(ShouldInstall(
		true,
		L"D:\\TITANFALL\\R1\\BIN\\X64_RETAIL\\CLIENT.DLL"),
		"stock client path matching was not case-insensitive");
	passed &= Check(!ShouldInstall(
		false,
		L"C:\\Titanfall\\r1\\bin\\x64_retail\\client.dll"),
		"dedicated/R1O mode accepted the client hook");
	passed &= Check(!ShouldInstall(
		true,
		L"C:\\TitanfallOnline\\bin\\x64_retail\\client.dll"),
		"non-stock client path was accepted");
	passed &= Check(!ShouldInstall(
		true,
		L"C:\\Titanfall\\r1\\bin\\x64_retail\\server.dll"),
		"wrong module filename was accepted");
	passed &= Check(!ShouldInstall(true, nullptr),
		"null module path was accepted");
	return passed;
}
}

int main()
{
	bool passed = true;
	passed &= TestNonNullRoutesDirectlyToLookup();
	passed &= TestNullReadinessSkipsLazyInit();
	passed &= TestLazyInitRecoveryRoutesToLookup();
	passed &= TestPersistentNullReturnsIntSentinel();
	passed &= TestInvalidDependenciesFailClosed();
	passed &= TestInstallationScope();
	if (!passed)
		return 1;
	std::cout << "client studio-header guard tests passed\n";
	return 0;
}
