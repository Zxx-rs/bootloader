#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "bl_eeprom.h"

#define LOG_TAG    "eeprom"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

/* ──────────────────────────────────────────────
 * 内部辅助函数 — I2C 总线操作
 * ────────────────────────────────────────────── */

/**
 * @brief  等待指定的 I2C 事件，带超时保护。
 * @return true 表示事件发生, false 表示超时
 */
static bool i2c_wait_event(uint32_t event)
{
    uint32_t timeout = BL24C512_I2C_TIMEOUT;
    while (I2C_CheckEvent(BL24C512_I2C, event) == ERROR)
    {
        if (--timeout == 0)
        {
            log_e("I2C event timeout: 0x%08X", event);
            return false;
        }
    }
    return true;
}

/**
 * @brief  等待指定的 I2C 标志位，带超时保护。
 * @return true 表示标志位达到预期状态, false 表示超时
 */
static bool i2c_wait_flag(uint32_t flag, FlagStatus expected)
{
    uint32_t timeout = BL24C512_I2C_TIMEOUT;
    while (I2C_GetFlagStatus(BL24C512_I2C, flag) != expected)
    {
        if (--timeout == 0)
        {
            log_e("I2C flags timeout: 0x%08X", flag);
            return false;
        }
    }
    return true;
}

/**
 * @brief  等待 EEPROM 内部写周期完成 (ACK 轮询方式)。
 *
 *  每次向 EEPROM 写入数据后，芯片会进入内部写周期 (最长 5 ms)。
 *  在此期间 EEPROM 不会应答任何 I2C 事务 (返回 NACK)。
 *  本函数循环发送 START + 设备地址，直到收到 ACK 为止。
 *
 * @return true 表示 EEPROM 已就绪, false 表示超时
 */
static bool eeprom_wait_ready(void)
{
    /* 为最长 5 ms 的写周期留出足够余量 */
    uint32_t timeout = BL24C512_I2C_TIMEOUT * 8;

    while (timeout--)
    {
        /* 发送起始条件 */
        I2C_GenerateSTART(BL24C512_I2C, ENABLE);
        if (!i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT))
            return false;

        /* 发送设备地址 (写方向) —— EEPROM 就绪后才会应答 ACK */
        I2C_Send7bitAddress(BL24C512_I2C, BL24C512_ADDR, I2C_Direction_Transmitter);

        /* 轮询 ADDR 标志 (ACK) 或 AF 标志 (NACK = 仍在写周期中) */
        uint32_t tmo = 5000;
        while (tmo--)
        {
            if (I2C_GetFlagStatus(BL24C512_I2C, I2C_FLAG_ADDR) != RESET)
            {
                /* 设备应答 ACK → 写周期已完成 */
                /* 读 SR2 以清除 ADDR 标志 */
                (void)BL24C512_I2C->SR2;
                I2C_GenerateSTOP(BL24C512_I2C, ENABLE);
                return true;
            }
            if (I2C_GetFlagStatus(BL24C512_I2C, I2C_FLAG_AF) != RESET)
            {
                /* NACK → 仍在写周期中; 清除 AF 标志, 退出内层循环重试 */
                I2C_ClearFlag(BL24C512_I2C, I2C_FLAG_AF);
                break;
            }
        }
        I2C_GenerateSTOP(BL24C512_I2C, ENABLE);
    }

    log_e("EEPROM 等待就绪超时");
    return false;
}

/**
 * @brief  发送 START + 设备地址 (写方向)。
 * @return true 表示成功, false 表示 I2C 错误
 */
static bool eeprom_start_write_addr(void)
{
    I2C_GenerateSTART(BL24C512_I2C, ENABLE);
    if (!i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT))
        return false;

    I2C_Send7bitAddress(BL24C512_I2C, BL24C512_ADDR, I2C_Direction_Transmitter);
    if (!i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
        return false;

    return true;
}

/**
 * @brief  发送 START + 设备地址 (读方向)。
 * @return true 表示成功, false 表示 I2C 错误
 */
static bool eeprom_start_read_addr(void)
{
    I2C_GenerateSTART(BL24C512_I2C, ENABLE);
    if (!i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT))
        return false;

    I2C_Send7bitAddress(BL24C512_I2C, BL24C512_ADDR, I2C_Direction_Receiver);
    if (!i2c_wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
        return false;

    return true;
}

/* ──────────────────────────────────────────────
 * I2C GPIO 及外设初始化
 * ────────────────────────────────────────────── */

/**
 * @brief  初始化 I2C1 的 GPIO 引脚 (PB6=SCL, PB7=SDA)。
 *         配置为开漏输出、复用功能、内部上拉。
 */
static void eeprom_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 使能 GPIOB 时钟 (可重复调用, 已使能时无副作用) */
    RCC_AHB1PeriphClockCmd(BL24C512_GPIO_CLK, ENABLE);

    /* PB6 = SCL, PB7 = SDA — 开漏, 复用功能, 上拉 */
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin   = BL24C512_SCL_PIN | BL24C512_SDA_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_OD;        /* I2C 协议要求开漏输出 */
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_UP;         /* 建议同时焊接外部上拉电阻 */
    GPIO_Init(BL24C512_GPIO_PORT, &GPIO_InitStruct);

    /* 将 PB6/PB7 连接到 I2C1 复用功能 */
    GPIO_PinAFConfig(BL24C512_GPIO_PORT, BL24C512_SCL_SRC, BL24C512_GPIO_AF);
    GPIO_PinAFConfig(BL24C512_GPIO_PORT, BL24C512_SDA_SRC, BL24C512_GPIO_AF);
}

/**
 * @brief  初始化 I2C1 外设: 快速模式 400 kHz, 7 位地址, 主模式。
 */
static void eeprom_i2c_init(void)
{
    /* 使能 I2C1 时钟 (可重复调用) */
    RCC_APB1PeriphClockCmd(BL24C512_I2C_CLK, ENABLE);

    /* 软件复位 I2C1 外设，清除 RCC_DeInit 遗留的中间状态 */
    I2C_DeInit(BL24C512_I2C);

    /* 解锁 I2C 总线: 将 SCL 临时切为 GPIO 输出，发 9 个脉冲释放从机 */
    {
        GPIO_InitTypeDef gpio;
        GPIO_StructInit(&gpio);
        gpio.GPIO_Pin  = BL24C512_SCL_PIN;
        gpio.GPIO_Mode = GPIO_Mode_OUT;
        gpio.GPIO_OType = GPIO_OType_OD;
        gpio.GPIO_Speed = GPIO_Speed_2MHz;
        gpio.GPIO_PuPd  = GPIO_PuPd_UP;
        GPIO_Init(BL24C512_GPIO_PORT, &gpio);
        for (int i = 0; i < 9; i++)
        {
            GPIO_ResetBits(BL24C512_GPIO_PORT, BL24C512_SCL_PIN);
            for (volatile int d = 0; d < 100; d++);
            GPIO_SetBits(BL24C512_GPIO_PORT, BL24C512_SCL_PIN);
            for (volatile int d = 0; d < 100; d++);
        }
        /* 恢复 SCL 为 AF 功能 */
        GPIO_PinAFConfig(BL24C512_GPIO_PORT, BL24C512_SCL_SRC, BL24C512_GPIO_AF);
        gpio.GPIO_Pin  = BL24C512_SCL_PIN | BL24C512_SDA_PIN;
        gpio.GPIO_Mode = GPIO_Mode_AF;
        gpio.GPIO_OType = GPIO_OType_OD;
        gpio.GPIO_Speed = GPIO_Speed_50MHz;
        gpio.GPIO_PuPd  = GPIO_PuPd_UP;
        GPIO_Init(BL24C512_GPIO_PORT, &gpio);
    }

    I2C_InitTypeDef I2C_InitStruct;
    I2C_StructInit(&I2C_InitStruct);

    I2C_InitStruct.I2C_ClockSpeed = BL24C512_I2C_SPEED;
    I2C_InitStruct.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitStruct.I2C_DutyCycle  = (BL24C512_I2C_SPEED > 100000)
                                    ? I2C_DutyCycle_2          /* 快速模式: T_low/T_high = 2:1 */
                                    : I2C_DutyCycle_16_9;      /* 标准模式 */
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;                     /* 本机为主设备, 自身地址无用 */
    I2C_InitStruct.I2C_Ack         = I2C_Ack_Enable;
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(BL24C512_I2C, &I2C_InitStruct);
    I2C_Cmd(BL24C512_I2C, ENABLE);
}

/**
 * @brief  初始化 BL24C512 EEPROM (GPIO + I2C 外设)。
 * @note   在 bootloader_main() 中调用一次即可。
 */
void bl_eeprom_init(void)
{
    eeprom_gpio_init();
    eeprom_i2c_init();
    log_i("BL24C512 EEPROM init (I2C1 @ %d kHz)", BL24C512_I2C_SPEED / 1000);
}

/* ──────────────────────────────────────────────
 * 公开 API
 * ────────────────────────────────────────────── */

/**
 * @brief  向 EEPROM 写入单个字节。
 *
 *  I2C 时序:
 *   START → 设备地址(W) → 存储地址高字节 → 存储地址低字节 → 数据 → STOP
 *   然后通过 ACK 轮询等待内部写周期完成。
 */
bool bl_eeprom_write_byte(uint16_t addr, uint8_t data)
{
    if (!eeprom_start_write_addr())
        return false;

    /* 发送存储地址 (高字节在前) */
    I2C_SendData(BL24C512_I2C, (uint8_t)(addr >> 8));
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    I2C_SendData(BL24C512_I2C, (uint8_t)(addr & 0xFF));
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    /* 发送数据字节 */
    I2C_SendData(BL24C512_I2C, data);
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    I2C_GenerateSTOP(BL24C512_I2C, ENABLE);

    /* 等待内部写周期完成 (BL24C512 最长约 5 ms) */
    return eeprom_wait_ready();
}

/**
 * @brief  在单页内写入最多 128 字节。
 * @note   若 (addr + len) 跨越 128 字节页边界，行为由 EEPROM 硬件决定
 *         (地址回绕到页首)。调用者应自行拆分为多页写入。
 */
bool bl_eeprom_write_page(uint16_t addr, const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > BL24C512_PAGE_SIZE)
    {
        log_e("write_page: longthe invaild %u (max %u)", len, BL24C512_PAGE_SIZE);
        return false;
    }

    if (data == NULL)
    {
        log_e("write_page: empty");
        return false;
    }

    if (!eeprom_start_write_addr())
        return false;

    /* 发送存储地址 (高字节在前) */
    I2C_SendData(BL24C512_I2C, (uint8_t)(addr >> 8));
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    I2C_SendData(BL24C512_I2C, (uint8_t)(addr & 0xFF));
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    /* 连续发送数据字节 (EEPROM 内部地址自动递增) */
    for (uint16_t i = 0; i < len; i++)
    {
        I2C_SendData(BL24C512_I2C, data[i]);
        if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
            return false;
    }

    I2C_GenerateSTOP(BL24C512_I2C, ENABLE);

    /* 等待内部写周期完成 */
    return eeprom_wait_ready();
}

/**
 * @brief  从 EEPROM 指定地址读取任意长度数据。
 *
 *  I2C 时序 (随机读):
 *   START → 设备地址(W) → 存储地址高字节 → 存储地址低字节
 *   重复 START → 设备地址(R) → 读数据(ACK) … → 读最后一个字节(NACK) → STOP
 */
bool bl_eeprom_read(uint16_t addr, uint8_t *data, uint16_t len)
{
    if (len == 0 || data == NULL)
    {
        log_e("read: param invaid (len=%u, data=%p)", len, data);
        return false;
    }

    /* ── 阶段1: 虚拟写操作, 设置内部地址指针 ── */
    if (!eeprom_start_write_addr())
        return false;

    I2C_SendData(BL24C512_I2C, (uint8_t)(addr >> 8));
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    I2C_SendData(BL24C512_I2C, (uint8_t)(addr & 0xFF));
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        return false;

    /* ── 阶段2: 重复 START → 读操作 ── */
    if (!eeprom_start_read_addr())
        return false;

    /* 前 (len - 1) 字节应答 ACK, 最后一字节不应答 NACK */
    for (uint16_t i = 0; i < len; i++)
    {
        /* 最后一个字节前关闭 ACK (发送 NACK 通知 EEPROM 停止发送) */
        if (i == len - 1)
        {
            I2C_AcknowledgeConfig(BL24C512_I2C, DISABLE);
        }

        if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_RECEIVED))
        {
            /* 出错时恢复 ACK 使能, 以免影响后续调用 */
            I2C_AcknowledgeConfig(BL24C512_I2C, ENABLE);
            return false;
        }

        data[i] = I2C_ReceiveData(BL24C512_I2C);
    }

    I2C_GenerateSTOP(BL24C512_I2C, ENABLE);

    /* 恢复 ACK 使能, 供后续操作使用 */
    I2C_AcknowledgeConfig(BL24C512_I2C, ENABLE);

    return true;
}

/**
 * @brief  自检: 在 EEPROM 末尾 16 字节写入已知模式并回读比对。
 *
 *  向 0xFFF0 ~ 0xFFFF 写入测试数据, 回读后逐字节比较。
 *  使用 EEPROM 顶端区域, 避免覆盖后续 OTA 标志存储区。
 *  原始内容不会被恢复 —— 这是破坏性测试。
 */
bool bl_eeprom_self_test(void)
{
    /* 测试模式 — 每字节取值不同, 用于检测地址错位 */
    static const uint8_t pattern[] = {
        0xA5, 0x5A, 0x00, 0xFF, 0x55, 0xAA, 0xC3, 0x3C,
        0xE7, 0x7E, 0x81, 0x42, 0xBD, 0xDB, 0x24, 0x99
    };

#define TEST_LEN  16
#define TEST_ADDR (BL24C512_SIZE - TEST_LEN)  /* 0xFFF0 — 末尾 16 字节 */

    uint8_t rbuf[TEST_LEN];

    log_i("EEPROM test:  0x%04X write %u bytes...", TEST_ADDR, TEST_LEN);

    /* 写入测试模式 */
    if (!bl_eeprom_write_page(TEST_ADDR, pattern, TEST_LEN))
    {
        log_e("EEPROM test: failed to write test pattern");
        return false;
    }

    /* 短暂延时确保写周期彻底结束 (双重保险) */
    for (volatile int i = 0; i < 10000; i++) { }

    /* 回读 */
    memset(rbuf, 0, sizeof(rbuf));
    if (!bl_eeprom_read(TEST_ADDR, rbuf, TEST_LEN))
    {
        log_e("EEPROM test: failed to read test data");
        return false;
    }

    /* 逐字节比对 */
    if (memcmp(pattern, rbuf, TEST_LEN) != 0)
    {
        log_e("EEPROM test: data mismatch!");
        log_e("  写入: %02X %02X %02X %02X %02X %02X %02X %02X ...",
              pattern[0], pattern[1], pattern[2], pattern[3],
              pattern[4], pattern[5], pattern[6], pattern[7]);
        log_e("  读出: %02X %02X %02X %02X %02X %02X %02X %02X ...",
              rbuf[0], rbuf[1], rbuf[2], rbuf[3],
              rbuf[4], rbuf[5], rbuf[6], rbuf[7]);
        return false;
    }

    log_i("EEPROM test: passed");
    return true;
}
