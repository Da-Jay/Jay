// I2C 功能: 位址掃描 + PCF8574 背板的字元 LCD (HD44780, 1602/2004), 不需額外函式庫
// 接線: SDA = D4, SCL = D5 (XIAO 預設 I2C 腳), VCC = 5V (多數背板) 或 3V3, GND
// 啟用 I2C 後 D4/D5 會交給 I2C 使用, 不能再當 GPIO 輸出 (韌體會回傳 409)
#pragma once
#include <Arduino.h>
#include <Wire.h>

class I2cLcd {
 public:
  uint8_t addr = 0, cols = 16, rows = 2;
  bool ready() const { return addr != 0; }

  void begin(uint8_t a, uint8_t c, uint8_t r) {
    addr = a; cols = c; rows = r;
    if (!addr) return;
    delay(50);
    expand(0);
    delay(5);
    write4(0x30, 0); delay(5);
    write4(0x30, 0); delayMicroseconds(150);
    write4(0x30, 0);
    write4(0x20, 0);          // 4-bit 模式
    command(0x28);            // 4-bit, 2 行, 5x8 點陣 (1602/2004 皆適用)
    command(0x0C);            // 顯示開, 游標關
    clear();
    command(0x06);            // 輸入時游標右移
  }
  void clear() { command(0x01); delay(2); }
  void setCursor(uint8_t c, uint8_t r) {
    static const uint8_t off[] = {0x00, 0x40, 0x14, 0x54};
    command(0x80 | (c + off[r < 4 ? r : 3]));
  }
  // 清除並顯示多行文字 (以 \n 分行, 僅 ASCII)
  void show(const String &text) {
    clear();
    int row = 0, start = 0;
    while (row < rows && start <= (int)text.length()) {
      int nl = text.indexOf('\n', start);
      String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
      line.replace("\r", "");
      setCursor(0, row);
      for (int i = 0; i < (int)line.length() && i < cols; i++) data((uint8_t)line[i]);
      row++;
      if (nl < 0) break;
      start = nl + 1;
    }
  }

 private:
  uint8_t bl = 0x08;  // 背光位元 (P3)
  void expand(uint8_t d) { Wire.beginTransmission(addr); Wire.write(d | bl); Wire.endTransmission(); }
  void pulse(uint8_t d) { expand(d | 0x04); delayMicroseconds(2); expand(d & ~0x04); delayMicroseconds(60); }
  void write4(uint8_t v, uint8_t rs) { uint8_t d = (v & 0xF0) | rs; expand(d); pulse(d); }
  void send(uint8_t v, uint8_t rs) { write4(v & 0xF0, rs); write4((v << 4) & 0xF0, rs); }
  void command(uint8_t v) { send(v, 0); }
  void data(uint8_t v) { send(v, 1); }
};
