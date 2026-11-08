# NodeMCU Wi-Fi 控制 GUI

PC 上的 Python 圖形介面, 經 Wi-Fi 控制 NodeMCU (ESP8266) 的 D3/D4 輸出, 並把輸入的文字顯示到 20x4 (2004) I2C LCD。

## 接線
LCD(2004 I2C 背板): SDA→D2, SCL→D1, VCC→VIN(5V), GND→GND。輸出 D3(GPIO0)、D4(GPIO2) 接 LED 時請串聯 220~330Ω 電阻。
> D3/D4 為開機腳位, 開機時不可被外部拉低; D4 同時是板載 LED (低電位亮)。

## 韌體
1. 複製 `firmware/wifi_secrets.h.example` 為 `firmware/wifi_secrets.h`, 填入 2.4GHz Wi-Fi (已被 .gitignore 排除)。
2. NodeMCU 1.0 (ESP-12E), 安裝 LiquidCrystal I2C, 上傳 `firmware/firmware.ino` (上傳速度請用 115200)。
3. 序列埠或 LCD 會顯示 IP; 也可用 `http://esp-lab.local/status`。

## GUI
```
python gui/esp_gui.py            # 預設 esp-lab.local
python gui/esp_gui.py 192.168.18.4
```
只需 Python 3 (含 tkinter), 不需安裝套件。勾選 D3/D4 切換輸出, 在文字框輸入內容後按「寫入 LCD」。

## HTTP API
`GET /status`、`GET /gpio?pin=D3&state=1`、`POST /lcd` (text=...)、`GET /lcd/clear`。無驗證, 僅限區網使用。
