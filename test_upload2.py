import requests

url = "http://localhost:3002/api/device/voice"
headers = {
    "X-Device-ID": "28:84:85:42:1C:74",
    "X-Device-Timestamp": "1672531200",
    "X-Device-Signature": "dummy", # if secret is off
    "Content-Type": "multipart/form-data; boundary=----ClassPetAudioBoundary"
}

body = b"------ClassPetAudioBoundary\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"record.wav\"\r\nContent-Type: audio/wav\r\n\r\n"
body += b"RIFF\x24\x00\x00\x00WAVEfmt "
body += b"\r\n------ClassPetAudioBoundary--\r\n"

response = requests.post(url, headers=headers, data=body)
print("status:", response.status_code)
print("text:", response.text)
