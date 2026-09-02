# Kernel-included objects (used by root Makefile via 'include')
CRYPTO_OBJS = bsd/crypto/crypto_hash.o \
              bsd/crypto/crypto_cipher.o \
              bsd/crypto/crypto_key.o \
              bsd/crypto/crypto_util.o

.PHONY: clean-crypto

clean-crypto:
	rm -f bsd/crypto/*.o
