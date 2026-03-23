/*
Photon Feeder Firmware
Part of the LumenPnP Project
MPL v2
2025
*/

#include "define.h"

#ifdef UNIT_TEST
  #include <ArduinoFake.h>
#else
  #include <Arduino.h>
  #include <HardwareSerial.h>
  #include <ArduinoUniqueID.h>
  #include <rs485/rs485bus.hpp>
#endif // UNIT_TEST

#ifndef MOTOR_DEPS
#define MOTOR_DEPS

#include <RotaryEncoder.h>

#endif

#include "FeederFloor.h"
#include "PhotonFeeder.h"
#include "PhotonFeederProtocol.h"
#include "PhotonNetworkLayer.h"

#include <rs485/rs485bus.hpp>
#include <rs485/bus_adapters/hardware_serial.h>
#include <rs485/filters/filter_by_value.h>
#include <rs485/protocols/photon.h>
#include <rs485/packetizer.h>

#include "bootloader.h"

#define BAUD_RATE 57600

//-----
// Global Variables
//-----

#ifdef UNIT_TEST
StreamFake ser();
#else
HardwareSerial ser(PA10, PA9);
#endif // ARDUINO

// Flash Storage
FeederFloor feederFloor;

// RS485
HardwareSerialBusIO busIO(&ser);
RS485Bus<RS485_BUS_BUFFER_SIZE> bus(busIO, _RE, DE);
PhotonProtocol photon_protocol;
Packetizer packetizer(bus, photon_protocol);
FilterByValue addressFilter(0);

// Encoder
RotaryEncoder encoder(DRIVE_ENC_A, DRIVE_ENC_B, RotaryEncoder::LatchMode::TWO03);

// Flags
bool drive_mode = false;
bool driving = false;
bool driving_direction = false;

// Feeder Class Instances
PhotonFeeder *feeder;
PhotonFeederProtocol *protocol;
PhotonNetworkLayer *network;

//-------
//FUNCTIONS
//-------

void checkPosition()
{
  encoder.tick(); // just call tick() to check the state.
}

//-------
//SETUP
//-------

// -------
// 串口地址编程模式
// 通过USB转TTL连接PA9/PA10，发送指令设置飞达地址
// 指令格式: "ADDR:XX\n"  其中XX为十六进制地址(01-FE)
// 查询当前地址: "ADDR?\n"
// 退出编程模式: "EXIT\n"
// -------

#define ADDR_PROGRAM_BAUD   115200       // 编程模式波特率
#define ADDR_PROGRAM_WAIT   5000         // 等待编程指令的超时时间(ms)
#define ADDR_CMD_BUFFER_SIZE 16          // 指令缓冲区大小

// 解析十六进制字符为数值
static uint8_t hexCharToVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0xFF; // 无效字符
}

// 串口地址编程模式处理函数
// 返回 true 表示进行了编程（需要重新初始化RS485串口波特率）
bool handleAddressProgramming() {
  char cmdBuffer[ADDR_CMD_BUFFER_SIZE];
  uint8_t cmdIndex = 0;
  uint32_t startTime = millis();
  bool programModeActive = true;
  bool didProgram = false;

  // 配置RS485方向控制引脚
  pinMode(DE, OUTPUT);
  pinMode(_RE, OUTPUT);

  // 用编程模式波特率初始化串口
  ser.begin(ADDR_PROGRAM_BAUD);
  
  // 先设置为发送模式，发送提示信息
  digitalWrite(DE, HIGH);   // DE=HIGH: 发送使能
  digitalWrite(_RE, HIGH);  // _RE=HIGH: 发送时不接收自己的回声

  // 蓝色LED闪烁，表示进入编程等待模式
  bool ledState = false;
  uint32_t ledTimer = millis();

  // 先发送提示信息
  ser.println(F("=== Photon Feeder Address Programming ==="));
  byte currentAddr = feederFloor.read_floor_address();
  ser.print(F("Current Address: 0x"));
  if (currentAddr < 0x10) ser.print('0');
  ser.println(currentAddr, HEX);
  ser.println(F("Commands:"));
  ser.println(F("  ADDR:XX  - Set address (XX = 01-FE hex)"));
  ser.println(F("  ADDR?    - Query current address"));
  ser.println(F("  EXIT     - Exit programming mode"));
  ser.print(F("Waiting for command ("));
  ser.print(ADDR_PROGRAM_WAIT / 1000);
  ser.println(F("s timeout)..."));
  ser.flush(); // 等待所有数据发送完毕

  // 切换到接收模式，等待用户输入
  digitalWrite(DE, LOW);    // DE=LOW: 发送禁用
  digitalWrite(_RE, LOW);   // _RE=LOW: 接收使能

  while (programModeActive) {
    // LED闪烁（蓝色，200ms间隔）
    if (millis() - ledTimer > 200) {
      ledState = !ledState;
      digitalWrite(LED_B, ledState ? LOW : HIGH);
      digitalWrite(LED_R, HIGH);
      digitalWrite(LED_G, HIGH);
      ledTimer = millis();
    }

    // 检查是否超时
    if (!didProgram && (millis() - startTime > ADDR_PROGRAM_WAIT)) {
      // 切换到发送模式回复
      digitalWrite(DE, HIGH);
      digitalWrite(_RE, HIGH);
      ser.println(F("Timeout, entering normal mode..."));
      ser.flush();
      digitalWrite(DE, LOW);
      digitalWrite(_RE, LOW);
      programModeActive = false;
      break;
    }

    // 读取串口数据（接收模式下）
    if (ser.available()) {
      char c = ser.read();
      startTime = millis(); // 收到任何数据都重置超时

      // 忽略单独的 \r 和 \n，处理换行
      if (c == '\n' || c == '\r') {
        if (cmdIndex > 0) {
          cmdBuffer[cmdIndex] = '\0';

          // 切换到发送模式，准备回复
          digitalWrite(DE, HIGH);
          digitalWrite(_RE, HIGH);
          
          // 解析指令 —— ADDR:XX
          if (strncmp(cmdBuffer, "ADDR:", 5) == 0) {
            if (cmdIndex == 7) {
              // 设置地址指令: ADDR:XX
              uint8_t high = hexCharToVal(cmdBuffer[5]);
              uint8_t low = hexCharToVal(cmdBuffer[6]);

              if (high == 0xFF || low == 0xFF) {
                ser.println(F("ERROR: Invalid hex address"));
              } else {
                uint8_t newAddr = (high << 4) | low;
                if (newAddr == 0x00 || newAddr == 0xFF) {
                  ser.println(F("ERROR: Address 0x00 and 0xFF are reserved"));
                } else {
                  // 写入Flash
                  bool success = feederFloor.write_floor_address(newAddr);
                  if (success) {
                    ser.print(F("OK: Address set to 0x"));
                    if (newAddr < 0x10) ser.print('0');
                    ser.println(newAddr, HEX);
                    ser.println(F("Entering normal mode..."));
                    // 绿色LED亮起表示成功
                    digitalWrite(LED_B, HIGH);
                    digitalWrite(LED_R, HIGH);
                    digitalWrite(LED_G, LOW);
                    delay(1000);
                    didProgram = true;
                    programModeActive = false; // 编程成功，退出编程模式
                  } else {
                    ser.println(F("ERROR: Flash write failed"));
                    // 红色LED亮起表示失败
                    digitalWrite(LED_B, HIGH);
                    digitalWrite(LED_G, HIGH);
                    digitalWrite(LED_R, LOW);
                    delay(1000);
                  }
                }
              }
            } else {
              ser.println(F("ERROR: Format should be ADDR:XX (e.g. ADDR:0A)"));
            }
          } else if (strcmp(cmdBuffer, "ADDR?") == 0) {
            // 查询地址指令
            uint8_t addr = feederFloor.read_floor_address();
            ser.print(F("Current Address: 0x"));
            if (addr < 0x10) ser.print('0');
            ser.println(addr, HEX);
            if (addr == 0x00) {
              ser.println(F("(Not programmed)"));
            }
          } else if (strcmp(cmdBuffer, "EXIT") == 0) {
            ser.println(F("Exiting programming mode..."));
            programModeActive = false;
          } else {
            ser.print(F("Unknown command: "));
            ser.println(cmdBuffer);
          }

          // 等待发送完毕，切回接收模式
          ser.flush();
          digitalWrite(DE, LOW);
          digitalWrite(_RE, LOW);
          
          cmdIndex = 0;
        }
        // cmdIndex == 0 时收到 \r 或 \n 直接忽略（处理 \r\n 双字节换行）
      } else {
        if (cmdIndex < ADDR_CMD_BUFFER_SIZE - 1) {
          cmdBuffer[cmdIndex++] = c;
        }
      }
    }
  }

  // 关闭所有LED
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);

  // 结束串口（后面会用RS485的波特率重新初始化）
  ser.end();

  return didProgram;
}

void setup() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  // 先关闭LED（直接操作引脚，因为feeder还未初始化）
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);

  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(MOTOR_ENABLE, OUTPUT);
  digitalWrite(MOTOR_ENABLE, HIGH);

  // ---- 检查是否进入串口地址编程模式 ----
  // 条件: 启动时按住SW1（底部按钮）
  // 或者: 始终进入编程等待（3秒超时）
  bool forceProgramMode = !digitalRead(SW1); // 按住SW1强制进入编程模式

  if (forceProgramMode) {
    // 按住按钮进入编程模式，不限时间等待
    // 用红蓝交替闪烁表示强制编程模式
    for (int i = 0; i < 6; i++) {
      digitalWrite(LED_R, (i % 2) ? HIGH : LOW);
      digitalWrite(LED_B, (i % 2) ? LOW : HIGH);
      delay(150);
    }
    digitalWrite(LED_R, HIGH);
    digitalWrite(LED_B, HIGH);
  }

  // 进入编程等待模式（有超时）
  handleAddressProgramming();

  // Setup Feeder
  feeder = new PhotonFeeder(DRIVE1, DRIVE2, PEEL1, PEEL2, LED_R, LED_G, LED_B, &encoder);
  network = new PhotonNetworkLayer(&bus, &packetizer, &addressFilter, &feederFloor);
  protocol = new PhotonFeederProtocol(feeder, &feederFloor, network, UniqueID, UniqueIDsize);

  byte addr = feederFloor.read_floor_address();

  if (addr == 0x00){ // not programmed
    // 不再自动写入默认地址，等待用户通过编程模式设置
    // 红色LED常亮表示未编程
    // (feeder已初始化，可以用set_rgb)
    feeder->set_rgb(true, false, false);
  }

  //Starting rs-485 serial
  ser.begin(BAUD_RATE);

  // attach interrupts for encoder pins
  attachInterrupt(digitalPinToInterrupt(DRIVE_ENC_A), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(DRIVE_ENC_B), checkPosition, CHANGE);

  feeder->resetEncoderPosition(0);
  feeder->setMmPosition(0);

}

void lifetime(){
  // lifetime testing loop
  uint32_t counter = millis();
  uint32_t interval = 3000;
  while(true){
    if(millis() > counter + interval){
      //reset counter to millis()
      counter = millis();
      //move
      feeder->feedDistance(40, true);
      feeder->resetEncoderPosition(0);
      feeder->setMmPosition(0);
    }
  }
}

void showVersion(){

  feeder->showVersion();

}

void topShortPress(){
  //turn led white for movement
  feeder->set_rgb(true, true, true);
  // move forward 2mm
  feeder->feedDistance(20, true);
}

void bottomShortPress(){
  //turn led white for movement
  feeder->set_rgb(true, true, true);
  // move forward 2mm
  feeder->feedDistance(20, false);

  if (feeder->getMoveResult() == PhotonFeeder::FeedResult::SUCCESS){
    feeder->set_rgb(false, false, false);
  }
  else{
    feeder->set_rgb(true, false, false);
  }
}

void topLongPress(){
  //we've got a long top press, lets drive forward, tape or film depending on drive_mode
  if(drive_mode){
    feeder->peel(true);
  }
  else{
    //resetting first feed, since we could now have a new tape type
    feeder->_first_feed_since_load = true;
    feeder->drive(true);
  }
      // set flag for concurrency to know driving state
  driving = true;
  driving_direction = true;
}

void bottomLongPress(){
  // moving in reverse, motor selected by drive_mode
  if(drive_mode){
    feeder->peel(false);
  }
  else{
    //resetting first feed, since we could now have a new tape type
    feeder->_first_feed_since_load = true;
    feeder->drive(false);
  }
    // set flag for concurrency to know driving state
  driving = true;
  driving_direction = false;

}

void bothLongPress(){
  //both are pressed, switching if we are driving tape or film

  if(drive_mode){
    feeder->set_rgb(false, false, true);
    drive_mode = false;
  }
  else{
    feeder->set_rgb(true, true, false);
    drive_mode = true;
  }

  //if both are held for a long time, we show current version id
  uint32_t timerStart = millis();

  bool alreadyFlashed = false;

  while( (!digitalRead(SW1) || !digitalRead(SW2))){
    //do nothing while waiting for debounce
    if((timerStart + 2000 < millis()) && !alreadyFlashed){
      feeder->set_rgb(false, false, false);
      showVersion();
      alreadyFlashed = true;
    }

    // if held for a really long time, reboot into the bootloader
    if((timerStart + 4000 < millis())){
      for (int n = 0; n < 10; n++) {
        feeder->set_rgb(!(n % 2), false, n % 2);
        delay(100);
      }
    }
    if((timerStart + 6000 < millis())){
      feeder->set_rgb(true, false, true);
      reboot_into_bootloader();
    }
  }

  //delay for debounce
  delay(50);
  feeder->set_rgb(false, false, false);
}

inline void checkButtons() {
  if(!driving){
    // Checking bottom button
    if(!digitalRead(SW1)){
      delay(LONG_PRESS_DELAY);
      // if bottom long press
      if(!digitalRead(SW1)){
        // if both long press
        if(!digitalRead(SW2)){
          bothLongPress();
        }
        // if just bottom long press
        else{
          bottomLongPress();
        }
      }
      // if bottom short press
      else{
        bottomShortPress();
      }
    }
    // Checking top button
    if(!digitalRead(SW2)){
      delay(LONG_PRESS_DELAY);
      // if top long press
      if(!digitalRead(SW2)){
        // if both long press
        if(!digitalRead(SW1)){
          bothLongPress();
        }
        // if just top long press
        else{
          topLongPress();
        }
      }
      // if top short press
      else{
        topShortPress();
      }
    }
  }
  else{
    if((driving_direction && digitalRead(SW2)) || (!driving_direction && digitalRead(SW1))){
      //stop all motors
      feeder->halt();
      //reset encoder and mm position
      feeder->resetEncoderPosition(0);
      feeder->setMmPosition(0);
      driving = false;
      delay(5);
    }
  }
}

inline void checkForRS485Packet() {
  protocol->tick();
}

//------
//  MAIN CONTROL LOOP
//------
void loop() {
  checkButtons();
  checkForRS485Packet();
}
