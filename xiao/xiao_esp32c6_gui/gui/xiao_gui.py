#!/usr/bin/env python3
"""XIAO ESP32-C6 Wi-Fi 控制 GUI: 分別開關 D0~D10 (ON=3.3V, OFF=接地), 並測試網路。
只用 Python 標準函式庫 (tkinter + urllib), 不需 pip 安裝。
用法: python xiao_gui.py [主機或IP]    預設 xiao-c6.local
"""
import concurrent.futures
import json
import os
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT = 12  # /nettest 需要數秒
SETTINGS_FILE = os.path.join(os.path.expanduser("~"), ".xiao_gui.json")
OUT_PINS = [f"D{i}" for i in range(11)]


def local_subnet_prefix():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0].rsplit(".", 1)[0] + "."
    except OSError:
        return None
    finally:
        s.close()


def _probe(host, timeout):
    try:
        with urllib.request.urlopen(f"http://{host}/status", timeout=timeout) as r:
            d = json.loads(r.read().decode("utf-8"))
        return host if "pins" in d and "D10" in d["pins"] else None
    except (OSError, ValueError):
        return None


def scan_network(timeout=0.7):
    prefix = local_subnet_prefix()
    hosts = ["xiao-c6.local"]
    if prefix:
        hosts += [f"{prefix}{i}" for i in range(1, 255)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
        return [h for h in ex.map(lambda x: _probe(x, timeout), hosts) if h]


class Client:
    def __init__(self, host):
        self.host = host

    def call(self, path, timeout=TIMEOUT, data=None):
        body = urllib.parse.urlencode(data).encode("utf-8") if data is not None else None
        with urllib.request.urlopen(f"http://{self.host}{path}", data=body, timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8"))

    def status(self):
        return self.call("/status", 4)

    def set_gpio(self, pin, state):
        return self.call("/gpio?" + urllib.parse.urlencode({"pin": pin, "state": int(bool(state))}), 4)

    def set_all(self, state):
        return self.call(f"/gpio/all?state={int(bool(state))}", 4)

    def i2c_enable(self, on):
        return self.call(f"/i2c/enable?on={int(bool(on))}", 5)

    def i2c_scan(self):
        return self.call("/i2c/scan", 8)["devices"]

    def lcd_config(self, addr, cols, rows):
        return self.call("/lcd/config?" + urllib.parse.urlencode({"addr": addr, "cols": cols, "rows": rows}), 6)

    def lcd_write(self, text, cols, rows):
        lines = [ln[:cols] for ln in text.splitlines()[:rows]]
        safe = "\n".join(ln.encode("ascii", "replace").decode("ascii") for ln in lines)
        return self.call("/lcd", 6, {"text": safe})

    def lcd_clear(self):
        return self.call("/lcd/clear", 5)

    def nettest(self):
        return self.call("/nettest")


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
    host = sys.argv[1] if len(sys.argv) > 1 else saved.get("host", "xiao-c6.local")
    root = tk.Tk()
    root.title("XIAO ESP32-C6 Wi-Fi 控制")
    client = Client(host)

    status_var = tk.StringVar(value="未連線")
    host_var = tk.StringVar(value=host)
    gpio_vars = {p: tk.BooleanVar() for p in OUT_PINS}

    def log(msg):
        out.configure(state="normal")
        out.insert("end", time.strftime("%H:%M:%S ") + msg + "\n")
        out.see("end")
        out.configure(state="disabled")

    def run(fn, *a):
        try:
            return fn(*a)
        except (urllib.error.URLError, OSError, ValueError, KeyError) as e:
            status_var.set(f"錯誤: {e}")
            return None

    def apply_status(st):
        if not st:
            return
        apply_i2c(st)
        for p, v in st["pins"].items():
            if p in gpio_vars:
                gpio_vars[p].set(bool(v))
        status_var.set(f"已連線 {st['ip']}  {st['ssid']}  {st['band']} ch{st['channel']}  RSSI {st['rssi']} dBm")

    def connect():
        client.host = host_var.get().strip()
        st = run(client.status)
        apply_status(st)
        if st:
            save_settings({"host": client.host})

    def scan_esp():
        status_var.set("掃描區網中 (約數秒)...")
        root.update_idletasks()
        found = scan_network()
        if not found:
            status_var.set("區網內找不到 XIAO (電腦與板子需在同一個網路)")
            return
        host_box.configure(values=found)
        host_var.set(found[0])
        connect()

    def toggle(pin):
        apply_status(run(client.set_gpio, pin, gpio_vars[pin].get()))

    def all_set(state):
        apply_status(run(client.set_all, state))

    def pc_ping(host_name):
        """PC 端對板子做 5 次 HTTP /status 往返時間"""
        times = []
        for _ in range(5):
            t = time.time()
            try:
                client.status()
                times.append((time.time() - t) * 1000)
            except (OSError, ValueError):
                pass
        if not times:
            return "PC→板子: 全部逾時"
        return f"PC→板子 往返: 平均 {sum(times)/len(times):.0f} ms (最小 {min(times):.0f} / 最大 {max(times):.0f}, 成功 {len(times)}/5)"

    def net_test():
        btn_net.configure(state="disabled")
        status_var.set("網路測試中 (約數秒)...")
        client.host = host_var.get().strip()

        def worker():
            lines = []
            try:
                st = client.status()
                lines.append(f"Wi-Fi: {st['ssid']} {st['band']} ch{st['channel']} RSSI {st['rssi']} dBm IP {st['ip']}")
                lines.append(pc_ping(client.host))
                r = client.nettest()
                for key, name in (("dns", "DNS 解析 google.com"), ("tcp", "TCP 連線 1.1.1.1:80"), ("http", "HTTP 外網 generate_204")):
                    item = r[key]
                    extra = f" → {item['ip']}" if key == "dns" and item.get("ip") else (f" (code {item['code']})" if key == "http" else "")
                    lines.append(f"{'✔' if item['ok'] else '✘'} {name}: {item['ms']} ms{extra}")
                lines.append("結論: " + ("板子可正常上網" if r["http"]["ok"] else "板子連得上 Wi-Fi 但無法上網/部分失敗"))
            except (urllib.error.URLError, OSError, ValueError, KeyError) as e:
                lines.append(f"✘ 無法連到板子: {e}")

            def done():
                for ln in lines:
                    log(ln)
                status_var.set(lines[-1])
                btn_net.configure(state="normal")
            root.after(0, done)

        threading.Thread(target=worker, daemon=True).start()

    # ---- I2C / LCD ----
    i2c_on = tk.BooleanVar(value=False)
    lcd_size = tk.StringVar(value=saved.get("lcd_size", "16x2"))
    lcd_addr = tk.StringVar(value="auto")
    lcd_dim = {"cols": 16, "rows": 2}

    def apply_i2c(st):
        if "i2c" not in st:
            return
        i2c_on.set(bool(st["i2c"]))
        if st.get("cols"):
            lcd_dim["cols"], lcd_dim["rows"] = st["cols"], st["rows"]
        i2c_info.set(f"I2C {'已啟用' if st['i2c'] else '未啟用'}  LCD {st.get('lcd', 'none')} {st.get('cols', '?')}x{st.get('rows', '?')}")

    def i2c_toggle():
        apply_status(run(client.i2c_enable, i2c_on.get()))

    def i2c_scan():
        if not i2c_on.get():
            i2c_on.set(True)
            apply_status(run(client.i2c_enable, True))
        devs = run(client.i2c_scan)
        if devs is None:
            return
        i2c_info.set("I2C 裝置: " + (", ".join(devs) if devs else "無 (確認接線 SDA=D4, SCL=D5、共地、上拉電阻)"))
        addr_box.configure(values=["auto"] + devs)

    def lcd_apply():
        try:
            c, r = (int(v) for v in lcd_size.get().lower().split("x"))
        except ValueError:
            status_var.set("規格格式錯誤, 例如 16x2")
            return
        if not i2c_on.get():
            i2c_on.set(True)
            apply_status(run(client.i2c_enable, True))
        st = run(client.lcd_config, lcd_addr.get().strip() or "auto", c, r)
        apply_status(st)
        if st:
            save_settings({"host": client.host, "lcd_size": lcd_size.get()})
            if st.get("lcd") == "none":
                status_var.set("找不到 LCD (請先「掃描 I2C」確認位址)")

    def lcd_send():
        text = lcd_text.get("1.0", "end").rstrip("\n")
        r = run(client.lcd_write, text, lcd_dim["cols"], lcd_dim["rows"])
        if r:
            status_var.set("已寫入 LCD" if r.get("lcd") else "韌體尚未偵測到 LCD (先按「套用」)")

    def lcd_clear():
        if run(client.lcd_clear):
            status_var.set("LCD 已清除")

    frm = ttk.Frame(root, padding=12)
    frm.grid()
    ttk.Label(frm, text="板子位址 (IP 或 xiao-c6.local):").grid(row=0, column=0, sticky="w")
    host_box = ttk.Combobox(frm, textvariable=host_var, values=[host], width=20)
    host_box.grid(row=0, column=1, padx=6)
    bar = ttk.Frame(frm)
    bar.grid(row=0, column=2)
    ttk.Button(bar, text="掃描板子", command=scan_esp).pack(side="left")
    ttk.Button(bar, text="連線/更新", command=connect).pack(side="left", padx=(4, 0))

    pins_frame = ttk.LabelFrame(frm, text="輸出 (ON = 3.3V, OFF = 接地)", padding=6)
    pins_frame.grid(row=1, column=0, columnspan=3, sticky="ew", pady=8)
    for i, pin in enumerate(OUT_PINS):
        ttk.Checkbutton(pins_frame, text=f"{pin} ON", variable=gpio_vars[pin],
                        command=lambda p=pin: toggle(p)).grid(row=i // 4, column=i % 4, sticky="w", padx=8, pady=2)
    allbar = ttk.Frame(pins_frame)
    allbar.grid(row=3, column=0, columnspan=4, sticky="w", pady=(6, 0))
    ttk.Button(allbar, text="全部 ON", command=lambda: all_set(1)).pack(side="left")
    ttk.Button(allbar, text="全部 OFF", command=lambda: all_set(0)).pack(side="left", padx=4)

    net = ttk.LabelFrame(frm, text="網路測試", padding=6)
    net.grid(row=2, column=0, columnspan=3, sticky="ew")
    btn_net = ttk.Button(net, text="開始測試 (Wi-Fi / 延遲 / DNS / TCP / HTTP)", command=net_test)
    btn_net.grid(row=0, column=0, sticky="w")
    out = tk.Text(net, width=64, height=9, state="disabled", font=("Consolas", 10))
    out.grid(row=1, column=0, pady=(6, 0))

    i2cf = ttk.LabelFrame(frm, text="I2C (SDA=D4, SCL=D5; 啟用後 D4/D5 不能當輸出)", padding=6)
    i2cf.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(8, 0))
    ttk.Checkbutton(i2cf, text="啟用 I2C", variable=i2c_on, command=i2c_toggle).grid(row=0, column=0, sticky="w")
    ttk.Button(i2cf, text="掃描 I2C", command=i2c_scan).grid(row=0, column=1, padx=6)
    ttk.Label(i2cf, text="LCD 位址:").grid(row=0, column=2)
    addr_box = ttk.Combobox(i2cf, textvariable=lcd_addr, values=["auto", "0x27", "0x3F"], width=7)
    addr_box.grid(row=0, column=3, padx=4)
    ttk.Label(i2cf, text="規格:").grid(row=0, column=4)
    ttk.Combobox(i2cf, textvariable=lcd_size, values=["16x2", "20x4", "16x4", "20x2", "8x2"], width=6).grid(row=0, column=5, padx=4)
    ttk.Button(i2cf, text="套用", command=lcd_apply).grid(row=0, column=6, padx=4)
    lcd_text = tk.Text(i2cf, width=24, height=4, font=("Consolas", 12))
    lcd_text.grid(row=1, column=0, columnspan=4, pady=6, sticky="w")
    lcdb = ttk.Frame(i2cf)
    lcdb.grid(row=1, column=4, columnspan=3, sticky="w")
    ttk.Button(lcdb, text="寫入 LCD", command=lcd_send).pack(side="left")
    ttk.Button(lcdb, text="清除", command=lcd_clear).pack(side="left", padx=4)
    i2c_info = tk.StringVar(value="I2C 未啟用")
    ttk.Label(i2cf, textvariable=i2c_info, foreground="#555").grid(row=2, column=0, columnspan=7, sticky="w")

    ttk.Label(frm, textvariable=status_var, foreground="#555").grid(row=4, column=0, columnspan=3, sticky="w", pady=(10, 0))
    root.after(300, connect)
    root.mainloop()


if __name__ == "__main__":
    main()
