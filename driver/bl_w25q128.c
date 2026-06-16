#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "bl_w25q128.h"

#define LOG_TAG    "w25q128"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

/* ── W25Q128 指令码 ── */
#define CMD_WRITE_ENABLE      0x06
#define CMD_WRITE_DISABLE     0x04
#define CMD_READ_STATUS1      0x05
#define CMD_READ_STATUS2      0x35
#define CMD_READ_DATA         0x03
#define CMD_PAGE_PROGRAM      0x02
#define CMD_SECTOR_ERASE      0x20   /* 4 KB  */
#define CMD_BLOCK_ERASE_32K   0x52
#define CMD_BLOCK_ERASE_64K   0xD8
#define CMD_CHIP_ERASE        0xC7
#define CMD_JEDEC_ID          0x9F
#define CMD_POWER_DOWN        0xB9
#define CMD_RELEASE_POWERDOWN 0xAB

/* 状态寄存器位 */
#define STATUS_BUSY           0x01   /* SR1: WIP (Write In Progress) */

/* SPI 速率: APB2=84MHz, prescaler=8 → 10.5 MHz (W25Q128 支持 104 MHz) */
#define SPI_PRESCALER         SPI_BaudRatePrescaler_8

/* 超时 */
#define W25Q128_TIMEOUT       0xFFFFFF

/* ── CS 控制 (软件片选, PE13 低有效) ── */
#define CS_LOW()  GPIO_ResetBits(W25Q128_CS_PORT, W25Q128_CS_PIN)
#define CS_HIGH() GPIO_SetBits(W25Q128_CS_PORT, W25Q128_CS_PIN)

/* ──────────────────────────────────────────────
 * SPI 底层操作
 * ────────────────────────────────────────────── */

/**
 * @brief  SPI 收发一个字节。
 */
static uint8_t spi_transfer_byte(uint8_t tx)
{
    while (SPI_I2S_GetFlagStatus(W25Q128_SPI, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(W25Q128_SPI, tx);
    while (SPI_I2S_GetFlagStatus(W25Q128_SPI, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(W25Q128_SPI);
}

/**
 * @brief  读取状态寄存器。
 */
static uint8_t read_status(uint8_t cmd)
{
    uint8_t status;
    CS_LOW();
    spi_transfer_byte(cmd);
    status = spi_transfer_byte(0xFF);
    CS_HIGH();
    return status;
}

/**
 * @brief  等待写操作完成 (轮询 BUSY 位)。
 */
static bool wait_busy(void)
{
    uint32_t timeout = W25Q128_TIMEOUT;
    while (read_status(CMD_READ_STATUS1) & STATUS_BUSY)
    {
        if (--timeout == 0)
        {
            log_e("W25Q128 wait busy timeout");
            return false;
        }
    }
    return true;
}

/**
 * @brief  发送写使能命令。
 */
static void write_enable(void)
{
    CS_LOW();
    spi_transfer_byte(CMD_WRITE_ENABLE);
    CS_HIGH();
}

/* ──────────────────────────────────────────────
 * GPIO 及 SPI 初始化
 * ────────────────────────────────────────────── */

static void w25q128_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    /* 使能 GPIOA (SPI) 和 GPIOE (CS) 时钟 */
    RCC_AHB1PeriphClockCmd(W25Q128_GPIO_CLK, ENABLE);

    /* PA5=SCK, PA6=MISO, PA7=MOSI */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = W25Q128_SCK_PIN | W25Q128_MISO_PIN | W25Q128_MOSI_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(W25Q128_GPIO_PORT, &gpio);

    GPIO_PinAFConfig(W25Q128_GPIO_PORT, W25Q128_SCK_SRC,  W25Q128_GPIO_AF);
    GPIO_PinAFConfig(W25Q128_GPIO_PORT, W25Q128_MISO_SRC, W25Q128_GPIO_AF);
    GPIO_PinAFConfig(W25Q128_GPIO_PORT, W25Q128_MOSI_SRC, W25Q128_GPIO_AF);

    /* PE13 = CS (推挽输出, 初始高电平 = 不选中) */
    gpio.GPIO_Pin   = W25Q128_CS_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(W25Q128_CS_PORT, &gpio);
    CS_HIGH();
}

static void w25q128_spi_init(void)
{
    RCC_APB2PeriphClockCmd(W25Q128_SPI_CLK, ENABLE);

    SPI_InitTypeDef spi;
    SPI_StructInit(&spi);
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;        /* Mode 0: CPOL=0, CPHA=0 */
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;        /* 软件 NSS */
    spi.SPI_BaudRatePrescaler = SPI_PRESCALER;       /* 84/8 = 10.5 MHz */
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_Init(W25Q128_SPI, &spi);
    SPI_Cmd(W25Q128_SPI, ENABLE);
}

void bl_w25q128_init(void)
{
    w25q128_gpio_init();
    w25q128_spi_init();
    log_i("W25Q128 init OK (SPI1 @ 10.5 MHz, CS=PE13)");
}

/* ──────────────────────────────────────────────
 * 基本操作
 * ────────────────────────────────────────────── */

bool bl_w25q128_read_id(uint8_t id[3])
{
    if (id == NULL) return false;

    CS_LOW();
    spi_transfer_byte(CMD_JEDEC_ID);
    id[0] = spi_transfer_byte(0xFF);  /* Manufacturer: Winbond = 0xEF */
    id[1] = spi_transfer_byte(0xFF);  /* Memory Type: 0x40 */
    id[2] = spi_transfer_byte(0xFF);  /* Capacity: 0x18 = 128Mbit */
    CS_HIGH();

    log_i("JEDEC ID: %02X %02X %02X (expected: EF 40 18)", id[0], id[1], id[2]);

    if (id[0] != 0xEF || id[1] != 0x40 || id[2] != 0x18)
    {
        log_e("W25Q128 ID mismatch");
        return false;
    }
    return true;
}

/**
 * @brief  发送 24 位地址 (3 字节, 高字节在前)。
 */
static void send_addr(uint32_t addr)
{
    spi_transfer_byte((uint8_t)(addr >> 16));
    spi_transfer_byte((uint8_t)(addr >> 8));
    spi_transfer_byte((uint8_t)(addr));
}

bool bl_w25q128_erase_sector(uint32_t addr)
{
    if (addr >= W25Q128_SIZE) return false;

    write_enable();
    CS_LOW();
    spi_transfer_byte(CMD_SECTOR_ERASE);
    send_addr(addr);
    CS_HIGH();

    if (!wait_busy())
    {
        log_e("sector erase timeout @ 0x%08X", addr);
        return false;
    }
    return true;
}

bool bl_w25q128_erase_block(uint32_t addr)
{
    if (addr >= W25Q128_SIZE) return false;

    write_enable();
    CS_LOW();
    spi_transfer_byte(CMD_BLOCK_ERASE_64K);
    send_addr(addr);
    CS_HIGH();

    if (!wait_busy())
    {
        log_e("block erase timeout @ 0x%08X", addr);
        return false;
    }
    return true;
}

bool bl_w25q128_erase_chip(void)
{
    log_w("chip erase started (may take ~100s)...");
    write_enable();
    CS_LOW();
    spi_transfer_byte(CMD_CHIP_ERASE);
    CS_HIGH();

    if (!wait_busy())
    {
        log_e("chip erase timeout");
        return false;
    }
    log_i("chip erase done");
    return true;
}

bool bl_w25q128_write_page(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (addr >= W25Q128_SIZE || data == NULL || len == 0 || len > W25Q128_PAGE_SIZE)
        return false;

    write_enable();
    CS_LOW();
    spi_transfer_byte(CMD_PAGE_PROGRAM);
    send_addr(addr);
    for (uint16_t i = 0; i < len; i++)
        spi_transfer_byte(data[i]);
    CS_HIGH();

    if (!wait_busy())
    {
        log_e("page write timeout @ 0x%08X", addr);
        return false;
    }
    return true;
}

bool bl_w25q128_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr >= W25Q128_SIZE || data == NULL || len == 0)
        return false;
    if (addr + len > W25Q128_SIZE)
        return false;

    uint32_t remaining = len;
    uint32_t offset    = 0;

    while (remaining > 0)
    {
        /* 计算当前页剩余空间 */
        uint32_t page_offset = addr & (W25Q128_PAGE_SIZE - 1);
        uint16_t chunk = (uint16_t)(W25Q128_PAGE_SIZE - page_offset);
        if (chunk > remaining) chunk = (uint16_t)remaining;

        if (!bl_w25q128_write_page(addr, data + offset, chunk))
            return false;

        addr      += chunk;
        offset    += chunk;
        remaining -= chunk;
    }
    return true;
}

void bl_w25q128_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr >= W25Q128_SIZE || data == NULL || len == 0)
        return;

    CS_LOW();
    spi_transfer_byte(CMD_READ_DATA);
    send_addr(addr);
    for (uint32_t i = 0; i < len; i++)
        data[i] = spi_transfer_byte(0xFF);
    CS_HIGH();
}

/* ──────────────────────────────────────────────
 * 自检
 * ────────────────────────────────────────────── */

bool bl_w25q128_self_test(void)
{
    uint8_t id[3];

    /* 1. 读 ID */
    if (!bl_w25q128_read_id(id))
        return false;

    /* 2. 在最末尾扇区做读写比对 (避免破坏 B区固件数据) */
    uint32_t test_addr = W25Q128_SIZE - W25Q128_SECTOR_SIZE;  /* 最后 4KB */
    uint8_t pattern[256];
    uint8_t rbuf[256];

    /* 生成测试模式 (每字节不同, 检测地址错位) */
    for (int i = 0; i < 256; i++)
        pattern[i] = (uint8_t)(i ^ 0xA5);

    log_i("erase test sector @ 0x%08X...", test_addr);
    if (!bl_w25q128_erase_sector(test_addr))
    {
        log_e("self-test: erase failed");
        return false;
    }

    log_i("write test page...");
    if (!bl_w25q128_write_page(test_addr, pattern, 256))
    {
        log_e("self-test: write failed");
        return false;
    }

    log_i("read back and compare...");
    memset(rbuf, 0, sizeof(rbuf));
    bl_w25q128_read(test_addr, rbuf, 256);

    if (memcmp(pattern, rbuf, 256) != 0)
    {
        log_e("self-test: data mismatch");
        return false;
    }
    log_i("W25Q128 self-test PASSED");
    return true;
}
