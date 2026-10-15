"""Use the selected AutoDebug kit APIs for build/flash/UART qualification.

Hardware acceptance is limited to a stationary mechanism near vertical. No
claim about full-range accuracy or physical disturbance recovery is made.
"""
from pathlib import Path
import json
import math
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
KIT = Path(r'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/stm32-autodebug')
sys.path.insert(0, str(KIT))
from autodebug.config import AutoDebugConfig
from autodebug.engine import AutoDebugEngine
from autodebug.hardware_probe import HardwareProbe
from autodebug.serial_monitor import SerialMonitor

report = {'scope': 'flash, UART commands, calibrated IMU and stationary HOLD only',
          'status': 'STARTED', 'commands': [], 'trace': []}
out = ROOT / 'build/autodebug_serial'
out.mkdir(parents=True, exist_ok=True)
cfg = AutoDebugConfig.load()
cfg.keil.uv4_path = r'E:/keil5/UV4/UV4.exe'
cfg.debugger.target_override = 'stm32f103c8'
cfg.debugger.probe_id = '37FF71064E573436B5C31E43'
cfg.debugger.pack_files = [r'E:/keil5/ARM/PACK/Keil/STM32F1xx_DFP/2.4.0']
cfg.debugger.frequency_hz = 1000000
cfg.debugger.auto_install_pack = False
monitor = SerialMonitor(cfg.serial, port='COM6', baudrate=115200)
probe = HardwareProbe(cfg.debugger)
running = False
cursor = 0

def capture(seconds):
    global cursor
    time.sleep(seconds)
    lines = monitor._drain_new(cursor)
    cursor += len(lines)
    return lines

def command(text, expected=None, wait=0.5):
    monitor._ser.write((text + '\r\n').encode('ascii'))
    monitor._ser.flush()
    lines = capture(wait)
    deadline = time.monotonic() + 3
    while expected is not None and not any(expected in x for x in lines) and time.monotonic() < deadline:
        lines.extend(capture(0.1))
    report['commands'].append({'sent': text, 'received': lines})
    print(text + ' -> ' + ' | '.join(x for x in lines if not x.startswith('H,'))[:600], flush=True)
    if expected is not None and not any(expected in x for x in lines):
        raise RuntimeError('Missing expected reply: ' + expected)
    return lines

def field(lines, name):
    match = re.search(r'\b' + re.escape(name) + r'=([+-]?\d+(?:\.\d+)?)', '\n'.join(lines))
    if not match or not math.isfinite(float(match.group(1))):
        raise RuntimeError('Missing or nonfinite field ' + name)
    return float(match.group(1))

try:
    # Check that the chosen UART is free before modifying the target.
    if not monitor.open():
        report['status'] = 'SERIAL_UNAVAILABLE'
        raise RuntimeError(monitor.open_error)
    monitor.close()
    if '--serial-only' not in sys.argv:
        print('COM6 available; building with AutoDebug.', flush=True)
        engine = AutoDebugEngine(cfg, verbose=True)
        built = engine.builder.build(str(ROOT/'Project/eXin.uvprojx'), None, rebuild=False)
        report['build'] = {'success': built.success, 'code': built.return_code, 'axf': built.axf_path}
        if not built.success:
            report['status'] = 'BUILD_FAILED'
            raise RuntimeError(built.failure_reason or 'Build failed')
        if not probe.open():
            report['status'] = 'FLASH_FAILED'
            raise RuntimeError('Cannot connect to the selected ST-Link/STM32F103C8')
        probe.clear_fault_registers()
        flashed = probe.flash(built.axf_path, halt_after=True)
        report['flash'] = flashed.__dict__
        if not flashed.success or not flashed.halted:
            report['status'] = 'FLASH_FAILED'
            raise RuntimeError(flashed.message)
        print('Flashed and halted; opening UART before resume.', flush=True)
    else:
        report['flash'] = {'skipped': True, 'reason': 'Testing firmware flashed in the preceding run'}
        if '--reset-before-run' in sys.argv:
            if not probe.open(): raise RuntimeError('Cannot connect for recalibration reset')
            probe.target.reset_and_halt()
            report['reset_for_calibration'] = True
    if not monitor.open():
        report['status'] = 'SERIAL_UNAVAILABLE'
        raise RuntimeError(monitor.open_error)
    if ('--serial-only' not in sys.argv or '--reset-before-run' in sys.argv) and not probe.resume():
        raise RuntimeError('Cannot resume core')
    running = True
    capture(4)
    command('')  # Finish any partial input without changing grammar.
    command('SERVO TRACE OFF', 'OK SERVO TRACE=OFF')
    command('SERVO HOLD OFF', 'OK SERVO HOLD=OFF')
    command('HELP', 'SERVO HOLD ON')
    command('SERVO SET 90', 'OK SERVO=90 HOLD=OFF')
    imu = command('IMU ?', 'X=')
    report['imu_relative_y'] = field(imu, 'Y')
    if abs(report['imu_relative_y']) > 10:
        raise RuntimeError('Not near the vertical calibration reference; skip automatic HOLD')
    command('SERVO HOLD ON', 'OK SERVO HOLD=ON')
    state = command('SERVO HOLD ?', 'FAULT=OFF')
    target = field(state, 'target')
    measured = field(state, 'measured')
    if abs(measured - (90 + report['imu_relative_y'])) > 2:
        raise RuntimeError('Firmware reference mapping does not match the new version')
    report['captured_target'] = target
    command('SERVO SET 95', 'ERR HOLD ACTIVE')  # Must reject, without moving.
    command('SERVO TRACE ON', 'OK SERVO TRACE=ON')
    print('Observing stationary HOLD for 8 seconds.', flush=True)
    lines = capture(8)
    report['trace'] = [x for x in lines if x.startswith('H,')]
    rows = []
    for line in report['trace']:
        values = line.split(',')
        if len(values) != 11:
            raise RuntimeError('Malformed TRACE row')
        numbers = [float(v) for v in values[1:10]]
        if not all(math.isfinite(v) for v in numbers) or values[10] != 'NONE':
            raise RuntimeError('TRACE reports fault or nonfinite data')
        rows.append(numbers)
    if len(rows) < 100:
        raise RuntimeError('Too few TRACE samples')
    gaps = [b[0]-a[0] for a,b in zip(rows, rows[1:])]
    outputs = [r[7] for r in rows]
    errors = [abs(r[3]) for r in rows]
    report['trace_summary'] = {'count': len(rows), 'tick_gap_min': min(gaps),
        'tick_gap_max': max(gaps), 'servo_min': min(outputs), 'servo_max': max(outputs),
        'max_abs_error': max(errors)}
    if any(g < 40 or g > 70 for g in gaps):
        raise RuntimeError('TRACE target tick interval is not about50 ms')
    if min(outputs) < 0 or max(outputs) > 180:
        raise RuntimeError('Output outside limits')
    if max(outputs)-min(outputs) > 10 or max(errors) > 10:
        raise RuntimeError('Static HOLD unstable; inspect trace before tuning')
    command('SERVO TRACE OFF', 'OK SERVO TRACE=OFF')
    command('SERVO HOLD ?', 'FAULT=OFF')
    report['status'] = 'SERIAL_AND_STATIC_HOLD_CHECKS_PASSED'
except Exception as exc:
    report['error'] = str(exc)
    if report['status'] == 'STARTED': report['status'] = 'SERIAL_CHECK_FAILED'
finally:
    # Leave the mechanism at its last valid position in manual mode.
    if monitor._ser is not None and running:
        try:
            command('SERVO TRACE OFF', wait=0.2)
            command('SERVO HOLD OFF', wait=0.2)
        except Exception as exc:
            report['cleanup_error'] = str(exc)
    monitor.close()
    if probe.target is not None and not running:
        probe.resume()
    probe.close()
    (out/'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    (out/'trace.csv').write_text('\n'.join(report['trace']), encoding='ascii')
    print(json.dumps({k: v for k, v in report.items() if k not in ('commands', 'trace')},
                     ensure_ascii=False, indent=2), flush=True)
sys.exit(0 if report['status'] == 'SERIAL_AND_STATIC_HOLD_CHECKS_PASSED' else 1)
