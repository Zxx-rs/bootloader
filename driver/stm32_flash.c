#include "stm32f4xx.h"
#include "stdio.h"
#include "stdint.h"
#include "utils.h"

#define LOG_TAG    "flash"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

#define FLASH_BASE_ADDRESS 0x08000000

typedef struct
{
    uint32_t sector;
    uint32_t size;
} sector_desc_t;

// 扇区映射
static const sector_desc_t sector_descs[] =
    {
        {FLASH_Sector_0, 16 * 1024},   // Sector 0: 16 KB
        {FLASH_Sector_1, 16 * 1024},   // Sector 1: 16 KB
        {FLASH_Sector_2, 16 * 1024},   // Sector 2: 16 KB
        {FLASH_Sector_3, 16 * 1024},   // Sector 3: 16 KB
        {FLASH_Sector_4, 64 * 1024},   // Sector 4: 64 KB
        {FLASH_Sector_5, 128 * 1024},  // Sector 5: 128 KB
        {FLASH_Sector_6, 128 * 1024},  // Sector 6: 128 KB
        {FLASH_Sector_7, 128 * 1024},  // Sector 7: 128 KB
        {FLASH_Sector_8, 128 * 1024},  // Sector 8: 128 KB
        {FLASH_Sector_9, 128 * 1024},  // Sector 9: 128 KB
        {FLASH_Sector_10, 128 * 1024}, // Sector10: 128 KB
        {FLASH_Sector_11, 128 * 1024}, // Sector11: 128 KB
};

void stm32_flash_lock(void)
{
    FLASH_Lock(); // 上锁
}

void stm32_flash_unlock(void)
{
    FLASH_Unlock(); // 解锁
}

void stm32_flash_erase(uint32_t address, uint32_t size)
{
    uint32_t addr = FLASH_BASE_ADDRESS;
    for (uint32_t i = 0; i < ARRAY_SIZE(sector_descs); i++)
    {
        // 只要当前这个物理扇区的首地址（addr），落在[address, address + size) 之内，这个扇区就得被擦除
        if (addr >= address && addr < address + size) // 只要
        {
            log_i("erasing sector %u at address 0x%08X size %u", i, addr, sector_descs[i].size);
            if (FLASH_EraseSector(sector_descs[i].sector, VoltageRange_3) != FLASH_COMPLETE)
            {
                log_e("flash erase error");
            }
        }
        addr += sector_descs[i].size;
    }
}
void stm32_flash_program(uint32_t address, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 4)
    {
        if (FLASH_ProgramWord(address + i, *(uint32_t *)(data + i)) != FLASH_COMPLETE)
        {
            log_e("flash program error at address 0x%08X", address + i);
        }
    }
}
