#include "../engine/core/materialsystem_dx11_txaa_lifetime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace r1delta::materialsystem_dx11;

static_assert(sizeof(TxaaQueuedResolveCommand) == 24);
static_assert(alignof(TxaaQueuedResolveCommand) == alignof(std::uintptr_t));
static_assert(sizeof(kExpectedTxaaQueuedResolveProducerPrologue) == 40);
static_assert(sizeof(kExpectedTxaaQueuedResolveCallback) == 9);
static_assert(sizeof(kExpectedTxaaQueueAllocatePrologue) == 40);
static_assert(sizeof(kExpectedTxaaQueuePublishPrologue) == 40);

struct FakeRenderTargetView
{
	std::uint32_t references = 1;
	std::uint32_t addRefCalls = 0;
	std::uint32_t releaseCalls = 0;
	std::vector<int> events;
};

FakeRenderTargetView* g_renderTargetView;
std::uintptr_t g_expectedCallbackArgument;
bool g_callbackSawRetainedView;
std::uint32_t g_callbackCalls;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

std::uint32_t __fastcall AddRefRenderTargetView(void* object)
{
	auto* const view = static_cast<FakeRenderTargetView*>(object);
	++view->addRefCalls;
	view->events.push_back(1);
	return ++view->references;
}

std::uint32_t __fastcall ReleaseRenderTargetView(void* object)
{
	auto* const view = static_cast<FakeRenderTargetView*>(object);
	++view->releaseCalls;
	view->events.push_back(3);
	return --view->references;
}

std::uintptr_t __fastcall ResolveTxaa(std::uintptr_t callbackArgument)
{
	++g_callbackCalls;
	g_callbackSawRetainedView = g_renderTargetView
		&& g_renderTargetView->references == 2;
	if (g_renderTargetView)
		g_renderTargetView->events.push_back(2);
	return callbackArgument == g_expectedCallbackArgument
		? 0x7A11u
		: 0;
}

std::uintptr_t __fastcall ThrowDuringResolve(std::uintptr_t)
{
	if (g_renderTargetView)
		g_renderTargetView->events.push_back(2);
	throw std::runtime_error("resolve failure");
}

void ResetResolveState(FakeRenderTargetView* view, std::uintptr_t callbackArgument)
{
	g_renderTargetView = view;
	g_expectedCallbackArgument = callbackArgument;
	g_callbackSawRetainedView = false;
	g_callbackCalls = 0;
}

bool TestRetailLayoutConstants()
{
	return Check(kTxaaQueuedResolveProducerRva == 0x5C540,
			"queued resolve producer RVA changed")
		&& Check(kTxaaQueuedResolveCallbackRva == 0x5AE20,
			"queued resolve callback RVA changed")
		&& Check(kTxaaQueueAllocateRva == 0x75C20,
			"queue allocator RVA changed")
		&& Check(kTxaaQueuePublishRva == 0x75510,
			"queue publisher RVA changed")
		&& Check(kTxaaSourceGlobalRva == 0x2983F8,
			"TXAA texture source global RVA changed")
		&& Check(kTxaaRenderTargetViewOffset == 0x8A0,
			"TXAA RTV field offset changed")
		&& Check(kQueuedRenderContextHardwareContextOffset == 0xD8,
			"queued callback-argument offset changed")
		&& Check(kTxaaResolveVirtualOffset == 0x1B0,
			"TXAA resolve virtual offset changed")
		&& Check(kTxaaQueuedResolveVtableEntryRva == 0x188178,
			"queued resolve vtable entry changed");
}

bool TestRenderTargetViewPointerChain()
{
	constexpr std::uintptr_t moduleBase = 0x10000000;
	constexpr std::uintptr_t sourceRoot = 0x20000000;
	constexpr std::uintptr_t textureBlock = 0x30000000;
	constexpr std::uintptr_t renderTargetView = 0x40000000;
	std::array<std::uintptr_t, 3> reads{};
	std::size_t readCount = 0;
	void* result = nullptr;

	const auto reader = [&](std::uintptr_t address, std::uintptr_t* value) {
		if (readCount < reads.size())
			reads[readCount] = address;
		++readCount;
		if (address == moduleBase + kTxaaSourceGlobalRva)
			*value = sourceRoot;
		else if (address == sourceRoot)
			*value = textureBlock;
		else if (address == textureBlock + kTxaaRenderTargetViewOffset)
			*value = renderTargetView;
		else
			return false;
		return true;
	};

	bool passed = Check(ReadTxaaRenderTargetView(moduleBase, reader, &result),
		"valid TXAA RTV pointer chain was rejected");
	passed &= Check(result == reinterpret_cast<void*>(renderTargetView),
		"TXAA RTV pointer chain returned the wrong object");
	passed &= Check(readCount == reads.size(),
		"TXAA RTV pointer chain performed an unexpected read count");
	passed &= Check(reads[0] == moduleBase + kTxaaSourceGlobalRva
			&& reads[1] == sourceRoot
			&& reads[2] == textureBlock + kTxaaRenderTargetViewOffset,
		"TXAA RTV pointer chain used the wrong addresses");
	return passed;
}

bool TestInvalidPointerChainFailsClosed()
{
	void* result = reinterpret_cast<void*>(0x1234);
	const auto nullRootReader = [](std::uintptr_t, std::uintptr_t* value) {
		*value = 0;
		return true;
	};
	bool passed = Check(!ReadTxaaRenderTargetView(
			0x10000000,
			nullRootReader,
			&result),
		"null TXAA source root was accepted");
	passed &= Check(result == nullptr,
		"failed TXAA pointer read preserved a stale output");
	passed &= Check(!ReadTxaaRenderTargetView(
			0,
			nullRootReader,
			&result),
		"null material-system base was accepted");
	passed &= Check(!ReadTxaaRenderTargetView(
			0x10000000,
			nullRootReader,
			nullptr),
		"null TXAA pointer output was accepted");
	return passed;
}

bool TestQueuedCommandOwnsViewUntilResolveCompletes()
{
	constexpr std::uintptr_t callbackArgument = 0x51510000;
	FakeRenderTargetView view;
	TxaaQueuedResolveCommand command{};
	ResetResolveState(&view, callbackArgument);

	bool passed = Check(RetainTxaaQueuedResolveCommand(
			&ResolveTxaa,
			callbackArgument,
			&view,
			&AddRefRenderTargetView,
			&command),
		"valid queued TXAA command was rejected");
	passed &= Check(command.callback == &ResolveTxaa
			&& command.argument == callbackArgument
			&& command.renderTargetView == &view,
		"queued TXAA command did not preserve its callback payload");
	view.events.push_back(4);
	passed &= Check(view.references == 2 && view.addRefCalls == 1,
		"queued TXAA command did not retain its RTV once");
	passed &= Check(view.events.size() == 2
			&& view.events[0] == 1
			&& view.events[1] == 4,
		"queued TXAA command was published before AddRef");

	std::array<std::uintptr_t, 4> queueState{};
	queueState[2] = reinterpret_cast<std::uintptr_t>(&command);
	queueState[3] = queueState[2];
	const std::uintptr_t result = ExecuteTxaaQueuedResolve(
		reinterpret_cast<std::uintptr_t>(queueState.data()),
		&ReleaseRenderTargetView);
	const std::uintptr_t expectedNext =
		reinterpret_cast<std::uintptr_t>(&command + 1);

	passed &= Check(result == 0x7A11u,
		"queued TXAA callback return value changed");
	passed &= Check(g_callbackCalls == 1 && g_callbackSawRetainedView,
		"queued TXAA callback ran without the retained RTV");
	passed &= Check(view.references == 1
			&& view.addRefCalls == 1
			&& view.releaseCalls == 1,
		"queued TXAA callback did not balance its RTV reference");
	passed &= Check(view.events.size() == 4
			&& view.events[0] == 1
			&& view.events[1] == 4
			&& view.events[2] == 2
			&& view.events[3] == 3,
		"queued TXAA lifetime ordering changed");
	passed &= Check(queueState[2] == expectedNext
			&& queueState[3] == expectedNext,
		"queued TXAA executor did not advance both cursors by 24 bytes");
	return passed;
}

bool TestNullViewStillExecutesResolve()
{
	constexpr std::uintptr_t callbackArgument = 0x61610000;
	TxaaQueuedResolveCommand command{};
	ResetResolveState(nullptr, callbackArgument);
	bool passed = Check(RetainTxaaQueuedResolveCommand(
			&ResolveTxaa,
			callbackArgument,
			nullptr,
			nullptr,
			&command),
		"null RTV command was rejected");

	std::array<std::uintptr_t, 4> queueState{};
	queueState[2] = reinterpret_cast<std::uintptr_t>(&command);
	queueState[3] = queueState[2];
	const std::uintptr_t result = ExecuteTxaaQueuedResolve(
		reinterpret_cast<std::uintptr_t>(queueState.data()),
		nullptr);
	passed &= Check(result == 0x7A11u && g_callbackCalls == 1,
		"null RTV suppressed TXAA callback");
	passed &= Check(queueState[2] == reinterpret_cast<std::uintptr_t>(&command + 1)
			&& queueState[3] == queueState[2],
		"null RTV command did not advance both cursors");
	return passed;
}

bool TestResolveExceptionStillReleasesView()
{
	constexpr std::uintptr_t callbackArgument = 0x71710000;
	FakeRenderTargetView view;
	TxaaQueuedResolveCommand command{};
	ResetResolveState(&view, callbackArgument);
	bool passed = Check(RetainTxaaQueuedResolveCommand(
			&ThrowDuringResolve,
			callbackArgument,
			&view,
			&AddRefRenderTargetView,
			&command),
		"exception test command was rejected");
	std::array<std::uintptr_t, 4> queueState{};
	queueState[2] = reinterpret_cast<std::uintptr_t>(&command);
	queueState[3] = queueState[2];

	bool threw = false;
	try {
		(void)ExecuteTxaaQueuedResolve(
			reinterpret_cast<std::uintptr_t>(queueState.data()),
			&ReleaseRenderTargetView);
	}
	catch (const std::runtime_error&) {
		threw = true;
	}
	passed &= Check(threw, "TXAA callback exception was swallowed");
	passed &= Check(view.references == 1
			&& view.addRefCalls == 1
			&& view.releaseCalls == 1,
		"TXAA callback exception leaked the retained RTV");
	passed &= Check(view.events.size() == 3
			&& view.events[0] == 1
			&& view.events[1] == 2
			&& view.events[2] == 3,
		"exception cleanup did not release after callback entry");
	passed &= Check(queueState[2] == reinterpret_cast<std::uintptr_t>(&command)
			&& queueState[3] == queueState[2],
		"failed TXAA callback advanced the queue cursor");
	return passed;
}

bool TestInvalidCommandInputsFailClosed()
{
	FakeRenderTargetView view;
	const auto sentinelCallback =
		reinterpret_cast<TxaaQueuedResolveCallback>(
			static_cast<std::uintptr_t>(0xA5A5));
	TxaaQueuedResolveCommand command{
		sentinelCallback,
		0xB6B6,
		reinterpret_cast<void*>(0x5A5A)
	};
	bool passed = Check(!RetainTxaaQueuedResolveCommand(
			nullptr,
			0x1000,
			&view,
			&AddRefRenderTargetView,
			&command),
		"null callback was accepted");
	passed &= Check(!RetainTxaaQueuedResolveCommand(
			&ResolveTxaa,
			0,
			&view,
			&AddRefRenderTargetView,
			&command),
		"null callback argument was accepted");
	passed &= Check(!RetainTxaaQueuedResolveCommand(
			&ResolveTxaa,
			0x1000,
			&view,
			nullptr,
			&command),
		"RTV without AddRef function was accepted");
	passed &= Check(!RetainTxaaQueuedResolveCommand(
			&ResolveTxaa,
			0x1000,
			&view,
			&AddRefRenderTargetView,
			nullptr),
		"null command output was accepted");
	passed &= Check(view.addRefCalls == 0 && view.references == 1,
		"invalid command input retained the RTV");
	passed &= Check(command.callback == sentinelCallback
			&& command.argument == 0xB6B6
			&& command.renderTargetView == reinterpret_cast<void*>(0x5A5A),
		"invalid command input modified its output");
	return passed;
}
}

int main()
{
	bool passed = true;
	passed &= TestRetailLayoutConstants();
	passed &= TestRenderTargetViewPointerChain();
	passed &= TestInvalidPointerChainFailsClosed();
	passed &= TestQueuedCommandOwnsViewUntilResolveCompletes();
	passed &= TestNullViewStillExecutesResolve();
	passed &= TestResolveExceptionStillReleasesView();
	passed &= TestInvalidCommandInputsFailClosed();
	if (!passed)
		return 1;
	std::cout << "materialsystem DX11 TXAA lifetime tests passed\n";
	return 0;
}
