// ECDSA P-256 verification through Windows CNG (bcrypt), used for identity
// tokens (p2p_identity.h).

#include "p2p_identity.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace p2p
{

bool VerifyP256(const P256PublicKey& key, const Sha256Digest& digest, const P256Signature& signature)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0)
        return false;

    std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + key.size());
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), key.data(), key.size());

    BCRYPT_KEY_HANDLE handle = nullptr;
    bool ok = false;
    if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &handle, blob.data(), static_cast<ULONG>(blob.size()), 0) == 0)
    {
        // CNG expects the raw r || s form the master server produces.
        ok = BCryptVerifySignature(handle, nullptr, const_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()),
                                   const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), 0) == 0;
        BCryptDestroyKey(handle);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

} // namespace p2p
