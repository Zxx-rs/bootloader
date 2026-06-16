#ifndef __BL_W25Q128_H__
#define __BL_W25Q128_H__

#include <stdint.h>
#include <stdbool.h>

/* W25Q128 参数
 *   容量: 128 Mbit = 16 MB
 *   页大小: 256 字节
 *   扇区: 4 KB (擦除单位)
 *   块:   64 KB / 32 KB
 *   接口: SPI1, Mode 0 (CPOL=0, CPHA=0)
 */

#define W25Q128_SIZE            (16 * 1024 * 1024)   /* 16 MB */
#define W25Q128_PAGE_SIZE       256
#define W25Q128_SECTOR_SIZE     4096                  /* 4 KB */
#define W25Q128_BLOCK_SIZE      (64 * 1024)           /* 64 KB */

/* SPI 引脚 */
#define W25Q128_SPI             SPI1
#define W25Q128_SPI_CLK         RCC_APB2Periph_SPI1
#define W25Q128_GPIO_CLK        (RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOE)
#define W25Q128_GPIO_PORT       GPIOA
#define W25Q128_SCK_PIN         GPIO_Pin_5
#define W25Q128_MISO_PIN        GPIO_Pin_6
#define W25Q128_MOSI_PIN        GPIO_Pin_7
#define W25Q128_SCK_SRC         GPIO_PinSource5
#define W25Q128_MISO_SRC        GPIO_PinSource6
#define W25Q128_MOSI_SRC        GPIO_PinSource7
#define W25Q128_GPIO_AF         GPIO_AF_SPI1

/* CS 引脚 (软件控制) */
#define W25Q128_CS_PORT         GPIOE
#define W25Q128_CS_CLK          RCC_AHB1Periph_GPIOE
#define W25Q128_CS_PIN          GPIO_Pin_13

/* ── API ── */

/**
 * @brief  初始化 SPI1 GPIO 及外设 (Mode 0, 10.5 MHz)。
 */
void bl_w25q128_init(void);

/**
 * @brief  读取 JEDEC ID (应返回 0xEF4018)。
 * @param  id  输出 3 字节 ID 缓冲区。
 * @return true 表示读取成功且 ID 正确。
 */
bool bl_w25q128_read_id(uint8_t id[3]);

/**
 * @brief  擦除一个扇区 (4 KB)。
 * @param  addr  24 位扇区起始地址 (0 ~ 16MB-1, 必须 4KB 对齐)。
 * @return true 表示成功。
 */
bool bl_w25q128_erase_sector(uint32_t addr);

/**
 * @brief  擦除一个 64KB 块。
 * @param  addr  24 位块起始地址 (必须 64KB 对齐)。
 * @return true 表示成功。
 */
bool bl_w25q128_erase_block(uint32_t addr);

/**
 * @brief  整片擦除 (耗时约 100 秒, 谨慎使用)。
 */
bool bl_w25q128_erase_chip(void);

/**
 * @brief  页写入 (最多 256 字节, 不可跨页)。
 * @param  addr  24 位写入地址。
 * @param  data  源数据指针。
 * @param  len   写入字节数 (1 ~ 256)。
 * @return true 表示成功。
 */
bool bl_w25q128_write_page(uint32_t addr, const uint8_t *data, uint16_t len);

/**
 * @brief  任意长度写入 (自动跨页拆分)。
 * @param  addr  24 位写入地址。
 * @param  data  源数据指针。
 * @param  len   写入字节数。
 * @return true 表示成功。
 */
bool bl_w25q128_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief  任意长度读取。
 * @param  addr  24 位读取地址。
 * @param  data  目标缓冲区。
 * @param  len   读取字节数。
 */
void bl_w25q128_read(uint32_t addr, uint8_t *data, uint32_t len);

/**
 * @brief  上电自检: 读 ID + 末尾扇区读写比对。
 * @note   使用最后 4KB 扇区, 原始内容不恢复。
 */
bool bl_w25q128_self_test(void);

#endif /* __BL_W25Q128_H__ */
