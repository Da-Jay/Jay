#!/usr/bin/env python3
"""NodeMCU(ESP8266) Wi-Fi 控制 GUI: 控制 D3~D8 開關 (ON=3.3V, OFF=接地), 並傳送文字到 20x4 LCD。
只用 Python 標準函式庫 (tkinter + urllib), 不需 pip 安裝。
用法: python esp_gui.py [主機或IP]    預設 esp-lab.local
"""
import concurrent.futures
import json
import os
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT = 4
SETTINGS_FILE = os.path.join(os.path.expanduser("~"), ".esp_gui.json")


OUT_PINS = ["D3", "D4", "D5", "D6", "D7", "D8"]


def local_subnet_prefix():
    """回傳本機區網前綴, 例如 '192.168.18.'; 失敗回傳 None"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # UDP, 不會真的送出封包
        return s.getsockname()[0].rsplit(".", 1)[0] + "."
    except OSError:
        return None
    finally:
        s.close()


def _probe(host, timeout):
    try:
        with urllib.request.urlopen(f"http://{host}/status", timeout=timeout) as r:
            d = json.loads(r.read().decode("utf-8"))
        return host if "pins" in d or "d3" in d else None
    except (OSError, ValueError):
        return None


def scan_network(prefix=None, timeout=0.7, extra_hosts=("esp-lab.local",)):
    """掃描區網 /24 (並試 mDNS 名稱), 回傳回應 /status 的 ESP 位址列表"""
    prefix = prefix or local_subnet_prefix()
    hosts = list(extra_hosts)
    if prefix:
        hosts += [f"{prefix}{i}" for i in range(1, 255)]
    found = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
        for h in ex.map(lambda x: _probe(x, timeout), hosts):
            if h:
                found.append(h)
    return found


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

    def lcd_write(self, text, cols=20, rows=4):
        # 超過 rows 行/cols 字元會被截掉; 非 ASCII 以 ? 取代
        lines = [ln[:cols] for ln in text.splitlines()[:rows]]
        safe = "\n".join(ln.encode("ascii", "replace").decode("ascii") for ln in lines)
        return self._call("/lcd", {"text": safe})

    def lcd_config(self, addr, cols, rows):
        """addr: 'auto' 或 '0x3F' 這類字串"""
        q = urllib.parse.urlencode({"addr": addr, "cols": cols, "rows": rows})
        return self._call("/lcd/config?" + q)

    def i2c_scan(self):
        return self._call("/i2c/scan")["devices"]

    def lcd_clear(self):
        return self._call("/lcd/clear")


def load_settings():
    try:
        with open(SETTINGS_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_settings(d):
    try:
        with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
            json.dump(d, f)
    except OSError:
        pass


def main():
    import tkinter as tk
    from tkinter import ttk

    saved = load_settings()
    host = sys.argv[1] if len(sys.argv) > 1 else saved.get("host", "esp-lab.local")
    root = tk.Tk()
    root.title("NodeMCU Wi-Fi 控制")
    client = EspClient(host)

    status_var = tk.StringVar(value="未連線")
    host_var = tk.StringVar(value=host)
    gpio_vars = {p: tk.BooleanVar() for p in OUT_PINS}
    size_var = tk.StringVar(value=saved.get("size", "20x4"))
    addr_var = tk.StringVar(value=saved.get("addr", "auto"))
    lcd_dim = {"cols": 20, "rows": 4}

    SIZES = ["16x2", "16x4", "20x2", "20x4", "8x2", "40x2"]

    def parse_size():
        c, r = size_var.get().lower().split("x")
        return int(c), int(r)

    def run(fn, *a):
        try:
            return fn(*a)
        except (urllib.error.URLError, OSError, ValueError, KeyError) as e:
            status_var.set(f"錯誤: {e}")
            return None

    def resize_text():
        cols, rows = lcd_dim["cols"], lcd_dim["rows"]
        text_box.configure(width=cols + 2, height=rows)
        hint_var.set(f"LCD 文字 (最多 {rows} 行 x {cols} 字, 僅英數/ASCII):")

    def apply_status(st):
        if not st:
            return
        for p, v in st.get("pins", {"D3": st["d3"], "D4": st["d4"]}).items():
            if p in gpio_vars:
                gpio_vars[p].set(bool(v))
        if "cols" in st:
            lcd_dim["cols"], lcd_dim["rows"] = st["cols"], st["rows"]
            resize_text()
        status_var.set(f"已連線 {st['ip']}  RSSI {st['rssi']} dBm  LCD {st['lcd']} {st.get('cols','?')}x{st.get('rows','?')}")

    def connect():
        client.host = host_var.get().strip()
        st = run(client.status)
        apply_status(st)
        if st:
            save_settings({"host": client.host, "size": size_var.get(), "addr": addr_var.get()})

    def apply_lcd_config():
        try:
            cols, rows = parse_size()
        except ValueError:
            status_var.set("規格格式錯誤, 例如 16x2")
            return
        addr = addr_var.get().strip() or "auto"
        st = run(client.lcd_config, addr, cols, rows)
        apply_status(st)
        if st:
            save_settings({"host": client.host, "size": size_var.get(), "addr": addr})
            status_var.set(status_var.get() + ("" if st["lcd"] != "none" else "  (該位址無回應)"))

    def scan_i2c():
        devs = run(client.i2c_scan)
        if devs is None:
            return
        status_var.set("I2C 裝置: " + (", ".join(devs) if devs else "無"))
        picks = ["auto"] + [d for d in devs]
        addr_box.configure(values=picks)
        if devs and addr_var.get() == "auto":
            addr_var.set(devs[0])

    def scan_esp():
        status_var.set("掃描區網中 (約數秒)...")
        root.update_idletasks()
        found = scan_network()
        if not found:
            status_var.set("區網內找不到 ESP (確認電腦與 ESP 在同一個 Wi-Fi)")
            return
        host_box.configure(values=found)
        host_var.set(found[0])
        connect()
        if len(found) > 1:
            status_var.set(status_var.get() + f"  (另有: {', '.join(found[1:])})")

    def toggle(pin):
        apply_status(run(client.set_gpio, pin, gpio_vars[pin].get()))

    def send_lcd():
        text = text_box.get("1.0", "end").rstrip("\n")
        r = run(client.lcd_write, text, lcd_dim["cols"], lcd_dim["rows"])
        if r:
            status_var.set("已寫入 LCD" + ("" if r.get("lcd") else " (韌體未偵測到 LCD)"))

    def clear_lcd():
        if run(client.lcd_clear):
            status_var.set("LCD 已清除")

    frm = ttk.Frame(root, padding=12)
    frm.grid()
    ttk.Label(frm, text="ESP 位址 (IP 或 esp-lab.local):").grid(row=0, column=0, sticky="w")
    host_box = ttk.Combobox(frm, textvariable=host_var, values=[host], width=20)
    host_box.grid(row=0, column=1, padx=6)
    bar = ttk.Frame(frm)
    bar.grid(row=0, column=2)
    ttk.Button(bar, text="掃描 ESP", command=scan_esp).pack(side="left")
    ttk.Button(bar, text="連線/更新", command=connect).pack(side="left", padx=(4, 0))

    ttk.Separator(frm).grid(row=1, column=0, columnspan=3, sticky="ew", pady=8)
    pins_frame = ttk.LabelFrame(frm, text="輸出 (ON = 3.3V, OFF = 接地)", padding=6)
    pins_frame.grid(row=2, column=0, columnspan=3, sticky="ew")
    for i, pin in enumerate(OUT_PINS):
        ttk.Checkbutton(pins_frame, text=f"{pin} ON", variable=gpio_vars[pin],
                        command=lambda p=pin: toggle(p)).grid(row=i // 3, column=i % 3, sticky="w", padx=8, pady=2)

    ttk.Separator(frm).grid(row=4, column=0, columnspan=3, sticky="ew", pady=8)
    cfg = ttk.LabelFrame(frm, text="LCD 設定", padding=6)
    cfg.grid(row=5, column=0, columnspan=3, sticky="ew")
    ttk.Label(cfg, text="規格 (欄x列):").grid(row=0, column=0, sticky="w")
    ttk.Combobox(cfg, textvariable=size_var, values=SIZES, width=8).grid(row=0, column=1, padx=6)
    ttk.Label(cfg, text="I2C 位址:").grid(row=0, column=2, sticky="w")
    addr_box = ttk.Combobox(cfg, textvariable=addr_var, values=["auto", "0x27", "0x3F"], width=8)
    addr_box.grid(row=0, column=3, padx=6)
    ttk.Button(cfg, text="掃描 I2C", command=scan_i2c).grid(row=0, column=4, padx=2)
    ttk.Button(cfg, text="套用", command=apply_lcd_config).grid(row=0, column=5, padx=2)

    hint_var = tk.StringVar()
    ttk.Label(frm, textvariable=hint_var).grid(row=6, column=0, columnspan=3, sticky="w", pady=(8, 0))
    text_box = tk.Text(frm, width=22, height=4, font=("Consolas", 14))
    text_box.grid(row=7, column=0, columnspan=3, pady=4)
    ttk.Button(frm, text="寫入 LCD", command=send_lcd).grid(row=8, column=0, sticky="w")
    ttk.Button(frm, text="清除 LCD", command=clear_lcd).grid(row=8, column=1, sticky="w")

    ttk.Label(frm, textvariable=status_var, foreground="#555").grid(row=9, column=0, columnspan=3, sticky="w", pady=(10, 0))
    resize_text()
    root.after(300, connect)
    root.mainloop()


if __name__ == "__main__":
    main()
