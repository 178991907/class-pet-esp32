const express = require('express');
const app = express();
const router = express.Router();
app.use(express.json());
router.post('/voice', (req, res, next) => {
  console.log('req.body:', req.body);
  res.json({ok:true});
});
app.use('/api/device', router);
const server = app.listen(0, () => {
  const http = require('http');
  const req = http.request({
    hostname: 'localhost',
    port: server.address().port,
    path: '/api/device/voice',
    method: 'POST',
    headers: { 'Content-Type': 'audio/wav' }
  }, (res) => { server.close(); });
  req.end();
});
