"""Optional STM32 serial monitor. No port is opened or command sent automatically.

Python 3.9+, Tkinter (usually bundled with Python), pyserial for live connection.
Parsing/building helpers intentionally work without Tkinter or pyserial installed.
"""
from __future__ import annotations

import csv
import math
import queue
import threading
from collections import deque
from dataclasses import dataclass
from datetime import datetime
from typing import Optional

TRACE_COLUMNS = ("ms", "target", "measured", "error", "P", "I", "D", "servo", "saturated", "fault")
TRACE_HEADER = "H," + ",".join(TRACE_COLUMNS)


@dataclass(frozen=True)
class TraceSample:
    ms: int
    target: float
    measured: float
    error: float
    p: float
    i: float
    d: float
    servo: float
    saturated: int
    fault: str

    def csv_row(self, received_at: str):
        return (received_at, self.ms, self.target, self.measured, self.error,
                self.p, self.i, self.d, self.servo, self.saturated, self.fault)


def parse_trace_line(line: str) -> Optional[TraceSample]:
    """Return None for headers, normal responses, malformed/non-finite frames."""
    parts = line.strip("\r\n").split(",")
    if len(parts) != 11 or parts[0] != "H":
        return None
    if not parts[1].isascii() or not parts[1].isdigit():
        return None
    try:
        ms = int(parts[1])
        values = [float(value) for value in parts[2:9]]
        saturated = int(parts[9])
    except (ValueError, OverflowError):
        return None
    if not 0 <= ms <= 0xFFFFFFFF or not all(math.isfinite(value) and abs(value) <= 100000 for value in values):
        return None
    if parts[9] not in ("0", "1") or saturated not in (0, 1):
        return None
    fault = parts[10]
    if not fault or len(fault) > 40 or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789" for c in fault):
        return None
    return TraceSample(ms, *values, saturated, fault)


def build_time_command(hour: str, minute: str, second: str, alarm: Optional[str] = None) -> str:
    fields = (hour, minute, second)
    if any(not field.isascii() or not field.isdigit() for field in fields):
        raise ValueError("时分秒必须为整数")
    h, m, s = map(int, fields)
    if not (0 <= h <= 23 and 0 <= m <= 59 and 0 <= s <= 59):
        raise ValueError("小时 0～23，分钟和秒 0～59")
    prefix = "TIME"
    if alarm is not None:
        if alarm not in ("1", "2", "3"):
            raise ValueError("闹钟编号必须为 1～3")
        prefix = "ALARM" + alarm
    return f"{prefix}({h:02d}:{m:02d}:{s:02d})"


def build_servo_command(angle: str) -> str:
    if not angle.isascii() or not angle.isdigit() or not 0 <= int(angle) <= 180:
        raise ValueError("舵机角度必须为 0～180 的整数")
    return f"SERVO SET {int(angle)}"


class SerialWorker(threading.Thread):
    """All port open/read/write/close happens here, keeping the GUI responsive."""

    def __init__(self, port: str, messages: queue.Queue):
        super().__init__(daemon=True, name="STM32Serial")
        self.port = port
        self.messages = messages
        self.commands = queue.Queue(maxsize=64)
        self.stop_requested = threading.Event()
        self.dropped_messages = 0

    def emit(self, kind: str, value):
        try:
            self.messages.put_nowait((kind, value))
        except queue.Full:
            self.dropped_messages += 1

    def enqueue(self, command: str) -> bool:
        if self.stop_requested.is_set():
            return False
        try:
            self.commands.put_nowait(command)
            return True
        except queue.Full:
            return False

    def run(self):
        connection = None
        pending = bytearray()
        invalid_line = False
        try:
            import serial
            connection = serial.Serial(self.port, 115200, timeout=0.05, write_timeout=0.5)
            self.emit("status", "connected")
            while not self.stop_requested.is_set():
                # One queued command per iteration: input cannot starve receiving.
                try:
                    command = self.commands.get_nowait()
                except queue.Empty:
                    command = None
                if command is not None:
                    payload = (command + "\r\n").encode("ascii", errors="strict")
                    sent = connection.write(payload)
                    if sent != len(payload):
                        raise OSError("串口未完整发送命令，请重新连接后核对设备状态")
                    self.emit("sent", command)
                chunk = connection.read(min(max(connection.in_waiting, 1), 2048))
                for byte in chunk:
                    if byte in (10, 13):
                        if invalid_line:
                            self.emit("error", "超长设备响应已丢弃")
                        elif pending:
                            timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
                            self.emit("line", (timestamp, pending.decode("ascii", errors="replace")))
                        pending.clear()
                        invalid_line = False
                    elif len(pending) < 4096 and not invalid_line:
                        pending.append(byte)
                    else:
                        invalid_line = True
                if self.dropped_messages:
                    try:
                        self.messages.put_nowait(("dropped", self.dropped_messages))
                        self.dropped_messages = 0
                    except queue.Full:
                        pass
        except Exception as error:  # Hardware errors are displayed, never kill Tk.
            self.emit("error", str(error))
        finally:
            if connection is not None:
                try:
                    connection.close()
                except Exception as error:
                    self.emit("error", str(error))
            self.emit("status", "disconnected")


class MonitorApp:
    def __init__(self, root):
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        self.tk, self.ttk = tk, ttk
        self.filedialog, self.messagebox = filedialog, messagebox
        self.root = root
        self.root.title("e芯 STM32 上位机 · USART1 / 115200")
        self.root.geometry("1120x850")
        self.root.minsize(900, 700)
        self.messages = queue.Queue(maxsize=1024)
        self.worker = None
        self.connected = False
        self.csv_file = None
        self.csv_writer = None
        self.points = deque(maxlen=600)
        self.last_ms = None
        self.chart_time = 0.0
        self.port = tk.StringVar()
        self.status = tk.StringVar(value="未连接 · 不会自动发送控制命令")
        self.latest = tk.StringVar(value="等待 TRACE 数据")
        self.command = tk.StringVar(value="HELP")
        self.servo_angle = tk.StringVar(value="90")
        self.alarm_number = tk.StringVar(value="1")
        self._build()
        self._refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self._close)
        self.root.after(50, self._poll)

    def _build(self):
        ttk = self.ttk
        container = ttk.Frame(self.root, padding=12)
        container.pack(fill="both", expand=True)
        toolbar = ttk.Frame(container)
        toolbar.pack(fill="x", pady=(0, 10))
        ttk.Label(toolbar, text="串口").pack(side="left")
        self.port_combo = ttk.Combobox(toolbar, textvariable=self.port, width=16, state="readonly")
        self.port_combo.pack(side="left", padx=6)
        ttk.Button(toolbar, text="刷新", command=self._refresh_ports).pack(side="left")
        ttk.Label(toolbar, text="115200 · 8N1").pack(side="left", padx=12)
        self.connect_button = ttk.Button(toolbar, text="连接", command=self._connect)
        self.connect_button.pack(side="left")
        ttk.Button(toolbar, text="断开", command=self._disconnect).pack(side="left", padx=6)
        ttk.Label(toolbar, textvariable=self.status).pack(side="left", padx=12)

        controls = ttk.LabelFrame(container, text="设备控制（点击才发送）", padding=8)
        controls.pack(fill="x")
        time_row = ttk.Frame(controls)
        time_row.pack(fill="x", pady=3)
        ttk.Label(time_row, text="时间").pack(side="left", padx=(0, 12))
        self.time_fields = self._time_inputs(time_row, ("23", "59", "55"))
        ttk.Button(time_row, text="设置 TIME", command=self._set_time).pack(side="left", padx=8)
        for text in ("HELP", "IMU ?", "SERVO ?", "ALARM ?"):
            ttk.Button(time_row, text=text, command=lambda command=text: self._send(command)).pack(side="left", padx=3)

        alarm_row = ttk.Frame(controls)
        alarm_row.pack(fill="x", pady=3)
        ttk.Label(alarm_row, text="闹钟").pack(side="left", padx=(0, 12))
        ttk.Combobox(alarm_row, textvariable=self.alarm_number, values=("1", "2", "3"),
                     width=3, state="readonly").pack(side="left", padx=(0, 8))
        self.alarm_fields = self._time_inputs(alarm_row, ("06", "30", "00"))
        ttk.Button(alarm_row, text="设置并启用", command=self._set_alarm).pack(side="left", padx=8)
        ttk.Button(alarm_row, text="OFF", command=lambda: self._send("ALARM" + self.alarm_number.get() + " OFF")).pack(side="left", padx=3)
        ttk.Button(alarm_row, text="DEL", command=lambda: self._send("ALARM" + self.alarm_number.get() + " DEL")).pack(side="left", padx=3)

        servo_row = ttk.Frame(controls)
        servo_row.pack(fill="x", pady=3)
        ttk.Label(servo_row, text="舵机").pack(side="left", padx=(0, 12))
        ttk.Spinbox(servo_row, textvariable=self.servo_angle, from_=0, to=180, width=5).pack(side="left")
        ttk.Label(servo_row, text="°").pack(side="left")
        ttk.Button(servo_row, text="确认 SET", command=self._set_servo).pack(side="left", padx=8)
        for text in ("HOLD ON", "HOLD OFF", "HOLD ?", "TRACE ON", "TRACE OFF", "TRACE ?"):
            ttk.Button(servo_row, text=text, command=lambda command=text: self._send("SERVO " + command)).pack(side="left", padx=3)

        command_row = ttk.Frame(controls)
        command_row.pack(fill="x", pady=(7, 3))
        ttk.Label(command_row, text="原始命令").pack(side="left", padx=(0, 8))
        command_entry = ttk.Entry(command_row, textvariable=self.command)
        command_entry.pack(side="left", fill="x", expand=True)
        command_entry.bind("<Return>", lambda event: self._send(self.command.get()))
        ttk.Button(command_row, text="发送", command=lambda: self._send(self.command.get())).pack(side="left", padx=8)

        plot_box = ttk.LabelFrame(container, text="目标 / 实测 / 误差（最近 10 秒）", padding=8)
        plot_box.pack(fill="both", expand=True, pady=10)
        ttk.Label(plot_box, textvariable=self.latest).pack(anchor="w")
        self.canvas = self.tk.Canvas(plot_box, height=240, background="#f8fafc", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True, pady=(5, 0))
        self.canvas.bind("<Configure>", lambda event: self._draw_plot())

        logbar = ttk.Frame(container)
        logbar.pack(fill="x")
        self.log_button = ttk.Button(logbar, text="开始记录 CSV", command=self._toggle_log)
        self.log_button.pack(side="left")
        ttk.Button(logbar, text="清空曲线", command=self._clear_plot).pack(side="left", padx=8)
        ttk.Button(logbar, text="清空文本", command=lambda: self.console.delete("1.0", "end")).pack(side="left")
        ttk.Label(logbar, text="CSV 包含电脑接收时间；开启 TRACE 需手动点击。每次连接不会改变舵机。").pack(side="left", padx=12)
        console_box = ttk.Frame(container)
        console_box.pack(fill="both", pady=(8, 0))
        self.console = self.tk.Text(console_box, height=10, wrap="none", font=("Consolas", 10))
        scrollbar = ttk.Scrollbar(console_box, orient="vertical", command=self.console.yview)
        self.console.configure(yscrollcommand=scrollbar.set)
        self.console.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    def _time_inputs(self, parent, defaults):
        fields = []
        for index, default in enumerate(defaults):
            variable = self.tk.StringVar(value=default)
            self.ttk.Spinbox(parent, textvariable=variable, from_=0,
                             to=23 if index == 0 else 59, width=3).pack(side="left")
            fields.append(variable)
            if index < 2:
                self.ttk.Label(parent, text=":").pack(side="left")
        return fields

    def _refresh_ports(self):
        try:
            from serial.tools import list_ports
            ports = sorted(port.device for port in list_ports.comports())
        except ImportError:
            self.status.set("未安装 pyserial：python -m pip install -r Tools/requirements.txt")
            ports = []
        self.port_combo.configure(values=ports)
        if self.port.get() not in ports:
            self.port.set("")  # User explicitly selects a port; never guess a device.

    def _connect(self):
        if self.worker and self.worker.is_alive():
            return
        port = self.port.get()
        if not port:
            self.messagebox.showinfo("选择串口", "请先选择与开发板连接的串口")
            return
        self.status.set("正在连接 " + port)
        self.connect_button.configure(state="disabled")
        self.worker = SerialWorker(port, self.messages)
        self.worker.start()

    def _disconnect(self):
        self.connected = False
        if self.worker:
            self.worker.stop_requested.set()
        self.status.set("正在断开" if self.worker and self.worker.is_alive() else "未连接")
        # No implicit HOLD/OFF/servo command: board keeps its configured state.

    def _send(self, command):
        if not self.connected or not self.worker:
            self.messagebox.showinfo("未连接", "请先选择串口并连接设备")
            return
        if not command or len(command) > 95 or any(ord(c) < 32 or ord(c) > 126 for c in command):
            self.messagebox.showerror("命令无效", "命令必须为 1～95 个可打印 ASCII 字符，不含换行")
            return
        if not self.worker.enqueue(command):
            self.messagebox.showerror("发送队列已满", "命令未入队，请稍后重试")

    def _set_time(self):
        try:
            self._send(build_time_command(*(field.get() for field in self.time_fields)))
        except ValueError as error:
            self.messagebox.showerror("时间无效", str(error))

    def _set_alarm(self):
        try:
            self._send(build_time_command(*(field.get() for field in self.alarm_fields), alarm=self.alarm_number.get()))
        except ValueError as error:
            self.messagebox.showerror("闹钟无效", str(error))

    def _set_servo(self):
        try:
            self._send(build_servo_command(self.servo_angle.get()))
        except ValueError as error:
            self.messagebox.showerror("角度无效", str(error))

    def _console_line(self, text):
        self.console.insert("end", text + "\n")
        if int(self.console.index("end-1c").split(".")[0]) > 500:
            self.console.delete("1.0", "101.0")
        self.console.see("end")

    def _poll(self):
        plot_changed = False
        for _ in range(100):
            try:
                kind, value = self.messages.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                self.connected = value == "connected"
                self.status.set("已连接 " + self.port.get() if self.connected else "未连接")
                self.connect_button.configure(state="disabled" if self.connected else "normal")
            elif kind == "sent":
                self._console_line("> " + value)
            elif kind == "error":
                self._console_line("! " + value)
            elif kind == "dropped":
                self._console_line(f"! 电脑接收队列已满，丢弃 {value} 条消息；CSV 可能不连续")
            elif kind == "line":
                timestamp, line = value
                self._console_line("< " + line)
                sample = parse_trace_line(line)
                if sample:
                    if self.last_ms is not None:
                        delta = (sample.ms - self.last_ms) & 0xFFFFFFFF
                        if delta > 10000:
                            self._clear_plot()
                        else:
                            self.chart_time += delta / 1000.0
                    self.last_ms = sample.ms
                    self.points.append((self.chart_time, sample))
                    self.latest.set(f"目标 {sample.target:+.1f}°   实测 {sample.measured:+.1f}°   误差 {sample.error:+.1f}°   "
                                    f"舵机 {sample.servo:.1f}°   饱和 {sample.saturated}   故障 {sample.fault}")
                    if self.csv_writer:
                        try:
                            self.csv_writer.writerow(sample.csv_row(timestamp))
                        except OSError as error:
                            self._console_line("! CSV 写入失败：" + str(error))
                            self._stop_log()
                    plot_changed = True
        if self.csv_file:
            try:
                self.csv_file.flush()
            except OSError as error:
                self._console_line("! CSV 刷新失败：" + str(error))
                self._stop_log()
        if plot_changed:
            self._draw_plot()
        self.root.after(50, self._poll)

    def _clear_plot(self):
        self.points.clear()
        self.last_ms = None
        self.chart_time = 0
        self._draw_plot()

    def _draw_plot(self):
        canvas = self.canvas
        canvas.delete("all")
        width, height = max(canvas.winfo_width(), 200), max(canvas.winfo_height(), 140)
        left, top, right, bottom = 55, 22, width - 18, height - 32
        now = self.chart_time
        start = max(0, now - 10)
        visible = [(t, s) for t, s in self.points if t >= start]
        values = [value for _, sample in visible for value in (sample.target, sample.measured, sample.error)]
        ymin, ymax = (min(values), max(values)) if values else (-20, 20)
        pad = max(2, (ymax - ymin) * 0.15)
        ymin, ymax = ymin - pad, ymax + pad
        def xy(t, value):
            return (left + (t - start) / 10 * (right - left),
                    bottom - (value - ymin) / (ymax - ymin) * (bottom - top))
        for index in range(5):
            y = top + index * (bottom - top) / 4
            value = ymax - index * (ymax - ymin) / 4
            canvas.create_line(left, y, right, y, fill="#dbe3ed")
            canvas.create_text(left - 7, y, text=f"{value:.1f}", anchor="e", fill="#475569")
        for index in range(6):
            x = left + index * (right - left) / 5
            canvas.create_line(x, top, x, bottom, fill="#e2e8f0")
            canvas.create_text(x, bottom + 16, text=f"{start + index * 2:.0f}s", fill="#475569")
        canvas.create_text(10, 8, text="°", anchor="nw", fill="#475569")
        for label, field, color, legend_x in (("目标", "target", "#dc2626", 70),
                ("实测", "measured", "#2563eb", 160), ("误差", "error", "#16a34a", 250)):
            canvas.create_text(legend_x, 8, text=label, fill=color, anchor="w")
            coordinates = [coord for t, sample in visible for coord in xy(t, getattr(sample, field))]
            if len(coordinates) >= 4:
                canvas.create_line(*coordinates, fill=color, width=2)

    def _toggle_log(self):
        if self.csv_file:
            self._stop_log()
            return
        path = self.filedialog.asksaveasfilename(title="保存 TRACE 记录", defaultextension=".csv",
            filetypes=[("CSV", "*.csv")], initialfile="stm32_trace.csv")
        if not path:
            return
        try:
            self.csv_file = open(path, "w", encoding="utf-8-sig", newline="")
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(("received_at",) + TRACE_COLUMNS)
            self.csv_file.flush()
            self.log_button.configure(text="停止记录 CSV")
            self._console_line("! CSV 记录开始：" + path)
        except OSError as error:
            self._stop_log()
            self.messagebox.showerror("无法记录", str(error))

    def _stop_log(self):
        if self.csv_file:
            try:
                self.csv_file.close()
            except OSError:
                pass
        self.csv_file = None
        self.csv_writer = None
        self.log_button.configure(text="开始记录 CSV")

    def _close(self):
        if self.worker:
            self.worker.stop_requested.set()
        self._stop_log()
        self.root.destroy()


def main():
    import tkinter as tk
    root = tk.Tk()
    MonitorApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
