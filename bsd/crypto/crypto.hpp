#pragma once
/*
 * crypto.hpp -- C++ crypto API for the xkern kernel.
 *
 * Provides RAII wrappers over the freestanding C crypto primitives.
 * No exceptions, no RTTI, no STL containers -- pure kernel C++20.
 *
 * Usage:
 *     #include "bsd/crypto/crypto.hpp"
 *
 *     kernel::crypto::Sha256 hasher;
 *     hasher.update(data, len);
 *     auto digest = hasher.finalize();
 *
 *     kernel::crypto::AesCipher enc;
 *     enc.init(AES_256, MODE_CBC, key, 32, iv, 16);
 *     u32 ct_len = enc.encrypt(plain, plain_len, cipher_out, max);
 */

#include "crypto_types.h"
#include "crypto_hash.h"
#include "crypto_cipher.h"
#include "crypto_key.h"
#include "crypto_util.h"

namespace kernel::crypto {

/* ---- Fixed-size byte array (no heap) ---- */
template<u32 N>
struct ByteVec {
    u8  data[N];
    u32 len = 0;

    const u8* begin() const { return data; }
    const u8* end()   const { return data + len; }
    u8*       begin()       { return data; }
    u8*       end()         { return data + len; }
    u32       size()  const { return len; }
    bool      empty() const { return len == 0; }
    const u8* ptr()   const { return data; }
};

using Sha256Digest = ByteVec<SHA256_DIGEST_SIZE>;
using Md5Digest    = ByteVec<MD5_DIGEST_SIZE>;
using Sha1Digest   = ByteVec<SHA1_DIGEST_SIZE>;
using HmacDigest   = ByteVec<SHA256_DIGEST_SIZE>;

/* ---- SHA-256 ---- */
class Sha256 {
public:
    Sha256()  { sha256_init(&ctx_); }
    ~Sha256() = default;

    void update(const u8* data, u32 len) { sha256_update(&ctx_, data, len); }
    Sha256Digest finalize() {
        Sha256Digest d;
        sha256_final(&ctx_, d.data);
        d.len = SHA256_DIGEST_SIZE;
        return d;
    }
    void reset() { sha256_init(&ctx_); }

    static Sha256Digest hash(const u8* data, u32 len) {
        Sha256 h;
        h.update(data, len);
        return h.finalize();
    }

private:
    sha256_ctx ctx_;
};

/* ---- MD5 ---- */
class Md5 {
public:
    Md5()  { md5_init(&ctx_); }
    ~Md5() = default;

    void update(const u8* data, u32 len) { md5_update(&ctx_, data, len); }
    Md5Digest finalize() {
        Md5Digest d;
        md5_final(&ctx_, d.data);
        d.len = MD5_DIGEST_SIZE;
        return d;
    }
    void reset() { md5_init(&ctx_); }

    static Md5Digest hash(const u8* data, u32 len) {
        Md5 h;
        h.update(data, len);
        return h.finalize();
    }

private:
    md5_ctx ctx_;
};

/* ---- SHA-1 ---- */
class Sha1 {
public:
    Sha1()  { sha1_init(&ctx_); }
    ~Sha1() = default;

    void update(const u8* data, u32 len) { sha1_update(&ctx_, data, len); }
    Sha1Digest finalize() {
        Sha1Digest d;
        sha1_final(&ctx_, d.data);
        d.len = SHA1_DIGEST_SIZE;
        return d;
    }
    void reset() { sha1_init(&ctx_); }

    static Sha1Digest hash(const u8* data, u32 len) {
        Sha1 h;
        h.update(data, len);
        return h.finalize();
    }

private:
    sha1_ctx ctx_;
};

/* ---- HMAC-SHA256 ---- */
inline HmacDigest hmac_sha256(const u8* key, u32 key_len,
                               const u8* data, u32 data_len) {
    HmacDigest d;
    ::hmac_sha256(key, key_len, data, data_len, d.data);
    d.len = SHA256_DIGEST_SIZE;
    return d;
}

/* ---- AES Cipher ---- */
class AesCipher {
public:
    AesCipher() = default;
    ~AesCipher() = default;

    void init(int algorithm, int mode,
              const u8* key, u32 key_len,
              const u8* iv = nullptr, u32 iv_len = 0) {
        aes_init(&ctx_, algorithm, mode, key, key_len, iv, iv_len);
    }

    u32 encrypt(const u8* in, u32 in_len, u8* out, u32 out_max) {
        return aes_encrypt(&ctx_, in, in_len, out, out_max);
    }

    u32 decrypt(const u8* in, u32 in_len, u8* out, u32 out_max) {
        return aes_decrypt(&ctx_, in, in_len, out, out_max);
    }

    static u32 ciphertext_len(u32 plaintext_len) {
        return aes_ciphertext_len(plaintext_len);
    }

private:
    aes_ctx ctx_{};
};

/* ---- Key ---- */
class Key {
public:
    Key() { key_init(&k_); }
    explicit Key(const u8* data, u32 len) { key_from_bytes(&k_, data, len); }
    ~Key() = default;

    void generate(u32 len) { key_generate(&k_, len); }

    void derive(const char* password, const u8* salt, u32 salt_len,
                u32 key_len, u32 iterations = 10000) {
        key_derive(&k_, password, salt, salt_len, key_len, iterations);
    }

    const u8* data() const { return k_.data; }
    u32       size() const { return k_.len; }

    void to_hex(char* hex, u32 hex_max) const { key_to_hex(&k_, hex, hex_max); }

    bool operator==(const Key& o) const { return key_verify(&k_, &o.k_) != 0; }
    bool operator!=(const Key& o) const { return !(*this == o); }

private:
    crypto_key k_;
};

/* ---- Utility functions ---- */
inline void sha256_hex(const u8* data, u32 len, char hex[65]) {
    crypto_sha256_hex(data, len, hex);
}

inline void md5_hex(const u8* data, u32 len, char hex[33]) {
    crypto_md5_hex(data, len, hex);
}

inline u32 base64_encode(const u8* data, u32 len, char* out, u32 out_max) {
    return crypto_base64_encode(data, len, out, out_max);
}

inline u32 base64_decode(const char* in, u32 in_len, u8* out, u32 out_max) {
    return crypto_base64_decode(in, in_len, out, out_max);
}

inline void bytes_to_hex(const u8* data, u32 len, char* hex, u32 hex_max) {
    crypto_bytes_to_hex(data, len, hex, hex_max);
}

inline u32 hex_to_bytes(const char* hex, u8* bytes, u32 bytes_max) {
    return crypto_hex_to_bytes(hex, bytes, bytes_max);
}

} // namespace kernel::crypto
