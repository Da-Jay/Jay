// XIAO ESP32-S3 Sense 測試韌體: HTTP API 控制 D0~D10 輸出, 並提供網路測試 (Wi-Fi 資訊 / DNS / TCP / HTTP)
// 輸出 ON = 3.3V, OFF = 0V; 每腳電流請 < 10 mA, 大負載請用電晶體/繼電器。
//
// API (同一區網, 無驗證, 請勿暴露到網際網路):
//   GET /status                  -> {"pins":{"D0":0,...,"D10":0},"ip":"...","ssid":"...","rssi":-55,"channel":36,"band":"5GHz","mac":"..."}
//   GET /gpio?pin=D0&state=1     -> 設定單一輸出 (pin = D0~D10, state = 0|1)
//   GET /gpio/all?state=0|1      -> 全部輸出
//   GET /capture                 -> 單張 JPEG 影像 (GUI 以輪詢方式顯示即時畫面)
//   GET /cam/size?name=QQVGA|QVGA|VGA|SVGA|XGA -> 切換解析度 (越大越慢)
//   GET /nettest                 -> 由板子端測試 DNS / TCP / HTTP, 回傳各項結果與耗時 (ms)
// 序列埠 (115200) 同步印出 IP 與操作紀錄。
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "wifi_secrets.h"   // 定義 WIFI_SSID / WIFI_PASS

// 使用板子變體定義的 D0~D10 (對應 GPIO 由 Arduino core 決定)
// D11 = 板載使用者 LED (GPIO21, 低電位亮; XIAO S3 沒有真正的 D11 腳, 韌體會自動反相, ON = LED 亮)
const uint8_t OUT_PINS[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, 21};
const char *OUT_NAMES[] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10", "D11"};
const int N_OUT = 12;
const int LED_IDX = 11;   // 低電位有效
int outState[N_OUT] = {0};
void writeOut(int i, int st) { outState[i] = st; digitalWrite(OUT_PINS[i], (i == LED_IDX) ? !st : st); }
const char *MDNS_NAME = "xiao-s3";   // http://xiao-s3.local/

WebServer server(80);


// ---- 鏡頭 (XIAO ESP32-S3 Sense, OV2640) ----
#define CAM_PWDN -1
#define CAM_RESET -1
#define CAM_XCLK 10
#define CAM_SIOD 40
#define CAM_SIOC 39
#define CAM_Y9 48
#define CAM_Y8 11
#define CAM_Y7 12
#define CAM_Y6 14
#define CAM_Y5 16
#define CAM_Y4 18
#define CAM_Y3 17
#define CAM_Y2 15
#define CAM_VSYNC 38
#define CAM_HREF 47
#define CAM_PCLK 13
void sendStatus();
bool camOk = false;
String camSize = "QVGA";

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_Y2; c.pin_d1 = CAM_Y3; c.pin_d2 = CAM_Y4; c.pin_d3 = CAM_Y5;
  c.pin_d4 = CAM_Y6; c.pin_d5 = CAM_Y7; c.pin_d6 = CAM_Y8; c.pin_d7 = CAM_Y9;
  c.pin_xclk = CAM_XCLK; c.pin_pclk = CAM_PCLK; c.pin_vsync = CAM_VSYNC; c.pin_href = CAM_HREF;
  c.pin_sccb_sda = CAM_SIOD; c.pin_sccb_scl = CAM_SIOC;
  c.pin_pwdn = CAM_PWDN; c.pin_reset = CAM_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) { Serial.printf("Camera init failed 0x%x (鏡頭排線沒插好? 沒開 PSRAM?)\n", err); return false; }
  Serial.println("Camera OK");
  return true;
}

void handleCapture() {
  if (!camOk) { server.send(503, "application/json", "{\"err\":\"camera not ready\"}"); return; }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { server.send(500, "application/json", "{\"err\":\"capture failed\"}"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleCamSize() {
  if (!camOk) { server.send(503, "application/json", "{\"err\":\"camera not ready\"}"); return; }
  String n = server.arg("name"); n.toUpperCase();
  framesize_t f;
  if (n == "QQVGA") f = FRAMESIZE_QQVGA; else if (n == "QVGA") f = FRAMESIZE_QVGA;
  else if (n == "VGA") f = FRAMESIZE_VGA; else if (n == "SVGA") f = FRAMESIZE_SVGA;
  else if (n == "XGA") f = FRAMESIZE_XGA;
  else { server.send(400, "application/json", "{\"err\":\"name must be QQVGA|QVGA|VGA|SVGA|XGA\"}"); return; }
  esp_camera_sensor_get()->set_framesize(esp_camera_sensor_get(), f);
  camSize = n;
  sendStatus();
}

String statusJson() {
  String j = "{\"pins\":{";
  for (int i = 0; i < N_OUT; i++) j += String(i ? "," : "") + "\"" + OUT_NAMES[i] + "\":" + String(outState[i]);
  int ch = WiFi.channel();
  j += "},\"ip\":\"" + WiFi.localIP().toString() + "\",\"ssid\":\"" + WiFi.SSID() + "\",\"rssi\":" + String(WiFi.RSSI()) +
       ",\"channel\":" + String(ch) + ",\"band\":\"" + (ch > 14 ? "5GHz" : "2.4GHz") + "\",\"mac\":\"" + WiFi.macAddress() +
       "\",\"cam\":" + String(camOk ? "true" : "false") + ",\"camsize\":\"" + camSize + "\",\"uptime\":" + String(millis() / 1000) + "}";
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
  Serial.println("\n=== XIAO ESP32-S3 Sense test firmware ===");
  for (int i = 0; i < N_OUT; i++) { pinMode(OUT_PINS[i], OUTPUT); writeOut(i, 0); }

  camOk = initCamera();
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
    if (idx < 0) { server.send(400, "application/json", "{\"err\":\"pin must be D0..D11\"}"); return; }
    writeOut(idx, st);
    Serial.printf("GPIO %s=%d\n", pin.c_str(), st);
    sendStatus();
  });
  server.on("/gpio/all", HTTP_GET, []() {
    int st = server.arg("state").toInt() ? 1 : 0;
    for (int i = 0; i < N_OUT; i++) writeOut(i, st);
    Serial.printf("GPIO all=%d\n", st);
    sendStatus();
  });
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/cam/size", HTTP_GET, handleCamSize);
  server.on("/nettest", HTTP_GET, handleNetTest);
  server.begin();
}

void loop() {
  server.handleClient();
  // 斷線自動重連 (每 10 秒檢查一次)
  static unsigned long last = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - last > 10000) { last = millis(); WiFi.reconnect(); }
}
