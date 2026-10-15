import csv
import importlib.util
import io
from pathlib import Path
import sys
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / "Tools" / "pc_monitor.py"
SPEC = importlib.util.spec_from_file_location("pc_monitor_under_test", MODULE_PATH)
monitor = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = monitor
SPEC.loader.exec_module(monitor)


class MonitorHelpersTest(unittest.TestCase):
    def test_trace_and_csv_fields(self):
        sample = monitor.parse_trace_line("H,4294967295,+15.0,+14.6,+0.4,+1.0,+13.3,-0.1,104.2,0,NONE\r\n")
        self.assertIsNotNone(sample)
        self.assertEqual(sample.ms, 0xFFFFFFFF)
        self.assertEqual(sample.measured, 14.6)
        self.assertEqual(sample.saturated, 0)
        output = io.StringIO()
        writer = csv.writer(output)
        writer.writerow(("received_at",) + monitor.TRACE_COLUMNS)
        timestamp = "2026-10-11T13:14:15.123+08:00"
        writer.writerow(sample.csv_row(timestamp))
        rows = list(csv.reader(io.StringIO(output.getvalue())))
        self.assertEqual(len(rows[0]), 11)
        self.assertEqual(rows[1][0], timestamp)
        self.assertEqual(rows[1][-1], "NONE")

    def test_malformed_frames_are_rejected(self):
        for line in (monitor.TRACE_HEADER, "OK TIME", "", "H,0,0,0,0,0,0,0,90,0",
                "H,-1,0,0,0,0,0,0,90,0,NONE", "H,4294967296,0,0,0,0,0,0,90,0,NONE",
                "H,1,nan,0,0,0,0,0,90,0,NONE", "H,1,0,inf,0,0,0,0,90,0,NONE",
                "H,1,1e300,0,0,0,0,0,90,0,NONE",
                "H,1,0,0,0,0,0,0,90,2,NONE", "H,1,0,0,0,0,0,0,90,0.0,NONE",
                "H,1,0,0,0,0,0,0,90,0,bad fault", "H,1,0,0,0,0,0,0,90,0,NONE,extra",
                "H,０,0,0,0,0,0,0,90,0,NONE"):
            self.assertIsNone(monitor.parse_trace_line(line), line)

    def test_strict_command_builders(self):
        self.assertEqual(monitor.build_time_command("0", "1", "2"), "TIME(00:01:02)")
        self.assertEqual(monitor.build_time_command("23", "59", "59", "3"), "ALARM3(23:59:59)")
        self.assertEqual(monitor.build_servo_command("180"), "SERVO SET 180")
        for args in (("24", "0", "0"), ("0", "60", "0"), ("0", "0", "60"),
                     ("-1", "0", "0"), ("1.0", "0", "0"), ("０", "0", "0")):
            with self.assertRaises(ValueError):
                monitor.build_time_command(*args)
        with self.assertRaises(ValueError):
            monitor.build_time_command("0", "0", "0", "4")
        for angle in ("-1", "181", "90.0", "+90", " 90", "９０"):
            with self.assertRaises(ValueError):
                monitor.build_servo_command(angle)


if __name__ == "__main__":
    unittest.main()
