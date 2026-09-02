#ifndef CRYPTO_UTIL_H
#define CRYPTO_UTIL_H

#include "crypto_types.h"
#include "crypto_hash.h"
#include "crypto_cipher.h"
#include "crypto_key.h"

#ifdef __cplusplus
extern "C" {
#endif

void crypto_sha256_hex(const u8 *data, u32 len, char hex[65]);
void crypto_md5_hex(const u8 *data, u32 len, char hex[33]);

u32 crypto_aes_encrypt(const u8 *key, u32 key_len,
                       const u8 *iv, u32 iv_len,
                       const u8 *in, u32 in_len,
                       u8 *out, u32 out_max);

u32 crypto_aes_decrypt(const u8 *key, u32 key_len,
                       const u8 *iv, u32 iv_len,
                       const u8 *in, u32 in_len,
                       u8 *out, u32 out_max);

u32 crypto_base64_encode(const u8 *data, u32 len, char *out, u32 out_max);
u32 crypto_base64_decode(const char *in, u32 in_len, u8 *out, u32 out_max);

void crypto_bytes_to_hex(const u8 *data, u32 len, char *hex, u32 hex_max);
u32  crypto_hex_to_bytes(const char *hex, u8 *bytes, u32 bytes_max);

void crypto_print_digest(const u8 *digest, u32 len);

#ifdef __cplusplus
}
#endif

#endif
