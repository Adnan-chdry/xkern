#ifndef CRYPTO_CIPHER_H
#define CRYPTO_CIPHER_H

#include "crypto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 rk[60];
    int nr;
    int mode;
    u8 iv[AES_BLOCK_SIZE];
} aes_ctx;

void aes_init(aes_ctx *ctx, int algorithm, int mode,
              const u8 *key, u32 key_len,
              const u8 *iv, u32 iv_len);

u32 aes_encrypt(aes_ctx *ctx,
                const u8 *in, u32 in_len,
                u8 *out, u32 out_max);

u32 aes_decrypt(aes_ctx *ctx,
                const u8 *in, u32 in_len,
                u8 *out, u32 out_max);

u32 aes_ciphertext_len(u32 plaintext_len);

#ifdef __cplusplus
}
#endif

#endif
