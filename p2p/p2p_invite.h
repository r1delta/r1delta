#pragma once

#include "p2p_protocol.h"
#include <string_view>

namespace p2p
{
enum class InviteRoute { Server, Iroh, Tailcat, Eos };
struct InviteTarget
{
    InviteRoute route;
    std::string address;
    uint16_t port;
};

// Discord's 128-byte SDK buffer includes the terminating NUL.
constexpr size_t kMaxInviteSecretSize = 127;
std::optional<InviteTarget> ParseInviteTarget(std::string_view secret);
std::string EncodeOverlayInvite(Backend backend, std::string_view address, uint16_t port);
// Revalidates even manually constructed targets; invalid targets produce no command.
std::string InviteConnectCommand(const InviteTarget& target);
} // namespace p2p
