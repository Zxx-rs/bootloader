#ifndef __BL_EEPROM_H
#define __BL_EEPROM_H

#include <stdint.h>
#include <stdbool.h>

/* BL24C512 EEPROM 参数
 *   容量: 512 Kbit = 64 KB
 *   I2C 7位地址: 0x50 (A0=A1=A2 接地)
 *   页大小: 128 字节
 *   写周期: 最长 5 ms
 *   I2C 时钟: 100 kHz (标准模式) 或 400 kHz (快速模式)
 */

#define BL24C512_I2C            I2C1
#define BL24C512_ADDR           0xA0   /* 8位地址: 0xA0=写, 0xA1=读 (7位地址 0x50 << 1) */
#define BL24C512_PAGE_SIZE      128
#define BL24C512_SIZE           65536  /* 共 64 KB */

/* I2C 引脚映射 */
#define BL24C512_I2C_CLK        RCC_APB1Periph_I2C1
#define BL24C512_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BL24C512_GPIO_PORT      GPIOB
#define BL24C512_SCL_PIN        GPIO_Pin_6
#define BL24C512_SDA_PIN        GPIO_Pin_7
#define BL24C512_SCL_SRC        GPIO_PinSource6
#define BL24C512_SDA_SRC        GPIO_PinSource7
#define BL24C512_GPIO_AF        GPIO_AF_I2C1

/* I2C 时钟频率 (Hz) */
#define BL24C512_I2C_SPEED      400000  /* 快速模式 400 kHz */

/* I2C 事件轮询最大重试次数 (防止死循环) */
#define BL24C512_I2C_TIMEOUT    0xFFFF

/* ──────────────────────────────────────────────
 * API 函数声明
 * ────────────────────────────────────────────── */

/**
 * @brief  初始化 I2C1 的 GPIO 和外设，用于与 BL24C512 通信。
 * @note   应在板级初始化阶段调用一次。需确保 GPIOB 和 I2C1 的时钟已由 RCC 使能。
 */
void bl_eeprom_init(void);

/**
 * @brief  向 EEPROM 写入单个字节。
 * @param  addr  16 位存储地址 (0 ~ 65535)
 * @param  data  待写入的字节
 * @return true 表示写入成功, false 表示 I2C 通信出错
 */
bool bl_eeprom_write_byte(uint16_t addr, uint8_t data);

/**
 * @brief  从指定地址开始写入一页数据 (≤128 字节)。
 * @param  addr  16 位存储起始地址
 * @param  data  源数据指针
 * @param  len   写入字节数 (1 ~ 128, 不得跨越页边界)
 * @return true 表示写入成功, false 表示参数错误或 I2C 通信出错
 */
bool bl_eeprom_write_page(uint16_t addr, const uint8_t *data, uint16_t len);

/**
 * @brief  从 EEPROM 指定地址读取任意长度数据 (顺序读)。
 * @param  addr  16 位存储起始地址
 * @param  data  目标缓冲区指针
 * @param  len   读取字节数
 * @return true 表示读取成功, false 表示 I2C 通信出错
 */
bool bl_eeprom_read(uint16_t addr, uint8_t *data, uint16_t len);

/**
 * @brief  自检: 在安全区域写入测试模式并回读比对。
 * @note   使用末尾 16 字节 (0xFFF0 ~ 0xFFFF) 避免破坏后续 OTA 标志存储区。
 *         原始内容不会被恢复 —— 这是破坏性测试。
 * @return true 表示回读数据一致, false 表示不一致或通信失败
 */
bool bl_eeprom_self_test(void);

#endif /* __BL_EEPROM_H */
