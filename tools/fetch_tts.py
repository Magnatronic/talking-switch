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
