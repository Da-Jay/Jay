#!/usr/bin/env python3
"""NodeMCU(ESP8266) Wi-Fi 控制 GUI: 控制 D3/D4 開關, 並傳送文字到 20x4 LCD。
只用 Python 標準函式庫 (tkinter + urllib), 不需 pip 安裝。
用法: python esp_gui.py [主機或IP]    預設 esp-lab.local
"""
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT = 4


class EspClient:
    def __init__(self, host):
        self.host = host

    def _url(self, path):
        return f"http://{self.host}{path}"

    def _call(self, path, data=None):
        body = urllib.parse.urlencode(data).encode("utf-8") if data is not None else None
        with urllib.request.urlopen(self._url(path), data=body, timeout=TIMEOUT) as r:
            return json.loads(r.read().decode("utf-8"))

    def status(self):
        return self._call("/status")

    def set_gpio(self, pin, state):
        q = urllib.parse.urlencode({"pin": pin, "state": int(bool(state))})
        return self._call("/gpio?" + q)

    def lcd_write(self, text):
        # LCD 為 20x4: 超過的行/字元會被截掉; 非 ASCII 以 ? 取代
        lines = [ln[:20] for ln in text.splitlines()[:4]]
        safe = "\n".join(ln.encode("ascii", "replace").decode("ascii") for ln in lines)
        return self._call("/lcd", {"text": safe})

    def lcd_clear(self):
        return self._call("/lcd/clear")


def main():
    import tkinter as tk
    from tkinter import ttk, messagebox

    host = sys.argv[1] if len(sys.argv) > 1 else "esp-lab.local"
    root = tk.Tk()
    root.title("NodeMCU Wi-Fi 控制")
    client = EspClient(host)

    status_var = tk.StringVar(value="未連線")
    host_var = tk.StringVar(value=host)
    gpio_vars = {"D3": tk.BooleanVar(), "D4": tk.BooleanVar()}

    def run(fn, *a):
        try:
            return fn(*a)
        except (urllib.error.URLError, OSError, ValueError) as e:
            status_var.set(f"錯誤: {e}")
            return None

    def apply_status(st):
        if not st:
            return
        gpio_vars["D3"].set(bool(st["d3"]))
        gpio_vars["D4"].set(bool(st["d4"]))
        status_var.set(f"已連線 {st['ip']}  RSSI {st['rssi']} dBm  LCD {st['lcd']}")

    def connect():
        client.host = host_var.get().strip()
        apply_status(run(client.status))

    def toggle(pin):
        apply_status(run(client.set_gpio, pin, gpio_vars[pin].get()))

    def send_lcd():
        text = text_box.get("1.0", "end").rstrip("\n")
        r = run(client.lcd_write, text)
        if r:
            status_var.set("已寫入 LCD" + ("" if r.get("lcd") else " (韌體未偵測到 LCD)"))

    def clear_lcd():
        if run(client.lcd_clear):
            status_var.set("LCD 已清除")

    frm = ttk.Frame(root, padding=12)
    frm.grid()
    ttk.Label(frm, text="ESP 位址 (IP 或 esp-lab.local):").grid(row=0, column=0, sticky="w")
    ttk.Entry(frm, textvariable=host_var, width=22).grid(row=0, column=1, padx=6)
    ttk.Button(frm, text="連線/更新狀態", command=connect).grid(row=0, column=2)

    ttk.Separator(frm).grid(row=1, column=0, columnspan=3, sticky="ew", pady=8)
    for i, pin in enumerate(("D3", "D4")):
        ttk.Checkbutton(frm, text=f"{pin} 輸出 ON", variable=gpio_vars[pin],
                        command=lambda p=pin: toggle(p)).grid(row=2 + i, column=0, sticky="w")

    ttk.Separator(frm).grid(row=4, column=0, columnspan=3, sticky="ew", pady=8)
    ttk.Label(frm, text="LCD 文字 (最多 4 行 x 20 字, 僅英數/ASCII):").grid(row=5, column=0, columnspan=3, sticky="w")
    text_box = tk.Text(frm, width=22, height=4, font=("Consolas", 14))
    text_box.grid(row=6, column=0, columnspan=3, pady=4)
    ttk.Button(frm, text="寫入 LCD", command=send_lcd).grid(row=7, column=0, sticky="w")
    ttk.Button(frm, text="清除 LCD", command=clear_lcd).grid(row=7, column=1, sticky="w")

    ttk.Label(frm, textvariable=status_var, foreground="#555").grid(row=8, column=0, columnspan=3, sticky="w", pady=(10, 0))
    root.after(300, connect)
    root.mainloop()


if __name__ == "__main__":
    main()
