"""Observe running IMU RAM through AutoDebug; no flash, reset or UART writes."""
from pathlib import Path
import json
import math
import re
import struct
import sys
import time

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, r'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/stm32-autodebug')
from autodebug.config import AutoDebugConfig
from autodebug.hardware_probe import HardwareProbe

cfg = AutoDebugConfig.load()
cfg.debugger.target_override = 'stm32f103c8'
cfg.debugger.probe_id = '37FF71064E573436B5C31E43'
cfg.debugger.pack_files = [r'E:/keil5/ARM/PACK/Keil/STM32F1xx_DFP/2.4.0']
cfg.debugger.frequency_hz = 1000000
cfg.debugger.connect_mode = 'attach'
listing = (root / 'Project/Listings/eXin.map').read_text(errors='replace')
match = re.search(r'^\s*latest\s+0x([0-9a-fA-F]+)\s+Data\s+\d+\s+imu.o', listing, re.M)
if not match:
    raise RuntimeError('Missing latest IMU symbol')
ptr = int(match.group(1), 16)
probe = HardwareProbe(cfg.debugger)
records = []
def read32(addr):
    # This probe returned the previous address's value on the first read.
    # Read the same address twice, then validate against the current HEX.
    probe.read_memory(addr, 32)
    return probe.read_memory(addr, 32)

try:
    if not probe.open():
        raise RuntimeError('Cannot attach to running board')
    print('Latest symbol: '+hex(ptr), flush=True)
    print('Core running: '+str(probe.target.is_running()), flush=True)
    if probe.target.is_halted():
        probe.resume()
    from intelhex import IntelHex
    expected = IntelHex(str(root / 'build/keil/eXin.hex'))
    mismatches = []
    for start, end in expected.segments():
        for addr in range(start, end - 3, 4):
            if (addr - start) % 256:
                continue
            actual = read32(addr)
            wanted = sum(expected[addr+i] << (8*i) for i in range(4))
            if actual != wanted:
                mismatches.append(hex(addr))
                if len(mismatches) <= 2:
                    print('Mismatch: '+hex(addr)+' actual='+str(actual)+' expected='+str(wanted), flush=True)
    print('Flash sample mismatches: '+str(mismatches[:12]), flush=True)
    if mismatches:
        raise RuntimeError('Running flash differs from current HEX/map; do not interpret RAM with this map')
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        stamp = read32(ptr + 40)
        probe.target.read_memory_block8(ptr, 48)
        data = bytes(probe.target.read_memory_block8(ptr, 48))
        after = read32(ptr + 40)
        if stamp == after == struct.unpack_from('<I', data, 40)[0]:
            values = struct.unpack('<10f', data[:40])
            rec = dict(zip(['gx', 'gy', 'gz', 'ax', 'ay', 'az', 'roll', 'pitch', 'yaw', 'temperature'], values))
            rec.update(ms=stamp, valid=bool(data[44]), calibrated=bool(data[45]))
            rec['norm'] = math.sqrt(sum(rec[key] ** 2 for key in ('ax', 'ay', 'az')))
            rec['gravity_pitch'] = math.degrees(math.atan2(-rec['ax'], -rec['ay']))
            if not records or records[-1]['ms'] != stamp:
                records.append(rec)
        time.sleep(0.025)
finally:
    probe.resume()
    probe.close()
    path = root / 'build/autodebug_serial/imu_observe_2026-10-14.json'
    path.write_text(json.dumps(records, indent=2), encoding='utf-8')
if not records:
    raise RuntimeError('No coherent running IMU snapshots')
stats = {key: [min(r[key] for r in records), max(r[key] for r in records)]
         for key in ('gx', 'gy', 'gz', 'ax', 'ay', 'az', 'norm', 'pitch', 'gravity_pitch', 'temperature')}
print(json.dumps({'samples': len(records), 'invalid': sum(not r['valid'] for r in records),
                  'ranges': stats, 'first': records[0], 'last': records[-1]}, indent=2))
