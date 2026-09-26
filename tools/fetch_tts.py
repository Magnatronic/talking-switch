"""Downloads the text-to-speech files that the setup page serves itself.

Run from the project folder:   python tools/fetch_tts.py

Everything is pinned to exact versions and saved under docs/tts/, so the
GitHub Pages site needs nothing from other websites. Re-running skips files
that are already there.
"""
import hashlib
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "tts"

ORT = "https://cdn.jsdelivr.net/npm/onnxruntime-web@1.18.0/dist/"
PIPER = "https://cdn.jsdelivr.net/npm/@diffusionstudio/piper-wasm@1.0.0/build/"
VITS = "https://cdn.jsdelivr.net/npm/@diffusionstudio/vits-web@1.0.3/dist/"
VOICES = "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/"

FILES = {
    # ONNX Runtime Web (MIT) - runs the voice model
    "ort.wasm.min.js": ORT + "ort.wasm.min.js",
    "ort-wasm-simd.wasm": ORT + "ort-wasm-simd.wasm",
    # piper-phonemize + eSpeak NG data - turns text into speech sounds
    "piper_phonemize.mjs": VITS + "piper-DeOu3H9E.js",
    "piper_phonemize.wasm": PIPER + "piper_phonemize.wasm",
    "piper_phonemize.data": PIPER + "piper_phonemize.data",
}
for name, path in [
    ("en_GB-cori-medium", "cori/medium/"),
    ("en_GB-alba-medium", "alba/medium/"),
    ("en_GB-southern_english_female-low", "southern_english_female/low/"),
    ("en_GB-northern_english_male-medium", "northern_english_male/medium/"),
]:
    FILES[f"voices/{name}.onnx"] = VOICES + path + name + ".onnx"
    FILES[f"voices/{name}.onnx.json"] = VOICES + path + name + ".onnx.json"
    FILES[f"voices/{name}.MODEL_CARD.txt"] = VOICES + path + "MODEL_CARD"


# Kokoro (Apache-2.0) - the default, more natural engine. kokoro.js redirects
# the HuggingFace addresses kokoro-js asks for to these copies.
KOKORO_JS = "https://cdn.jsdelivr.net/npm/kokoro-js@1.2.1/"
TRANSFORMERS = "https://cdn.jsdelivr.net/npm/@huggingface/transformers@3.5.1/dist/"
KOKORO_MODEL = ("https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/"
                "1939ad2a8e416c0acfeecc08a694d14ef25f2231/")
FILES.update({
    "kokoro/kokoro.web.js": KOKORO_JS + "dist/kokoro.web.js",
    "kokoro/ort-wasm-simd-threaded.jsep.mjs": TRANSFORMERS + "ort-wasm-simd-threaded.jsep.mjs",
    "kokoro/ort-wasm-simd-threaded.jsep.wasm": TRANSFORMERS + "ort-wasm-simd-threaded.jsep.wasm",
    "kokoro/model/config.json": KOKORO_MODEL + "config.json",
    "kokoro/model/tokenizer.json": KOKORO_MODEL + "tokenizer.json",
    "kokoro/model/tokenizer_config.json": KOKORO_MODEL + "tokenizer_config.json",
    "kokoro/model/onnx/model_quantized.onnx": KOKORO_MODEL + "onnx/model_quantized.onnx",
})
for v in ["bf_emma", "bf_isabella", "bm_george", "bm_fable"]:
    FILES[f"kokoro/model/voices/{v}.bin"] = KOKORO_JS + f"voices/{v}.bin"


def main():
    for rel, url in FILES.items():
        dest = ROOT / rel
        if dest.exists() and dest.stat().st_size > 0:
            print(f"have  {rel}")
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        print(f"get   {rel} ...", flush=True)
        tmp = dest.with_suffix(dest.suffix + ".part")
        with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
            while chunk := r.read(1 << 20):
                f.write(chunk)
        tmp.replace(dest)
    print()
    for rel in FILES:
        p = ROOT / rel
        h = hashlib.sha256(p.read_bytes()).hexdigest()[:16]
        print(f"{p.stat().st_size / 1e6:8.1f} MB  {h}  {rel}")


if __name__ == "__main__":
    main()
