// Text-to-speech with Piper voices, run entirely in the browser.
// Every file comes from this site (see tools/fetch_tts.py); the text never
// leaves the computer. Downloads are kept in the browser's Cache Storage,
// so after the first use it works offline.

const BASE = new URL('./', import.meta.url);
const CACHE = 'talking-switch-tts-v1';

export const VOICES = [
  { id: 'en_GB-cori-medium', name: 'Cori – English, female' },
  { id: 'en_GB-alba-medium', name: 'Alba – Scottish, female' },
  { id: 'en_GB-southern_english_female-low', name: 'Southern English, female' },
  { id: 'en_GB-northern_english_male-medium', name: 'Northern English, male' },
];

// Fetches a file from this site, from the cache if we've had it before.
// onProgress(loadedBytes, totalBytes) is called while downloading.
async function getFile(path, onProgress) {
  const url = new URL(path, BASE).href;
  let cache = null;
  try {
    cache = await caches.open(CACHE);
    const hit = await cache.match(url);
    if (hit) return hit.arrayBuffer();
  } catch { /* no Cache Storage (e.g. private window): just download */ }

  const res = await fetch(url);
  if (!res.ok) throw new Error(`Couldn't download ${path.split('/').pop()} (${res.status})`);
  const total = +res.headers.get('Content-Length') || 0;
  const reader = res.body.getReader();
  const parts = [];
  let loaded = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value);
    loaded += value.length;
    if (onProgress) onProgress(loaded, total);
  }
  const blob = new Blob(parts);
  if (cache) { try { await cache.put(url, new Response(blob)); } catch { /* storage full: fine */ } }
  return blob.arrayBuffer();
}

// Whether a voice has already been downloaded.
export async function isCached(voiceId) {
  try {
    const cache = await caches.open(CACHE);
    return !!(await cache.match(new URL(`voices/${voiceId}.onnx`, BASE).href));
  } catch { return false; }
}

// ---------------------------------------------------------------- engine
let ortPromise = null;
function loadOrt() {
  ortPromise ||= new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = new URL('ort.wasm.min.js', BASE).href;
    s.onload = () => {
      const ort = window.ort;
      ort.env.wasm.wasmPaths = BASE.href;
      ort.env.wasm.numThreads = 1;  // GitHub Pages can't enable multi-threading
      resolve(ort);
    };
    s.onerror = () => { ortPromise = null; reject(new Error("Couldn't load the speech engine")); };
    document.head.append(s);
  });
  return ortPromise;
}

let phonemizer = null;  // { create, wasmBinary, data }
async function loadPhonemizer(onProgress) {
  if (phonemizer) return phonemizer;
  const mod = await import(new URL('piper_phonemize.mjs', BASE).href);
  const [wasmBinary, data] = await Promise.all([
    getFile('piper_phonemize.wasm'),
    getFile('piper_phonemize.data', (l, t) => onProgress && onProgress('speech sounds', l, t)),
  ]);
  phonemizer = { create: mod.createPiperPhonemize, wasmBinary, data };
  return phonemizer;
}

// Text -> Piper phoneme ids, using eSpeak NG inside piper-phonemize.
async function phonemize(text, espeakVoice, onProgress) {
  const p = await loadPhonemizer(onProgress);
  return new Promise((resolve, reject) => {
    p.create({
      wasmBinary: p.wasmBinary,
      getPreloadedPackage: () => p.data,
      locateFile: (f) => new URL(f.endsWith('.data') ? 'piper_phonemize.data' : 'piper_phonemize.wasm', BASE).href,
      print: (line) => {
        try { resolve(JSON.parse(line).phoneme_ids); } catch (e) { reject(e); }
      },
      printErr: (line) => console.warn('phonemize:', line),
    }).then((m) => m.callMain(['-l', espeakVoice, '--input', JSON.stringify([{ text }]), '--espeak_data', '/espeak-ng-data']))
      .catch(reject);
  });
}

const sessions = new Map();  // voiceId -> { session, config }
async function loadVoice(voiceId, onProgress) {
  if (sessions.has(voiceId)) return sessions.get(voiceId);
  const ort = await loadOrt();
  const [config, model] = await Promise.all([
    getFile(`voices/${voiceId}.onnx.json`).then((b) => JSON.parse(new TextDecoder().decode(b))),
    getFile(`voices/${voiceId}.onnx`, (l, t) => onProgress && onProgress('voice', l, t)),
  ]);
  const v = { session: await ort.InferenceSession.create(model), config };
  sessions.set(voiceId, v);
  return v;
}

// Speaks `text` with a voice. speed: 1 = normal, 0.8 = slower, 1.2 = faster.
// onProgress(what, loadedBytes, totalBytes) reports first-time downloads.
// Returns { samples: Float32Array, sampleRate }.
export async function synthesize(text, voiceId, speed = 1, onProgress) {
  text = text.trim();
  if (!text) throw new Error('Type something to say first');
  const [{ session, config }, ort] = await Promise.all([loadVoice(voiceId, onProgress), loadOrt()]);
  const ids = await phonemize(text, config.espeak.voice, onProgress);
  const inf = config.inference;
  const feeds = {
    input: new ort.Tensor('int64', BigInt64Array.from(ids.map(BigInt)), [1, ids.length]),
    input_lengths: new ort.Tensor('int64', BigInt64Array.from([BigInt(ids.length)]), [1]),
    scales: new ort.Tensor('float32', Float32Array.from([inf.noise_scale, inf.length_scale / speed, inf.noise_w]), [3]),
  };
  if (Object.keys(config.speaker_id_map || {}).length) feeds.sid = new ort.Tensor('int64', BigInt64Array.from([0n]), [1]);
  const { output } = await session.run(feeds);
  return { samples: output.data, sampleRate: config.audio.sample_rate };
}
