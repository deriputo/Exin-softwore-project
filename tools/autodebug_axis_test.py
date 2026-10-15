"""Identify the mechanical rotation axis using actual gravity samples at90/100/80."""
from pathlib import Path
import json, re, struct, sys, time, math
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,r'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/stm32-autodebug')
from autodebug.config import AutoDebugConfig
from autodebug.hardware_probe import HardwareProbe
from autodebug.serial_monitor import SerialMonitor
cfg=AutoDebugConfig.load(); cfg.debugger.target_override='stm32f103c8'
cfg.debugger.probe_id='37FF71064E573436B5C31E43'
cfg.debugger.pack_files=[r'E:/keil5/ARM/PACK/Keil/STM32F1xx_DFP/2.4.0']
cfg.debugger.frequency_hz=1000000
probe=HardwareProbe(cfg.debugger); monitor=SerialMonitor(cfg.serial,port='COM6',baudrate=115200)
listing=(root/'Project/Listings/eXin.map').read_text(errors='replace')
ptr=int(re.search(r'^\s*latest\s+0x([\da-fA-F]+)\s+Data\s+\d+\s+imu.o',listing,re.M).group(1),16)
records=[]; cursor=0
def command(text):
    global cursor
    monitor._ser.write((text+'\r\n').encode()); monitor._ser.flush()
    time.sleep(1); lines=monitor._drain_new(cursor); cursor+=len(lines)
    print(text+' -> '+' | '.join(lines),flush=True)
    if not any('OK' in x for x in lines): raise RuntimeError('Command did not acknowledge')
try:
    if not monitor.open(): raise RuntimeError(monitor.open_error)
    if not probe.open(): raise RuntimeError('Cannot open probe')
    probe.resume()
    command('SERVO HOLD OFF')
    for angle in [90,100,80]:
        command('SERVO SET '+str(angle)); time.sleep(1)
        values=[]
        for _ in range(3):
            raw=b''.join(struct.pack('<I',probe.read_memory(ptr+offset,32)) for offset in range(0,24,4))
            values.append(struct.unpack('<6f',raw)); time.sleep(0.1)
        avg=[sum(v[i] for v in values)/len(values) for i in range(6)]
        regs = {'CR1':0x40012c00,'CCER':0x40012c20,'PSC':0x40012c28,
                'ARR':0x40012c2c,'CCR1':0x40012c34,'BDTR':0x40012c44,
                'GPIOA_CRH':0x40010804,'RCC_APB2ENR':0x40021018}
        timer={name:probe.read_memory(addr,32) for name,addr in regs.items()}
        timer['CNT1']=probe.read_memory(0x40012c24,32); time.sleep(0.007)
        timer['CNT2']=probe.read_memory(0x40012c24,32)
        record={'servo':angle,'gyro':avg[:3],'gravity':avg[3:],'timer':timer}; records.append(record)
        print(json.dumps(record),flush=True)
    g0,g1,g2=[r['gravity'] for r in records]
    a=[g1[1]*g2[2]-g1[2]*g2[1],g1[2]*g2[0]-g1[0]*g2[2],g1[0]*g2[1]-g1[1]*g2[0]]
    norm=math.sqrt(sum(x*x for x in a)); axis=[x/norm for x in a] if norm>0.01 else None
    print('Estimated sensor axis: '+str(axis),flush=True)
finally:
    if monitor._ser is not None:
        try: command('SERVO HOLD OFF'); command('SERVO SET 90')
        except Exception: pass
    monitor.close(); probe.resume(); probe.close()
    (root/'build/autodebug_serial/axis_test.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
