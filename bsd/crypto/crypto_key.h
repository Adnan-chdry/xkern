#ifndef CRYPTO_KEY_H
#define CRYPTO_KEY_H

#include "crypto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u8  data[AES_256_KEY_SIZE];
    u32 len;
} crypto_key;

void key_init(crypto_key *k);
void key_from_bytes(crypto_key *k, const u8 *data, u32 len);
void key_generate(crypto_key *k, u32 len);
void key_derive(crypto_key *k, const char *password,
                const u8 *salt, u32 salt_len,
                u32 key_len, u32 iterations);
int  key_verify(const crypto_key *a, const crypto_key *b);

void key_to_hex(const crypto_key *k, char *hex, u32 hex_max);

#ifdef __cplusplus
}
#endif

#endif
