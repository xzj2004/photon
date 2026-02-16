#pragma once

#include <Arduino.h>

// STM32F031K6: 32KB Flash, 1KB page size
// 使用最后一页存储数据 (地址: 0x08007C00 - 0x08007FFF)
#define FLASH_STORAGE_PAGE_ADDR  ((uint32_t)0x08007C00)
#define FLASH_PAGE_SIZE          1024

// 数据结构: 使用简单的 magic + address 格式
#define FLASH_DATA_MAGIC         0xAA55

class FeederFloor {
  public:
    FeederFloor();

    uint8_t read_floor_address();
    bool write_floor_address(uint8_t address);

  private:
    void flash_unlock();
    void flash_lock();
    bool flash_erase_page(uint32_t page_addr);
    bool flash_write_halfword(uint32_t addr, uint16_t data);
};
