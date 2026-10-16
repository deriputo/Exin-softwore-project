"""Offline Tk checks. No serial ports, BLE scans or hardware commands."""
import sys
import unittest
from pathlib import Path
import tkinter as tk

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pc_monitor import MonitorApp


class OfflineApp(MonitorApp):
    def _refresh_ports(self):
        pass


class MonitorUiTest(unittest.TestCase):
    def setUp(self):
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = OfflineApp(self.root)

    def tearDown(self):
        self.root.destroy()

    def test_minimum_ble_layout_and_transport_lock(self):
        app = self.app
        self.root.geometry("1100x820")
        app.transport.set("蓝牙BLE")
        app._change_transport()
        self.root.deiconify()
        self.root.update()
        self.assertTrue(app.ble_box.winfo_ismapped())
        self.assertGreaterEqual(app.canvas.winfo_height(), 70)
        bottom = app.console.winfo_rooty() + app.console.winfo_height()
        self.assertLessEqual(bottom, self.root.winfo_rooty() + self.root.winfo_height())
        app._lock_connection_controls(True)
        self.assertEqual(str(app.mode_combo.cget("state")), "disabled")
        self.assertTrue(all(str(entry.cget("state")) == "disabled" for entry in app.uuid_entries))
        app._lock_connection_controls(False)
        app.transport.set("USB串口")
        app._change_transport()
        self.root.update()
        self.assertFalse(app.ble_box.winfo_ismapped())

    def test_button_bindings_keep_existing_commands(self):
        sent = []
        self.app._send = sent.append

        def descendants(widget):
            for child in widget.winfo_children():
                yield child
                yield from descendants(child)

        buttons = {str(widget.cget("text")): widget for widget in descendants(self.root)
                   if widget.winfo_class() == "TButton"}
        for label in ("HELP", "IMU ?", "SERVO ?", "ALARM ?", "HOLD ON", "HOLD OFF",
                      "状态 ?", "TRACE ON", "TRACE OFF", "TRACE ?", "确认角度  /  SET",
                      "设置时间  /  TIME", "设置并启用", "停用", "删除"):
            buttons[label].invoke()
        self.assertEqual(sent, ["HELP", "IMU ?", "SERVO ?", "ALARM ?", "SERVO HOLD ON",
            "SERVO HOLD OFF", "SERVO HOLD ?", "SERVO TRACE ON", "SERVO TRACE OFF", "SERVO TRACE ?",
            "SERVO SET 90", "TIME(23:59:55)", "ALARM1(06:30:00)", "ALARM1 OFF", "ALARM1 DEL"])

    def test_trace_metrics_log_and_reset(self):
        app = self.app
        app.messages.put(("line", ("2026-10-17T00:00:00", "H,1,90,89.5,0.5,0,0,0,90,0,NONE")))
        app._poll()
        self.assertEqual([v.get() for v in app.metric_values], ["+90.0°", "+89.5°", "+0.5°"])
        self.assertEqual(len(app.points), 1)
        self.assertIn("H,1,90", app.console.get("1.0", "end"))
        app._clear_plot()
        self.assertEqual([v.get() for v in app.metric_values], ["—"] * 3)
        self.assertEqual(len(app.points), 0)
        self.assertEqual(app.latest.get(), "等待 TRACE 数据")


if __name__ == "__main__":
    unittest.main()
