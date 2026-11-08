// ESP32 基本測試: 板載 LED 閃爍 + 序列埠輸出 + 序列埠指令
// Arduino IDE: 開發板選 "ESP32 Dev Module" (需先安裝 esp32 by Espressif 板子套件)
// 序列埠監控視窗: 115200 bps
//
// 功能:
//  - 每 500 ms 切換 LED, 並在序列埠印出狀態與運行秒數
//  - 開機時印出晶片型號、CPU 頻率、Flash 大小、可用記憶體
//  - 輸入 "LED ON" / "LED OFF" / "BLINK" 可控制 LED (與 linkit7688 範例指令一致)

#ifndef LED_BUILTIN
#define LED_BUILTIN 2   // 多數 ESP32 DevKit 的板載 LED 在 GPIO2
#endif

const uint8_t LED_PIN = LED_BUILTIN;
const unsigned long BLINK_MS = 500;

unsigned long lastToggle = 0;
bool ledState = false;
bool blinking = true;
String cmd;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32 basic test ===");
  Serial.printf("Chip: %s rev %d, cores: %d\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("CPU: %u MHz, Flash: %u KB, Free heap: %u bytes\n",
                ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / 1024, ESP.getFreeHeap());
  Serial.println("Commands: LED ON / LED OFF / BLINK");
}

void handleCommand(const String &c) {
  if (c == "LED ON")       { blinking = false; ledState = true;  digitalWrite(LED_PIN, HIGH); }
  else if (c == "LED OFF") { blinking = false; ledState = false; digitalWrite(LED_PIN, LOW); }
  else if (c == "BLINK")   { blinking = true; }
  else { Serial.println("{\"err\":\"unknown cmd\"}"); return; }
  Serial.println("{\"ok\":true}");
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') { cmd.trim(); if (cmd.length()) handleCommand(cmd); cmd = ""; }
    else if (cmd.length() < 32) cmd += ch;
  }

  if (blinking && millis() - lastToggle >= BLINK_MS) {
    lastToggle = millis();
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    Serial.printf("LED=%s uptime=%lus\n", ledState ? "ON" : "OFF", millis() / 1000);
  }
}
