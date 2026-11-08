// NodeMCU CH340 V3 (ESP8266) 板子測試: I2C LCD1602 + LED + 序列埠
// Arduino IDE: 開發板選 "NodeMCU 1.0 (ESP-12E Module)", 序列埠監控 115200
// 需安裝函式庫: "LiquidCrystal I2C" (Frank de Brabander)
//
// 接線:
//   LCD SDA -> D2 (GPIO4)   LCD SCL -> D1 (GPIO5)   VCC -> VIN(5V)   GND -> GND
//   LED 正極 -> D5 (GPIO14)  LED 負極 -> D6 (GPIO12, 程式輸出 LOW 當地)
//
// 功能: 掃描 I2C 位址, LCD 顯示測試訊息與運行秒數, LED 每 0.5 秒閃爍, 結果從序列埠輸出。

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const uint8_t SDA_PIN = D2;
const uint8_t SCL_PIN = D1;
const uint8_t LED_POS = D5;
const uint8_t LED_NEG = D6;
const unsigned long BLINK_MS = 500;
const unsigned long LCD_MS = 1000;

LiquidCrystal_I2C *lcd = nullptr;
uint8_t lcdAddr = 0;
unsigned long lastBlink = 0, lastLcd = 0;
bool ledState = false;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== NodeMCU board test ===");
  Serial.printf("Chip ID: %06X, CPU: %u MHz, Flash: %u KB, Free heap: %u\n",
                ESP.getChipId(), ESP.getCpuFreqMHz(), ESP.getFlashChipRealSize() / 1024, ESP.getFreeHeap());

  pinMode(LED_POS, OUTPUT);
  pinMode(LED_NEG, OUTPUT);
  digitalWrite(LED_NEG, LOW);   // D6 當地
  digitalWrite(LED_POS, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("Scanning I2C (SDA=D2, SCL=D1)...");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found device at 0x%02X\n", a);
      if (!lcdAddr && (a == 0x27 || a == 0x3F)) lcdAddr = a;
      found++;
    }
  }
  if (!found) Serial.println("  no I2C device found -> check SDA/SCL wiring and LCD power");
  if (!lcdAddr) {
    Serial.println("LCD test: FAIL (no 0x27/0x3F)");
    return;
  }

  lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->setCursor(0, 0);
  lcd->print("NodeMCU test OK");
  Serial.printf("LCD test: OK at 0x%02X (if screen is blank/blocks, adjust contrast pot on backpack)\n", lcdAddr);
}

void loop() {
  unsigned long now = millis();

  if (now - lastBlink >= BLINK_MS) {
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(LED_POS, ledState);
    Serial.printf("LED=%s uptime=%lus heap=%u\n", ledState ? "ON" : "OFF", now / 1000, ESP.getFreeHeap());
  }

  if (lcd && now - lastLcd >= LCD_MS) {
    lastLcd = now;
    lcd->setCursor(0, 1);
    lcd->print("Uptime: ");
    lcd->print(now / 1000);
    lcd->print("s    ");
  }
}
