/**
 * @file    firmware_sign.c
 * @brief   Firmware signature verification — SHA-256 + ECDSA P-256.
 */

#include "firmware_sign.h"
#include "sha256.h"

#define uECC_PLATFORM uECC_arch_other
#define uECC_OPTIMIZATION_LEVEL 0
#include "uECC.h"
#include "bl_w25q128.h"
#include "bl_eeprom.h"
#include <string.h>

#define LOG_TAG    "fw_sign"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

/* ── ECDSA P-256 public key (64 bytes: X || Y), auto-generated ── */
static const uint8_t ECDSA_PUBLIC_KEY[64] = {
    0xfb, 0xf9, 0x79, 0x76, 0x61, 0x80, 0xd8, 0x15,
    0x49, 0x20, 0xd8, 0x39, 0x9a, 0x92, 0x5d, 0xc1,
    0xf1, 0x53, 0xbe, 0xa6, 0x56, 0x69, 0x33, 0x49,
    0x6b, 0x73, 0x4f, 0xb4, 0x34, 0xd3, 0xa3, 0xd9,
    0x8d, 0x06, 0x3e, 0x64, 0x26, 0xae, 0xfb, 0xeb,
    0xc5, 0x49, 0x5a, 0xdd, 0x61, 0x62, 0xcc, 0x75,
    0x75, 0x61, 0xb8, 0x01, 0x5b, 0xde, 0x05, 0x74,
    0xd3, 0x24, 0xbe, 0x01, 0x6e, 0x85, 0x4c, 0x7e,
};

#define FW_CHUNK_SIZE  4096

/* ──────────────────────────────────────────────
 * SHA-256 of firmware in W25Q128 B区 (streaming)
 * ────────────────────────────────────────────── */

static bool firmware_sha256(uint8_t digest[32], uint32_t fw_len, uint8_t *work_buf)
{
    sha256_ctx_t ctx;
    uint8_t *buf = work_buf;
    uint32_t remaining = fw_len;
    uint32_t offset = 0;

    sha256_init(&ctx);

    while (remaining > 0)
    {
        uint32_t chunk = FW_CHUNK_SIZE;
        if (chunk > remaining) chunk = remaining;

        bl_w25q128_read(offset, buf, chunk);
        sha256_update(&ctx, buf, chunk);

        remaining -= chunk;
        offset += chunk;
    }

    sha256_final(&ctx, digest);
    return true;
}

/* ──────────────────────────────────────────────
 * Signature Presence Check
 * ────────────────────────────────────────────── */

bool firmware_sign_is_present(const uint8_t signature[FIRMWARE_SIGN_SIZE])
{
    for (int i = 0; i < FIRMWARE_SIGN_SIZE; i++)
    {
        if (signature[i] != 0xFF) return true;
    }
    return false;
}

/* ──────────────────────────────────────────────
 * Signature Verification
 * ────────────────────────────────────────────── */

bool firmware_sign_verify(const uint8_t signature[FIRMWARE_SIGN_SIZE],
                           uint32_t fw_len, uint8_t *work_buf)
{
    uint8_t hash[32];

    log_i("Verifying firmware signature (%u bytes)...", fw_len);

    /* Step 1: SHA-256 of B区 firmware */
    if (!firmware_sha256(hash, fw_len, work_buf))
    {
        log_e("SHA-256 computation failed");
        return false;
    }

    /* Step 2: ECDSA P-256 verification */
    if (!uECC_verify(ECDSA_PUBLIC_KEY, hash, 32, signature, uECC_secp256r1()))
    {
        log_e("Signature verification FAILED");
        return false;
    }

    log_i("Signature verified OK");
    return true;
}

/* ──────────────────────────────────────────────
 * EEPROM Transient Storage
 * ────────────────────────────────────────────── */

bool firmware_sign_store(const uint8_t signature[FIRMWARE_SIGN_SIZE])
{
    uint8_t buf[FIRMWARE_SIGN_SIZE];

    if (signature != NULL)
        memcpy(buf, signature, FIRMWARE_SIGN_SIZE);
    else
        memset(buf, 0xFF, FIRMWARE_SIGN_SIZE);

    if (!bl_eeprom_write_page(SIGN_STORE_ADDR, buf, FIRMWARE_SIGN_SIZE))
    {
        log_e("Failed to store signature");
        return false;
    }
    return true;
}

bool firmware_sign_read(uint8_t signature[FIRMWARE_SIGN_SIZE])
{
    if (!bl_eeprom_read(SIGN_STORE_ADDR, signature, FIRMWARE_SIGN_SIZE))
    {
        log_e("Failed to read signature");
        return false;
    }
    return true;
}
