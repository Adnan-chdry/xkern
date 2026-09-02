#include "crypto_key.h"
#include "crypto_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "string.h"

extern void *memcpy(void *, const void *, unsigned int);
extern void *memset(void *, int, unsigned int);
extern int   memcmp(const void *, const void *, unsigned int);
extern unsigned int strlen(const char *);


static u32 simple_rng_state = 0x12345678;

static u32 simple_rng(void) {
    simple_rng_state ^= simple_rng_state << 13;
    simple_rng_state ^= simple_rng_state >> 17;
    simple_rng_state ^= simple_rng_state << 5;
    return simple_rng_state;
}

void key_init(crypto_key *k) {
    memset(k->data, 0, AES_256_KEY_SIZE);
    k->len = 0;
}

void key_from_bytes(crypto_key *k, const u8 *data, u32 len) {
    if (len > AES_256_KEY_SIZE) len = AES_256_KEY_SIZE;
    memcpy(k->data, data, len);
    k->len = len;
}

void key_generate(crypto_key *k, u32 len) {
    u32 i;
    if (len > AES_256_KEY_SIZE) len = AES_256_KEY_SIZE;
    for (i = 0; i < len; i++)
        k->data[i] = (u8)(simple_rng() ^ (i * 0x37 + 0xAB));
    k->len = len;
}

void key_derive(crypto_key *k, const char *password,
                const u8 *salt, u32 salt_len,
                u32 key_len, u32 iterations) {
    u8 block[AES_256_KEY_SIZE];
    u32 i, j;
    u32 pass_len = strlen(password);

    if (key_len > AES_256_KEY_SIZE) key_len = AES_256_KEY_SIZE;

    for (i = 0; i < key_len; i++) {
        u8 u = (u8)(i + 1);
        u8 accum[SHA256_DIGEST_SIZE];
        u8 tmp[SHA256_DIGEST_SIZE];

        hmac_sha256((const u8 *)password, pass_len, &u, 1, accum);
        hmac_sha256((const u8 *)password, pass_len, accum, SHA256_DIGEST_SIZE, tmp);
        memcpy(accum, tmp, SHA256_DIGEST_SIZE);

        for (j = 1; j < iterations; j++) {
            u8 prev[SHA256_DIGEST_SIZE];
            hmac_sha256((const u8 *)password, pass_len, accum, SHA256_DIGEST_SIZE, prev);
            int k2;
            for (k2 = 0; k2 < SHA256_DIGEST_SIZE; k2++)
                accum[k2] ^= prev[k2];
        }

        block[i] = accum[0] ^ accum[1] ^ accum[2] ^ accum[3] ^
                   accum[4] ^ accum[5] ^ accum[6] ^ accum[7] ^
                   accum[8] ^ accum[9] ^ accum[10] ^ accum[11] ^
                   accum[12] ^ accum[13] ^ accum[14] ^ accum[15] ^
                   accum[16] ^ accum[17] ^ accum[18] ^ accum[19] ^
                   accum[20] ^ accum[21] ^ accum[22] ^ accum[23] ^
                   accum[24] ^ accum[25] ^ accum[26] ^ accum[27] ^
                   accum[28] ^ accum[29] ^ accum[30] ^ accum[31];
    }

    memcpy(k->data, block, key_len);
    k->len = key_len;
}

int key_verify(const crypto_key *a, const crypto_key *b) {
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, a->len) == 0;
}

void key_to_hex(const crypto_key *k, char *hex, u32 hex_max) {
    static const char hextab[] = "0123456789abcdef";
    u32 i;
    u32 max_bytes = k->len;
    if (max_bytes * 2 + 1 > hex_max) max_bytes = (hex_max - 1) / 2;

    for (i = 0; i < max_bytes; i++) {
        hex[i*2]   = hextab[(k->data[i] >> 4) & 0x0f];
        hex[i*2+1] = hextab[k->data[i] & 0x0f];
    }
    hex[max_bytes * 2] = '\0';
}

#ifdef __cplusplus
}
#endif
