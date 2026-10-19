#include "crypto.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>

namespace osl::crypto {

bool Sha1(const uint8_t* data, size_t size, uint8_t (&digest)[20]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0))) {
        return false;
    }
    const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data), ULONG(size), digest, 20);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return BCRYPT_SUCCESS(status);
}

bool Sha256(const uint8_t* data, size_t size, uint8_t (&digest)[32]) {
    return BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data), ULONG(size),
                                     digest, 32));
}

bool HmacSha256(const uint8_t* key, size_t key_size, const uint8_t* data, size_t size, uint8_t (&digest)[32]) {
    return BCRYPT_SUCCESS(BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, const_cast<PUCHAR>(key), ULONG(key_size),
                                     const_cast<PUCHAR>(data), ULONG(size), digest, 32));
}

struct Sha256Stream::Impl {
    BCRYPT_HASH_HANDLE hash = nullptr;
};

Sha256Stream::Sha256Stream() : impl_(new Impl) {
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &impl_->hash, nullptr, 0, nullptr, 0, 0))) {
        impl_->hash = nullptr;
    }
}

Sha256Stream::~Sha256Stream() {
    if (impl_->hash) {
        BCryptDestroyHash(impl_->hash);
    }
}

bool Sha256Stream::Update(const void* data, size_t size) {
    return impl_->hash && BCRYPT_SUCCESS(BCryptHashData(impl_->hash, static_cast<PUCHAR>(const_cast<void*>(data)),
                                                       ULONG(size), 0));
}

bool Sha256Stream::Finish(uint8_t (&digest)[32]) {
    return impl_->hash && BCRYPT_SUCCESS(BCryptFinishHash(impl_->hash, digest, sizeof(digest), 0));
}

struct AesKey::Impl {
    BCRYPT_KEY_HANDLE handle = nullptr;
};

AesKey::AesKey() : impl_(new Impl) {}

AesKey::~AesKey() {
    if (impl_->handle) {
        BCryptDestroyKey(impl_->handle);
    }
}

bool AesKey::Init(AesMode mode, const uint8_t* key) {
    BCRYPT_ALG_HANDLE algorithm = mode == AesMode::kEcb ? BCRYPT_AES_ECB_ALG_HANDLE : BCRYPT_AES_CBC_ALG_HANDLE;
    return BCRYPT_SUCCESS(
        BCryptGenerateSymmetricKey(algorithm, &impl_->handle, nullptr, 0, const_cast<PUCHAR>(key), 16, 0));
}

bool AesKey::Encrypt(uint8_t* data, size_t size, uint8_t* iv) {
    ULONG done = 0;
    return impl_->handle && BCRYPT_SUCCESS(BCryptEncrypt(impl_->handle, data, ULONG(size), nullptr, iv, iv ? 16 : 0,
                                                         data, ULONG(size), &done, 0));
}

bool AesKey::Decrypt(uint8_t* data, size_t size, uint8_t* iv) {
    ULONG done = 0;
    return impl_->handle && BCRYPT_SUCCESS(BCryptDecrypt(impl_->handle, data, ULONG(size), nullptr, iv, iv ? 16 : 0,
                                                         data, ULONG(size), &done, 0));
}

bool RsaDecryptPkcs1(const uint8_t* modulus, const uint8_t* prime1, const uint8_t* prime2, const uint8_t* cipher,
                     std::vector<uint8_t>& message) {
    static constexpr uint8_t kPublicExponent[] = {0x01, 0x00, 0x01};
    std::vector<uint8_t> blob(sizeof(BCRYPT_RSAKEY_BLOB) + sizeof(kPublicExponent) + 256 + 128 + 128);
    auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
    header->Magic = BCRYPT_RSAPRIVATE_MAGIC;
    header->BitLength = 2048;
    header->cbPublicExp = sizeof(kPublicExponent);
    header->cbModulus = 256;
    header->cbPrime1 = header->cbPrime2 = 128;
    uint8_t* at = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
    at = std::copy_n(kPublicExponent, sizeof(kPublicExponent), at);
    at = std::copy_n(modulus, 256, at);
    at = std::copy_n(prime1, 128, at);
    std::copy_n(prime2, 128, at);

    BCRYPT_KEY_HANDLE key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(BCRYPT_RSA_ALG_HANDLE, nullptr, BCRYPT_RSAPRIVATE_BLOB, &key, blob.data(),
                                            ULONG(blob.size()), 0))) {
        return false;
    }
    uint8_t plain[256];
    ULONG length = 0;
    const NTSTATUS status = BCryptDecrypt(key, const_cast<PUCHAR>(cipher), 256, nullptr, nullptr, 0, plain,
                                          sizeof(plain), &length, BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(key);
    if (!BCRYPT_SUCCESS(status)) {
        return false;
    }
    message.assign(plain, plain + length);
    return true;
}

} // namespace osl::crypto
