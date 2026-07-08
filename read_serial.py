import serial
import sys
import time

try:
    ser = serial.Serial('/dev/cu.usbmodem141201', 115200, timeout=1)
    print("Connected to serial port")
    # Reset ESP32
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setDTR(True)
    ser.setRTS(False)
    time.sleep(0.1)
    
    end_time = time.time() + 45
    while time.time() < end_time:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', errors='replace').strip())
            sys.stdout.flush()
    ser.close()
except Exception as e:
    print("Error:", e)
