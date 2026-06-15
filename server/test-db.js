import { initDb, allAsync, runAsync } from './db.js';
async function test() {
  try {
    await initDb();
    console.log("Database initialized");
    const sources = await allAsync('SELECT * FROM music_sources ORDER BY priority DESC, created_at DESC');
    console.log("Sources:", sources);
    
    await runAsync(
      'INSERT INTO music_sources (id, name, script_code, priority, is_enabled, failure_count, created_at) VALUES (?, ?, ?, ?, 1, 0, ?)',
      'test-id', 'test', 'console.log("test")', 0, Date.now()
    );
    console.log("Insert success");
  } catch (error) {
    console.error("Test Error:", error);
  }
}
test();
