# XIAO ESP32-S3 Sense Wi-Fi 測試 (D0~D10 + 網路測試)

XIAO ESP32-S3 Sense (OV2640 鏡頭) 經 Wi-Fi 由 PC 的 Python GUI 控制 D0~D10 輸出 (ON=3.3V, OFF=接地)、測試網路, 並在 GUI 顯示鏡頭即時畫面。

## 韌體 (PlatformIO)
1. 複製 `src/wifi_secrets.h.example` 為 `src/wifi_secrets.h`, 填入 Wi-Fi (S3 僅支援 2.4GHz; 已被 .gitignore 排除)。
2. 需 PlatformIO Core 6.2.1b2 以上 (`pip install -U https://github.com/platformio/platformio-core/archive/refs/heads/develop.zip`); `pio run -t upload` (COM16, pioarduino 平台, 首次下載約數分鐘)。
   若出現 MissingPackageManifestError, 刪除 `C:\p\packages\framework-arduinoespressif32-libs` 後重跑 (換不同晶片時會發生)。
3. `pio device monitor` 看序列埠 (115200) 的 IP。若無法上傳: 按住 B (BOOT) 鍵再插 USB。

## GUI
需先 `pip install pillow` (鏡頭畫面解碼用)。
```
python gui/xiao_gui.py            # 預設 xiao-s3.local
python gui/xiao_gui.py 192.168.1.50
```
- 勾選 D0~D11 個別開關, 另有全部 ON/OFF。
- 鏡頭: 按「開啟鏡頭」顯示即時畫面 (輪詢 /capture), 可切換解析度 QQVGA~XGA (越大越慢) 與旋轉 0/90/180/270 度 (鏡頭方向不對時用), 「存檔」存成 ~/Pictures/xiao_*.jpg。
- 偵測到人: 勾選「偵測到人時 D11 ON」(需 `pip install "opencv-python<5" numpy`; OpenCV 5 預覽版缺少偵測功能, 請用 4.x)。GUI 在電腦端用 OpenCV (全身 HOG + 正面臉 + 上半身) 偵測, 有人就 ON、無人 3 秒後 OFF, 並在畫面框出。S3 沒有 D11 腳, **D11 = 板載使用者 LED (GPIO21, 亮=ON)**。畫面方向不對時先設「旋轉」, 偵測才準; 解析度建議 VGA。
- 「開始測試」: 顯示 SSID/頻段/頻道/RSSI、PC→板子延遲, 以及板子端 DNS、TCP、HTTP 外網測試。

## HTTP API
`GET /status`、`GET /gpio?pin=D0&state=1`、`GET /gpio/all?state=0|1`、`GET /capture` (JPEG)、`GET /cam/size?name=QVGA`、`GET /nettest`。無驗證, 僅限區網。

> 鏡頭排線需插好 (Sense 擴充板); 鏡頭佔用 GPIO10~18、38~40、47、48, 與 D0~D10 (GPIO1~9、43、44) 不衝突。
> D0~D10 以 Arduino 板子定義的 D 腳名稱為準; 部分腳位 (D6/D7 為 UART、部分為 strapping) 外接負載請小心開機狀態。
