"""Optional STM32 USB/BLE monitor. No device connects or command sends automatically.

Python 3.9+, Tkinter, pyserial for USB; Bleak for Bluetooth LE.
Parsing/building helpers intentionally work without Tkinter or pyserial installed.
"""
from __future__ import annotations

import csv
import asyncio
import math
import queue
import threading
from collections import deque
from dataclasses import dataclass
from datetime import datetime
from typing import Optional

TRACE_COLUMNS = ("ms", "target", "measured", "error", "P", "I", "D", "servo", "saturated", "fault")
TRACE_HEADER = "H," + ",".join(TRACE_COLUMNS)
BLE_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
BLE_CHARACTERISTIC = "0000ffe1-0000-1000-8000-00805f9b34fb"


class LineReceiver:
    """Reassemble UART lines across arbitrary serial reads / BLE notifications."""

    def __init__(self, emit):
        self.emit = emit
        self.pending = bytearray()
        self.invalid = False

    def feed(self, chunk):
        for byte in chunk:
            if byte in (10, 13):
                if self.invalid:
                    self.emit("error", "超长设备响应已丢弃")
                elif self.pending:
                    timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
                    self.emit("line", (timestamp, self.pending.decode("ascii", errors="replace")))
                self.pending.clear()
                self.invalid = False
            elif len(self.pending) < 4096 and not self.invalid:
                self.pending.append(byte)
            else:
                self.invalid = True


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
        receiver = LineReceiver(self.emit)
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
                receiver.feed(chunk)
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


class BleWorker(SerialWorker):
    """Own the BLE event loop in a background MTA thread, away from Tk."""

    def __init__(self, device, messages, service=BLE_SERVICE,
                 rx=BLE_CHARACTERISTIC, tx=BLE_CHARACTERISTIC):
        super().__init__(device.address, messages)
        self.device = device
        self.service_uuid, self.rx_uuid, self.tx_uuid = service, rx, tx
        self.name = "STM32BLE"

    async def session(self, client_factory):
        receiver = LineReceiver(self.emit)
        async with client_factory(self.device, timeout=15.0) as client:
            if self.stop_requested.is_set():
                return
            service = client.services.get_service(self.service_uuid)
            if service is None:
                raise ValueError("未找到 BLE 服务 " + self.service_uuid + "，请核对 UUID")
            rx = service.get_characteristic(self.rx_uuid)
            tx = service.get_characteristic(self.tx_uuid)
            if rx is None or not set(rx.properties).intersection(("notify", "indicate")):
                raise ValueError("接收 UUID 不存在或不支持通知")
            if tx is None or not set(tx.properties).intersection(("write", "write-without-response")):
                raise ValueError("发送 UUID 不存在或不支持写入")
            # Prefer acknowledged writes if offered; explicitly select the operation.
            response = "write" in tx.properties
            await client.start_notify(rx, lambda sender, data: receiver.feed(data))
            self.emit("status", "connected")
            while not self.stop_requested.is_set():
                if not client.is_connected:
                    raise OSError("蓝牙连接已断开，请重新扫描连接")
                try:
                    command = self.commands.get_nowait()
                except queue.Empty:
                    await asyncio.sleep(0.02)
                    continue
                payload = (command + "\r\n").encode("ascii", errors="strict")
                # Legacy HC-04 MTU and 9600 baud UART: conservative 20-byte chunks.
                for offset in range(0, len(payload), 20):
                    if self.stop_requested.is_set():
                        return
                    await asyncio.wait_for(client.write_gatt_char(
                        tx, payload[offset:offset + 20], response=response), timeout=3.0)
                    await asyncio.sleep(0.03)
                self.emit("sent", command)
                if self.dropped_messages:
                    count = self.dropped_messages
                    self.dropped_messages = 0
                    self.emit("dropped", count)

    def run(self):
        try:
            from bleak import BleakClient
            asyncio.run(self.session(BleakClient))
        except ImportError:
            self.emit("error", "缺少 bleak，请按蓝牙使用说明安装 tools/requirements.txt")
        except Exception as error:
            self.emit("error", "蓝牙：" + str(error))
        finally:
            self.emit("status", "disconnected")


class MonitorApp:
    def __init__(self, root):
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        self.tk, self.ttk = tk, ttk
        self.filedialog, self.messagebox = filedialog, messagebox
        self.root = root
        self.root.title("e芯 STM32 上位机 · USB串口 / HC-04 BLE")
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
        self.transport = tk.StringVar(value="USB串口")
        self.ble_service = tk.StringVar(value=BLE_SERVICE)
        self.ble_rx = tk.StringVar(value=BLE_CHARACTERISTIC)
        self.ble_tx = tk.StringVar(value=BLE_CHARACTERISTIC)
        self.ble_devices = {}
        self.scanning = False
        self.connection_label = ""
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
        self.mode_combo = ttk.Combobox(toolbar, textvariable=self.transport,
                                      values=("USB串口", "蓝牙BLE"), width=10, state="readonly")
        self.mode_combo.pack(side="left")
        self.mode_combo.bind("<<ComboboxSelected>>", self._change_transport)
        self.port_combo = ttk.Combobox(toolbar, textvariable=self.port, width=30, state="readonly")
        self.port_combo.pack(side="left", padx=6)
        self.refresh_button = ttk.Button(toolbar, text="刷新 / 扫描", command=self._refresh_ports)
        self.refresh_button.pack(side="left")
        self.connect_button = ttk.Button(toolbar, text="连接", command=self._connect)
        self.connect_button.pack(side="left")
        ttk.Button(toolbar, text="断开", command=self._disconnect).pack(side="left", padx=6)
        ttk.Label(toolbar, textvariable=self.status).pack(side="left", padx=12)

        self.ble_box = ttk.LabelFrame(container, text="BLE配置：默认HC-04；先断开手机小程序，开启电脑蓝牙", padding=4)
        self.uuid_entries = []
        for label, variable in (("服务", self.ble_service), ("接收", self.ble_rx), ("发送", self.ble_tx)):
            row = ttk.Frame(self.ble_box)
            row.pack(fill="x")
            ttk.Label(row, text=label, width=6).pack(side="left")
            entry = ttk.Entry(row, textvariable=variable, width=44)
            entry.pack(side="left")
            self.uuid_entries.append(entry)
        # Inserted above controls only while BLE is selected.
        self.toolbar = toolbar

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
        if self.scanning or (self.worker and self.worker.is_alive()):
            return
        if self.transport.get() == "蓝牙BLE":
            self.scanning = True
            self._lock_connection_controls(True)
            self.status.set("正在扫描蓝牙（约5秒）…")
            def scan():
                try:
                    from bleak import BleakScanner
                    devices = asyncio.run(BleakScanner.discover(timeout=5.0))
                    self.messages.put(("scan", (devices, None)))
                except Exception as error:
                    self.messages.put(("scan", ([], str(error))))
            threading.Thread(target=scan, daemon=True, name="BLEScan").start()
            return
        try:
            from serial.tools import list_ports
            ports = sorted(port.device for port in list_ports.comports())
        except ImportError:
            self.status.set("未安装 pyserial：python -m pip install -r tools/requirements.txt")
            ports = []
        self.port_combo.configure(values=ports)
        if self.port.get() not in ports:
            self.port.set("")  # User explicitly selects a port; never guess a device.

    def _lock_connection_controls(self, locked):
        self.mode_combo.configure(state="disabled" if locked else "readonly")
        self.port_combo.configure(state="disabled" if locked else "readonly")
        self.refresh_button.configure(state="disabled" if locked else "normal")
        self.connect_button.configure(state="disabled" if locked else "normal")
        for entry in self.uuid_entries:
            entry.configure(state="disabled" if locked else "normal")

    def _change_transport(self, event=None):
        self.port.set("")
        self.port_combo.configure(values=())
        if self.transport.get() == "蓝牙BLE":
            self.ble_box.pack(fill="x", after=self.toolbar, pady=(0, 8))
            self.status.set("BLE：点击扫描并选择HC-04；不是COM口")
        else:
            self.ble_box.pack_forget()
            self.status.set("USB串口：115200 · 8N1")
            self._refresh_ports()

    def _connect(self):
        if self.scanning or (self.worker and self.worker.is_alive()):
            return
        port = self.port.get()
        if not port:
            self.messagebox.showinfo("选择设备", "请先刷新/扫描并选择设备")
            return
        if self.transport.get() == "蓝牙BLE":
            from uuid import UUID
            try:
                uuids = [str(UUID(v.get().strip())) for v in
                         (self.ble_service, self.ble_rx, self.ble_tx)]
                self.worker = BleWorker(self.ble_devices[port], self.messages, *uuids)
            except (ValueError, KeyError):
                self.messagebox.showerror("蓝牙配置", "请选择已扫描的设备，并填写完整有效的UUID")
                return
        else:
            self.worker = SerialWorker(port, self.messages)
        self.connection_label = self.transport.get() + " " + port
        self.status.set("正在连接 " + port)
        self._lock_connection_controls(True)
        self._clear_plot()
        self.worker.start()

    def _disconnect(self):
        self.connected = False
        if self.worker:
            self.worker.stop_requested.set()
        self.status.set("正在断开" if self.worker and self.worker.is_alive() else "未连接")
        # No implicit HOLD/OFF/servo command: board keeps its configured state.

    def _send(self, command):
        if not self.connected or not self.worker:
            self.messagebox.showinfo("未连接", "请先选择USB串口或蓝牙设备并连接")
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
                self.status.set("已连接 " + self.connection_label if self.connected else "未连接")
                self._lock_connection_controls(self.connected)
            elif kind == "scan":
                devices, error = value
                self.scanning = False
                self.ble_devices = {f"{d.name or '未命名'} [{d.address}]": d for d in devices}
                self.port.set("")
                self.port_combo.configure(values=sorted(self.ble_devices))
                self._lock_connection_controls(False)
                self.status.set(f"扫描到 {len(devices)} 个设备，请选择HC-04")
                if error:
                    self.status.set("扫描失败，详见下方；请确认已安装bleak并开启电脑蓝牙")
                    self._console_line("! 蓝牙扫描：" + error)
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
