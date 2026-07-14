/**
 * @file    firmware_sign.h
 * @brief   Firmware signature verification layer.
 *
 * Wraps SHA-256 + ECDSA P-256 verification for use by the BootLoader.
 * Also handles storing/retrieving the 64-byte signature from EEPROM.
 *
 * Public key is compiled into the BootLoader binary (see firmware_sign.c).
 */

#ifndef __FIRMWARE_SIGN_H__
#define __FIRMWARE_SIGN_H__

#include <stdint.h>
#include <stdbool.h>

#define FIRMWARE_SIGN_SIZE  64    /* ECDSA P-256 raw signature: r(32B) || s(32B) */

/* EEPROM address for signature temporary storage (transient, not dual-copy) */
#define SIGN_STORE_ADDR     0x0080

/**
 * @brief  Verify firmware authenticity using ECDSA P-256 signature.
 *
 * Reads the entire firmware from W25Q128 B区 (offset 0), computes SHA-256
 * incrementally in 4KB chunks (reusing packet_buffer), and verifies the
 * ECDSA signature against the hard-coded public key.
 *
 * @param  signature  64-byte raw ECDSA signature (r[32] || s[32])
 * @param  fw_len     firmware length in bytes
 * @param  work_buf   scratch buffer, at least 4096 bytes (pass packet_buffer)
 * @return true if signature is valid, false if firmware is tampered/corrupt
 *
 * @note   Must be called BEFORE boot_perform_upgrade() starts modifying A区.
 *         Reads ~fw_len bytes from W25Q128 via SPI (~1-2 seconds for 448KB).
 */
bool firmware_sign_verify(const uint8_t signature[FIRMWARE_SIGN_SIZE],
                           uint32_t fw_len, uint8_t *work_buf);

/**
 * @brief  Check whether the stored signature is valid (not all 0xFF).
 * @param  signature  64-byte buffer read from EEPROM
 * @return true if the blob looks like a real signature (not erased flash)
 */
bool firmware_sign_is_present(const uint8_t signature[FIRMWARE_SIGN_SIZE]);

/**
 * @brief  Store signature to EEPROM transient area.
 * @param  signature  64-byte raw signature (or NULL to erase)
 * @return true on success
 */
bool firmware_sign_store(const uint8_t signature[FIRMWARE_SIGN_SIZE]);

/**
 * @brief  Read signature from EEPROM transient area.
 * @param  signature  out: 64-byte buffer
 * @return true on successful read
 */
bool firmware_sign_read(uint8_t signature[FIRMWARE_SIGN_SIZE]);

#endif /* __FIRMWARE_SIGN_H__ */
