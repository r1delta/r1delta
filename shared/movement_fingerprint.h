#pragma once

// Movement program identity shared by the CLIENT and SERVER VMs. Header-only so tests/movement_fingerprint_tests.cpp
// compiles exactly the code the game ships.

#include <Windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#pragma comment(lib, "bcrypt.lib")

namespace r1delta::movement
{
inline constexpr char kMovementNativeStateFingerprintTag[] = "R1Delta movement native state format 2";

// The program source is a Squirrel verbatim string (@"...") inside scripts/vscripts/movement/*.nut, so it carries the
// line endings of the file it was checked out as: CRLF from a Windows/CI checkout, LF from a Linux one. Both peers must
// agree on the program, not on the checkout, so CRLF and lone CR become LF. Nothing else is touched: Squirrel treats
// CR as whitespace, so the normalized text compiles to the same program, but trailing blanks can sit inside a string
// literal and are therefore not stripped.
inline std::string NormalizeMovementSource(std::string_view source)
{
    std::string normalized;
    normalized.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        if (source[index] != '\r') { normalized.push_back(source[index]); continue; }
        normalized.push_back('\n');
        if (index + 1 < source.size() && source[index + 1] == '\n') ++index;
    }
    return normalized;
}

// name/slots/source -> 64 hex characters + NUL. `source` is hashed after NormalizeMovementSource.
inline bool FingerprintMovementSource(const char* name, const char* source, unsigned slots, char (&text)[65])
{
    const std::string normalized = NormalizeMovementSource(source);
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    unsigned char digest[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    constexpr char version[] = "R1Delta movement API 3";
    const auto append = [&](const void* bytes, std::size_t length)
    {
        return BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(length), 0) >= 0;
    };
    if (ok) ok = append(version, sizeof(version))
        && append(kMovementNativeStateFingerprintTag, sizeof(kMovementNativeStateFingerprintTag))
        && append(name, std::strlen(name) + 1) && append(&slots, sizeof(slots)) && append(normalized.data(), normalized.size());
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(digest); ++i) { text[i * 2] = digits[digest[i] >> 4]; text[i * 2 + 1] = digits[digest[i] & 15]; }
    text[64] = 0;
    return true;
}
}
