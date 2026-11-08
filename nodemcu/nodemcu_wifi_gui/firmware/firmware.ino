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
int d3 = 0, d4 = 0;

void lcdShow(const String &text) {
  if (!lcd) return;
  lcd->clear();
  int row = 0, start = 0;
  while (row < 4 && start <= (int)text.length()) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    line.replace("\r", "");
    lcd->setCursor(0, row);
    lcd->print(line.substring(0, 20));
    row++;
    if (nl < 0) break;
    start = nl + 1;
  }
}

void sendStatus() {
  String j = "{\"d3\":" + String(d3) + ",\"d4\":" + String(d4) +
             ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"lcd\":\"" +
             (lcdAddr ? "0x" + String(lcdAddr, HEX) : String("none")) +
             "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
  server.send(200, "application/json", j);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== NodeMCU WiFi GUI firmware ===");
  pinMode(PIN_D3, OUTPUT); pinMode(PIN_D4, OUTPUT);
  digitalWrite(PIN_D3, LOW); digitalWrite(PIN_D4, LOW);

  Wire.begin(D2, D1);
  for (uint8_t a : {0x27, 0x3F}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { lcdAddr = a; break; }
  }
  if (lcdAddr) {
    lcd = new LiquidCrystal_I2C(lcdAddr, 20, 4);
    lcd->init(); lcd->backlight();
    lcdShow("NodeMCU ready\nConnecting WiFi...");
    Serial.printf("LCD 20x4 at 0x%02X\n", lcdAddr);
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
  server.begin();
}

void loop() {
  server.handleClient();
  MDNS.update();
}
