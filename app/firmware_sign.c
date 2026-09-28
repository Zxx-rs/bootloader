/**
 * @file    firmware_sign.c
 * @brief   固件签名校验 — SHA-256 哈希 + ECDSA P-256 验签
 *
 * 公钥 ECDSA_PUBLIC_KEY 编译进 BootLoader 二进制，烧录后不可更改。
 * 验签流程:
 *   1. 从 W25Q128 B区 分块读取固件 → SHA-256 哈希
 *   2. 用硬编码公钥验证 ECDSA 签名
 *   3. 通过 → 继续升级 / 失败 → 拒绝并清标志
 */

#include "firmware_sign.h"
#include "sha256.h"

/* 禁用 ARM 汇编，走纯 C 路径 (ARMCC 不支持 GCC 内联汇编) */
#define uECC_PLATFORM uECC_arch_other
#define uECC_OPTIMIZATION_LEVEL 0
#include "uECC.h"
#include "bl_w25q128.h"
#include "bl_eeprom.h"
#include <string.h>

#define LOG_TAG    "fw_sign"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

/* ── ECDSA P-256 公钥 (64 字节: X坐标 || Y坐标)，由 generate_keypair.sh 自动生成 ── */
static const uint8_t ECDSA_PUBLIC_KEY[64] = {
  0xcf, 0x98, 0x54, 0xf3, 0x4e, 0x96, 0x0f, 0xe1, 0x68, 0x64, 0xa6, 0xb5,
  0xd6, 0x43, 0xfb, 0x2f, 0x81, 0x39, 0x66, 0x6b, 0x48, 0xb6, 0xbc, 0xc2,
  0xec, 0x03, 0x38, 0xa2, 0xda, 0x68, 0x26, 0xa1, 0x04, 0xd3, 0x49, 0xbd,
  0x93, 0xe6, 0xbb, 0x73, 0xd0, 0xc6, 0x85, 0xd1, 0x20, 0x1b, 0xde, 0x5d,
  0x56, 0x8e, 0x6c, 0x15, 0x81, 0x6a, 0x67, 0xc5, 0xd9, 0x7e, 0x3f, 0x18,
  0x59, 0x03, 0x24, 0xa8
};

/* 每次从 SPI Flash 读取的块大小，复用 packet_buffer */
#define FW_CHUNK_SIZE  4096

/* ──────────────────────────────────────────────
 * SHA-256 流式计算 — 从 W25Q128 B区 分块读取
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

        bl_w25q128_read(offset, buf, chunk);   /* 从 B区 读一块 */
        sha256_update(&ctx, buf, chunk);        /* 喂入哈希引擎 */

        remaining -= chunk;
        offset += chunk;
    }

    sha256_final(&ctx, digest);                 /* 输出 32 字节哈希值 */
    return true;
}

/* ──────────────────────────────────────────────
 * 签名存在性检查 — 全 0xFF 表示 EEPROM 擦除状态（无签名）
 * ────────────────────────────────────────────── */

bool firmware_sign_is_present(const uint8_t signature[FIRMWARE_SIGN_SIZE])
{
    for (int i = 0; i < FIRMWARE_SIGN_SIZE; i++)
    {
        if (signature[i] != 0xFF) return true;   /* 至少一个字节不是 0xFF */
    }
    return false;                                 /* 全是 0xFF，无签名 */
}

/* ──────────────────────────────────────────────
 * 签名验证 — SHA-256 + ECDSA P-256
 * ────────────────────────────────────────────── */

bool firmware_sign_verify(const uint8_t signature[FIRMWARE_SIGN_SIZE],
                           uint32_t fw_len, uint8_t *work_buf)
{
    uint8_t hash[32];

    log_i("Verifying firmware signature (%u bytes)...", fw_len);

    /* 第1步: 计算 B区 固件的 SHA-256 哈希 (流式，边读边算) */
    if (!firmware_sha256(hash, fw_len, work_buf))
    {
        log_e("SHA-256 computation failed");
        return false;
    }

    /* 第2步: ECDSA P-256 验签 (用硬编码公钥验证哈希和签名是否匹配) */
    if (!uECC_verify(ECDSA_PUBLIC_KEY, hash, 32, signature, uECC_secp256r1()))
    {
        log_e("Signature verification FAILED");
        return false;
    }

    log_i("Signature verified OK");
    return true;
}

/* ──────────────────────────────────────────────
 * EEPROM 暂存 — 签名是瞬态数据，只在 SET_FLAG 和验签之间用一次
 * 如果这段窗口内掉电，上位机重新 OTA 即可，因此单区存储足够
 * ────────────────────────────────────────────── */

bool firmware_sign_store(const uint8_t signature[FIRMWARE_SIGN_SIZE])
{
    uint8_t buf[FIRMWARE_SIGN_SIZE];

    if (signature != NULL)
        memcpy(buf, signature, FIRMWARE_SIGN_SIZE);   /* 保存签名 */
    else
        memset(buf, 0xFF, FIRMWARE_SIGN_SIZE);        /* 擦除（写全 0xFF） */

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
