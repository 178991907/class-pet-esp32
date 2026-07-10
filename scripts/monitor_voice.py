#!/usr/bin/env python3
"""Monitor ESP32 serial output - no reset, just passive capture for voice interaction."""
import serial
import time
import sys

PORT = '/dev/cu.usbmodem141201'
BAUD = 115200
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 25

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)

# Clear buffers
ser.reset_input_buffer()
ser.reset_output_buffer()

print(f"=== Passive capture for {DURATION}s ===")
start = time.time()
audio_lines = []
while time.time() - start < DURATION:
    try:
        data = ser.readline()
        if data:
            line = data.decode('utf-8', errors='replace').rstrip()
            if line:
                print(line, flush=True)
                # Collect audio-related lines for analysis
                if any(k in line for k in ['🎙', '🔊', '录音', '音量', '电平', 'postVoice', 'voice', 'voice', 'ES8311', 'DAC', 'ADC', 'I2S', 'audio', 'Audio', 'WAV', 'upload', '上传', 'ASR', '识别', '听清', 'audio_url']):
                    audio_lines.append(line)
    except Exception:
        pass

ser.close()
print(f"\n=== {len(audio_lines)} audio-related lines captured ===")
for l in audio_lines:
    print(f"  • {l}")
