# NodeMCU CH340 V3 (ESP8266) 板子測試

## 接線
| 元件 | 接腳 |
|---|---|
| LCD1602 (I2C 背板) SDA | D2 (GPIO4) |
| LCD1602 SCL | D1 (GPIO5) |
| LCD VCC / GND | VIN(5V) / GND |
| LED 正極 | D5 (GPIO14) |
| LED 負極 | D6 (GPIO12, 程式輸出 LOW 當地) |

> **LED 請串聯一顆 220~330Ω 電阻**。GPIO 直接驅動 LED 沒有限流, 可能燒壞 LED 或晶片腳位。

## 使用
1. Arduino IDE 偏好設定加入額外網址 `http://arduino.esp8266.com/stable/package_esp8266com_index.json`,
   開發板管理員安裝 **esp8266**。
2. 函式庫管理員安裝 **LiquidCrystal I2C** (Frank de Brabander)。
3. 開發板選 **NodeMCU 1.0 (ESP-12E Module)**, 選對應 COM 埠 (CH340 需安裝驅動), 上傳 `nodemcu_board_test.ino`。
4. 開啟序列埠監控視窗, 速率 115200。

## 預期結果
- 序列埠印出晶片資訊與 I2C 掃描結果 (LCD 通常為 0x27 或 0x3F), 接著 `LCD test: OK`。
- LCD 第 1 行顯示 `NodeMCU test OK`, 第 2 行顯示運行秒數。
- LED 每 0.5 秒閃爍, 序列埠每次印 `LED=ON/OFF uptime=Ns`。

## 疑難排解
- 掃不到裝置: 檢查 SDA/SCL 是否接反、LCD 是否接 5V、杜邦線接觸。
- 有位址但螢幕全黑/只有方塊: 調整 LCD 背板上的藍色可變電阻 (對比)。
- 上傳失敗: 確認 CH340 驅動與 COM 埠, 換一條可傳資料的 USB 線。
