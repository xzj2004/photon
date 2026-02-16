![Photon Firmware](photon-firmware.png)

# Photon Firmware - 免 EEPROM 修改版

> 🔧 本仓库为 [photonfirmware/photon](https://github.com/photonfirmware/photon) 的 fork 版本，针对 **无 EEPROM 的 STM32F031K6** 硬件进行了适配。

Photon 是一款开源的贴片机飞达固件，最初作为 LumenPnP 项目的一部分开发，旨在支持多种硬件平台。

---

## ✨ 本 Fork 的特色修改

### 免 EEPROM - 使用 Flash 存储地址

原版固件使用 EEPROM 存储飞达地址等配置数据。由于部分 STM32F031K6 芯片**没有板载 EEPROM**，本修改版使用 **STM32 内部 Flash** 模拟持久化存储。

#### 技术实现

| 项目 | 说明 |
|------|------|
| **存储位置** | Flash 最后一页 (`0x08007C00 - 0x08007FFF`)，共 1KB |
| **数据格式** | Magic 校验值 (`0xAA55`) + 地址字节 |
| **页大小** | 1024 字节（STM32F031K6 规格） |
| **擦写次数** | STM32 Flash 典型支持 10,000 次擦写 |

#### 主要改动文件

- `FeederFloor.h` - 重新定义存储类，添加 Flash 操作接口
- `FeederFloor.cpp` - 实现 Flash 解锁、擦除、半字写入、读取等底层操作
- `main.cpp` - 适配新的 `FeederFloor` 类

#### 代码示例

```cpp
// 读取飞达地址
uint8_t addr = feederFloor.read_floor_address();

// 写入飞达地址
bool success = feederFloor.write_floor_address(0x01);
```

---

## 🔨 编译与烧录

本项目使用 [PlatformIO](http://platformio.org) 进行构建。

### 使用 Black Magic Probe

```sh
pio run
```

### 使用串口烧录（如 FTDI）

```sh
pio run -e photon-serial -t upload --upload-port /dev/ttyUSB0
```

---

## 📦 版本选择

- 使用标记为 `Latest` 的稳定版本
- 带 `rc` 或 `Pre-Release` 标签的为测试版本

---

## 🔗 相关链接

- 原项目: [photonfirmware/photon](https://github.com/photonfirmware/photon)
- LumenPnP 项目: [https://opulo.io](https://opulo.io)
- PlatformIO: [http://platformio.org](http://platformio.org)
- Black Magic Probe: [https://black-magic.org](https://black-magic.org)

---

## 📄 许可证

MPL v2.0
