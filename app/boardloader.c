#include "stdint.h"
#include "stdbool.h"
#include "stdio.h"
#include <string.h>
#include "stm32f4xx.h"
#include "board.h"
#include "bl_uart.h"
#include "bl_eeprom.h"
#include "bl_w25q128.h"
#include "boot_info.h"
#include "tim_delay.h"
#include "stm32_flash.h"
#include "magic_header.h"
#include "ringbuffer.h"
#include "crc16.h"
#include "crc32.h"
#include "utils.h"

#define LOG_TAG "boot"
#define LOG_LVL ELOG_LVL_INFO

#include "elog.h"
#define BL_VERSION "0.0.1"
#define BL_ADDRESS 0x08000000
#define BL_SIZE (48 * 1024)
#define BOOT_DELAY 3000
#define APP_VOTR_ADDR 0x08010000
#define RX_BUFFER_SIZE (5 * 1024)
#define RX_TIMEOUT_MS 20
#define PAYLOAD_SIZE_MAX (4096 + 8)				   // 4096为Program指令的数据长度，8为Program指令的地址(4)和长度(4)
#define PACKET_SIZE_MAX (4 + PAYLOAD_SIZE_MAX + 2) // header(1) + opcode(1) + length(2) + payload + crc16(2)

// 协议常量
#define PACKET_HEADER_REQUEST 0xAA
#define PACKET_HEADER_RESPONSE 0x55

// 数据包结构常量
#define PACKET_HEADER_SIZE 1
#define PACKET_OPCODE_SIZE 1
#define PACKET_LENGTH_SIZE 2
#define PACKET_CRC_SIZE 2

#define PACKET_HEADER_OFFSET 0
#define PACKET_OPCODE_OFFSET 1
#define PACKET_LENGTH_OFFSET 2
#define PACKET_PAYLOAD_OFFSET 4
#define PACKET_MIN_SIZE (PACKET_HEADER_SIZE + PACKET_OPCODE_SIZE + PACKET_LENGTH_SIZE + PACKET_CRC_SIZE)

// 参数长度常量
#define ADDR_SIZE_PARAM_LENGTH 8	  // uint addr + uint size
#define ADDR_SIZE_CRC_PARAM_LENGTH 12 // uint addr + uint size + uint crc
#define BUF_SIZE      4096   /* 搬运/备份缓冲区大小，复用 packet_buffer */
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
	PACKET_OPCODE_RESET    = 0x21,  // 复位
	PACKET_OPCODE_BOOT     = 0x22,  // 引导跳转 (复位后由判决逻辑接管)
	PACKET_OPCODE_SET_FLAG = 0x84,  // 写入 OTA 标志位 (boot_info)
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
	TIM_DeInit(TIM6); // 关闭定时器，恢复为复位状态
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
	if (packet_payload_length != 1)
	{
		log_e("inquery packet length error");
		bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_PARAM, NULL, 0);
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
		bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OPCODE, NULL, 0);
		break;
	}
	}
}

static void bl_opcode_erase_handler(void)
{
	log_i("erase handler (B: W25Q128)");
	if (packet_payload_length != ADDR_SIZE_PARAM_LENGTH)
	{
		bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
		log_e("erase packet length error: %d", packet_payload_length);
		return;
	}
	uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
	uint32_t address = get_u32_inc(&payload);
	uint32_t size    = get_u32_inc(&payload);

	/* 验证地址在 W25Q128 范围内 */
	if (address >= W25Q128_SIZE || address + size > W25Q128_SIZE)
	{
		log_e("erase address=0x%08X, size=%u out of W25Q128 range", address, size);
		bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}

	log_i("erase B: 0x%08X ~ 0x%08X (%u bytes)", address, address + size - 1, size);

	/* 向上对齐到扇区边界 (4KB), 逐个扇区擦除 */
	uint32_t start_sector = address / W25Q128_SECTOR_SIZE;
	uint32_t end_sector   = (address + size + W25Q128_SECTOR_SIZE - 1) / W25Q128_SECTOR_SIZE;
	for (uint32_t sec = start_sector; sec < end_sector; sec++)
	{
		if (!bl_w25q128_erase_sector(sec * W25Q128_SECTOR_SIZE))
		{
			log_e("erase B sector %u failed", sec);
			bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_, NULL, 0);
			return;
		}
	}
	log_i("erase B done: %u sector(s)", end_sector - start_sector);
	bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_program_handler(void)
{
	log_i("program handler (B: W25Q128)");
	if (packet_payload_length <= ADDR_SIZE_PARAM_LENGTH)
	{
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		log_e("program packet length error: %d", packet_payload_length);
		return;
	}
	uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
	uint32_t address = get_u32_inc(&payload);
	uint32_t size    = get_u32_inc(&payload);
	uint8_t *data    = payload;

	/* 验证地址在 W25Q128 范围内 */
	if (address >= W25Q128_SIZE || address + size > W25Q128_SIZE)
	{
		log_e("program address=0x%08X, size=%u out of W25Q128 range", address, size);
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}

	if (size != packet_payload_length - ADDR_SIZE_PARAM_LENGTH)
	{
		log_e("program size %u != payload %u", size, packet_payload_length - ADDR_SIZE_PARAM_LENGTH);
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}

	if (!bl_w25q128_write(address, data, size))
	{
		log_e("program B failed @ 0x%08X", address);
		bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_, NULL, 0);
		return;
	}
	log_i("program B: 0x%08X, %u bytes OK", address, size);
	bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_verify_handler(void)
{
	log_i("verify handler (B: W25Q128)");
	if (packet_payload_length != ADDR_SIZE_CRC_PARAM_LENGTH)
	{
		bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
		log_e("verify packet length error: %d", packet_payload_length);
		return;
	}
	uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
	uint32_t address = get_u32_inc(&payload);
	uint32_t size    = get_u32_inc(&payload);
	uint32_t crc     = get_u32_inc(&payload);

	if (address >= W25Q128_SIZE || address + size > W25Q128_SIZE)
	{
		log_e("verify address=0x%08X, size=%u out of W25Q128 range", address, size);
		bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}

	log_i("verify B: 0x%08X, %u bytes, expected CRC=0x%08X", address, size, crc);

	/* 分块读取 B区 并增量计算 CRC32 (避免大固件撑爆 RAM) */
#define CRC_CHUNK_SIZE 1024
	uint8_t chunk[CRC_CHUNK_SIZE];
	uint32_t ccrc    = 0;
	uint32_t offset  = 0;
	uint32_t elapsed = 0;

	while (offset < size)
	{
		uint32_t chunk_len = CRC_CHUNK_SIZE;
		if (chunk_len > size - offset) chunk_len = size - offset;

		bl_w25q128_read(address + offset, chunk, chunk_len);
		ccrc = crc32_continue(ccrc, chunk, chunk_len);
		offset += chunk_len;

		/* 进度日志 (每 64KB 打印一次) */
		elapsed += chunk_len;
		if (elapsed >= 65536 || offset >= size)
		{
			log_d("verify progress: %u / %u bytes", offset, size);
			elapsed = 0;
		}
	}

	if (ccrc != crc)
	{
		log_e("verify FAIL: calc=0x%08X, expected=0x%08X", ccrc, crc);
		bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_VERIFY, NULL, 0);
		return;
	}

	log_i("verify B OK: CRC=0x%08X", ccrc);
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
	log_i("boot handler reboot to trigger OTA decision");
	bl_response(PACKET_OPCODE_BOOT, PACKET_ERRCODE_OK, NULL, 0);
	tim_delay_ms(100);
	NVIC_SystemReset();
}

/* SET_FLAG: 上位机通知 BootLoader 写入 OTA 标志位
 * Payload: boot_flag(4B) + firmware_len(4B) + firmware_crc(4B) + version(16B) = 28 bytes
 */
static void bl_opcode_set_flag_handler(void)
{
	log_i("SET_FLAG handler");
	uint32_t payload_len = sizeof(uint32_t) * 3 + 16;
	if (packet_payload_length != payload_len)
	{
		log_e("SET_FLAG length error: %u != %u", packet_payload_length, payload_len);
		bl_response(PACKET_OPCODE_SET_FLAG, PACKET_ERRCODE_PARAM, NULL, 0);
		return;
	}

	uint8_t *p = packet_buffer + PACKET_PAYLOAD_OFFSET;
	boot_info_t info;
	info.boot_flag    = get_u32_inc(&p);
	info.firmware_len = get_u32_inc(&p);
	info.firmware_crc = get_u32_inc(&p);
	memcpy(info.version, p, 16);
	info.version[15] = '0';

	log_i("SET_FLAG: flag=0x%02X, len=%u, crc=0x%08X, ver=%s",
	      info.boot_flag, info.firmware_len, info.firmware_crc, info.version);

	if (!boot_info_write(&info))
	{
		log_e("SET_FLAG: boot_info write failed");
		bl_response(PACKET_OPCODE_SET_FLAG, PACKET_ERRCODE_, NULL, 0);
		return;
	}
	bl_response(PACKET_OPCODE_SET_FLAG, PACKET_ERRCODE_OK, NULL, 0);
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
	case PACKET_OPCODE_SET_FLAG:
		bl_opcode_set_flag_handler();
		break;
	case PACKET_OPCODE_RESET:
		bl_opcode_reset_handler();
		break;
	case PACKET_OPCODE_BOOT:
		bl_opcode_boot_handler();
		break;
	default:
		log_w("Unknown command: %02X", packet_opcode);
		break;
	}
}

static bool bl_byte_handler(uint8_t data)
{
	bool full_packet = false;
	// 超时处理
	static uint64_t last_byte_ms;
	uint64_t now = tim_get_ms();
	if (now - last_byte_ms > RX_TIMEOUT_MS)
	{
		if (packet_state != PACKET_STATE_HEADER) // 防止正常帧与干扰帧之间的粘连，超过时间判别为数据包接收超时，并打印日志
		{
			log_w("packet timeout: %llu ms", now - last_byte_ms);
		}
		packet_state = PACKET_STATE_HEADER;
		packet_index = 0;
	}
	last_byte_ms = now;
	// // 根据当前状态进行解析
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
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_BOOT ||
			packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_SET_FLAG)
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
				if (packet_payload_length > 0) // 若有效载荷长度大于0，进入接收有效载荷状态，否则直接进入接收CRC16状态
				{
					packet_state = PACKET_STATE_PAYLOAD;
				}
				else
				{
					packet_state = PACKET_STATE_CRC16; // 若有效载荷长度大于0，进入接收有效载荷状态，否则直接进入接收CRC16状态
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

// 检测按键是否被长按以进入BootLoader模式(持续3秒)

static bool key_trap_check(void)
{
	for (uint32_t t = 0; t < BOOT_DELAY; t += 10)
	{
		tim_delay_ms(10);
		if (!key_read(key1)) // // 按键松开，退出BootLoader
			return false;
	}
	log_w("key pressed, trap into boot");
	return true;
}

static void wait_key_release(void) // 释放按键，防止进入BootLoader后按键状态未释放导致误认为再次按下而复位

{
	while (key_read(key1))
		tim_delay_ms(10);
}

static bool key_press_check(void) // 已经进入BootLoader模式，检测按键是否被按下，若按下返回true

{
	if (!key_read(key1))
		return false;
	tim_delay_ms(10); // 擦除
	if (!key_read(key1))
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

/* 公用: 进入 BootLoader UART 待命模式 (LED 亮, 等待上位机指令) */
static void enter_uart_ota_mode(void)
{
	led_on(led1);
	wait_key_release();
	while (1)
	{
		if (key_press_check())
		{
			log_w("key pressed, rebooting...");
			tim_delay_ms(2);
			NVIC_SystemReset();
		}
		if (!rb_empty(rx_rb))
		{
			uint8_t byte;
			rb_get(rx_rb, &byte);
			if (bl_byte_handler(byte))
				bl_packet_handler();
		}
	}
}

/* ──────────────────────────────────────────────
 * NORMAL 模式：无固件更新，三步陷入判断后跳转 APP
 * ────────────────────────────────────────────── */

static void boot_normal_mode(void)
{
	bool trapboot = false;
	/* 陷阱1: Magic Header 无效 → 进入 BootLoader */
	if (!trapboot)
		trapboot = magic_header_trap_boot();
	/* 陷阱2: 按键长按 3 秒 → 进入 BootLoader */
	if (!trapboot)
		trapboot = key_trap_check();
	/* 陷阱3: 上电 3 秒内串口收到数据 → 进入 BootLoader */
	if (!trapboot)
		trapboot = rx_trap_boot();
	if (!trapboot)
		boot_application();
	enter_uart_ota_mode();
}

/* ──────────────────────────────────────────────
 * 固件升级施工 — B区(W25Q128) → A区(内部Flash)
 *
 *  Step 1/6: 备份当前 A区到 W25Q128@0x800000 (回滚锚点, 已存在则跳过)
 *  Step 2/6: flag = TESTING (0x02), 加锁防掉电
 *  Step 3/6: 擦除全量 A区
 *  Step 4/6: B→A 分块搬运, 同步 CRC 双向比对
 *  Step 5/6: magic_header_write() 写入 0x0800C000
 *  Step 6/6: flag = PENDING_VERIFY (0x03), 等待 APP 确认，NVIC_SystemReset(), 下次启动走判决流程
 *
 *  防掉电: Step1 写 TESTING 后任何步骤断电 → 下次启动重做搬运
 *  回滚:   升级完成但 APP 无法运行时, 长按 KEY4 从备份恢复
 * ────────────────────────────────────────────── */

static void boot_perform_upgrade(boot_info_t *info)
{
	log_i("=== Firmware upgrade started ===");
	log_i("  version: %s, size: %u, CRC: 0x%08X",
	      info->version, info->firmware_len, info->firmware_crc);

	/* 关闭 UART3 接收中断，防止升级期间 ringbuffer 积压 */
	USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);

	uint8_t *buf = (uint8_t *)packet_buffer; /* 复用 4KB 协议缓冲区 */

	/* ── 第 1 步：备份当前 A区到 W25Q128（回滚锚点） ── */
	{
		uint32_t a_size = STM32_FLASH_SIZE - BL_SIZE;//app size

		/* 检查备份区是否已有数据（跳过重复备份，TESTING 重做时免等待） */
		uint32_t chk_word;
		bl_w25q128_read(BACKUP_BASE_ADDR, (uint8_t *)&chk_word, 4);
		if (chk_word == 0xFFFFFFFF)
		{
			log_i("Step 1/6: backup A -> W25Q128@0x%08X (%u bytes)", BACKUP_BASE_ADDR, a_size);
			for (uint32_t addr = BACKUP_BASE_ADDR; addr < BACKUP_BASE_ADDR + a_size; addr += W25Q128_SECTOR_SIZE)
			{
				if (!bl_w25q128_erase_sector(addr))
				{
					log_e("backup erase failed @ 0x%08X", addr);
					return;
				}
			}
			uint32_t offset = 0;
			while (offset < a_size)
			{
				uint32_t chunk = BUF_SIZE;
				if (chunk > a_size - offset) chunk = a_size - offset;
				memcpy(buf, (uint8_t *)(APP_VOTR_ADDR + offset), chunk);
				if (!bl_w25q128_write(BACKUP_BASE_ADDR + offset, buf, chunk))
				{
					log_e("backup write failed @ 0x%08X", offset);
					return;
				}
				offset += chunk;
			}
			log_i("backup OK");
		}
		else
		{
			log_i("Step 1/6: backup already exists, skip");
		}
	}

	/* ── 第 2 步：加锁 ── 
	防掉电，如果断电发生在 Step 3~5 之间，重启后检测到 TESTING 标志仍然存在，
	继续未完成的升级流程，而不是误跳到残废 APP*/
	log_i("Step 2/6: set boot_flag = TESTING (lock)");
	info->boot_flag = BOOT_FLAG_TESTING;
	if (!boot_info_write(info))
	{
		log_e("Failed to set TESTING flag, abort");
		return;
	}

	/* ── 第 3 步：擦除整个 A区 (内部 Flash) ──
	 *  擦全量而非仅 firmware_len：
	 *  防止旧固件比新固件大时，尾部残留旧代码。
	 *  stm32_flash_erase 内部按扇区对齐，不会多擦。 */
	{
		uint32_t a_size = STM32_FLASH_SIZE - BL_SIZE; /* A区总容量 */
		log_i("Step 3/6: erase full A area (0x%08X, %u bytes)",
		      APP_VOTR_ADDR, a_size);
		stm32_flash_unlock();
		stm32_flash_erase(APP_VOTR_ADDR, a_size);
		stm32_flash_lock();
	}

	/* ── 第 4 步：搬运 B区 → A区，同步计算 CRC 双向比对 ── */
	log_i("Step 4/6: copy B -> A (%u bytes), CRC in-pass", info->firmware_len);
	uint32_t src_crc = 0;   /* B区读取时累积 */
	uint32_t dst_crc = 0;   /* A区写入后回读累积 */
	uint32_t offset  = 0;
	while (offset < info->firmware_len)
	{
		uint32_t chunk = BUF_SIZE;
		if (chunk > info->firmware_len - offset)
			chunk = info->firmware_len - offset;

		bl_w25q128_read(offset, buf, chunk);
		src_crc = crc32_continue(src_crc, buf, chunk);

		stm32_flash_unlock();
		stm32_flash_program(APP_VOTR_ADDR + offset, buf, chunk);
		stm32_flash_lock();

		dst_crc = crc32_continue(dst_crc, (uint8_t *)(APP_VOTR_ADDR + offset), chunk);

		offset += chunk;
		if ((offset & 0xFFFF) == 0 || offset >= info->firmware_len)
			log_d("copy+verify: %u / %u bytes", offset, info->firmware_len);
	}

	if (src_crc != dst_crc)
	{
		log_e("COPY FAIL: B CRC=0x%08X != A CRC=0x%08X", src_crc, dst_crc);
		return;
	}
	log_i("B->A copy OK, CRC=0x%08X", src_crc);

	/* ── 第 5 步：写入 Magic Header 到 0x0800C000 ── */
	log_i("Step 5/6: write Magic Header to 0x%08X", MAGIC_HEADER_ADDR);
	if (!magic_header_write(APP_VOTR_ADDR, info->firmware_len, src_crc, info->version))
	{
		log_e("Magic Header write failed");
		return;
	}
	log_i("Magic Header written OK");

	/* ── 第 6 步：改账本闭环 ── */
	log_i("Step 6/6: upgrade done, set boot_flag = PENDING_VERIFY");
	info->boot_flag    = BOOT_FLAG_PENDING_VERIFY;
	info->firmware_len = 0;
	info->firmware_crc = 0;
	memset(info->version, 0, sizeof(info->version));
	if (!boot_info_write(info))
	{
		log_e("Failed to write final boot_info");
		return;
	}

	log_i("=== Upgrade complete, rebooting... ===");
	tim_delay_ms(100);
	NVIC_SystemReset();
}

/* ──────────────────────────────────────────────
 * Bootloader 主入口
 *
 *  启动判决三步法:
 *   第一步: 验证账本 — 从 EEPROM 读 boot_info 并校验 CRC。
 *   第二步: 根据 boot_flag 命运判决:
 *           NORMAL (0x00) → 正常模式，三步陷阱判断后跳 APP。
 *           NEW_FW (0x01) → 固件升级模式，搬运 B→A。
 *           TESTING(0x02) → 掉电恢复模式，重新搬运 B→A。
 *   第三步: 升级完成后改账本闭环 — boot_flag 写回 NORMAL。
 * ────────────────────────────────────────────── */

void bootloader_main(void)
{
	log_i("Bootloader start");
	led_init(led1);
	key_init(key1);
	bl_uart_init();
	bl_uart_register_rx_callback(bl_rx_handler);
	rx_rb = rb_new(rx_ringbuffer, RX_BUFFER_SIZE);
	/* 初始化 BL24C512 EEPROM (I2C1: PB6=SCL, PB7=SDA) */
	bl_eeprom_init();
	/* 初始化 W25Q128 外挂 Flash (SPI1: PA5=SCK, PA6=MISO, PA7=MOSI, PE13=CS) */
	bl_w25q128_init();
	/* ──── 第一步：读取OTA标志位 ──── */
	boot_info_t boot_info;
	if (!boot_info_read(&boot_info))
	{
		/* CRC 校验失败（两区均坏），已自动使用默认值 (NORMAL) */
		log_w("boot_info recovered with defaults");
	}
	/* ──── 第二步：根据 boot_flag 做出判决 ──── */
	/* 快速检查B区有效性：若flag 要求升级但B区为空，清标志走NORMAL */
	if (boot_info.boot_flag == BOOT_FLAG_NEW_FW ||
	    boot_info.boot_flag == BOOT_FLAG_TESTING)
	{
		uint32_t first_word;
		bl_w25q128_read(0, (uint8_t *)&first_word, 4);
		if (first_word == 0xFFFFFFFF || boot_info.firmware_len == 0)
		{
			log_w("B area empty, clearing boot_flag to NORMAL");
			boot_info.boot_flag    = BOOT_FLAG_NORMAL;
			boot_info.firmware_len = 0;
			boot_info.firmware_crc = 0;
			boot_info_write(&boot_info);
		}
	}

	switch (boot_info.boot_flag)
	{
	case BOOT_FLAG_NORMAL:
		/* 没有固件更新，直接走三步陷阱判断 → 跳转 APP */
		log_i("Flag: NORMAL -- no firmware update, booting app");
		boot_normal_mode();
		break;

	case BOOT_FLAG_PENDING_VERIFY:
		/* 搬运已完成，等待用户确认 APP 正常。
		 * 无延时直接跳 APP。若 APP 有问题，用户长按 KEY1 触发回滚。 */
		log_i("Flag: PENDING_VERIFY -- verifying new APP...");
		{
			bool rollback = key_trap_check();  /* 长按 3s = 回滚 */
			if (rollback)
			{
				log_w("KEY1 held: ROLLBACK triggered!");
				/* 从 W25Q128 备份区恢复旧固件到 A区 */
				uint32_t a_size = STM32_FLASH_SIZE - BL_SIZE;
				log_i("Restoring backup from W25Q128@0x%08X...", BACKUP_BASE_ADDR);
				stm32_flash_unlock();
				stm32_flash_erase(APP_VOTR_ADDR, a_size);
				uint8_t *rbuf = (uint8_t *)packet_buffer;
				for (uint32_t off = 0; off < a_size; off += BUF_SIZE)
				{
					uint32_t chunk = BUF_SIZE;
					if (chunk > a_size - off) chunk = a_size - off;
					bl_w25q128_read(BACKUP_BASE_ADDR + off, rbuf, chunk);
					stm32_flash_program(APP_VOTR_ADDR + off, rbuf, chunk);
				}
				stm32_flash_lock();
				/* 回滚完成，清除标志 */
				boot_info.boot_flag    = BOOT_FLAG_NORMAL;
				boot_info.firmware_len = 0;
				boot_info.firmware_crc = 0;
				memset(boot_info.version, 0, sizeof(boot_info.version));
				boot_info_write(&boot_info);
				log_i("Rollback complete, rebooting...");
				tim_delay_ms(100);
				NVIC_SystemReset();
			}
			else
			{
				/* 无按键 → 直接跳 APP */
				boot_application();
				/* APP 校验失败 → 进入 UART 模式 */
				log_w("APP invalid, entering bootloader UART mode");
				enter_uart_ota_mode();
			}
		}
		break;

	case BOOT_FLAG_NEW_FW:
		/* 有新固件，启动升级施工程序 */
		log_i("Flag: NEW_FW -- firmware update required, starting upgrade");
		boot_perform_upgrade(&boot_info);
		/* 如果升级失败没有复位，降级进入 BootLoader UART 模式 */
		log_e("Upgrade failed, falling back to bootloader UART mode");
		enter_uart_ota_mode();
	case BOOT_FLAG_TESTING:
		/* 上次升级中途断电，重新搬运 */
		log_w("Flag: TESTING -- previous upgrade was interrupted, redoing...");
		boot_perform_upgrade(&boot_info);
		/* 再次失败 → 降级进入 BootLoader UART 模式 */
		log_e("Recovery upgrade failed, falling back to bootloader UART mode");
		enter_uart_ota_mode();
	default:
		log_e("Unknown boot_flag: 0x%08X, treating as NORMAL", boot_info.boot_flag);
		boot_normal_mode();
		break;
	}
}
