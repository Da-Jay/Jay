# NodeMCU 網路功能測試

1. 複製 `wifi_secrets.h.example` 為 `wifi_secrets.h`, 填入 2.4GHz Wi-Fi 的 SSID/密碼 (此檔不會上傳到 Git)。
2. 開發板 NodeMCU 1.0 (ESP-12E), 函式庫 LiquidCrystal I2C, 上傳速度若失敗請降到 115200。
3. 序列埠 115200 預期:
   - `Scan: N networks` (測試無線電; 沒有 wifi_secrets.h 時只做這步)
   - `Connected: IP=... RSSI=...`
   - `DNS www.google.com: OK`
   - `HTTP GET generate_204: 204`
   - `NETWORK TEST: PASS`
4. LCD 會依序顯示掃描數、IP/訊號強度、DNS/HTTP 結果。
