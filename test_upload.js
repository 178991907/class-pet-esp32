const http = require('http');

const options = {
  hostname: 'localhost',
  port: 3002,
  path: '/api/device/voice',
  method: 'POST',
  headers: {
    'Content-Type': 'audio/wav',
    'X-Device-ID': 'test_mac'
  }
};

const req = http.request(options, (res) => {
  console.log(`STATUS: ${res.statusCode}`);
  res.setEncoding('utf8');
  res.on('data', (chunk) => {
    console.log(`BODY: ${chunk}`);
  });
});

req.on('error', (e) => {
  console.error(`problem with request: ${e.message}`);
});

// Write a fake WAV header and some data
const fakeWav = Buffer.alloc(100);
req.write(fakeWav);
req.end();
