#ifndef _CRC_CRC32_H
#define _CRC_CRC32_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus  */

#include <stddef.h>
#include <stdint.h>

uint32_t crc32(const unsigned char *s, size_t len);

/* 增量 CRC32: 从 prev_crc 继续计算, prev_crc=0 等价于 crc32() */
uint32_t crc32_continue(uint32_t prev_crc, const unsigned char *s, size_t len);

#ifdef __cplusplus
}
#endif /* __cplusplus  */
#endif
