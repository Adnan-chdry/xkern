#include "crypto_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "string.h"

extern void *memcpy(void *, const void *, unsigned int);
extern void *memset(void *, int, unsigned int);
extern void *memmove(void *, const void *, unsigned int);
extern int   memcmp(const void *, const void *, unsigned int);


static const u32 sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static u32 rotr(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }
static u32 ch(u32 x, u32 y, u32 z) { return (x & y) ^ (~x & z); }
static u32 maj(u32 x, u32 y, u32 z) { return (x & y) ^ (x & z) ^ (y & z); }
static u32 bsig0(u32 x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
static u32 bsig1(u32 x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
static u32 ssig0(u32 x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
static u32 ssig1(u32 x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

static u32 read_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static void write_be32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);  p[3] = (u8)(v);
}

static void sha256_transform(sha256_ctx *ctx, const u8 block[64]) {
    u32 w[64];
    u32 a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = read_be32(block + i * 4);
    for (i = 16; i < 64; i++)
        w[i] = ssig1(w[i-2]) + w[i-7] + ssig0(w[i-15]) + w[i-16];

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + bsig1(e) + ch(e, f, g) + sha256_k[i] + w[i];
        t2 = bsig0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sha256_update(sha256_ctx *ctx, const u8 *data, u32 len) {
    u32 fill = 64 - ctx->buf_len;
    ctx->total_len += len;

    if (ctx->buf_len && len >= fill) {
        memcpy(ctx->buf + ctx->buf_len, data, fill);
        sha256_transform(ctx, ctx->buf);
        data += fill;
        len -= fill;
        ctx->buf_len = 0;
    }

    while (len >= 64) {
        sha256_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    if (len) {
        memcpy(ctx->buf + ctx->buf_len, data, len);
        ctx->buf_len += len;
    }
}

void sha256_final(sha256_ctx *ctx, u8 digest[SHA256_DIGEST_SIZE]) {
    u8 pad[64];
    u32 plen = ctx->buf_len;
    int i;

    pad[0] = 0x80;
    memset(pad + 1, 0, 63);

    sha256_update(ctx, pad, (plen < 56) ? (56 - plen) : (120 - plen));

    u8 lenpad[8];
    u64 bits = ctx->total_len * 8;
    for (i = 7; i >= 0; i--) {
        lenpad[i] = (u8)(bits & 0xff);
        bits >>= 8;
    }
    sha256_update(ctx, lenpad, 8);

    for (i = 0; i < 8; i++)
        write_be32(digest + i * 4, ctx->state[i]);
}

void sha256_hash(const u8 *data, u32 len, u8 digest[SHA256_DIGEST_SIZE]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* --- MD5 --- */

static const u32 md5_t[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const int md5_s[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
};

static u32 read_le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void write_le32(u8 *p, u32 v) {
    p[0] = (u8)(v); p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static void md5_transform(md5_ctx *ctx, const u8 block[64]) {
    u32 w[16];
    u32 a, b, c, d, f, g, temp;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = read_le32(block + i * 4);

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];

    for (i = 0; i < 64; i++) {
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5*i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3*i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7*i) % 16;
        }

        temp = d;
        d = c;
        c = b;
        b = b + ((a + f + md5_t[i] + w[g]) << md5_s[i] | (a + f + md5_t[i] + w[g]) >> (32 - md5_s[i]));
        a = temp;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
}

void md5_init(md5_ctx *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void md5_update(md5_ctx *ctx, const u8 *data, u32 len) {
    u32 fill = 64 - ctx->buf_len;
    ctx->total_len += len;

    if (ctx->buf_len && len >= fill) {
        memcpy(ctx->buf + ctx->buf_len, data, fill);
        md5_transform(ctx, ctx->buf);
        data += fill;
        len -= fill;
        ctx->buf_len = 0;
    }

    while (len >= 64) {
        md5_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    if (len) {
        memcpy(ctx->buf + ctx->buf_len, data, len);
        ctx->buf_len += len;
    }
}

void md5_final(md5_ctx *ctx, u8 digest[MD5_DIGEST_SIZE]) {
    u8 pad[64];
    u32 plen = ctx->buf_len;
    int i;

    pad[0] = 0x80;
    memset(pad + 1, 0, 63);

    md5_update(ctx, pad, (plen < 56) ? (56 - plen) : (120 - plen));

    u8 lenpad[8];
    u64 bits = ctx->total_len * 8;
    for (i = 0; i < 8; i++) {
        lenpad[i] = (u8)(bits & 0xff);
        bits >>= 8;
    }
    md5_update(ctx, lenpad, 8);

    for (i = 0; i < 4; i++)
        write_le32(digest + i * 4, ctx->state[i]);
}

void md5_hash(const u8 *data, u32 len, u8 digest[MD5_DIGEST_SIZE]) {
    md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
}

/* --- SHA-1 --- */

static void sha1_transform(sha1_ctx *ctx, const u8 block[64]) {
    u32 w[80];
    u32 a, b, c, d, e, temp;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = read_be32(block + i * 4);
    for (i = 16; i < 80; i++)
        w[i] = rotr(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3]; e = ctx->state[4];

    for (i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }

        temp = rotr(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotr(b, 30); b = a; a = temp;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d; ctx->state[4] += e;
}

void sha1_init(sha1_ctx *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sha1_update(sha1_ctx *ctx, const u8 *data, u32 len) {
    u32 fill = 64 - ctx->buf_len;
    ctx->total_len += len;

    if (ctx->buf_len && len >= fill) {
        memcpy(ctx->buf + ctx->buf_len, data, fill);
        sha1_transform(ctx, ctx->buf);
        data += fill;
        len -= fill;
        ctx->buf_len = 0;
    }

    while (len >= 64) {
        sha1_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    if (len) {
        memcpy(ctx->buf + ctx->buf_len, data, len);
        ctx->buf_len += len;
    }
}

void sha1_final(sha1_ctx *ctx, u8 digest[SHA1_DIGEST_SIZE]) {
    u8 pad[64];
    u32 plen = ctx->buf_len;
    int i;

    pad[0] = 0x80;
    memset(pad + 1, 0, 63);

    sha1_update(ctx, pad, (plen < 56) ? (56 - plen) : (120 - plen));

    u8 lenpad[8];
    u64 bits = ctx->total_len * 8;
    for (i = 7; i >= 0; i--) {
        lenpad[i] = (u8)(bits & 0xff);
        bits >>= 8;
    }
    sha1_update(ctx, lenpad, 8);

    for (i = 0; i < 5; i++)
        write_be32(digest + i * 4, ctx->state[i]);
}

void sha1_hash(const u8 *data, u32 len, u8 digest[SHA1_DIGEST_SIZE]) {
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}

/* --- HMAC-SHA256 --- */

void hmac_sha256(const u8 *key, u32 key_len,
                 const u8 *data, u32 data_len,
                 u8 mac[SHA256_DIGEST_SIZE]) {
    u8 k_pad[64];
    u8 k_hash[SHA256_DIGEST_SIZE];
    u8 o_pad[64], i_pad[64];
    sha256_ctx hctx;
    int i;

    if (key_len > 64) {
        sha256_hash(key, key_len, k_hash);
        key = k_hash;
        key_len = SHA256_DIGEST_SIZE;
    }

    memset(k_pad, 0, 64);
    memcpy(k_pad, key, key_len);

    for (i = 0; i < 64; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }

    sha256_init(&hctx);
    sha256_update(&hctx, i_pad, 64);
    sha256_update(&hctx, data, data_len);
    sha256_final(&hctx, mac);

    sha256_init(&hctx);
    sha256_update(&hctx, o_pad, 64);
    sha256_update(&hctx, mac, SHA256_DIGEST_SIZE);
    sha256_final(&hctx, mac);
}

#ifdef __cplusplus
}
#endif
