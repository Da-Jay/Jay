// XIAO ESP32-C6 測試韌體: HTTP API 控制 D0~D10 輸出, 並提供網路測試 (Wi-Fi 資訊 / DNS / TCP / HTTP)
// 輸出 ON = 3.3V, OFF = 0V; 每腳電流請 < 10 mA, 大負載請用電晶體/繼電器。
//
// API (同一區網, 無驗證, 請勿暴露到網際網路):
//   GET /status                  -> {"pins":{"D0":0,...,"D10":0},"ip":"...","ssid":"...","rssi":-55,"channel":36,"band":"5GHz","mac":"..."}
//   GET /gpio?pin=D0&state=1     -> 設定單一輸出 (pin = D0~D10, state = 0|1)
//   GET /gpio/all?state=0|1      -> 全部輸出
//   GET /nettest                 -> 由板子端測試 DNS / TCP / HTTP, 回傳各項結果與耗時 (ms)
// 序列埠 (115200) 同步印出 IP 與操作紀錄。
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include "wifi_secrets.h"   // 定義 WIFI_SSID / WIFI_PASS

// 使用板子變體定義的 D0~D10 (對應 GPIO 由 Arduino core 決定)
const uint8_t OUT_PINS[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10};
const char *OUT_NAMES[] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10"};
const int N_OUT = 11;
int outState[N_OUT] = {0};
const char *MDNS_NAME = "xiao-c6";   // http://xiao-c6.local/

WebServer server(80);

String statusJson() {
  String j = "{\"pins\":{";
  for (int i = 0; i < N_OUT; i++) j += String(i ? "," : "") + "\"" + OUT_NAMES[i] + "\":" + String(outState[i]);
  int ch = WiFi.channel();
  j += "},\"ip\":\"" + WiFi.localIP().toString() + "\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) +
       ",\"channel\":" + String(ch) + ",\"band\":\"" + (ch > 14 ? "5GHz" : "2.4GHz") + "\",\"mac\":\"" + WiFi.macAddress() +
       "\",\"uptime\":" + String(millis() / 1000) + "}";
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
  Serial.println("\n=== XIAO ESP32-C6 test firmware ===");
  for (int i = 0; i < N_OUT; i++) { pinMode(OUT_PINS[i], OUTPUT); digitalWrite(OUT_PINS[i], LOW); }

#ifdef USE_EXT_ANTENNA
  // C6 板載 RF 開關: GPIO3 LOW = 啟用開關, GPIO14 HIGH = 外接天線 (需在 platformio.ini build_flags 加 -DUSE_EXT_ANTENNA)
  pinMode(3, OUTPUT); digitalWrite(3, LOW);
  pinMode(14, OUTPUT); digitalWrite(14, HIGH);
#endif
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
  server.on("/nettest", HTTP_GET, handleNetTest);
  server.begin();
}

void loop() {
  server.handleClient();
  // 斷線自動重連 (每 10 秒檢查一次)
  static unsigned long last = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - last > 10000) { last = millis(); WiFi.reconnect(); }
}
