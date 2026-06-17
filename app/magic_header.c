#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "crc32.h"
#include "utils.h"
#include "magic_header.h"
#include "stm32_flash.h"

#define MAGIC_HEADER_MAGIC 0x4D414749 // "MAGI"

typedef struct
{
    uint32_t magic;         // 魔数，用于标识这是一个有效的 Magic Header
    uint32_t bitmask;       // 位掩码，用于标识哪些字段有效
    uint32_t reserved1[6];  // 预留字段，供未来扩展使用

    uint32_t data_type;     // 固件类型，可选固件类型和启动位置
    uint32_t data_offset;   // 固件文件相对于 Magic Header 的偏移
    uint32_t data_address;  // 固件写入的实际地址
    uint32_t data_length;   // 固件长度
    uint32_t data_crc32;    // 固件的 CRC32 校验值
    uint32_t reserved2[11]; // 预留字段，供未来扩展使用

    char version[128];      // 固件版本字符串

    uint32_t reserved3[6];  // 预留字段，供未来扩展使用
    uint32_t this_address;  // 该结构体在存储介质中的实际地址
    uint32_t this_crc32;    // 该结构体本身的 CRC32 校验值
} magic_header_t;

bool magic_header_validate(void)
{
    // 0x0800C000 是 Magic Header 的存储地址，从此地址开始的 256 字节被映射为一个 magic_header_t 结构体
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;

    if (header->magic != MAGIC_HEADER_MAGIC)
        return false;

    uint32_t ccrc = crc32((uint8_t *)header, offset_of(magic_header_t, this_crc32));
    if (ccrc != header->this_crc32)
        return false;

    return true;
}

magic_header_type_t magic_header_get_type(void)
{
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return (magic_header_type_t)header->data_type;
}

uint32_t magic_header_get_offset(void)
{
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_offset;
}

uint32_t magic_header_get_address(void)
{
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_address;
}

uint32_t magic_header_get_length(void)
{
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_length;
}

uint32_t magic_header_get_crc32(void)
{
    magic_header_t *header = (magic_header_t *)MAGIC_HEADER_ADDR;
    return header->data_crc32;
}

bool magic_header_write(uint32_t fw_addr, uint32_t fw_len, uint32_t fw_crc, const char *version)
{
    magic_header_t header;

    memset(&header, 0, sizeof(header));

    header.magic        = MAGIC_HEADER_MAGIC;
    header.data_type    = MAGIC_HEADER_TYPE_APP;
    header.data_offset  = fw_addr - MAGIC_HEADER_ADDR;  /* 头部到固件的 Flash 偏移 */
    header.data_address = fw_addr;
    header.data_length  = fw_len;
    header.data_crc32   = fw_crc;
    header.this_address = MAGIC_HEADER_ADDR;

    if (version)
    {
        strncpy(header.version, version, sizeof(header.version) - 1);
        header.version[sizeof(header.version) - 1] = '\0';
    }

    /* 计算头部自身 CRC (前 252 字节, 不含 this_crc32 字段) */
    header.this_crc32 = crc32((uint8_t *)&header, offset_of(magic_header_t, this_crc32));

    /* 擦除并写入 */
    stm32_flash_unlock();
    stm32_flash_erase(MAGIC_HEADER_ADDR, sizeof(header));
    stm32_flash_program(MAGIC_HEADER_ADDR, (uint8_t *)&header, sizeof(header));
    stm32_flash_lock();

    return magic_header_validate();
}
