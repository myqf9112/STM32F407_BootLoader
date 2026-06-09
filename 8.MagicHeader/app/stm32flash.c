#include "stm32flash.h"
#include "stdint.h"
#include "stdio.h"
#define FLASH_BASE_ADDR 0x08000000
typedef struct
{
    uint32_t sector;
    uint32_t size;
} sector_desc_t;

static const sector_desc_t sector_desc[] =
{
        {FLASH_Sector_0, 16 * 1024},   // sector 0, 16KB
        {FLASH_Sector_1, 16 * 1024},   // sector 1, 16KB
        {FLASH_Sector_2, 16 * 1024},   // sector 2, 16KB
        {FLASH_Sector_3, 16 * 1024},   // sector 3, 16KB
        {FLASH_Sector_4, 64 * 1024},   // sector 4, 64KB
        {FLASH_Sector_5, 128 * 1024},  // sector 5, 128KB
        {FLASH_Sector_6, 128 * 1024},  // sector 6, 128KB
        {FLASH_Sector_7, 128 * 1024},  // sector 7, 128KB
        {FLASH_Sector_8, 128 * 1024},  // sector 8, 128KB
        {FLASH_Sector_9, 128 * 1024},  // sector 9, 128KB
        {FLASH_Sector_10, 128 * 1024}, // sector 10, 128KB
        {FLASH_Sector_11, 128 * 1024}  // sector 11, 128KB
};

void stm32_flash_unlock(void)
{
    FLASH_Unlock();
}
void stm32_flash_lock(void)
{
    FLASH_Lock();
}

void stm32_flash_erase(uint32_t address, uint32_t size)
{
uint32_t addr = FLASH_BASE_ADDR;
    for (uint32_t i = 0; i < sizeof(sector_desc) / sizeof(sector_desc_t); i++)
    {
        if (addr >= address && addr < address + size)
        {
            printf("erasing sector %lu at address 0x%08lx size %lu\n", i, addr, sector_desc[i].size);
            if (FLASH_EraseSector(sector_desc[i].sector, VoltageRange_3) != FLASH_COMPLETE)
            {
                printf("flash erase error\n");
            }
        }
        addr += sector_desc[i].size;
    }
}
void stm32_flash_program(uint32_t address, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 4)
    {
        if (FLASH_ProgramWord(address + i, *(uint32_t *)(data + i)) != FLASH_COMPLETE)
        {
            printf("failed to program word at address 0x%08lx\n", address + i);
        }
    }
}
