#ifndef __BOOT_INFO_H__
#define __BOOT_INFO_H__

#include <stdint.h>
#include <stdbool.h>

/* ── OTA 升级状态标志 ── */
#define BOOT_FLAG_NORMAL      0x00  /* 正常运行，无需升级 */
#define BOOT_FLAG_NEW_FW      0x01  /* 有新固件需搬运（B区→A区） */
#define BOOT_FLAG_TESTING     0x02  /* 刚升级完，需运行测试确认 */

/* ── EEPROM 存储布局 ──
 *  0x0000 ~ 0x003F : boot_info_t A区（主标志位，Bootloader 默认读取此区域）
 *  0x0040 ~ 0x007F : boot_info_t B区（备份标志位，内容与 A 区完全相同）
 *  0x0080 ~ ...    : 预留后续 OTA 使用
 */
#define BOOT_INFO_A_ADDR      0x0000
#define BOOT_INFO_B_ADDR      0x0040
#define BOOT_INFO_SIZE        40      /* 每个区保留 40 字节 */

/* ── OTA 标志位结构体 ── */
typedef struct {
    uint32_t boot_flag;       /* 升级状态标志 */
    uint32_t firmware_len;    /* 固件大小（字节） */
    uint32_t firmware_crc;    /* 固件全局 CRC32 校验值 */
    char     version[16];     /* 当前或目标版本号（含 '\0' 结尾） */
    uint32_t this_crc;        /* 本结构体前 28 字节的 CRC32（自校验） */
} boot_info_t;

/* ── API ── */

/**
 * @brief  从 EEPROM 读取 boot_info（优先 A 区，校验失败则回退 B 区）。
 * @param  info  输出参数，指向 boot_info_t 缓冲区。
 * @return true 表示读取成功且 CRC 校验通过，false 表示 A/B 两区均校验失败。
 */
bool boot_info_read(boot_info_t *info);

/**
 * @brief  将 boot_info 同时写入 EEPROM 的 A 区和 B 区（双备份）。
 * @param  info  指向待写入的 boot_info_t。
 * @return true 表示两区均写入成功，false 表示至少一区写入失败。
 */
bool boot_info_write(const boot_info_t *info);

/**
 * @brief  初始化 boot_info 为工厂默认值（正常模式，版本 "0.0.0"）。
 * @note   仅在首次上电或两区均损坏时调用。
 * @param  info  输出参数，写入默认值。
 */
void boot_info_init_default(boot_info_t *info);

/**
 * @brief  打印 boot_info 内容到日志（INFO 级别）。
 */
void boot_info_dump(const boot_info_t *info);

/**
 * @brief  便捷函数：设置新固件信息并写入 EEPROM（B→A 双区）。
 * @param  len      固件大小（字节）
 * @param  crc      固件 CRC32 校验值
 * @param  version  版本号字符串（最长 15 字符，自动截断）
 * @return true 表示写入成功
 */
bool boot_info_set_firmware(uint32_t len, uint32_t crc, const char *version);

#endif /* __BOOT_INFO_H__ */
