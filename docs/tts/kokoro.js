// Text-to-speech with Kokoro (more natural than Piper, a little slower),
// run entirely in the browser. kokoro-js asks HuggingFace for its model and
// voices; this module redirects those requests to the copies on this site
// (see tools/fetch_tts.py). Everything is cached in the browser (the model by
// Transformers.js, the rest here), so after the first use it works offline.
// The text never leaves the computer.

const BASE = new URL('./kokoro/', import.meta.url);
const HF = 'https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/main/';
const CACHE = 'talking-switch-kokoro-v1';

export const VOICES = [
  { id: 'bf_emma', name: 'Emma – English, female' },
  { id: 'bf_isabella', name: 'Isabella – English, female' },
  { id: 'bm_george', name: 'George – English, male' },
  { id: 'bm_fable', name: 'Fable – English, male' },
];

// Serve this site's copies instead of HuggingFace, cache-first.
let progressCb = null;
const realFetch = window.fetch.bind(window);
async function kokoroFetch(input, init) {
  let url = typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;
  if (url.startsWith(HF)) url = new URL('model/' + url.slice(HF.length), BASE).href;
  if (!url.startsWith(BASE.href)) return realFetch(input, init);

  // Transformers.js caches the model files itself; we cache the rest.
  const mine = url.includes('/voices/') || url.endsWith('.wasm');
  let cache = null;
  try {
    if (mine) cache = await caches.open(CACHE);
    if (cache) { const hit = await cache.match(url); if (hit) return hit; }
  } catch { /* no Cache Storage: just download */ }
  const res = await realFetch(url, init);
  if (!res.ok) throw new Error(`Couldn't download ${url.split('/').pop()} (${res.status})`);
  // report progress on the big files while caching them
  const total = +res.headers.get('Content-Length') || 0;
  const reader = res.body.getReader();
  const parts = [];
  let loaded = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value);
    loaded += value.length;
    if (progressCb && total > 5e6) progressCb('natural voice engine', loaded, total);
  }
  const type = res.headers.get('Content-Type') || 'application/octet-stream';
  const copy = new Response(new Blob(parts, { type }), { status: 200, headers: { 'Content-Type': type } });
  if (cache) { try { await cache.put(url, copy.clone()); } catch { /* storage full: fine */ } }
  return copy;
}

let ttsPromise = null;
async function load() {
  ttsPromise ||= (async () => {
    window.fetch = kokoroFetch;  // before the library starts fetching
    const { KokoroTTS, env } = await import(new URL('kokoro.web.js', BASE).href);
    env.wasmPaths = BASE.href;  // the ONNX runtime files on this site
    return KokoroTTS.from_pretrained('onnx-community/Kokoro-82M-v1.0-ONNX', { dtype: 'q8', device: 'wasm' });
  })();
  try { return await ttsPromise; } catch (e) { ttsPromise = null; throw e; }
}

// Speaks `text` with a voice. speed: 1 = normal. onProgress(what, loaded,
// total) reports the first-time download. Returns { samples, sampleRate }.
export async function synthesize(text, voiceId, speed = 1, onProgress) {
  text = text.trim();
  if (!text) throw new Error('Type something to say first');
  progressCb = onProgress;
  try {
    const tts = await load();
    const audio = await tts.generate(text, { voice: voiceId, speed });
    return { samples: audio.audio, sampleRate: audio.sampling_rate };
  } finally {
    progressCb = null;
  }
}
