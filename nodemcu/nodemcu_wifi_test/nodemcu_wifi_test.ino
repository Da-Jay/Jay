// NodeMCU (ESP8266) 網路功能測試: Wi-Fi 掃描 -> 連線 -> DNS -> HTTP, 結果顯示於序列埠 (115200) 與 LCD
// 接線同 nodemcu_board_test (LCD: SDA=D2, SCL=D1; LED: D5 正極 / D6 負極)
// 若沒有 wifi_secrets.h 只做掃描 (測試無線電); 有的話再測連線、DNS、HTTP。

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#define HAVE_SECRETS 1
#endif

const uint8_t LED_POS = D5, LED_NEG = D6;
LiquidCrystal_I2C *lcd = nullptr;

void show(const String &l1, const String &l2) {
  Serial.println(l1 + " | " + l2);
  if (!lcd) return;
  lcd->clear();
  lcd->setCursor(0, 0); lcd->print(l1.substring(0, 16));
  lcd->setCursor(0, 1); lcd->print(l2.substring(0, 16));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== NodeMCU WiFi test ===");
  pinMode(LED_POS, OUTPUT); pinMode(LED_NEG, OUTPUT);
  digitalWrite(LED_NEG, LOW); digitalWrite(LED_POS, LOW);

  Wire.begin(D2, D1);
  for (uint8_t a : {0x27, 0x3F}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      lcd = new LiquidCrystal_I2C(a, 16, 2);
      lcd->init(); lcd->backlight();
      break;
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  show("WiFi scanning", "...");
  int n = WiFi.scanNetworks();
  Serial.printf("Scan: %d networks\n", n);
  for (int i = 0; i < n; i++)
    Serial.printf("  %2d. %-32s ch%-2d %4d dBm %s\n", i + 1, WiFi.SSID(i).c_str(),
                  WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "secured");
  show("Scan: " + String(n) + " APs", n > 0 ? "radio OK" : "radio FAIL?");
  delay(2000);

#ifdef HAVE_SECRETS
  show("Connecting...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(250); digitalWrite(LED_POS, !digitalRead(LED_POS)); }
  digitalWrite(LED_POS, LOW);
  if (WiFi.status() != WL_CONNECTED) {
    show("WiFi FAIL", "status=" + String(WiFi.status()));
    Serial.println("Connect FAILED (check SSID/password, must be 2.4GHz)");
    return;
  }
  Serial.printf("Connected: IP=%s GW=%s DNS=%s RSSI=%d dBm\n", WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str(), WiFi.RSSI());
  show("IP " + WiFi.localIP().toString(), String("RSSI ") + WiFi.RSSI() + "dBm");
  delay(2000);

  IPAddress ip;
  bool dnsOk = WiFi.hostByName("www.google.com", ip);
  Serial.printf("DNS www.google.com: %s %s\n", dnsOk ? "OK" : "FAIL", dnsOk ? ip.toString().c_str() : "");

  WiFiClient client;
  HTTPClient http;
  int code = -1;
  if (http.begin(client, "http://connectivitycheck.gstatic.com/generate_204")) {
    code = http.GET();
    http.end();
  }
  Serial.printf("HTTP GET generate_204: %d (204 = Internet OK)\n", code);
  show(String("DNS ") + (dnsOk ? "OK" : "FAIL"), String("HTTP ") + code + (code == 204 ? " OK" : " FAIL"));
  Serial.println(code == 204 && dnsOk ? "NETWORK TEST: PASS" : "NETWORK TEST: FAIL");
#else
  Serial.println("No wifi_secrets.h -> scan only.");
#endif
}

void loop() {}
