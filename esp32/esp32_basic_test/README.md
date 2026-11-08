# ESP32 基本測試

## 使用
1. Arduino IDE → 偏好設定 → 額外的開發板管理員網址加入
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`,
   再於「開發板管理員」安裝 **esp32 by Espressif Systems**。
2. 開發板選 "ESP32 Dev Module", 選對應 COM 埠, 上傳 `esp32_basic_test.ino`。
   (上傳卡在 Connecting... 時, 按住板上 BOOT 鍵。)
3. 開啟序列埠監控視窗, 速率 115200。

## 預期結果
- 板載 LED (GPIO2) 每 0.5 秒閃爍。
- 序列埠印出晶片資訊, 之後每 0.5 秒印 `LED=ON/OFF uptime=Ns`。
- 輸入 `LED ON`、`LED OFF`、`BLINK` (行尾換行) 可控制 LED, 回應 `{"ok":true}`。

## 注意
部分板子 (如 ESP32-S3/C3) 板載 LED 腳位不同, 若 LED 不亮請改 `LED_PIN`。

## 之後可擴充
Wi-Fi 連線、MQTT、藍牙 (BLE)、感測器 (I2C/DHT 等)。
