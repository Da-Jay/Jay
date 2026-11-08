# XIAO ESP32-C5 Wi-Fi 測試 (D0~D10 + 網路測試)

XIAO ESP32-C5 (外接天線版) 經 Wi-Fi 由 PC 的 Python GUI 控制 D0~D10 輸出 (ON=3.3V, OFF=接地), 並測試網路。

## 韌體 (PlatformIO)
1. 複製 `src/wifi_secrets.h.example` 為 `src/wifi_secrets.h`, 填入 Wi-Fi (C5 支援 2.4G/5G; 已被 .gitignore 排除)。
2. 需 PlatformIO Core 6.2.1b2 以上 (`pip install -U https://github.com/platformio/platformio-core/archive/refs/heads/develop.zip`); `pio run -t upload` (COM13, pioarduino 平台, 首次下載約數分鐘)。
3. `pio device monitor` 看序列埠 (115200) 的 IP。若無法上傳: 按住 B (BOOT) 鍵再插 USB。

## GUI
```
python gui/xiao_gui.py            # 預設 xiao-c5.local
python gui/xiao_gui.py 192.168.1.50
```
- 勾選 D0~D10 個別開關, 另有全部 ON/OFF。
- 「開始測試」: 顯示 SSID/頻段/頻道/RSSI、PC→板子延遲, 以及板子端 DNS、TCP、HTTP 外網測試。

## HTTP API
`GET /status`、`GET /gpio?pin=D0&state=1`、`GET /gpio/all?state=0|1`、`GET /nettest`。無驗證, 僅限區網。

> D0~D10 以 Arduino 板子定義的 D 腳名稱為準; 部分腳位 (D6/D7 為 UART、部分為 strapping) 外接負載請小心開機狀態。
