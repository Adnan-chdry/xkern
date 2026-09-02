#include "crypto_cipher.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "string.h"

extern void *memcpy(void *, const void *, unsigned int);
extern void *memset(void *, int, unsigned int);
extern void *memmove(void *, const void *, unsigned int);
extern int   memcmp(const void *, const void *, unsigned int);


static const u8 sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const u8 rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static u8 xtime(u8 x) { return (x << 1) ^ (((x >> 7) & 1) * 0x1b); }

static u8 xmul(u8 a, u8 b) {
    u8 p = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return p;
}

static void add_round_key(u8 state[16], const u32 *rk, int round) {
    int i;
    for (i = 0; i < 4; i++) {
        state[i*4+0] ^= (u8)(rk[round*4+i] >> 24);
        state[i*4+1] ^= (u8)(rk[round*4+i] >> 16);
        state[i*4+2] ^= (u8)(rk[round*4+i] >> 8);
        state[i*4+3] ^= (u8)(rk[round*4+i]);
    }
}

static void sub_bytes(u8 state[16]) {
    int i;
    for (i = 0; i < 16; i++) state[i] = sbox[state[i]];
}

static void shift_rows(u8 s[16]) {
    u8 t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void mix_columns(u8 s[16]) {
    int c;
    for (c = 0; c < 4; c++) {
        int i = c * 4;
        u8 a0 = s[i], a1 = s[i+1], a2 = s[i+2], a3 = s[i+3];
        u8 t = a0 ^ a1 ^ a2 ^ a3;
        s[i+0] ^= xtime(a0 ^ a1) ^ t;
        s[i+1] ^= xtime(a1 ^ a2) ^ t;
        s[i+2] ^= xtime(a2 ^ a3) ^ t;
        s[i+3] ^= xtime(a3 ^ a0) ^ t;
    }
}

static void sub_bytes_inv(u8 state[16]) {
    static const u8 inv_sbox[256] = {
        0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
        0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
        0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
        0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
        0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
        0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
        0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
        0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
        0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
        0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
        0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
        0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
        0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
        0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
        0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
        0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
    };
    int i;
    for (i = 0; i < 16; i++) state[i] = inv_sbox[state[i]];
}

static void shift_rows_inv(u8 s[16]) {
    u8 t;
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

static void mix_columns_inv(u8 s[16]) {
    int c;
    for (c = 0; c < 4; c++) {
        int i = c * 4;
        u8 a0 = s[i], a1 = s[i+1], a2 = s[i+2], a3 = s[i+3];
        s[i+0] = xmul(a0,0x0e) ^ xmul(a1,0x0b) ^ xmul(a2,0x0d) ^ xmul(a3,0x09);
        s[i+1] = xmul(a0,0x09) ^ xmul(a1,0x0e) ^ xmul(a2,0x0b) ^ xmul(a3,0x0d);
        s[i+2] = xmul(a0,0x0d) ^ xmul(a1,0x09) ^ xmul(a2,0x0e) ^ xmul(a3,0x0b);
        s[i+3] = xmul(a0,0x0b) ^ xmul(a1,0x0d) ^ xmul(a2,0x09) ^ xmul(a3,0x0e);
    }
}

static void key_expand(const u8 *key, u32 key_len, u32 *rk, int *nr) {
    int nk = key_len / 4;
    int i, j;
    u32 temp;

    *nr = nk + 6;

    for (i = 0; i < nk; i++) {
        rk[i] = ((u32)key[4*i] << 24) | ((u32)key[4*i+1] << 16) |
                ((u32)key[4*i+2] << 8) | (u32)key[4*i+3];
    }

    for (i = nk; i < 4 * (*nr + 1); i++) {
        temp = rk[i-1];
        if (i % nk == 0) {
            temp = ((u32)sbox[(temp >> 16) & 0xff] << 24) |
                   ((u32)sbox[(temp >> 8) & 0xff] << 16) |
                   ((u32)sbox[temp & 0xff] << 8) |
                   (u32)sbox[(temp >> 24) & 0xff];
            temp ^= (u32)rcon[i/nk] << 24;
        } else if (nk > 6 && i % nk == 4) {
            temp = ((u32)sbox[(temp >> 24) & 0xff] << 24) |
                   ((u32)sbox[(temp >> 16) & 0xff] << 16) |
                   ((u32)sbox[(temp >> 8) & 0xff] << 8) |
                   (u32)sbox[temp & 0xff];
        }
        rk[i] = rk[i-nk] ^ temp;
    }
}

void aes_init(aes_ctx *ctx, int algorithm, int mode,
              const u8 *key, u32 key_len,
              const u8 *iv, u32 iv_len) {
    ctx->mode = mode;
    key_expand(key, key_len, ctx->rk, &ctx->nr);
    if (iv && iv_len >= AES_BLOCK_SIZE)
        memcpy(ctx->iv, iv, AES_BLOCK_SIZE);
    else
        memset(ctx->iv, 0, AES_BLOCK_SIZE);
}

static void aes_block_encrypt(const u32 *rk, int nr, const u8 in[16], u8 out[16]) {
    u8 state[16];
    int i;
    memcpy(state, in, 16);

    add_round_key(state, rk, 0);
    for (i = 1; i < nr; i++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, rk, i);
    }
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, rk, nr);

    memcpy(out, state, 16);
}

static void aes_block_decrypt(const u32 *rk, int nr, const u8 in[16], u8 out[16]) {
    u8 state[16];
    int i;
    memcpy(state, in, 16);

    add_round_key(state, rk, nr);
    for (i = nr - 1; i > 0; i--) {
        shift_rows_inv(state);
        sub_bytes_inv(state);
        add_round_key(state, rk, i);
        mix_columns_inv(state);
    }
    shift_rows_inv(state);
    sub_bytes_inv(state);
    add_round_key(state, rk, 0);

    memcpy(out, state, 16);
}

static void xor_block(u8 a[16], const u8 b[16]) {
    int i;
    for (i = 0; i < 16; i++) a[i] ^= b[i];
}

u32 aes_ciphertext_len(u32 plaintext_len) {
    return ((plaintext_len / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;
}

u32 aes_encrypt(aes_ctx *ctx,
                const u8 *in, u32 in_len,
                u8 *out, u32 out_max) {
    u8 block[16], prev[16], counter[16];
    u32 total = aes_ciphertext_len(in_len);
    u32 off = 0;
    int pad_val, i;

    if (out_max < total) return 0;

    if (ctx->mode == MODE_ECB) {
        u8 pad_buf[16];
        pad_val = AES_BLOCK_SIZE - (in_len % AES_BLOCK_SIZE);
        memcpy(pad_buf + (AES_BLOCK_SIZE - pad_val), in + off, in_len % AES_BLOCK_SIZE);
        memset(pad_buf, 0, AES_BLOCK_SIZE - pad_val);
        for (i = AES_BLOCK_SIZE - pad_val; i < AES_BLOCK_SIZE; i++)
            pad_buf[i] = in[off + (i - (AES_BLOCK_SIZE - pad_val))];
        for (i = 0; i < AES_BLOCK_SIZE - (in_len % AES_BLOCK_SIZE); i++)
            pad_buf[AES_BLOCK_SIZE - pad_val + i] = (u8)pad_val;

        while (off + AES_BLOCK_SIZE <= in_len) {
            aes_block_encrypt(ctx->rk, ctx->nr, in + off, out + off);
            off += AES_BLOCK_SIZE;
        }
        aes_block_encrypt(ctx->rk, ctx->nr, pad_buf, out + off);
        return total;
    }

    if (ctx->mode == MODE_CBC) {
        memcpy(prev, ctx->iv, 16);
        pad_val = AES_BLOCK_SIZE - (in_len % AES_BLOCK_SIZE);

        while (off + AES_BLOCK_SIZE <= in_len) {
            memcpy(block, in + off, 16);
            xor_block(block, prev);
            aes_block_encrypt(ctx->rk, ctx->nr, block, out + off);
            memcpy(prev, out + off, 16);
            off += AES_BLOCK_SIZE;
        }

        memset(block, 0, 16);
        memcpy(block, in + off, in_len % AES_BLOCK_SIZE);
        for (i = 0; i < pad_val; i++)
            block[(in_len % AES_BLOCK_SIZE) + i] = (u8)pad_val;
        xor_block(block, prev);
        aes_block_encrypt(ctx->rk, ctx->nr, block, out + off);
        return total;
    }

    if (ctx->mode == MODE_CTR) {
        memcpy(counter, ctx->iv, 16);
        while (off < in_len) {
            u8 keystream[16];
            aes_block_encrypt(ctx->rk, ctx->nr, counter, keystream);
            u32 chunk = (in_len - off > 16) ? 16 : (in_len - off);
            for (i = 0; i < (int)chunk; i++)
                out[off + i] = in[off + i] ^ keystream[i];
            off += chunk;
            for (i = 15; i >= 0; i--) {
                if (++counter[i]) break;
            }
        }
        return in_len;
    }

    return 0;
}

u32 aes_decrypt(aes_ctx *ctx,
                const u8 *in, u32 in_len,
                u8 *out, u32 out_max) {
    u8 block[16], prev[16], counter[16];
    u32 off = 0;
    int i;

    if (in_len == 0 || in_len % AES_BLOCK_SIZE != 0) return 0;

    if (ctx->mode == MODE_ECB) {
        while (off < in_len) {
            aes_block_decrypt(ctx->rk, ctx->nr, in + off, out + off);
            off += AES_BLOCK_SIZE;
        }
        if (off > 0) {
            u8 pad_val = out[off - 1];
            if (pad_val > 0 && pad_val <= AES_BLOCK_SIZE) {
                off -= pad_val;
            }
        }
        return off;
    }

    if (ctx->mode == MODE_CBC) {
        memcpy(prev, ctx->iv, 16);
        while (off < in_len) {
            aes_block_decrypt(ctx->rk, ctx->nr, in + off, block);
            xor_block(block, prev);
            memcpy(out + off, block, 16);
            memcpy(prev, in + off, 16);
            off += AES_BLOCK_SIZE;
        }
        if (off > 0) {
            u8 pad_val = out[off - 1];
            if (pad_val > 0 && pad_val <= AES_BLOCK_SIZE) {
                off -= pad_val;
            }
        }
        return off;
    }

    if (ctx->mode == MODE_CTR) {
        memcpy(counter, ctx->iv, 16);
        while (off < in_len) {
            u8 keystream[16];
            aes_block_encrypt(ctx->rk, ctx->nr, counter, keystream);
            u32 chunk = (in_len - off > 16) ? 16 : (in_len - off);
            for (i = 0; i < (int)chunk; i++)
                out[off + i] = in[off + i] ^ keystream[i];
            off += chunk;
            for (i = 15; i >= 0; i--) {
                if (++counter[i]) break;
            }
        }
        return in_len;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif
