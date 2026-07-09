import FormData from 'form-data';
try {
  const form = new FormData();
  form.append('file', Buffer.alloc(0), { filename: 'record.wav', contentType: 'audio/wav' });
  console.log("FormData success");
} catch(e) {
  console.log("FormData error:", e);
}
