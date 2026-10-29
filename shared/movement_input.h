#pragma once

#include <cstddef>
#include <cstdint>

namespace r1delta::movement
{
inline constexpr std::size_t kMovementCommandSize = 0x134;
inline constexpr std::size_t kMovementVerifiedCommandSize = 0x138;
inline constexpr std::size_t kMovementInputOffset = 0x7C;
inline constexpr std::size_t kMovementInputPresenceOffset = 0x94;

// Six existing camera words; only buttons is assigned in API v3. No CUserCmd
// member is inserted, and no native buttons, angles or movement field is edited.
struct MovementCommandInput
{
    std::uint32_t buttons{};
    std::uint32_t reserved[5]{};
};
static_assert(sizeof(MovementCommandInput) == 24);
static_assert(kMovementInputOffset + sizeof(MovementCommandInput) == kMovementInputPresenceOffset);

struct MovementInputCallbacks
{
    // True only for an accepted identical-v3-fingerprint session. This must not
    // disappear while that session's queued/backup commands can still be sent.
    bool (*sessionCapable)(){};
    bool (*playerEnabled)(std::uintptr_t player){};
    // Called once per newly constructed opted-in command, never on prediction
    // replay or network retransmission. False means failure, NOT zero buttons.
    bool (*build)(std::uintptr_t player, std::uintptr_t command, std::uint32_t& buttons){};
    // Required: invalidate/disable the failed program; the failed command carries
    // no custom input. Do not let an opted-in player continue with a dead callback.
    void (*failed)(std::uintptr_t player, std::uintptr_t command){};
};

// Call after MinHook initialization, from the serialized module-install path.
// All three client hooks are installed/enabled together or removed on failure.
// Callback pointers are immutable after a successful installation. False is a
// hard readiness failure: parent must refuse predicted-movement opt-in.
// Server requires the same supported server.dll in listen, legacy and R1O modes;
// its existing raw-bit decoder already matches this integer delta encoding.
bool InstallMovementInputClient(std::uintptr_t clientModule, const MovementInputCallbacks& callbacks);
bool InstallMovementInputServer(std::uintptr_t serverModule);
bool MovementInputClientReady();
bool MovementInputServerReady();

// enabled must include domain readiness, negotiated capability and this player's
// opt-in. Reads only the actual received/stored CUserCmd; never samples live input.
// False (including a clear presence bit) sets buttons to zero. A disabled command
// cannot inherit custom input even though native delta decode copies its baseline.
bool ReadMovementCommandInput(std::uintptr_t command, bool enabled, std::uint32_t& buttons);

// Exact native evidence (Client2015 client SHA-256 5b3f0080...e7afd5e;
// TFO server SHA-256 50faad92...11ff41e4; full digests are in the implementation):
// * client +1A83D0 constructs 0x134-byte cmds, ring size 300. +1A5350 has
//   Win64 ABI int64(input*, int splitSlot, cmd*, previousCmd*). The sole build
//   call returns to +1A87C8. Our post-call callback precedes seed +279950,
//   storage +24A5E0, verified-copy +278840 and CRC +278AD0.
//   +7B1B0 has ABI player*(int splitSlot), the same command-owner resolver
//   used by +1A83D0 (not the distinct +7B120 local/view-player resolver).
// * client +2791B0: int64(bf_write*, const cmd* to, const cmd* from).
//   Camera helper calls return to +279524/53D/556/56F/588/5A1.
//   +278E20: char(bf_write*, float from [XMM1], float to [XMM2]).
//   We bit-copy those registers and call +278CE0, the native integer delta
//   helper char(bf_write*, uint32 from, uint32 to, int width=32), ONLY at
//   those six sites in a capable session with this command's presence set.
//   Wire layout stays one presence bit, then six (changed bit + optional raw32)
//   fields. Integer equality preserves every bit, including 0x80000000 and NaNs;
//   no coordinate/angle quantizer or additional protocol bit is introduced.
// * server +2576F0: int(bf_read*, cmd* to, const cmd* from), copies baseline
//   through +257500, then +259355 unconditionally overwrites presence. Raw32
//   stores at +2594AA/595F6/59745/59894/599E3/59B32 preserve all six words.
//   No reader patch is needed. Its exact image and camera decode are guarded.
// * native copy +278840 copies all six words and presence byte. CRC +278AD0
//   includes +7C..+93 (24 bytes) and +94 (1 byte), so the callback must precede
//   verified copy/CRC. We do not recompute or bypass either native operation.
// * the native constructor resets camera storage/presence. In capable sessions
//   we clear those owned fields after +1A5350 too (it may copy a previous cmd),
//   including when no player/program is enabled. No callback then runs. Outside
//   capable sessions all native paths are unchanged. Every other cmd field is
//   unchanged in every case. The server never runs an input callback.
}
