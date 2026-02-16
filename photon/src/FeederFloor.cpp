#include "FeederFloor.h"

// STM32F0 Flash 寄存器定义
#define FLASH_BASE_ADDR      ((uint32_t)0x40022000)
#define FLASH_ACR            (*(volatile uint32_t*)(FLASH_BASE_ADDR + 0x00))
#define FLASH_KEYR           (*(volatile uint32_t*)(FLASH_BASE_ADDR + 0x04))
#define FLASH_SR             (*(volatile uint32_t*)(FLASH_BASE_ADDR + 0x0C))
#define FLASH_CR             (*(volatile uint32_t*)(FLASH_BASE_ADDR + 0x10))
#define FLASH_AR             (*(volatile uint32_t*)(FLASH_BASE_ADDR + 0x14))

// FLASH_SR 位定义
#define FLASH_SR_BSY         (1 << 0)
#define FLASH_SR_EOP         (1 << 5)

// FLASH_CR 位定义
#define FLASH_CR_PG          (1 << 0)
#define FLASH_CR_PER         (1 << 1)
#define FLASH_CR_STRT        (1 << 6)
#define FLASH_CR_LOCK        (1 << 7)

// Flash 解锁密钥
#define FLASH_KEY1           ((uint32_t)0x45670123)
#define FLASH_KEY2           ((uint32_t)0xCDEF89AB)

FeederFloor::FeederFloor() {
}

void FeederFloor::flash_unlock() {
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

void FeederFloor::flash_lock() {
    FLASH_CR |= FLASH_CR_LOCK;
}

bool FeederFloor::flash_erase_page(uint32_t page_addr) {
    // 等待之前的操作完成
    while (FLASH_SR & FLASH_SR_BSY);
    
    // 设置页擦除
    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = page_addr;
    FLASH_CR |= FLASH_CR_STRT;
    
    // 等待擦除完成
    while (FLASH_SR & FLASH_SR_BSY);
    
    // 清除 PER 位
    FLASH_CR &= ~FLASH_CR_PER;
    
    // 检查擦除是否成功 (读取地址应该为 0xFFFF)
    return (*(volatile uint16_t*)page_addr == 0xFFFF);
}

bool FeederFloor::flash_write_halfword(uint32_t addr, uint16_t data) {
    // 等待之前的操作完成
    while (FLASH_SR & FLASH_SR_BSY);
    
    // 设置编程位
    FLASH_CR |= FLASH_CR_PG;
    
    // 写入半字
    *(volatile uint16_t*)addr = data;
    
    // 等待写入完成
    while (FLASH_SR & FLASH_SR_BSY);
    
    // 清除 PG 位
    FLASH_CR &= ~FLASH_CR_PG;
    
    // 验证写入
    return (*(volatile uint16_t*)addr == data);
}

uint8_t FeederFloor::read_floor_address() {
    // 读取 magic 值
    uint16_t magic = *(volatile uint16_t*)FLASH_STORAGE_PAGE_ADDR;
    
    // 检查 magic 是否正确
    if (magic != FLASH_DATA_MAGIC) {
        // Flash 未初始化或数据无效
        // 0x00 表示未编程
        return 0x00;
    }
    
    // 读取地址值 (存储在 magic 之后)
    uint16_t addr_data = *(volatile uint16_t*)(FLASH_STORAGE_PAGE_ADDR + 2);
    
    return (uint8_t)(addr_data & 0xFF);
}

bool FeederFloor::write_floor_address(uint8_t address) {
    // 禁用中断以确保原子操作
    __disable_irq();
    
    // 解锁 Flash
    flash_unlock();
    
    // 擦除页
    if (!flash_erase_page(FLASH_STORAGE_PAGE_ADDR)) {
        flash_lock();
        __enable_irq();
        return false;
    }
    
    // 写入 magic
    if (!flash_write_halfword(FLASH_STORAGE_PAGE_ADDR, FLASH_DATA_MAGIC)) {
        flash_lock();
        __enable_irq();
        return false;
    }
    
    // 写入地址 (作为半字，高字节为 0xFF 作为校验)
    uint16_t addr_data = 0xFF00 | address;
    if (!flash_write_halfword(FLASH_STORAGE_PAGE_ADDR + 2, addr_data)) {
        flash_lock();
        __enable_irq();
        return false;
    }
    
    // 锁定 Flash
    flash_lock();
    
    // 恢复中断
    __enable_irq();
    
    // 验证写入
    return (read_floor_address() == address);
}
