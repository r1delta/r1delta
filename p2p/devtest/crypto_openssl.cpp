// OpenSSL stand-in for p2p_crypto_win.cpp in the Linux harness/tests.

#include "../p2p_identity.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

namespace p2p
{

bool VerifyP256(const P256PublicKey& key, const Sha256Digest& digest, const P256Signature& signature)
{
    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    BIGNUM* x = BN_bin2bn(key.data(), 32, nullptr);
    BIGNUM* y = BN_bin2bn(key.data() + 32, 32, nullptr);
    ECDSA_SIG* sig = ECDSA_SIG_new();
    BIGNUM* r = BN_bin2bn(signature.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature.data() + 32, 32, nullptr);
    bool ok = false;
    if (ec && x && y && sig && r && s && EC_KEY_set_public_key_affine_coordinates(ec, x, y) == 1 &&
        ECDSA_SIG_set0(sig, r, s) == 1)
    {
        r = s = nullptr; // owned by sig now
        ok = ECDSA_do_verify(digest.data(), static_cast<int>(digest.size()), sig, ec) == 1;
    }
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(sig);
    BN_free(x);
    BN_free(y);
    EC_KEY_free(ec);
    return ok;
}

} // namespace p2p
