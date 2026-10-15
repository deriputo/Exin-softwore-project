"""Read current IMU diagnostics through the selected AutoDebug probe API."""
from pathlib import Path
import json, re, struct, sys, time
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, r'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/stm32-autodebug')
from autodebug.config import AutoDebugConfig
from autodebug.hardware_probe import HardwareProbe
cfg = AutoDebugConfig.load()
cfg.debugger.target_override = 'stm32f103c8'
cfg.debugger.probe_id = '37FF71064E573436B5C31E43'
cfg.debugger.pack_files = [r'E:/keil5/ARM/PACK/Keil/STM32F1xx_DFP/2.4.0']
cfg.debugger.frequency_hz = 1000000
cfg.debugger.connect_mode = 'under-reset'
text = (root/'Project/Listings/eXin.map').read_text(errors='replace')
def address(name):
    match = re.search(r'^\s*'+name+r'\s+0x([0-9a-fA-F]+)\s+Data\s+\d+\s+imu.o', text, re.M)
    if not match: raise RuntimeError('Missing map symbol '+name)
    return int(match.group(1), 16)
probe = HardwareProbe(cfg.debugger)
records = []
try:
    if not probe.open(): raise RuntimeError('Cannot connect')
    watch = '--watch' in sys.argv
    probe.resume()
    watch_path = root/'build/autodebug_serial/imu_watch.jsonl'
    if watch: watch_path.write_text('',encoding='utf-8')
    for index in range(60 if watch else 3):
        if not watch: probe.read_fault_registers(halt_if_running=True)
        def read(name, size):
            ptr = address(name)
            return b''.join(struct.pack('<I', probe.read_memory(ptr+i, 32)) for i in range(0, size, 4))[:size]
        latest = read('latest', 48); core = read('core', 76)
        floats = struct.unpack('<10f', latest[:40])
        rec = dict(zip(['gx','gy','gz','ax','ay','az','roll','pitch','yaw','temperature'], floats))
        rec.update(valid=bool(latest[44]), calibrated=bool(latest[45]),
                   calibration_count=struct.unpack_from('<H',core,64)[0],
                   reason=read('fault_reason',24).split(b'\0')[0].decode(errors='replace'))
        rec['pwm_us'] = probe.read_memory(0x40012c34,32)
        records.append(rec)
        if watch:
            with watch_path.open('a',encoding='utf-8') as stream: stream.write(json.dumps(rec)+'\n')
            if index % 10 == 0: print('Watching IMU, sample '+str(index)+', PWM='+str(rec['pwm_us']),flush=True)
        else: print(json.dumps(rec, ensure_ascii=False), flush=True)
        if not watch: probe.resume()
        time.sleep(1 if watch else 0.5)
finally:
    probe.resume(); probe.close()
    (root/'build/autodebug_serial/imu_snapshot.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
