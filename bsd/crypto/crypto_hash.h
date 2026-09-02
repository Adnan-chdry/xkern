#ifndef CRYPTO_HASH_H
#define CRYPTO_HASH_H

#include "crypto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 state[8];
    u64 total_len;
    u8  buf[64];
    u32 buf_len;
} sha256_ctx;

typedef struct {
    u32 state[4];
    u64 total_len;
    u8  buf[64];
    u32 buf_len;
} md5_ctx;

typedef struct {
    u32 state[5];
    u64 total_len;
    u8  buf[64];
    u32 buf_len;
} sha1_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const u8 *data, u32 len);
void sha256_final(sha256_ctx *ctx, u8 digest[SHA256_DIGEST_SIZE]);
void sha256_hash(const u8 *data, u32 len, u8 digest[SHA256_DIGEST_SIZE]);

void md5_init(md5_ctx *ctx);
void md5_update(md5_ctx *ctx, const u8 *data, u32 len);
void md5_final(md5_ctx *ctx, u8 digest[MD5_DIGEST_SIZE]);
void md5_hash(const u8 *data, u32 len, u8 digest[MD5_DIGEST_SIZE]);

void sha1_init(sha1_ctx *ctx);
void sha1_update(sha1_ctx *ctx, const u8 *data, u32 len);
void sha1_final(sha1_ctx *ctx, u8 digest[SHA1_DIGEST_SIZE]);
void sha1_hash(const u8 *data, u32 len, u8 digest[SHA1_DIGEST_SIZE]);

void hmac_sha256(const u8 *key, u32 key_len,
                 const u8 *data, u32 data_len,
                 u8 mac[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
