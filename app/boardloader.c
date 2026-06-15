#include "stdint.h"
#include "stdbool.h"
#include "stdio.h"
#include <string.h>
#include "stm32f4xx.h"
#include "board.h"
#include "bl_uart.h"
#include "tim_delay.h"
#include "stm32_flash.h"
#include "magic_header.h"
#include "ringbuffer.h"
#include "crc16.h"
#include "crc32.h"
#include "utils.h"

#define LOG_TAG    "boot"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

#define BL_VERSION "0.0.1"
#define BL_ADDRESS 0x08000000
#define BL_SIZE (48 * 1024)
#define BOOT_DELAY 3000
#define APP_VOTR_ADDR 0x08010000
#define RX_BUFFER_SIZE (5 * 1024)
#define RX_TIMEOUT_MS 20
#define PAYLOAD_SIZE_MAX (4096 + 8)				   // 4096为Program最大数据长度，8为Program指令的地址(4)和长度(4)
#define PACKET_SIZE_MAX (4 + PAYLOAD_SIZE_MAX + 2) // header(1) + opcode(1) + length(2) + payload + crc16(2)

// 协议常数
#define PACKET_HEADER_REQUEST 0xAA
#define PACKET_HEADER_RESPONSE 0x55

// 数据包结构常数
#define PACKET_HEADER_SIZE 1
#define PACKET_OPCODE_SIZE 1
#define PACKET_LENGTH_SIZE 2
#define PACKET_CRC_SIZE 2
#define PACKET_HEADER_OFFSET 0
#define PACKET_OPCODE_OFFSET 1
#define PACKET_LENGTH_OFFSET 2
#define PACKET_PAYLOAD_OFFSET 4
#define PACKET_MIN_SIZE (PACKET_HEADER_SIZE + PACKET_OPCODE_SIZE + PACKET_LENGTH_SIZE + PACKET_CRC_SIZE)

// 参数长度常数
#define ADDR_SIZE_PARAM_LENGTH 8  // uint addr + uint size
#define ADDR_SIZE_CRC_PARAM_LENGTH 12  // uint addr + uint size + uint crc

typedef enum
{
	PACKET_STATE_HEADER,  // 帧头，1字节，固定为0xAA
	PACKET_STATE_OPCODE,  // 操作码，1字节
	PACKET_STATE_LENGTH,  // 数据长度，2字节，Payload的长度。采用小端模式，低字节在前，高字节在后
	PACKET_STATE_PAYLOAD, // 有效载荷
	PACKET_STATE_CRC16,	  // CRC校验，2字节，采用小端模式，低字节在前，高字节在后
} packet_state_t;

typedef enum
{
	PACKET_OPCODE_INQUERY = 0x01, // 查询
	PACKET_OPCODE_ERASE = 0x81,	  // 擦除
	PACKET_OPCODE_PROGRAM = 0x82, // 编程写入
	PACKET_OPCODE_VERIFY = 0x83,  // 验证
	PACKET_OPCODE_RESET = 0x21,	  // 重启
	PACKET_OPCODE_BOOT = 0x22,	  // 启动跳转
} packet_opcode_t;

typedef enum
{
	INQUERY_SUBCODE_VERSION = 0x00, // 查询版本号
	INQUERY_SUBCODE_MTU = 0x01,		// 查询MTU(最大传输单元)
} inquery_subcode_t;

typedef enum
{
	PACKET_ERRCODE_OK = 0x00,
	PACKET_ERRCODE_OPCODE = 0X01,
	PACKET_ERRCODE_OVERFLOW = 0X02,
	PACKET_ERRCODE_TIMEOUT = 0X03,
	PACKET_ERRCODE_FORMAT = 0X04,
	PACKET_ERRCODE_VERIFY = 0X05,
	PACKET_ERRCODE_PARAM = 0X06,
	PACKET_ERRCODE_ = 0XFF,
} packet_errcode_t;

static uint8_t rx_ringbuffer[RX_BUFFER_SIZE];
static rb_t rx_rb;

static packet_state_t packet_state = PACKET_STATE_HEADER;
static uint8_t packet_buffer[PACKET_SIZE_MAX];
static uint32_t packet_index;
static packet_opcode_t packet_opcode;
static uint16_t packet_payload_length;

static bool application_validate(void)
{
	if (!magic_header_validate())
	{
		log_e("magic header invalid");
		return false;
	}

	uint32_t addr = magic_header_get_address();
	uint32_t size = magic_header_get_length();
	uint32_t crc = magic_header_get_crc32();
	uint32_t ccrc = crc32((uint8_t *)addr, size);
	if (crc != ccrc)
	{
		log_e("application crc error: expected %08X, got %08X", crc, ccrc);
		return false;
	}

	return true;
}

static void boot_application(void)
{
	if (!application_validate())
	{
		log_e("application validate failed, cannot boot");
		return;
	}

	log_w("Jump to application.....");
	tim_delay_ms(2);
	led_off(led1);
	TIM_DeInit(TIM6); // 将所有外设恢复为复位状态
	USART_DeInit(USART1);
	USART_DeInit(USART3);
	RCC_DeInit();
	__disable_irq();
	NVIC_DisableIRQ(TIM6_DAC_IRQn); // 关闭中断
	NVIC_DisableIRQ(USART1_IRQn);
	NVIC_DisableIRQ(USART3_IRQn);
	extern void JumpApp(uint32_t base);
	JumpApp(APP_VOTR_ADDR);
}

static void bl_response(packet_opcode_t opcode, packet_errcode_t errcode, const uint8_t *data, uint16_t length)
{
//	uint8_t *response = packet_buffer;
//	response[0] = 0x55;
//	response[1] = opcode;
//	response[2] = errcode;
//	response[3] = (uint8_t)(length & 0xFF);
//	response[4] = (uint8_t)((length >> 8) & 0xFF);
//	if (length > 0)
//		memcpy(&response[5], data, length);
//	uint16_t crc = crc16(response, 5 + length);
//	response[5 + length] = (uint8_t)(crc & 0xFF);
//	response[6 + length] = (uint8_t)((crc >> 8) & 0xFF);
//	bl_uart_write(response, 7 + length);
    uint8_t *response = packet_buffer, *prsp = response;
    put_u8_inc(&prsp, PACKET_HEADER_RESPONSE);
    put_u8_inc(&prsp, (uint8_t)opcode);
    put_u8_inc(&prsp, (uint8_t)errcode);
    put_u16_inc(&prsp, length);
    put_bytes_inc(&prsp, data, length);
    uint16_t crc = crc16(response, prsp - response);
    put_u16_inc(&prsp, crc);

    bl_uart_write(response, prsp - response);
}

static void bl_opcode_inquery_handler(void)
{
	log_i("inquery handler");
	if (packet_payload_length != 1)// 查询指令的有效载荷长度必须为1字节，表示查询子指令
	{
		log_e("inquery packet length error");
		return;
	}
	uint8_t subcode = get_u8(packet_buffer + PACKET_PAYLOAD_OFFSET);
	switch (subcode)
	{
	case INQUERY_SUBCODE_VERSION:
	{
		bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, (const uint8_t *)BL_VERSION, strlen(BL_VERSION));
		break;
	}
	case INQUERY_SUBCODE_MTU:
	{
		uint8_t bmtu[2];
		put_u16(bmtu, PAYLOAD_SIZE_MAX);
		bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, (const uint8_t *)bmtu, sizeof(bmtu));
		break;
	}
	default:
	{
		log_w("unknown inquery subcode: %02X", subcode);
		break;
	}
	}
}

static void bl_opcode_erase_handler(void)
{
	log_i("erase handler");
	if (packet_payload_length != ADDR_SIZE_PARAM_LENGTH) // 擦除地址4字节+擦除大小4字节
	{
		bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
		log_e("erase packet length error: %d", packet_payload_length);
		return;
	}

    uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
    uint32_t address = get_u32_inc(&payload);// 从有效载荷中解析出擦除地址和擦除大小
    uint32_t size = get_u32_inc(&payload);

	if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE) // 确保擦除地址范围在flash范围内
	{
		log_e("erase address=0x%08X, size=%u out of range", address, size);
		bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}
	if (address >= BL_ADDRESS && address < BL_ADDRESS + BL_SIZE) // 保护bootloader区域不被擦除
	{
		log_e("address 0x%08X is protected", address);
		bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}
	log_d("erase address=0x%08X, size=%u", address, size);
	stm32_flash_unlock();			  // 解锁
	stm32_flash_erase(address, size); // 擦除
	stm32_flash_lock();				  // 上锁
	bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_program_handler(void)
{
	log_i("program handler");
	if (packet_payload_length <= ADDR_SIZE_PARAM_LENGTH) // 至少9个字节 = 写入地址4字节 + 写入大小4字节 + 写入内容
	{
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		log_e("program packet length error: %d", packet_payload_length);
		return;
	}

	uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
    uint32_t address = get_u32_inc(&payload);
    uint32_t size = get_u32_inc(&payload);
	uint8_t *data = payload; // 指针指向写入数据首地址
	if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
	{
		log_e("program address=0x%08X, size=%u out of range", address, size);
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}
	if (address >= BL_ADDRESS && address < BL_ADDRESS + BL_SIZE)
	{
		log_e("address 0x%08X is protected", address);
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}
	if (size != packet_payload_length - ADDR_SIZE_PARAM_LENGTH) // 写入大小与有效载荷不匹配
	{
		log_e("program size %u does not match payload length %u", size, packet_payload_length - ADDR_SIZE_PARAM_LENGTH);
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}
	log_i("program address=0x%08X, size=%u", address, size);
	stm32_flash_unlock();
	stm32_flash_program(address, data, size); // 执行写入操作
	stm32_flash_lock();
	bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, NULL, 0);
}
static void bl_opcode_verify_handler(void)
{
	log_i("Verify handler");
	if (packet_payload_length != ADDR_SIZE_CRC_PARAM_LENGTH) // 校验起始地址+检验大小+32位校验码
	{
		bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
		log_e("verify packet length error: %d", packet_payload_length);
		return;
	}
	uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
    uint32_t address = get_u32_inc(&payload);
    uint32_t size = get_u32_inc(&payload);
	uint32_t crc = get_u32_inc(&payload);
	if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
	{
		log_e("verify address=0x%08X, size=%u out of range", address, size);
		bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}
	log_d("verify address=0x%08X, size=%u, crc=0x%08X", address, size, crc);
	uint32_t ccrc = crc32((uint8_t *)address, size);
	if (ccrc != crc)
	{
		log_e("verify failed: expected 0x%08X, got 0x%08X", crc, ccrc);
		bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_VERIFY, NULL, 0);
		return;
	}
	bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_OK, NULL, 0);
}
static void bl_opcode_reset_handler(void)
{
	log_i("Reset handler");
	bl_response(PACKET_OPCODE_RESET, PACKET_ERRCODE_OK, NULL, 0);
	log_w("Reset......");
	tim_delay_ms(2);
	NVIC_SystemReset();
}

static void bl_opcode_boot_handler(void)
{
	log_i("Boot handler");
	bl_response(PACKET_OPCODE_BOOT, PACKET_ERRCODE_OK, NULL, 0);

	boot_application();
}
static void bl_packet_handler(void)
{
	switch (packet_opcode)
	{
	case PACKET_OPCODE_INQUERY:
		bl_opcode_inquery_handler();
		break;
	case PACKET_OPCODE_ERASE:
		bl_opcode_erase_handler();
		break;
	case PACKET_OPCODE_PROGRAM:
		bl_opcode_program_handler();
		break;
	case PACKET_OPCODE_VERIFY:
		bl_opcode_verify_handler();
		break;
	case PACKET_OPCODE_RESET:
		bl_opcode_reset_handler();
		break;
	case PACKET_OPCODE_BOOT:
		bl_opcode_boot_handler();
		break;
	default:
		// 未知指令
		log_w("Unknown command: %02X", packet_opcode);
		break;
	}
}

static bool bl_byte_handler(uint8_t data)
{
	bool full_packet = false;

	// 超时检验
	static uint64_t last_byte_ms;
	uint64_t now = tim_get_ms();
	if (now - last_byte_ms > RX_TIMEOUT_MS)
	{
		if (packet_state != PACKET_STATE_HEADER)//防止两包完整数据帧之间的间隔过长导致误判为数据包接收超时，错误打印日志
		{
			log_w("packet timeout: %llu ms", now - last_byte_ms);
		}
		packet_state = PACKET_STATE_HEADER;
		packet_index = 0;
	}
	last_byte_ms = now;

	// 处理接收状态机数据
	log_v("recv: %02X", data);
	packet_buffer[packet_index++] = data;
	switch (packet_state)
	{
	case PACKET_STATE_HEADER:
		if (packet_buffer[PACKET_HEADER_OFFSET] == PACKET_HEADER_REQUEST)
		{
			packet_state = PACKET_STATE_OPCODE;
			log_d("header ok");
		}
		else
		{
			log_w("header error: %02X", packet_buffer[PACKET_HEADER_OFFSET]);
			packet_state = PACKET_STATE_HEADER;
			packet_index = 0;
		}
		break;
	case PACKET_STATE_OPCODE:
		if (
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_INQUERY ||
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_ERASE ||
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_PROGRAM ||
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_VERIFY ||
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_RESET ||
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_BOOT)
		{
			packet_opcode = (packet_opcode_t)packet_buffer[PACKET_OPCODE_OFFSET];
			packet_state = PACKET_STATE_LENGTH;
			log_d("opcode ok: %02X", packet_buffer[PACKET_OPCODE_OFFSET]);
		}
		else
		{
			log_w("opcode error: %02X", packet_buffer[PACKET_OPCODE_OFFSET]);
			packet_state = PACKET_STATE_HEADER;
			packet_index = 0;
		}
		break;
	case PACKET_STATE_LENGTH:
		if (packet_index == PACKET_PAYLOAD_OFFSET)
		{
			 uint16_t payload_length = get_u16(packet_buffer + PACKET_LENGTH_OFFSET);
			if (payload_length <= PAYLOAD_SIZE_MAX)
			{
				log_d("length ok: %u", payload_length);
				packet_payload_length = payload_length;
				if (packet_payload_length > 0)//当有效载荷长度大于0时，进入接收有效载荷状态，否则直接进入接收CRC16状态
				{
					packet_state = PACKET_STATE_PAYLOAD;
				}
				else
				{
					packet_state = PACKET_STATE_CRC16;//例如重启，跳转指令没有有效载荷，直接进入接收CRC16状态
				}
			}
			else
			{
				log_w("length error: %u", payload_length);
				packet_state = PACKET_STATE_HEADER;
				packet_index = 0;
			}
		}
		break;
	case PACKET_STATE_PAYLOAD:
		if (packet_index == PACKET_PAYLOAD_OFFSET + packet_payload_length)
		{
			packet_state = PACKET_STATE_CRC16;
			log_d("payload ok");
		}
		break;
	case PACKET_STATE_CRC16:
		if (packet_index == PACKET_MIN_SIZE + packet_payload_length)
		{
			uint16_t received_crc = get_u16(packet_buffer + PACKET_PAYLOAD_OFFSET + packet_payload_length);
			uint16_t calculated_crc = crc16(packet_buffer, PACKET_PAYLOAD_OFFSET + packet_payload_length);
			if (received_crc == calculated_crc)
			{
				full_packet = true;
				log_d("CRC16 ok: %04X", received_crc);
				log_d("packet received: opcode=%02X, length=%u", packet_opcode, packet_payload_length);
				if (LOG_LVL >= ELOG_LVL_VERBOSE)
                        elog_hexdump("payload", 16, packet_buffer, PACKET_MIN_SIZE + packet_payload_length);
			}
			else
			{
				log_w("CRC error: received %04X, calculated %04X", received_crc, calculated_crc);
			}
			packet_state = PACKET_STATE_HEADER;
			packet_index = 0;
		}
		break;
	default:
		break;
	}
	return full_packet;
}

static void bl_rx_handler(const uint8_t *data, uint32_t size)
{
	rb_puts(rx_rb, data, size);
}
// 检测按键是否持续按下达到进入bootloader的条件(按下3秒)
static bool key_trap_check(void)
{
	for (uint32_t t = 0; t < BOOT_DELAY; t += 10)
	{
		tim_delay_ms(10);
		if (!key_read(key4)) // 按键松开，不进入bootloader
			return false;
	}
	log_w("key pressed, trap into boot");
	return true;
}

static void wait_key_release(void) // 释放按键，避免进入bootloader后按键状态未释放导致误判为再次按键触发重启
{
	while (key_read(key4))
		tim_delay_ms(10);
}

static bool key_press_check(void) // 已经进入bootloader模式，检测按键是否被按下，按下则返回true
{
	if (!key_read(key4))
		return false;

	tim_delay_ms(10); // 消抖
	if (!key_read(key4))
		return false;
	return true;
}

bool magic_header_trap_boot(void)
{
	if (!magic_header_validate())
	{
		log_w("magic header invalid, trap into boot");
		return true;
	}

	if (!application_validate())
	{
		log_w("application validate failed, trap into boot");
		return true;
	}

	return false;
}

bool rx_trap_boot(void)
{
    for (uint32_t i = 0; i < 3000; i += 10)
    {
        tim_delay_ms(10);
        if (!rb_empty(rx_rb))
        {
            log_w("data received, trap into boot");
            return true;
        }
    }
	 log_i("3 seconds passed, jump to application");
    return false;
}

void bootloader_main(void)
{
	log_i("Bootloader start");
	led_init(led1);
	key_init(key4);
	bl_uart_init();
	bl_uart_register_rx_callback(bl_rx_handler);
	rx_rb = rb_new(rx_ringbuffer, RX_BUFFER_SIZE);

	bool trapboot = false;
	if (!trapboot)
	// 检测magic header是否有效，或者应用程序是否有效，无效则进入bootloader
		trapboot = magic_header_trap_boot();

	if (!trapboot)
	//检测是否按键按下达到进入bootloader的条件(按下3秒)
		trapboot = key_trap_check();
	
	if (!trapboot)
	//检测在程序上电后3秒内是否有数据通过串口接收，若有则进入bootloader
		trapboot = rx_trap_boot();

	if (!trapboot)
		boot_application();

	led_on(led1);
	wait_key_release(); // 等待释放按键，避免进入bootloader后按键状态未释放导致误判为再次按键触发重启

	while (1)
	{
		if (key_press_check())
		{
			log_w("key pressed, rebooting...");
			tim_delay_ms(2);
			NVIC_SystemReset(); // bootloader模式下，按下按键则重启
		}
		if (!rb_empty(rx_rb))
		{
			uint8_t byte;
			rb_get(rx_rb, &byte);
			if (bl_byte_handler(byte))
			{
				bl_packet_handler();
			}
		}
	}
}
