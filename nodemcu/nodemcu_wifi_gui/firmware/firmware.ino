// NodeMCU (ESP8266) Wi-Fi 控制韌體: HTTP API 控制 D3/D4 輸出與 20x4 I2C LCD (2004)
// 開發板: NodeMCU 1.0 (ESP-12E); 函式庫: LiquidCrystal I2C (Frank de Brabander)
// 接線: LCD SDA=D2, SCL=D1, VCC=VIN(5V), GND; 輸出 D3(GPIO0)、D4(GPIO2)
// 注意: D3/D4 是開機腳位(開機時須為高電位), 外接負載請勿在開機時把它們拉低。D4 同時是板載 LED (低電位亮)。
//
// API (同一區網, 無驗證, 請勿暴露到網際網路):
//   GET  /status                      -> {"d3":0,"d4":1,"ip":"...","lcd":"0x3F","rssi":-60}
//   GET  /gpio?pin=D3&state=1         -> 設定輸出 (pin = D3|D4, state = 0|1)
//   POST /lcd   (text=第1行\n第2行...) -> 清除並顯示, 最多 4 行 x 20 字元 (僅 ASCII)
//   GET  /lcd/clear
//   GET  /lcd/config?addr=auto|0x27|0x3F&cols=16|20&rows=2|4  -> 重新設定 LCD 位址/規格 (執行中生效)
//   GET  /i2c/scan                    -> {"devices":["0x3F",...]}

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "wifi_secrets.h"   // 定義 WIFI_SSID / WIFI_PASS (請複製 wifi_secrets.h.example)

const uint8_t PIN_D3 = D3, PIN_D4 = D4;
const char *MDNS_NAME = "esp-lab";   // 可用 http://esp-lab.local/

ESP8266WebServer server(80);
LiquidCrystal_I2C *lcd = nullptr;
uint8_t lcdAddr = 0;
uint8_t lcdCols = 20, lcdRows = 4;
int d3 = 0, d4 = 0;

bool isLcdAddr(uint8_t a) { return (a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F); }

bool i2cPresent(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

// 依位址與規格(重新)初始化 LCD; addr=0 表示不使用
void initLcd(uint8_t addr, uint8_t cols, uint8_t rows) {
  delete lcd;
  lcd = nullptr;
  lcdAddr = addr; lcdCols = cols; lcdRows = rows;
  if (!addr) return;
  lcd = new LiquidCrystal_I2C(addr, cols, rows);
  lcd->init(); lcd->backlight();
}

void lcdShow(const String &text) {
  if (!lcd) return;
  lcd->clear();
  int row = 0, start = 0;
  while (row < lcdRows && start <= (int)text.length()) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    line.replace("\r", "");
    lcd->setCursor(0, row);
    lcd->print(line.substring(0, lcdCols));
    row++;
    if (nl < 0) break;
    start = nl + 1;
  }
}

void sendStatus() {
  String j = "{\"d3\":" + String(d3) + ",\"d4\":" + String(d4) +
             ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"lcd\":\"" +
             (lcdAddr ? "0x" + String(lcdAddr, HEX) : String("none")) +
             "\",\"cols\":" + String(lcdCols) + ",\"rows\":" + String(lcdRows) +
             ",\"rssi\":" + String(WiFi.RSSI()) + "}";
  server.send(200, "application/json", j);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== NodeMCU WiFi GUI firmware ===");
  pinMode(PIN_D3, OUTPUT); pinMode(PIN_D4, OUTPUT);
  digitalWrite(PIN_D3, LOW); digitalWrite(PIN_D4, LOW);

  // 掃描全部 I2C 位址 (PCF8574 為 0x20~0x27, PCF8574A 為 0x38~0x3F), 取第一個 LCD 背板
  // 找不到時再試 SDA/SCL 對調 (D1/D2), 方便診斷接反
  for (int attempt = 0; attempt < 2 && !lcdAddr; attempt++) {
    uint8_t sda = attempt ? D1 : D2, scl = attempt ? D2 : D1;
    Wire.begin(sda, scl);
    Serial.printf("I2C scan with SDA=GPIO%d SCL=GPIO%d\n", sda, scl);
    for (uint8_t a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        Serial.printf("I2C device at 0x%02X\n", a);
        if (!lcdAddr && ((a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F))) lcdAddr = a;
      }
    }
    if (attempt && lcdAddr) Serial.println("NOTE: LCD found only with SDA/SCL SWAPPED -> rewire SDA=D2, SCL=D1");
  }
  if (lcdAddr) {
    initLcd(lcdAddr, lcdCols, lcdRows);
    lcdShow("NodeMCU ready\nConnecting WiFi...");
    Serial.printf("LCD %ux%u at 0x%02X\n", lcdCols, lcdRows, lcdAddr);
  } else {
    Serial.println("LCD not found (HTTP API still works)");
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi FAILED");
    lcdShow("WiFi FAILED");
    return;
  }
  String ip = WiFi.localIP().toString();
  Serial.println("Connected, IP=" + ip);
  lcdShow("WiFi OK\nIP " + ip + "\nhttp://" + MDNS_NAME + ".local");
  MDNS.begin(MDNS_NAME);

  server.on("/status", HTTP_GET, sendStatus);
  server.on("/gpio", HTTP_GET, []() {
    String pin = server.arg("pin"); pin.toUpperCase();
    int st = server.arg("state").toInt() ? 1 : 0;
    if (pin == "D3")      { d3 = st; digitalWrite(PIN_D3, st); }
    else if (pin == "D4") { d4 = st; digitalWrite(PIN_D4, st); }
    else { server.send(400, "application/json", "{\"err\":\"pin must be D3 or D4\"}"); return; }
    Serial.printf("GPIO %s=%d\n", pin.c_str(), st);
    sendStatus();
  });
  server.on("/lcd", HTTP_POST, []() {
    String t = server.arg("text");
    Serial.println("LCD <- " + t);
    lcdShow(t);
    server.send(200, "application/json", String("{\"ok\":true,\"lcd\":") + (lcd ? "true" : "false") + "}");
  });
  server.on("/lcd/clear", HTTP_GET, []() {
    if (lcd) lcd->clear();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/i2c/scan", HTTP_GET, []() {
    String j = "{\"devices\":[";
    bool first = true;
    for (uint8_t a = 1; a < 127; a++) {
      if (!i2cPresent(a)) continue;
      if (!first) j += ",";
      j += "\"0x" + String(a, HEX) + "\"";
      first = false;
    }
    server.send(200, "application/json", j + "]}");
  });
  server.on("/lcd/config", HTTP_GET, []() {
    int cols = server.arg("cols").toInt(), rows = server.arg("rows").toInt();
    if (!(cols == 16 || cols == 20 || cols == 8 || cols == 40) || rows < 1 || rows > 4) {
      server.send(400, "application/json", "{\"err\":\"cols must be 8|16|20|40, rows 1..4\"}");
      return;
    }
    String a = server.arg("addr");
    uint8_t addr = 0;
    if (a == "" || a == "auto") {
      for (uint8_t x = 1; x < 127 && !addr; x++) if (isLcdAddr(x) && i2cPresent(x)) addr = x;
    } else {
      addr = (uint8_t)strtol(a.c_str(), nullptr, 0);
      if (!addr || addr > 127) { server.send(400, "application/json", "{\"err\":\"bad addr\"}"); return; }
    }
    initLcd(addr, cols, rows);
    Serial.printf("LCD config: addr=0x%02X %dx%d found=%d\n", addr, cols, rows, addr && i2cPresent(addr));
    if (lcd) lcdShow("LCD " + String(cols) + "x" + String(rows) + "\n0x" + String(addr, HEX));
    sendStatus();
  });
  server.begin();
}

void loop() {
  server.handleClient();
  MDNS.update();
}
