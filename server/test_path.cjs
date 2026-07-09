const express = require('express');
const app = express();
const router = express.Router();
router.post('/voice', (req, res, next) => {
  console.log('req.path:', req.path);
  console.log('req.originalUrl:', req.originalUrl);
  res.json({ok:true});
});
app.use('/api/device', router);
const server = app.listen(0, () => {
  const http = require('http');
  const req = http.request({
    hostname: 'localhost',
    port: server.address().port,
    path: '/api/device/voice',
    method: 'POST'
  }, (res) => { server.close(); });
  req.end();
});
