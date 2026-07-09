import express from 'express';
import multer from 'multer';

const app = express();
const upload = multer({ storage: multer.memoryStorage() });

app.post('/test', upload.single('audio'), (req, res) => {
  if (!req.file) {
    return res.status(400).json({ error: 'No file' });
  }
  res.json({ success: true, size: req.file.size });
});

app.listen(3003, () => console.log('Listening on 3003'));
