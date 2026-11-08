// XIAO ESP32-C5 測試韌體: HTTP API 控制 D0~D10 輸出, 並提供網路測試 (Wi-Fi 資訊 / DNS / TCP / HTTP)
// 輸出 ON = 3.3V, OFF = 0V; 每腳電流請 < 10 mA, 大負載請用電晶體/繼電器。
//
// API (同一區網, 無驗證, 請勿暴露到網際網路):
//   GET /status                  -> {"pins":{"D0":0,...,"D10":0},"ip":"...","ssid":"...","rssi":-55,"channel":36,"band":"5GHz","mac":"..."}
//   GET /gpio?pin=D0&state=1     -> 設定單一輸出 (pin = D0~D10, state = 0|1)
//   GET /gpio/all?state=0|1      -> 全部輸出
//   GET /i2c/enable?on=1|0       -> 啟用/停用 I2C (SDA=D4, SCL=D5; 啟用後 D4/D5 不能當輸出)
//   GET /i2c/scan                -> {"devices":["0x27",...]} (需先啟用)
//   POST /lcd (text=第1行\n第2行) / GET /lcd/clear / GET /lcd/config?addr=auto|0x27&cols=16&rows=2 -> PCF8574 背板字元 LCD
//   GET /nettest                 -> 由板子端測試 DNS / TCP / HTTP, 回傳各項結果與耗時 (ms)
// 序列埠 (115200) 同步印出 IP 與操作紀錄。
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include "wifi_secrets.h"   // 定義 WIFI_SSID / WIFI_PASS
#include "i2c_lcd.h"

// 使用板子變體定義的 D0~D10 (對應 GPIO 由 Arduino core 決定)
const uint8_t OUT_PINS[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10};
const char *OUT_NAMES[] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10"};
const int N_OUT = 11;
int outState[N_OUT] = {0};
const char *MDNS_NAME = "xiao-c5";   // http://xiao-c5.local/

WebServer server(80);


// ---- I2C / LCD ----
bool i2cOn = false;
I2cLcd lcd;
const int I2C_IDX_A = 4, I2C_IDX_B = 5;   // OUT_NAMES 的 D4, D5

bool i2cPresent(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
bool isLcdAddr(uint8_t a) { return (a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F); }

void i2cEnable(bool on) {
  if (on == i2cOn) return;
  if (on) {
    Wire.begin(SDA, SCL);          // SDA=D4, SCL=D5 (由板子定義)
    Wire.setClock(100000);
  } else {
    Wire.end();
    lcd.addr = 0;
    for (int i : {I2C_IDX_A, I2C_IDX_B}) { pinMode(OUT_PINS[i], OUTPUT); digitalWrite(OUT_PINS[i], LOW); outState[i] = 0; }
  }
  i2cOn = on;
}

uint8_t findLcd() { for (uint8_t a = 1; a < 127; a++) if (isLcdAddr(a) && i2cPresent(a)) return a; return 0; }

String statusJson() {
  String j = "{\"pins\":{";
  for (int i = 0; i < N_OUT; i++) j += String(i ? "," : "") + "\"" + OUT_NAMES[i] + "\":" + String(outState[i]);
  int ch = WiFi.channel();
  j += "},\"ip\":\"" + WiFi.localIP().toString() + "\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) +
       ",\"channel\":" + String(ch) + ",\"band\":\"" + (ch > 14 ? "5GHz" : "2.4GHz") + "\",\"mac\":\"" + WiFi.macAddress() +
       "\",\"i2c\":" + String(i2cOn ? "true" : "false") + ",\"lcd\":\"" + (lcd.ready() ? "0x" + String(lcd.addr, HEX) : String("none")) + "\",\"cols\":" + String(lcd.cols) + ",\"rows\":" + String(lcd.rows) + ",\"uptime\":" + String(millis() / 1000) + "}";
  return j;
}

void sendStatus() { server.send(200, "application/json", statusJson()); }

void handleNetTest() {
  String j = "{";
  // 1) DNS
  unsigned long t = millis();
  IPAddress ip;
  bool dnsOk = WiFi.hostByName("www.google.com", ip);
  j += "\"dns\":{\"ok\":" + String(dnsOk ? "true" : "false") + ",\"ms\":" + String(millis() - t) + ",\"ip\":\"" + (dnsOk ? ip.toString() : String("")) + "\"}";
  // 2) TCP 連線 (Cloudflare 1.1.1.1:80)
  t = millis();
  WiFiClient c;
  bool tcpOk = c.connect(IPAddress(1, 1, 1, 1), 80, 3000);
  j += ",\"tcp\":{\"ok\":" + String(tcpOk ? "true" : "false") + ",\"ms\":" + String(millis() - t) + "}";
  c.stop();
  // 3) HTTP (Google generate_204, 回應 204 代表可上網)
  t = millis();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  int code = -1;
  if (http.begin("http://connectivitycheck.gstatic.com/generate_204")) { code = http.GET(); http.end(); }
  j += ",\"http\":{\"ok\":" + String(code == 204 ? "true" : "false") + ",\"code\":" + String(code) + ",\"ms\":" + String(millis() - t) + "}";
  j += ",\"rssi\":" + String(WiFi.RSSI()) + ",\"channel\":" + String(WiFi.channel()) + "}";
  Serial.println("nettest: " + j);
  server.send(200, "application/json", j);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== XIAO ESP32-C5 test firmware ===");
  for (int i = 0; i < N_OUT; i++) { pinMode(OUT_PINS[i], OUTPUT); digitalWrite(OUT_PINS[i], LOW); }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(250); Serial.print('.'); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi FAILED (SSID/密碼錯誤? 見 src/wifi_secrets.h)");
  } else {
    Serial.println("\nConnected, IP=" + WiFi.localIP().toString() + " RSSI=" + String(WiFi.RSSI()) + " ch=" + String(WiFi.channel()));
    MDNS.begin(MDNS_NAME);
  }

  server.on("/status", HTTP_GET, sendStatus);
  server.on("/gpio", HTTP_GET, []() {
    String pin = server.arg("pin"); pin.toUpperCase();
    int st = server.arg("state").toInt() ? 1 : 0;
    int idx = -1;
    for (int i = 0; i < N_OUT; i++) if (pin == OUT_NAMES[i]) idx = i;
    if (idx < 0) { server.send(400, "application/json", "{\"err\":\"pin must be D0..D10\"}"); return; }
    if (i2cOn && (idx == I2C_IDX_A || idx == I2C_IDX_B)) { server.send(409, "application/json", "{\"err\":\"D4/D5 used by I2C\"}"); return; }
    outState[idx] = st;
    digitalWrite(OUT_PINS[idx], st);
    Serial.printf("GPIO %s=%d\n", pin.c_str(), st);
    sendStatus();
  });
  server.on("/gpio/all", HTTP_GET, []() {
    int st = server.arg("state").toInt() ? 1 : 0;
    for (int i = 0; i < N_OUT; i++) { outState[i] = st; digitalWrite(OUT_PINS[i], st); }
    Serial.printf("GPIO all=%d\n", st);
    sendStatus();
  });
  server.on("/i2c/enable", HTTP_GET, []() { i2cEnable(server.arg("on").toInt() != 0); sendStatus(); });
  server.on("/i2c/scan", HTTP_GET, []() {
    if (!i2cOn) { server.send(409, "application/json", "{\"err\":\"enable I2C first\"}"); return; }
    String j = "{\"devices\":[";
    bool first = true;
    for (uint8_t a = 1; a < 127; a++) {
      if (!i2cPresent(a)) continue;
      j += String(first ? "" : ",") + "\"0x" + String(a, HEX) + "\"";
      first = false;
    }
    server.send(200, "application/json", j + "]}");
  });
  server.on("/lcd/config", HTTP_GET, []() {
    if (!i2cOn) { server.send(409, "application/json", "{\"err\":\"enable I2C first\"}"); return; }
    int cols = server.arg("cols").toInt(), rows = server.arg("rows").toInt();
    if (!(cols == 8 || cols == 16 || cols == 20 || cols == 40) || rows < 1 || rows > 4) { server.send(400, "application/json", "{\"err\":\"cols 8|16|20|40, rows 1..4\"}"); return; }
    String a = server.arg("addr");
    uint8_t addr = (a == "" || a == "auto") ? findLcd() : (uint8_t)strtol(a.c_str(), nullptr, 0);
    if (addr && !i2cPresent(addr)) addr = 0;
    lcd.begin(addr, cols, rows);
    if (lcd.ready()) lcd.show("LCD " + String(cols) + "x" + String(rows) + "\n0x" + String(addr, HEX));
    sendStatus();
  });
  server.on("/lcd", HTTP_POST, []() {
    if (lcd.ready()) lcd.show(server.arg("text"));
    server.send(200, "application/json", String("{\"ok\":true,\"lcd\":") + (lcd.ready() ? "true" : "false") + "}");
  });
  server.on("/lcd/clear", HTTP_GET, []() { if (lcd.ready()) lcd.clear(); server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/nettest", HTTP_GET, handleNetTest);
  server.begin();
}

void loop() {
  server.handleClient();
  // 斷線自動重連 (每 10 秒檢查一次)
  static unsigned long last = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - last > 10000) { last = millis(); WiFi.reconnect(); }
}
