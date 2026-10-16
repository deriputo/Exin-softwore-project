import csv
import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import asyncio
import queue
from types import SimpleNamespace

MODULE_PATH = Path(__file__).resolve().parents[1] / "Tools" / "pc_monitor.py"
SPEC = importlib.util.spec_from_file_location("pc_monitor_under_test", MODULE_PATH)
monitor = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = monitor
SPEC.loader.exec_module(monitor)


class BleTransportTest(unittest.IsolatedAsyncioTestCase):
    def make_session(self, properties=None, fail_write=False, service_present=True):
        messages = queue.Queue()
        worker = monitor.BleWorker(SimpleNamespace(address="test-address"), messages)
        characteristic = SimpleNamespace(properties=properties or ["notify", "write-without-response"])
        service = SimpleNamespace(get_characteristic=lambda uuid: characteristic)
        writes = []

        class Client:
            is_connected = True
            closed = False
            services = SimpleNamespace(get_service=lambda uuid: service if service_present else None)

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                self.closed = True

            async def start_notify(self, char, callback):
                self.callback = callback
                # Fragmentation is arbitrary, even inside CRLF or a TRACE field.
                callback(char, b"OK TI")
                callback(char, b"ME\r")
                callback(char, b"\nH,1,90,89,1,0,0,0,90,0,NONE\r\n")

            async def write_gatt_char(self, char, data, response):
                if fail_write:
                    raise OSError("radio disconnected")
                writes.append((data, response))
                if data.endswith(b"\r\n"):
                    worker.stop_requested.set()

        client = Client()
        return worker, messages, client, writes

    async def test_fragmented_notifications_and_long_command_chunks(self):
        worker, messages, client, writes = self.make_session()
        command = "X" * 70
        worker.enqueue(command)
        await asyncio.wait_for(worker.session(lambda *a, **kw: client), 2)
        self.assertTrue(client.closed)
        self.assertEqual(b"".join(data for data, _ in writes), (command + "\r\n").encode())
        self.assertTrue(all(len(data) <= 20 and response is False for data, response in writes))
        events = list(messages.queue)
        lines = [value[1] for kind, value in events if kind == "line"]
        self.assertEqual(lines[0], "OK TIME")
        self.assertIsNotNone(monitor.parse_trace_line(lines[1]))
        self.assertIn(("sent", command), events)

    async def test_acknowledged_write_and_no_implicit_control_commands(self):
        worker, messages, client, writes = self.make_session(["notify", "write"])
        task = asyncio.create_task(worker.session(lambda *a, **kw: client))
        await asyncio.sleep(0.03)
        self.assertEqual(writes, [])  # Connection/notification subscription does not move servo.
        worker.enqueue("IMU ?")
        await asyncio.wait_for(task, 2)
        self.assertEqual(writes, [(b"IMU ?\r\n", True)])

    async def test_wrong_service_and_read_only_characteristic(self):
        for kwargs in ({"service_present": False}, {"properties": ["notify"]},
                       {"properties": ["write"]}):
            worker, messages, client, writes = self.make_session(**kwargs)
            with self.assertRaises(ValueError):
                await worker.session(lambda *a, **kw: client)
            self.assertTrue(client.closed)
            self.assertEqual(writes, [])
            self.assertNotIn(("status", "connected"), list(messages.queue))

    async def test_write_failure_does_not_report_sent_or_replay(self):
        worker, messages, client, writes = self.make_session(fail_write=True)
        worker.enqueue("SERVO SET 90")
        with self.assertRaises(OSError):
            await worker.session(lambda *a, **kw: client)
        self.assertTrue(client.closed)
        self.assertFalse(any(kind == "sent" for kind, value in list(messages.queue)))

    async def test_idle_disconnect_and_cancel_during_connect(self):
        worker, messages, client, writes = self.make_session()
        client.is_connected = False
        with self.assertRaises(OSError):
            await worker.session(lambda *a, **kw: client)
        worker, messages, client, writes = self.make_session()
        worker.stop_requested.set()
        await worker.session(lambda *a, **kw: client)
        self.assertTrue(client.closed)
        self.assertEqual(list(messages.queue), [])

    def test_line_overflow_recovers_and_bounded_command_queue(self):
        events = []
        receiver = monitor.LineReceiver(lambda *event: events.append(event))
        receiver.feed(b"X" * 4097 + b"\r\nOK\n")
        self.assertEqual(events[0][0], "error")
        self.assertEqual(events[1][1][1], "OK")
        worker, _, _, _ = self.make_session()
        self.assertTrue(all(worker.enqueue("HELP") for _ in range(64)))
        self.assertFalse(worker.enqueue("HELP"))
        worker.stop_requested.set()
        self.assertFalse(worker.enqueue("HELP"))


class MonitorHelpersTest(unittest.TestCase):
    def test_usb_worker_still_uses_115200_and_shared_line_receiver(self):
        messages = queue.Queue()
        worker = monitor.SerialWorker("COM_TEST", messages)
        opened, written, closed = [], [], []

        class Port:
            in_waiting = 0

            def write(self, payload):
                written.append(payload)
                return len(payload)

            def read(self, size):
                worker.stop_requested.set()
                return b"OK TIME\r\n"

            def close(self):
                closed.append(True)

        def open_port(*args, **kwargs):
            opened.append((args, kwargs))
            return Port()

        worker.enqueue("TIME(14:30:05)")
        with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=open_port)}):
            worker.run()
        self.assertEqual(opened[0][0], ("COM_TEST", 115200))
        self.assertEqual(written, [b"TIME(14:30:05)\r\n"])
        self.assertEqual(closed, [True])
        self.assertIn(("status", "disconnected"), list(messages.queue))
        self.assertTrue(any(kind == "line" and value[1] == "OK TIME"
                            for kind, value in list(messages.queue)))

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
