#!/usr/bin/env python3
"""Reset ESP32 via DTR/RTS and capture boot serial logs."""
import serial
import time
import sys

PORT = '/dev/cu.usbmodem141201'
BAUD = 115200
DURATION = 18  # seconds

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.5)
except Exception as e:
    print(f"Error opening serial port: {e}")
    sys.exit(1)

# ESP32 hardware reset via DTR/RTS
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setDTR(True)
ser.setRTS(False)
time.sleep(0.05)
ser.setDTR(False)

print(f"=== Reset sent, capturing {DURATION}s of boot log ===")
start = time.time()
line_count = 0
while time.time() - start < DURATION:
    try:
        data = ser.readline()
        if data:
            line = data.decode('utf-8', errors='replace').rstrip()
            if line:
                print(line, flush=True)
                line_count += 1
    except Exception:
        pass

ser.close()
print(f"\n=== Captured {line_count} lines in {DURATION}s ===")
