/**
 * @file    firmware_sign.h
 * @brief   固件签名校验层 — SHA-256 + ECDSA P-256
 *
 * 包装 SHA-256 哈希和 ECDSA P-256 验签，供 BootLoader 调用。
 * 同时负责签名的 EEPROM 存取（SET_FLAG 下发 → 暂存 → 复位后取出验签）。
 *
 * 公钥编译在 firmware_sign.c 中，烧录在 BootLoader 内部 Flash。
 */

#ifndef __FIRMWARE_SIGN_H__
#define __FIRMWARE_SIGN_H__

#include <stdint.h>
#include <stdbool.h>

#define FIRMWARE_SIGN_SIZE  64    /* ECDSA P-256 裸签名: r(32B) || s(32B) */

/* EEPROM 地址 — 签名暂存区（瞬态数据，验签后丢弃），单区存储无需双冗余 */
#define SIGN_STORE_ADDR     0x0080

/**
 * @brief  验证固件真伪（ECDSA P-256 验签）。
 *
 * 从 W25Q128 B区 分块读取固件，流式计算 SHA-256，再用硬编码公钥验签。
 *
 * @param  signature  64 字节裸 ECDSA 签名 (r[32] || s[32])
 * @param  fw_len     固件大小（字节）
 * @param  work_buf   临时工作缓冲区（至少 4096 字节，传入 packet_buffer）
 * @return true = 签名有效，固件可信；false = 签名无效或固件被篡改
 *
 * @note   必须在 boot_perform_upgrade() 修改 A区 之前调用。
 *         读取 W25Q128 耗时约 1~2 秒（448KB / SPI 10.5MHz）。
 */
bool firmware_sign_verify(const uint8_t signature[FIRMWARE_SIGN_SIZE],
                           uint32_t fw_len, uint8_t *work_buf);

/**
 * @brief  检查签名是否有效（不是全 0xFF 擦除状态）。
 * @param  signature  64 字节缓冲区
 * @return true = 存在有效签名，false = 擦除状态 / 无签名
 */
bool firmware_sign_is_present(const uint8_t signature[FIRMWARE_SIGN_SIZE]);

/**
 * @brief  将签名写入 EEPROM 暂存区。
 * @param  signature  64 字节签名，传 NULL 则写入全 0xFF（擦除）
 * @return true = 写入成功
 */
bool firmware_sign_store(const uint8_t signature[FIRMWARE_SIGN_SIZE]);

/**
 * @brief  从 EEPROM 暂存区读取签名。
 * @param  signature  输出缓冲区（64 字节）
 * @return true = 读取成功
 */
bool firmware_sign_read(uint8_t signature[FIRMWARE_SIGN_SIZE]);

#endif /* __FIRMWARE_SIGN_H__ */
