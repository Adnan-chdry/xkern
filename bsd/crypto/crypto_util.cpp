#include "crypto_util.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "string.h"
#include "stdio.h"

extern void *memcpy(void *, const void *, unsigned int);
extern void *memset(void *, int, unsigned int);
extern int   memcmp(const void *, const void *, unsigned int);
extern unsigned int strlen(const char *);


static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void crypto_sha256_hex(const u8 *data, u32 len, char hex[65]) {
    u8 digest[SHA256_DIGEST_SIZE];
    sha256_hash(data, len, digest);
    crypto_bytes_to_hex(digest, SHA256_DIGEST_SIZE, hex, 65);
}

void crypto_md5_hex(const u8 *data, u32 len, char hex[33]) {
    u8 digest[MD5_DIGEST_SIZE];
    md5_hash(data, len, digest);
    crypto_bytes_to_hex(digest, MD5_DIGEST_SIZE, hex, 33);
}

u32 crypto_aes_encrypt(const u8 *key, u32 key_len,
                       const u8 *iv, u32 iv_len,
                       const u8 *in, u32 in_len,
                       u8 *out, u32 out_max) {
    aes_ctx ctx;
    int algo = (key_len >= AES_256_KEY_SIZE) ? CIPHER_AES_256 : CIPHER_AES_128;
    aes_init(&ctx, algo, MODE_CBC, key, key_len, iv, iv_len);
    return aes_encrypt(&ctx, in, in_len, out, out_max);
}

u32 crypto_aes_decrypt(const u8 *key, u32 key_len,
                       const u8 *iv, u32 iv_len,
                       const u8 *in, u32 in_len,
                       u8 *out, u32 out_max) {
    aes_ctx ctx;
    int algo = (key_len >= AES_256_KEY_SIZE) ? CIPHER_AES_256 : CIPHER_AES_128;
    aes_init(&ctx, algo, MODE_CBC, key, key_len, iv, iv_len);
    return aes_decrypt(&ctx, in, in_len, out, out_max);
}

u32 crypto_base64_encode(const u8 *data, u32 len, char *out, u32 out_max) {
    u32 i, j = 0;
    u32 needed = ((len + 2) / 3) * 4 + 1;
    if (out_max < needed) return 0;

    for (i = 0; i < len; i += 3) {
        u32 a = data[i];
        u32 b = (i + 1 < len) ? data[i + 1] : 0;
        u32 c = (i + 2 < len) ? data[i + 2] : 0;
        u32 triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return j;
}

static int b64_dec(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

u32 crypto_base64_decode(const char *in, u32 in_len, u8 *out, u32 out_max) {
    u32 i, j = 0;
    u32 pad = 0;

    if (in_len >= 2 && in[in_len - 1] == '=') pad++;
    if (in_len >= 2 && in[in_len - 2] == '=') pad++;

    u32 needed = (in_len / 4) * 3 - pad;
    if (out_max < needed) return 0;

    for (i = 0; i < in_len; i += 4) {
        int a = b64_dec(in[i]);
        int b = (i + 1 < in_len) ? b64_dec(in[i + 1]) : 0;
        int c = (i + 2 < in_len && in[i + 2] != '=') ? b64_dec(in[i + 2]) : -1;
        int d = (i + 3 < in_len && in[i + 3] != '=') ? b64_dec(in[i + 3]) : -1;

        if (a < 0) a = 0;
        if (b < 0) b = 0;

        u32 triple = ((u32)a << 18) | ((u32)b << 12);
        if (c >= 0) triple |= ((u32)c << 6);
        if (d >= 0) triple |= (u32)d;

        if (j < out_max) out[j++] = (u8)(triple >> 16);
        if (j < out_max && in[i + 2] != '=') out[j++] = (u8)(triple >> 8);
        if (j < out_max && in[i + 3] != '=') out[j++] = (u8)(triple);
    }
    return j;
}

void crypto_bytes_to_hex(const u8 *data, u32 len, char *hex, u32 hex_max) {
    static const char hextab[] = "0123456789abcdef";
    u32 i;
    u32 max_bytes = len;
    if (max_bytes * 2 + 1 > hex_max) max_bytes = (hex_max - 1) / 2;

    for (i = 0; i < max_bytes; i++) {
        hex[i * 2]     = hextab[(data[i] >> 4) & 0x0f];
        hex[i * 2 + 1] = hextab[data[i] & 0x0f];
    }
    hex[max_bytes * 2] = '\0';
}

u32 crypto_hex_to_bytes(const char *hex, u8 *bytes, u32 bytes_max) {
    u32 len = strlen(hex);
    u32 i, count = 0;
    if (len % 2 != 0) return 0;

    for (i = 0; i < len && count < bytes_max; i += 2) {
        u8 hi, lo;
        if (hex[i] >= '0' && hex[i] <= '9') hi = hex[i] - '0';
        else if (hex[i] >= 'a' && hex[i] <= 'f') hi = hex[i] - 'a' + 10;
        else if (hex[i] >= 'A' && hex[i] <= 'F') hi = hex[i] - 'A' + 10;
        else return 0;

        if (hex[i+1] >= '0' && hex[i+1] <= '9') lo = hex[i+1] - '0';
        else if (hex[i+1] >= 'a' && hex[i+1] <= 'f') lo = hex[i+1] - 'a' + 10;
        else if (hex[i+1] >= 'A' && hex[i+1] <= 'F') lo = hex[i+1] - 'A' + 10;
        else return 0;

        bytes[count++] = (hi << 4) | lo;
    }
    return count;
}

void crypto_print_digest(const u8 *digest, u32 len) {
    char hex[65];
    crypto_bytes_to_hex(digest, len, hex, sizeof(hex));
    printf("%s", hex);
}

#ifdef __cplusplus
}
#endif
