#pragma once

#include <array>
#include <cstdint>

namespace ScriptErrorTelemetry
{

enum class VmContext : std::uint8_t
{
    Server = 0,
    Client,
    Ui,
    Count
};

struct NotificationSnapshot
{
    std::uint64_t sequence = 0;
    bool hasResult = false;
    bool isNew = false;
    std::array<char, 53> code{};
};

void Initialize() noexcept;
void BeginErrorBlock(VmContext context) noexcept;
void CapturePrint(VmContext context, const char* text) noexcept;
void EndErrorBlock(VmContext context) noexcept;
bool GetNotificationSnapshot(VmContext context, NotificationSnapshot& snapshot) noexcept;

} // namespace ScriptErrorTelemetry
