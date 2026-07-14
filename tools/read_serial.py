#!/usr/bin/env python3
# 读取 ESP32-S3 CDC 串口启动日志 (用于验证 SD 卡挂载与字库加载)
import serial, sys, time

def main():
    port = "/dev/cu.usbmodem141201"
    baud = 115200
    try:
        ser = serial.Serial(port, baud, timeout=1.0)
    except Exception as e:
        print("无法打开串口:", e)
        sys.exit(1)
    ser.reset_input_buffer()
    start = time.time()
    print("=== 开始读取串口日志 (25s) ===", flush=True)
    while time.time() - start < 25:
        try:
            data = ser.read(1024)
        except Exception as e:
            print("读取错误:", e, flush=True)
            break
        if data:
            try:
                text = data.decode('utf-8', errors='replace')
            except Exception:
                text = repr(data)
            sys.stdout.write(text)
            sys.stdout.flush()
    ser.close()
    print("\n=== 读取结束 ===", flush=True)

if __name__ == "__main__":
    main()
