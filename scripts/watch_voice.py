#!/usr/bin/env python3
"""Wait for voice trigger and capture detailed voice pipeline logs."""
import serial
import time
import sys
import threading

PORT = '/dev/cu.usbmodem141201'
BAUD = 115200
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 60

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)

ser.reset_input_buffer()

print(f"=== Watching for voice interaction (up to {DURATION}s) ===")
print("    >>> Press the voice button NOW to trigger recording <<<")
print()
start = time.time()
captured = []
while time.time() - start < DURATION:
    try:
        data = ser.readline()
        if data:
            line = data.decode('utf-8', errors='replace').rstrip()
            if line:
                print(f"[{time.time()-start:6.1f}s] {line}", flush=True)
                captured.append(line)
    except Exception:
        pass

ser.close()
print()
print(f"=== Total {len(captured)} lines ===")
