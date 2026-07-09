import requests

url = "http://localhost:3002/api/device/voice"
headers = {
    "X-Device-ID": "28:84:85:42:1C:74",
    "X-Device-Timestamp": "1672531200",
    "X-Device-Signature": "dummy" # if secret is off
}
files = {
    "audio": ("record.wav", b"RIFF\x24\x00\x00\x00WAVEfmt ", "audio/wav")
}

response = requests.post(url, headers=headers, files=files)
print(response.status_code)
print(response.text)
