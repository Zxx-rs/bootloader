/**
 * @file    sha256.c
 * @brief   Minimal SHA-256 — FIPS 180-4 compliant, embedded-optimized.
 *
 * Features omitted intentionally:
 *   - No HMAC           (not needed for firmware signing)
 *   - No SHA-224        (P-256 uses SHA-256 directly)
 *   - No hardware accel  (software is ~100 ms for 448 KB on 168 MHz M4)
 *
 * Implementation notes:
 *   - Big-endian byte order per FIPS; uint32 reads are byteswapped on LE hosts.
 *   - Uses only 32-bit unsigned arithmetic (no uint64).
 *   - rotr / sigma / Sigma / Ch / Maj expanded inline for speed.
 */

#include "sha256.h"
#include <string.h>

/* ── Right-rotate helpers ── */
#define ROTR32(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))

/* ── Logical functions (FIPS 180-4 §4.1.2) ── */
#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define BSIG0(x)  (ROTR32(x,  2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define BSIG1(x)  (ROTR32(x,  6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define SSIG0(x)  (ROTR32(x,  7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define SSIG1(x)  (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

/* ── Round constants (first 32 bits of fractional parts of cube roots of first 64 primes) ── */
static const uint32_t K[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

/* ── Read big-endian uint32 from byte buffer ── */
static inline uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
           ((uint32_t)p[3]);
}

/* ── Write uint32 as big-endian to byte buffer ── */
static inline void write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v);
}

/* ── Core compression on one 512-bit block (64 bytes) ── */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[SHA256_BLOCK_SIZE])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    int i;

    /* Prepare message schedule W[0..15] from block */
    for (i = 0; i < 16; i++)
        W[i] = read_be32(&block[i * 4]);

    /* Extend to W[16..63] */
    for (i = 16; i < 64; i++)
        W[i] = SSIG1(W[i - 2]) + W[i - 7] + SSIG0(W[i - 15]) + W[i - 16];

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* 64 rounds */
    for (i = 0; i < 64; i++)
    {
        t1 = h + BSIG1(e) + CH(e, f, g) + K[i] + W[i];
        t2 = BSIG0(a) + MAJ(a, b, c);
        h  = g;
        g  = f;
        f  = e;
        e  = d + t1;
        d  = c;
        c  = b;
        b  = a;
        a  = t1 + t2;
    }

    /* Update state */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* ── Public API ── */

/**
 * @brief  Initialize SHA-256 context with standard IV (H[0..7]).
 */
void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6A09E667;
    ctx->state[1] = 0xBB67AE85;
    ctx->state[2] = 0x3C6EF372;
    ctx->state[3] = 0xA54FF53A;
    ctx->state[4] = 0x510E527F;
    ctx->state[5] = 0x9B05688C;
    ctx->state[6] = 0x1F83D9AB;
    ctx->state[7] = 0x5BE0CD19;

    ctx->count[0] = 0;
    ctx->count[1] = 0;
    ctx->index    = 0;
}

/**
 * @brief  Feed data into the hash. Call as many times as needed.
 */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    /* Update total bit count */
    uint32_t bits_lo = (uint32_t)(len << 3);
    ctx->count[0] += bits_lo;
    if (ctx->count[0] < bits_lo)
        ctx->count[1]++;

    for (i = 0; i < len; i++)
    {
        ctx->buf[ctx->index++] = data[i];
        if (ctx->index == SHA256_BLOCK_SIZE)
        {
            sha256_transform(ctx, ctx->buf);
            ctx->index = 0;
        }
    }
}

/**
 * @brief  Finalize hash and produce 32-byte digest.
 * @note   ctx is left in an undefined state; do not call update() after final().
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint32_t i;

    /* Append bit '1' (0x80 byte) */
    ctx->buf[ctx->index++] = 0x80;

    /* If not enough room for 8-byte length, pad current block and transform */
    if (ctx->index > SHA256_BLOCK_SIZE - 8)
    {
        memset(&ctx->buf[ctx->index], 0, SHA256_BLOCK_SIZE - ctx->index);
        sha256_transform(ctx, ctx->buf);
        ctx->index = 0;
    }

    /* Zero-fill remaining space before length */
    memset(&ctx->buf[ctx->index], 0, SHA256_BLOCK_SIZE - ctx->index - 8);

    /* Append total bit count as 64-bit big-endian */
    write_be32(&ctx->buf[56], ctx->count[1]);  /* hi */
    write_be32(&ctx->buf[60], ctx->count[0]);  /* lo */
    sha256_transform(ctx, ctx->buf);

    /* Emit digest */
    for (i = 0; i < 8; i++)
        write_be32(&digest[i * 4], ctx->state[i]);
}
