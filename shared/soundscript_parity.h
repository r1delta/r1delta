#pragma once
#include <cstdint>

// Sound-script parity between a client and the server it joins.
//
// Sound events carry an alias *index*, so a client whose alias table differs
// from the server's plays the wrong sounds (and, one past the end of its table,
// used to read a zeroed record and crash). Retail compares one CRC from
// svc_ServerInfo and can only say "do not match"; r1delta.wpatch disables that
// check and this one runs instead, naming the aliases that differ.
//
// 1. Client: on connect it sets the userinfo convar `_r1snd` to its table
//    fingerprint. The engine sends every userinfo convar in the NET_SetConVar it
//    writes at SIGNONSTATE_CONNECTED; the NET_SetConVar writer appends the
//    client's 256 name-bucket hashes to messages that carry the marker (only the
//    client's own signon message does, never a server's outgoing one).
// 2. Server: the NET_SetConVar reader knows the sender (m_pMessageHandler is
//    the client). If the index -> name mapping differs, the server sends that
//    client its alias names for the differing buckets, then a verdict, as
//    svc_StringCmd (`r1d_sndscript`).
// 3. Client: diffs those buckets against its own names, prints both sides and,
//    when the server enforces parity (`delta_soundtable_check 1`), disconnects
//    with the names in the reason.

constexpr char kSoundScriptMarkerConVar[] = "_r1snd";
constexpr char kSoundScriptCommand[] = "r1d_sndscript";
constexpr int kSoundScriptBuckets = 256;
constexpr int kSoundScriptUploadChunks = 8;

struct SoundScriptUploadChunk {
	char name[16];
	char value[260];
};

// --- Client ------------------------------------------------------------------
// Registers the marker convar and the server-executable reply command.
void SoundScriptParity_RegisterClient();
// Before the connect packet: publish the fingerprint in the marker convar.
void SoundScriptParity_ClientConnecting();
// NET_SetConVar writer: when the outgoing message carries the marker, returns
// the bucket chunks to append (0 otherwise) and starts awaiting the verdict.
int SoundScriptParity_ClientUploadChunks(bool messageCarriesMarker,
	SoundScriptUploadChunk (&out)[kSoundScriptUploadChunks]);

// --- Server ------------------------------------------------------------------
// One client's upload, collected while reading its NET_SetConVar.
struct SoundScriptUpload {
	bool present = false;
	char fingerprint[64] = {};
	std::uint32_t buckets[kSoundScriptBuckets] = {};
	std::uint32_t chunkMask = 0;

	// True when the convar belongs to the parity exchange; such convars are
	// consumed here and never applied as userinfo.
	bool Accept(const char* name, const char* value);
	bool HasAllBuckets() const { return chunkMask == (1u << kSoundScriptUploadChunks) - 1; }
};

// Compares a client's upload with the local table and, on a mismatch, sends
// that client the server's names for the differing buckets plus the verdict.
void SoundScriptParity_ServerCheck(int playerSlot, const SoundScriptUpload& upload);
