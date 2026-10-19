#include "crypto.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cstring>

namespace osl::crypto {
namespace {

bool Digest(const EVP_MD* type, const uint8_t* data, size_t size, uint8_t* digest, unsigned expected) {
    unsigned length = 0;
    return EVP_Digest(data, size, digest, &length, type, nullptr) == 1 && length == expected;
}

struct BigNumber {
    BIGNUM* value = BN_new();
    ~BigNumber() { BN_free(value); }
    BigNumber(const BigNumber&) = delete;
    BigNumber& operator=(const BigNumber&) = delete;
    BigNumber() = default;
    explicit BigNumber(const uint8_t* bytes, int size) { BN_bin2bn(bytes, size, value); }
};

} // namespace

bool Sha1(const uint8_t* data, size_t size, uint8_t (&digest)[20]) {
    return Digest(EVP_sha1(), data, size, digest, 20);
}

bool Sha256(const uint8_t* data, size_t size, uint8_t (&digest)[32]) {
    return Digest(EVP_sha256(), data, size, digest, 32);
}

bool HmacSha256(const uint8_t* key, size_t key_size, const uint8_t* data, size_t size, uint8_t (&digest)[32]) {
    unsigned length = 0;
    return HMAC(EVP_sha256(), key, int(key_size), data, size, digest, &length) != nullptr && length == 32;
}

struct Sha256Stream::Impl {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    bool ok = false;
};

Sha256Stream::Sha256Stream() : impl_(new Impl) {
    impl_->ok = impl_->context && EVP_DigestInit_ex(impl_->context, EVP_sha256(), nullptr) == 1;
}

Sha256Stream::~Sha256Stream() { EVP_MD_CTX_free(impl_->context); }

bool Sha256Stream::Update(const void* data, size_t size) {
    return impl_->ok && EVP_DigestUpdate(impl_->context, data, size) == 1;
}

bool Sha256Stream::Finish(uint8_t (&digest)[32]) {
    unsigned length = 0;
    return impl_->ok && EVP_DigestFinal_ex(impl_->context, digest, &length) == 1 && length == 32;
}

struct AesKey::Impl {
    AesMode mode = AesMode::kEcb;
    uint8_t key[16] = {};
    bool ready = false;

    bool Run(bool encrypt, uint8_t* data, size_t size, const uint8_t* iv) const {
        if (!ready || size % 16 || size > 0x7FFFFFF0u || (mode == AesMode::kCbc && !iv)) {
            return false;
        }
        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (!context) {
            return false;
        }
        const EVP_CIPHER* type = mode == AesMode::kEcb ? EVP_aes_128_ecb() : EVP_aes_128_cbc();
        int length = 0, tail = 0;
        const bool ok = EVP_CipherInit_ex(context, type, nullptr, key, iv, encrypt ? 1 : 0) == 1 &&
                        EVP_CIPHER_CTX_set_padding(context, 0) == 1 &&
                        EVP_CipherUpdate(context, data, &length, data, int(size)) == 1 && size_t(length) == size &&
                        EVP_CipherFinal_ex(context, data + length, &tail) == 1;
        EVP_CIPHER_CTX_free(context);
        return ok;
    }
};

AesKey::AesKey() : impl_(new Impl) {}

AesKey::~AesKey() = default;

bool AesKey::Init(AesMode mode, const uint8_t* key) {
    impl_->mode = mode;
    std::memcpy(impl_->key, key, sizeof(impl_->key));
    impl_->ready = true;
    return true;
}

bool AesKey::Encrypt(uint8_t* data, size_t size, uint8_t* iv) { return impl_->Run(true, data, size, iv); }

bool AesKey::Decrypt(uint8_t* data, size_t size, uint8_t* iv) { return impl_->Run(false, data, size, iv); }

bool RsaDecryptPkcs1(const uint8_t* modulus, const uint8_t* prime1, const uint8_t* prime2, const uint8_t* cipher,
                     std::vector<uint8_t>& message) {
    BN_CTX* context = BN_CTX_new();
    BigNumber n(modulus, 256), p(prime1, 128), q(prime2, 128), c(cipher, 256), e, d, phi, one, p1, q1, plain;
    uint8_t block[256];
    bool ok = context && BN_set_word(e.value, 65537) && BN_set_word(one.value, 1) && BN_sub(p1.value, p.value, one.value) &&
              BN_sub(q1.value, q.value, one.value) && BN_mul(phi.value, p1.value, q1.value, context) &&
              BN_mod_inverse(d.value, e.value, phi.value, context) &&
              BN_mod_exp(plain.value, c.value, d.value, n.value, context) &&
              BN_bn2binpad(plain.value, block, sizeof(block)) == int(sizeof(block));
    BN_CTX_free(context);
    if (!ok || block[0] != 0x00 || block[1] != 0x02) {
        return false;
    }
    size_t at = 2;
    while (at < sizeof(block) && block[at] != 0) {
        ++at;
    }
    if (at < 10 || at >= sizeof(block)) {
        return false;
    }
    message.assign(block + at + 1, block + sizeof(block));
    return true;
}

} // namespace osl::crypto
