#ifndef CRYPTO_TYPES_H
#define CRYPTO_TYPES_H

#include "types.h"

#define CRYPTO_SUCCESS     0
#define CRYPTO_ERR_INPUT   1
#define CRYPTO_ERR_KEY     2
#define CRYPTO_ERR_IV      3
#define CRYPTO_ERR_BUF     4
#define CRYPTO_ERR_INIT    5
#define CRYPTO_ERR_FAIL    6

#define HASH_SHA256  0
#define HASH_MD5     1
#define HASH_SHA1    2

#define CIPHER_AES_128  0
#define CIPHER_AES_256  1
#define CIPHER_DES      2

#define MODE_ECB  0
#define MODE_CBC  1
#define MODE_CTR  2

#define SHA256_DIGEST_SIZE  32
#define MD5_DIGEST_SIZE     16
#define SHA1_DIGEST_SIZE    20
#define AES_BLOCK_SIZE      16
#define AES_128_KEY_SIZE    16
#define AES_256_KEY_SIZE    32
#define DES_BLOCK_SIZE      8
#define DES_KEY_SIZE        8
#define HMAC_MAX_BLOCK      128

#endif
