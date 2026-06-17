#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "boot_info.h"
#include "bl_eeprom.h"
#include "crc32.h"

#define LOG_TAG    "boot_info"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

/* boot_info 有效载荷大小：不含 this_crc 字段 */
#define BOOT_INFO_PAYLOAD_SIZE  (sizeof(boot_info_t) - sizeof(uint32_t))

/* ──────────────────────────────────────────────
 * 内部辅助
 * ────────────────────────────────────────────── */

/**
 * @brief  计算 boot_info 前 28 字节的 CRC32（不含 this_crc 字段）。
 */
static uint32_t boot_info_calc_crc(const boot_info_t *info)
{
    return crc32((const uint8_t *)info, BOOT_INFO_PAYLOAD_SIZE);
}

/**
 * @brief  校验单个 boot_info 副本的 CRC。
 * @return true 表示 CRC 正确。
 */
static bool boot_info_check_crc(const boot_info_t *info)
{
    uint32_t calc = boot_info_calc_crc(info);
    if (calc != info->this_crc)
    {
        log_w("boot_info CRC mismatch: calc=0x%08X, stored=0x%08X", calc, info->this_crc);
        return false;
    }
    return true;
}

/**
 * @brief  从指定 EEPROM 地址读取一个 boot_info 副本并校验 CRC。
 * @param  addr  EEPROM 地址（BOOT_INFO_A_ADDR 或 BOOT_INFO_B_ADDR）
 * @param  info  输出参数
 * @return true 表示读取成功且 CRC 通过
 */
static bool boot_info_read_from(uint16_t addr, boot_info_t *info)
{
    memset(info, 0, sizeof(boot_info_t));
    if (!bl_eeprom_read(addr, (uint8_t *)info, sizeof(boot_info_t)))
    {
        log_e("boot_info read from 0x%04X failed", addr);
        return false;
    }
    return boot_info_check_crc(info);
}

/**
 * @brief  将单个 boot_info 副本写入指定 EEPROM 地址。
 * @param  addr  EEPROM 地址
 * @param  info  待写入的数据
 * @return true 表示写入成功
 */
static bool boot_info_write_to(uint16_t addr, const boot_info_t *info)
{
    /* 跨页写入：boot_info 大小 32 字节，小于 128 字节页大小，无需拆分 */
    if (!bl_eeprom_write_page(addr, (const uint8_t *)info, sizeof(boot_info_t)))
    {
        log_e("boot_info write to 0x%04X failed", addr);
        return false;
    }
    return true;
}

/* ──────────────────────────────────────────────
 * Boot Info — OTA flag persistence layer
 *
 *  Dual-copy strategy (B-anchored, A-follow):
 *   Write: B area first (anchor) → A area (primary)
 *   Read:  A area first → B area fallback → self-healing
 *
 *  This ensures at least one valid copy survives
 *  a power loss at any point during the write.
 * ────────────────────────────────────────────── */

/**
 * @brief  从 EEPROM 读取 boot_info（启动判决 + 自愈）。
 *
 *  Bootloader 启动判决法：
 *   1. 优先读取 A 区（主区），校验 CRC。
 *      → 通过：直接使用 A 区数据执行。
 *      → 同时检查 B 区是否完好，若 B 区损坏则顺手修复。
 *
 *   2. A 区 CRC 失败（掉电导致写入中断）：
 *      → 放弃 A 区，转去读取 B 区（备份区）。
 *        因为 B 区在 A 区之前写入，数据 100% 完好。
 *      → 用 B 区数据"救活"系统，并自动把 B 区数据刷回 A 区（自愈）。
 *
 *   3. A/B 两区均损坏（极端情况，如 EEPROM 硬件故障）：
 *      → 使用工厂默认值（BOOT_FLAG_NORMAL, version "0.0.0"）。
 */
bool boot_info_read(boot_info_t *info)
{
    if (info == NULL) return false;

    /* 优先读取 A 区 */
    if (boot_info_read_from(BOOT_INFO_A_ADDR, info))
    {
        log_i("boot_info read from A OK, boot_flag=0x%02X", info->boot_flag);

        /* 如果 A 区成功但 B 区损坏，自动修复 B 区 */
        boot_info_t b_info;
        if (!boot_info_read_from(BOOT_INFO_B_ADDR, &b_info))
        {
            log_w("B area corrupt, recovering from A...");
            boot_info_write_to(BOOT_INFO_B_ADDR, info);
        }
        return true;
    }

    /* A 区失败，回退 B 区 */
    log_w("A area CRC failed, fallback to B area");
    if (boot_info_read_from(BOOT_INFO_B_ADDR, info))
    {
        log_i("boot_info read from B area OK, boot_flag=0x%02X", info->boot_flag);

        /* B 区 OK，自动修复 A 区 */
        log_w("Recovering A area from B...");
        boot_info_write_to(BOOT_INFO_A_ADDR, info);
        return true;
    }

    /* 两区均失败，使用默认值并写入 EEPROM（首次上电初始化） */
    log_e("Both A/B areas corrupt, initializing with defaults");
    boot_info_init_default(info);
    boot_info_write(info);  /* 将默认值持久化，下次启动直接命中 */
    return false;
}

/**
 * @brief  将 boot_info 写入 EEPROM（B 区优先，A 区跟进）。
 *
 *  写入策略（防掉电）：
 *   1. 先写入 B 区（备份区）并校验。
 *      B 区是"安全锚点"——必须在 A 区之前保证完好。
 *   2. B 区写入成功后，再写入 A 区（主区）。
 *      如果此时掉电，A 区损坏但 B 区 100% 完好。
 *      下次启动时 CRC 校验会发现 A 区损坏，自动用 B 区恢复（自愈）。
 *   3. 如果 B 区写入失败，直接返回 false，A 区不会被修改。
 *      保证不会出现"新数据写一半、旧数据被破坏"的情况。
 *
 *  写入前自动计算并填入 this_crc 字段。
 */
bool boot_info_write(const boot_info_t *info)
{
    if (info == NULL) return false;

    /* 准备写入副本（含正确 CRC） */
    boot_info_t wr = *info;
    wr.this_crc = boot_info_calc_crc(&wr);

    /* 步骤 1：先写 B 区（备份锚点） */
    if (!boot_info_write_to(BOOT_INFO_B_ADDR, &wr))
    {
        log_e("B info write failed, A info untouched");
        return false;
    }
    log_i("B info written OK");

    /* 步骤 2：B 区成功后，再写 A 区（主区） */
    if (!boot_info_write_to(BOOT_INFO_A_ADDR, &wr))
    {
        log_e("A area write failed, but B info is intact (will self-heal on next boot)");
        return false;
    }

    log_i("boot_info written to B->A dual areas, boot_flag=0x%02X", wr.boot_flag);
    return true;
}

/**
 * @brief  初始化 boot_info 为工厂默认值。
 */
void boot_info_init_default(boot_info_t *info)
{
    if (info == NULL) return;

    memset(info, 0, sizeof(boot_info_t));
    info->boot_flag    = BOOT_FLAG_NORMAL;
    info->firmware_len = 0;
    info->firmware_crc = 0;
    strncpy(info->version, "0.0.0", sizeof(info->version) - 1);
    info->version[sizeof(info->version) - 1] = '\0';
    info->this_crc     = boot_info_calc_crc(info);
}

/**
 * @brief  打印 boot_info 内容到日志。
 */
void boot_info_dump(const boot_info_t *info)
{
    if (info == NULL) return;

    const char *flag_str;
    switch (info->boot_flag)
    {
    case BOOT_FLAG_NORMAL:  flag_str = "NORMAL";  break;
    case BOOT_FLAG_NEW_FW:  flag_str = "NEW_FW";  break;
    case BOOT_FLAG_TESTING: flag_str = "TESTING";  break;
    default:                flag_str = "UNKNOWN";  break;
    }

    log_i("-- boot_info -----------------");
    log_i("  boot_flag    = 0x%08X (%s)", info->boot_flag, flag_str);
    log_i("  firmware_len = %u bytes", info->firmware_len);
    log_i("  firmware_crc = 0x%08X", info->firmware_crc);
    log_i("  version      = %s", info->version);
    log_i("  this_crc     = 0x%08X", info->this_crc);
    log_i("-------------------------------");
}

/**
 * @brief  便捷函数：收到新固件后设置 boot_info 并持久化。
 *
 *  boot_flag 自动设为 BOOT_FLAG_NEW_FW，表示有新固件待处理。
 *  写入走标准的 B→A 双区策略（防掉电）。
 */
bool boot_info_set_firmware(uint32_t len, uint32_t crc, const char *version)
{
    boot_info_t info;

    /* 从 EEPROM 读取当前值作为基础（保留其他字段） */
    boot_info_read(&info);

    /* 设置新固件信息 */
    info.boot_flag    = BOOT_FLAG_NEW_FW;
    info.firmware_len = len;
    info.firmware_crc = crc;
    strncpy(info.version, version ? version : "unknown", sizeof(info.version) - 1);
    info.version[sizeof(info.version) - 1] = '\0';

    /* B→A 双区写入 */
    log_i("New firmware: ver=%s, len=%u, crc=0x%08X", info.version, len, crc);
    return boot_info_write(&info);
}
