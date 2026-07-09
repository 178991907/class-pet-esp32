const express = require('express');
const multer = require('multer');
const http = require('http');
const app = express();
const upload = multer({ storage: multer.memoryStorage() });

app.use(express.json());

app.post('/voice', express.raw({ type: 'audio/wav', limit: '10mb' }), upload.single('audio'), (req, res) => {
  console.log('req.body isBuffer:', Buffer.isBuffer(req.body));
  console.log('req.body type:', typeof req.body);
  console.log('req.body:', req.body);
  res.json({ ok: true });
});

const server = app.listen(0, () => {
  const port = server.address().port;
  const req = http.request({
    hostname: 'localhost',
    port: port,
    path: '/voice',
    method: 'POST',
    headers: { 'Content-Type': 'audio/wav' }
  }, (res) => {
    server.close();
  });
  req.write(Buffer.from('fake data'));
  req.end();
});
