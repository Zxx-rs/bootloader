/**
 * @file    sha256.h
 * @brief   Minimal SHA-256 implementation for embedded firmware verification.
 *
 * Implements only the init/update/final streaming API needed to hash firmware
 * read from SPI Flash in chunks. No dynamic allocation, no stdlib dependency.
 *
 * Typical usage:
 *   sha256_ctx_t ctx;
 *   sha256_init(&ctx);
 *   while (remaining > 0) {
 *       read_chunk(buf, chunk);
 *       sha256_update(&ctx, buf, chunk);
 *   }
 *   sha256_final(&ctx, digest);  // digest[32]
 */

#ifndef __SHA256_H__
#define __SHA256_H__

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint8_t  buf[SHA256_BLOCK_SIZE]; /* pending bytes before next block */
    uint32_t state[8];               /* H[0..7] intermediate hash state   */
    uint32_t count[2];               /* total bits processed (lo, hi)     */
    uint8_t  index;                  /* bytes buffered in buf (0..63)     */
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

#endif /* __SHA256_H__ */
