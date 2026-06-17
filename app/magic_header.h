#ifndef __MAGIC_HEADER_H__
#define __MAGIC_HEADER_H__

#include <stdbool.h>
#include <stdint.h>

#define MAGIC_HEADER_ADDR  0x0800C000  /* Magic Header ��ַ */

typedef enum
{
    MAGIC_HEADER_TYPE_APP = 1,
} magic_header_type_t;

bool magic_header_validate(void);
magic_header_type_t magic_header_get_type(void);
uint32_t magic_header_get_offset(void);
uint32_t magic_header_get_address(void);
uint32_t magic_header_get_length(void);
uint32_t magic_header_get_crc32(void);

/**
 * @brief  将固件描述信息写入 0x0800C000 的 Magic Header。
 *         Bootloader 搬运 B→A 成功后调用，持久化固件元数据。
 * @param  fw_addr  固件在 A区的起始地址 (APP_VOTR_ADDR)
 * @param  fw_len   固件长度（字节）
 * @param  fw_crc   固件 CRC32（Bootloader 搬运时自己计算的）
 * @param  version  版本号字符串（最长 127 + '\0'）
 * @return true 表示写入成功
 */
bool magic_header_write(uint32_t fw_addr, uint32_t fw_len, uint32_t fw_crc, const char *version);

#endif /* __MAGIC_HEADER_H__ */
