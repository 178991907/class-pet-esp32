import fs from 'fs';
const sampleRate = 8000;
const duration = 1; // 1 second
const numSamples = sampleRate * duration;
const buffer = Buffer.alloc(44 + numSamples);
// Write WAV header
buffer.write('RIFF', 0);
buffer.writeUInt32LE(36 + numSamples, 4);
buffer.write('WAVE', 8);
buffer.write('fmt ', 12);
buffer.writeUInt32LE(16, 16);
buffer.writeUInt16LE(1, 20); // PCM
buffer.writeUInt16LE(1, 22); // Mono
buffer.writeUInt32LE(sampleRate, 24);
buffer.writeUInt32LE(sampleRate, 28);
buffer.writeUInt16LE(1, 32);
buffer.writeUInt16LE(8, 34); // 8 bit
buffer.write('data', 36);
buffer.writeUInt32LE(numSamples, 40);
// Write Sine wave
for (let i = 0; i < numSamples; i++) {
  const t = i / sampleRate;
  const val = Math.sin(2 * Math.PI * 1000 * t); // 1000 Hz
  buffer.writeUInt8(Math.floor((val + 1) * 127.5), 44 + i);
}
fs.writeFileSync('server/public/audio/ringtone.wav', buffer);
