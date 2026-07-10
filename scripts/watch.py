#!/usr/bin/env python3
"""Watch serial and print timestamped lines. 90s default."""
import serial, time, sys
ser = serial.Serial('/dev/cu.usbmodem141201', 115200, timeout=0.3)
ser.reset_input_buffer()
start = time.time()
dur = int(sys.argv[1]) if len(sys.argv) > 1 else 90
while time.time() - start < dur:
    try:
        data = ser.readline()
        if data:
            line = data.decode('utf-8', errors='replace').rstrip()
            if line:
                print(f'[{time.time()-start:6.1f}s] {line}', flush=True)
    except:
        pass
