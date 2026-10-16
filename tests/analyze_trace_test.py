import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_trace import analyze, read_csv, report, timeline, chart
from pc_monitor import TraceSample, TRACE_COLUMNS


def samples():
    return [TraceSample(i * 50, 90, 80 if i < 10 else 89.5, 999,
                        0, 0, 0, 90, 0, "NONE") for i in range(41)]


class AnalysisTest(unittest.TestCase):
    def test_recovery_uses_device_time_and_recomputes_error(self):
        result, rows = analyze(samples())
        self.assertEqual(result["recovery_s"], 0.5)
        self.assertEqual(result["stable_mean_error_deg"], 0.5)
        self.assertEqual(result["stable_peak_to_peak_deg"], 0)
        self.assertEqual(result["max_abs_error_deg"], 10)
        self.assertIn("不可用", report({**result, "recovery_s": None}, rows, "<test>"))
        self.assertIn("&lt;test&gt;", report(result, rows, "<test>"))
        ET.fromstring(chart(rows, 0.15, 1.0))

    def test_late_excursion_is_not_settled(self):
        data = samples()
        data[-5] = TraceSample(1800, 90, 92, 0, 0, 0, 0, 90, 0, "NONE")
        self.assertIsNone(analyze(data)[0]["recovery_s"])

    def test_fault_gap_and_insufficient_duration(self):
        for fault, delta in (("IMU_INVALID", 0), ("NONE", 200)):
            data = samples()
            data[3] = TraceSample(150, 90, 80, 0, 0, 0, 0, 90, 0, fault)
            if delta:
                data = [TraceSample(s.ms + (delta if i >= 4 else 0), s.target,
                        s.measured, s.error, s.p, s.i, s.d, s.servo, s.saturated, s.fault)
                        for i, s in enumerate(data)]
            result = analyze(data)[0]
            self.assertFalse(result["performance_valid"])
            self.assertIsNone(result["recovery_s"])
        self.assertIsNone(analyze(samples()[:20])[0]["recovery_s"])

    def test_wrap_restart_target_change_and_parameters(self):
        data = samples()
        wrapped = [TraceSample((s.ms + 0xffffff00) & 0xffffffff, s.target,
                    s.measured, s.error, s.p, s.i, s.d, s.servo, s.saturated, s.fault) for s in data]
        self.assertAlmostEqual(timeline(wrapped)[-1], 2)
        with self.assertRaises(ValueError):
            timeline([data[1], data[0]])
        with self.assertRaises(ValueError):
            timeline([data[0], data[0]])
        data[2] = TraceSample(100, 100, 89, 0, 0, 0, 0, 90, 0, "NONE")
        with self.assertRaises(ValueError):
            analyze(data)
        self.assertIsNotNone(analyze(data, start=0.5)[0]["recovery_s"])
        for options in ({"band": 0}, {"dwell": float("nan")}, {"gap": -1}, {"end": 3}):
            with self.assertRaises(ValueError):
                analyze(samples(), **options)

    def test_csv_cli_and_overwrite_protection(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "trace.csv"
            with source.open("w", newline="", encoding="utf-8-sig") as stream:
                writer = csv.writer(stream)
                writer.writerow(("received_at",) + TRACE_COLUMNS)
                for sample in samples():
                    writer.writerow(sample.csv_row("ignored"))
            self.assertEqual(len(read_csv(source)), 41)
            command = [sys.executable, str(Path(__file__).resolve().parents[1] / "tools/analyze_trace.py"), str(source)]
            result = subprocess.run(command, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            output = source.with_name("trace_analysis.json")
            self.assertEqual(json.loads(output.read_text())["recovery_s"], 0.5)
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)
            source.write_text("ms,target\n1,2\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                read_csv(source)


if __name__ == "__main__":
    unittest.main()
