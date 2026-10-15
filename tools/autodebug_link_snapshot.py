"""Read UART/protocol diagnostics; no flash, reset or UART transmission."""
from pathlib import Path
import json, re, struct, sys, time
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, r'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/stm32-autodebug')
from autodebug.config import AutoDebugConfig
from autodebug.hardware_probe import HardwareProbe
from intelhex import IntelHex
cfg = AutoDebugConfig.load()
cfg.debugger.target_override = 'stm32f103c8'
cfg.debugger.probe_id = '37FF71064E573436B5C31E43'
cfg.debugger.pack_files = [r'E:/keil5/ARM/PACK/Keil/STM32F1xx_DFP/2.4.0']
cfg.debugger.frequency_hz = 1000000
cfg.debugger.connect_mode = 'attach'
listing = (root/'Project/Listings/eXin.map').read_text(errors='replace')
def symbol(name, obj):
    m = re.search(r'^\s*'+name+r'\s+0x([0-9a-fA-F]+)\s+Data\s+(\d+)\s+'+re.escape(obj), listing, re.M)
    if not m: raise RuntimeError('Missing symbol '+name)
    return int(m[1],16), int(m[2])
probe = HardwareProbe(cfg.debugger)
records = []
def read32(addr):
    probe.read_memory(addr,32)
    return probe.read_memory(addr,32)
def block(addr,size):
    probe.target.read_memory_block8(addr,size)
    return bytes(probe.target.read_memory_block8(addr,size))
try:
    if not probe.open(): raise RuntimeError('Cannot attach')
    if probe.target.is_halted(): probe.resume()
    expected = IntelHex(str(root/'build/keil/eXin.hex'))
    for start,end in expected.segments():
        for addr in range(start,end-3,1024):
            wanted = sum(expected[addr+i] << (8*i) for i in range(4))
            if read32(addr) != wanted: raise RuntimeError('HEX mismatch; stop RAM interpretation')
    for _ in range(3):
        rec = {'now_ms':read32(symbol('now','app.o')[0])}
        p,n = symbol('control','app.o')
        data = block(p,n)
        rec['control_bytes'] = data.hex()
        # Current AC5 build uses one-byte enums; only decode this known layout.
        if n == 136:
            rec['control_config'] = dict(zip(
                ['kp','ki','kd','deadband','target_max','target_min','integral_limit','slew','span'],
                struct.unpack_from('<9f',data,0)))
            rec['control_config'].update(axis=data[48],direction=struct.unpack_from('<b',data,49)[0],mode=data[50])
            rec['control'] = dict(zip(
                ['target','measured','error','p','i','d','output'],
                struct.unpack_from('<7f',data,60)))
            rec['control'].update(pulse=struct.unpack_from('<H',data,88)[0],
                                  hold=data[90],saturated=data[91],fault=data[92])
            rec['control']['bias'] = struct.unpack_from('<f',data,108)[0]
        rec['pwm_cached_pulse'] = struct.unpack('<H',block(*symbol('servo_pulse_us','pwm.o')))[0]
        rec['uart_stats'] = dict(zip(['received','dropped','errors','tx_busy'],struct.unpack('<4I',block(*symbol('stats','serial.o')))))
        for ring in ('rx_ring','tx_ring'):
            p,n = symbol(ring,'serial.o')
            rec[ring] = dict(zip(['head','tail'],struct.unpack('<HH',block(p+n-4,4))))
        p,n = symbol('protocol','app.o')
        data = block(p,n)
        rec['protocol_tail_hex'] = data[-32:].hex()
        rec['protocol_flags'] = list(data[564:574])
        rec['partial_line'] = data[64:160].split(b'\0')[0].decode(errors='replace')
        p,n = symbol('latest','imu.o')
        data = block(p,n)
        values = struct.unpack('<10f',data[:40])
        rec['imu'] = dict(zip(['gx','gy','gz','ax','ay','az','roll','pitch','yaw','temperature'],values))
        rec['imu'].update(ms=struct.unpack_from('<I',data,40)[0],valid=bool(data[44]),calibrated=bool(data[45]))
        rec['usart_cr1'] = read32(0x4001380c)
        rec['usart_brr'] = read32(0x40013808)
        registers = {'TIM1_CR1':0x40012c00, 'TIM1_CCMR1':0x40012c18,
                     'TIM1_CCER':0x40012c20, 'TIM1_PSC':0x40012c28,
                     'TIM1_ARR':0x40012c2c, 'TIM1_CCR1':0x40012c34,
                     'TIM1_BDTR':0x40012c44, 'GPIOA_CRH':0x40010804,
                     'AFIO_MAPR':0x40010004, 'RCC_CFGR':0x40021004,
                     'RCC_APB2ENR':0x40021018, 'DBGMCU_CR':0xe0042004}
        rec['pwm'] = {name:read32(addr) for name,addr in registers.items()}
        rec['pwm']['CNT1'] = read32(0x40012c24)
        time.sleep(.007)
        rec['pwm']['CNT2'] = read32(0x40012c24)
        records.append(rec)
        print(json.dumps(rec),flush=True)
        time.sleep(.5)
finally:
    probe.resume(); probe.close()
    (root/'build/autodebug_serial/link_snapshot.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
