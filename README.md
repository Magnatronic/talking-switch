# TalkingSwitch – M5StickS3 + Unit Key

Firmware for a big-button accessibility switch: talking switch, step-by-step,
Bluetooth/USB keyboard key, and IR remote, all in one.

## What you need

- M5StickS3
- M5Stack Unit Key (U144) on the StickS3's Grove port
- The printed base and cap (`unit-key-big-cap.scad`)
- Arduino IDE 2.x

## Arduino IDE setup

1. **Board package:** Boards Manager → install **esp32 by Espressif Systems**, version 3.3.0 or newer.
2. **Libraries:** Library Manager → install **M5Unified** (it pulls in M5GFX).
3. **Board:** *ESP32S3 Dev Module*, then set Tools to:

| Setting | Value |
|---|---|
| USB CDC On Boot | Enabled |
| Flash Size | 8MB (64Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | OPI PSRAM |
| USB Mode | **USB-OTG (TinyUSB)** (wired USB keyboard + Bluetooth). *Hardware CDC and JTAG* also works, but then the keyboard is Bluetooth only |

Open `TalkingSwitch/TalkingSwitch.ino` (Arduino needs the sketch folder and
`.ino` to share a name). The `partitions.csv` in that folder replaces the
partition layout automatically, giving ~4.8MB for recordings. Keep it next to the `.ino`.

If the screen says **"No PSRAM!"**, change PSRAM to *QSPI PSRAM* and upload again.

In TinyUSB mode the StickS3 still shows up as a COM port and uploads normally reset it
automatically. If an upload fails, put the StickS3 into download mode
(see M5Stack's StickS3 docs for the button sequence) and upload again.

## Setup page (USB)

The setup page is `docs/index.html`, published with **GitHub Pages** so staff just open a link in **Chrome or
Edge** (Firefox and Safari can't talk to USB devices). Plug the StickS3 in, close the Arduino Serial Monitor,
click **Connect** and pick the switch. The switch is set up over the USB cable – no Wi-Fi – and recordings and
typed words stay on the computer.

- **Messages tab:** 4 messages. For each: a name, ▶ Play, **Change** (record with the computer's microphone or a
  headset, **type the words** (spoken in the switch's voice – see Settings), or
  upload an audio file; then listen and save) and Delete. Silence is trimmed and the level evened out.
- **IR remote tab:** 4 remote-control codes, separate from the messages. For each: a name, **Learn / Test / Delete**,
  and an optional **sound** (made the same three ways) that the switch says as it sends the code.
- **Keyboard tab:** the key, including a **custom key or shortcut** such as Win+H (dictation).
- **Settings tab:** the switch's **voice** and speed for typed messages and scanning prompts (natural: Emma, Isabella,
  George, Fable; quick: Cori, Alba, Southern English female, Northern English male – stored on the switch, so every
  computer uses the same one; changing it remakes the prompts), everything from the SETTINGS menu with explanations,
  and the Bluetooth name and *Forget all paired devices*.
- **Mode and volume** are at the top of the page.

**Typed speech** runs in the browser with two engines. **Kokoro** (the default) sounds much more natural and
takes a few seconds per phrase; **Piper** is almost instant but flatter. Both, and their voices, are served by the
same GitHub Pages site (`docs/tts/`, about 390MB, fetched by `python tools/fetch_tts.py`), so IT only needs to allow
the one `github.io` address. The first use downloads about 115MB for Kokoro (all its voices) or ~80MB per Piper
voice; after that it's cached and works offline. Opening `docs/index.html` straight from disk still does everything
except typed speech.

### Publishing on GitHub Pages

1. Put this folder in a GitHub repository with `git push` (the voice files are ~64MB each: GitHub warns above
   50MB but accepts up to 100MB; they're too big for the web uploader).
2. On GitHub: **Settings → Pages → Build and deployment → Deploy from a branch**, branch `main`, folder **/docs**.
3. After a minute the page is at `https://<account>.github.io/<repository>/`.

To test locally: `python -m http.server 8765 --directory docs`, then open http://localhost:8765.

## Using it

The switch holds **4 messages** (for SPEAK) and, separately, **4 IR codes** (for IR). Each IR code can have its own
sound, e.g. IR code 1 "Bubbles" turns the bubble tube on *and* says "Bubbles!". An IR code from older firmware
becomes IR code 1.

**Student:** press the big switch. What happens depends on the mode.

| Mode | Big switch does | LED glow |
|---|---|---|
| SPEAK | Plays the selected message. Keep holding to move on to the next message (every 2s while held). See *Play style* for hold-to-play and latch | message colour: 1 green, 2 blue, 3 purple, 4 orange |
| KEYBOARD | Holds down the chosen key while pressed | purple |
| IR | Sends the selected IR code, and says its sound if it has one. Keep holding to move to the next code. With *Hold to play* it keeps sending while held, like holding a remote's volume button | code colour: 1 green, 2 blue, 3 purple, 4 orange |
| SETTINGS | Keeps doing whatever the previous mode did | as previous mode |

**Staff (StickS3 buttons):**

| Button | Action |
|---|---|
| A click | Next mode – SETTINGS is last, then back to SPEAK |
| A hold | SPEAK: record a message while held (release to stop). IR: learn the selected code – point the remote at the end of the StickS3 from 30cm or more and press within 8s |
| B click | SPEAK: choose message. IR: choose code (plays its sound as a cue). KEYBOARD: choose key |
| B hold | Volume (4 levels) |

When the screen is dim or off, the first press of A or B only wakes it.

### Settings

Click A until the screen says **SETTINGS**. **B click** changes the value, **hold A** goes to the next setting,
**A click** leaves. Changes are saved straight away. It goes back to the previous mode after 30s untouched.

| Setting | Choices (default **bold**) | What it does |
|---|---|---|
| Play style | **Tap**, Hold to play, Latch, Scan | *Tap*: a press plays the whole message. *Hold to play*: plays (looping) only while the switch is held – classic cause and effect. *Latch*: one press starts it looping, the next press stops it. *Scan*: choice-making – see below |
| Scan speed | 1.5s, 2s, **3s**, 4s, 5s | Scan: how long each choice is offered |
| Scan rounds | 1, **2**, 3 | Scan: how many times round before it stops by itself |
| Hold for next msg | Off, 1s, **1.5s**, 2s, 3s | SPEAK: how long the student holds the switch to move to the next message. Turn off for students who can't let go quickly |
| Press must last | **Instant**, 0.1s, 0.25s, 0.5s, 1s | Filters accidental brushes |
| Ignore repeats for | Off, 0.2s, **0.4s**, 0.8s, 1.5s | Filters tremor and bounces after a press |
| Switch wakes screen | **No**, Yes | Whether student presses light up the screen |
| Sleep after | Off, 2 min, **5 min**, 15 min | On battery, with no presses: sleep to save power. The big switch still works (see below) |
| Auto power off | **Never**, 30 min, 60 min, 2 hours | On battery, with no presses: turn off completely, e.g. at the end of the day |
| Key action | **Momentary**, Latch | KEYBOARD: *Momentary* holds the key while the switch is held; *Latch* – one press holds the key down, the next lets it go (for students who can't keep the switch pressed). The screen says "Key held" and the LED glows brighter while latched |
| Press sound | **Off**, Click, Beep | KEYBOARD mode, and IR codes with no sound of their own: a sound on each press, as feedback |
| Forget BT devices | B, then B again to confirm | Clears every paired computer/tablet. Also remove the switch in that device's Bluetooth settings |
| Recording boost | Off, Low, **Medium**, High | Makes new recordings louder, at the cost of some harshness |

### The screen

- **Top bar:** the mode, in its colour, and the battery (% and icon; green bolt = charging, red = low).
- **Middle:** what the switch will do – the message's name and length, the key and whether it's sending by USB
  or Bluetooth, or the IR code's name and whether it has a code and sound.
- **Bottom:** volume bars, USB/BT connection (green = connected) and the press counter.
- A white frame appears while the big switch is held down.

Recordings are trimmed and boosted automatically, stored in flash, and survive power-off.

### Scanning (choice-making with one switch)

Set **Play style** to **Scan**. In SPEAK mode, a press starts the switch offering each message in turn: its LED
colour, its name on screen and a short **spoken prompt** played quietly ("Drink"… "Music"… "Toilet"). The next
press **chooses** – the full message plays ("Can I have a drink, please?"). With no choice it stops after the set
number of rounds. In IR mode it offers the IR codes the same way, so a student can choose what to switch on.

The prompts are made automatically by the setup page from each message's or IR code's **name**, in the chosen
voice – name a message and its prompt appears. Items without a name get a soft beep (IR codes fall back to their
own sound). Only messages / codes that are set up are offered, so two messages make a simple two-way choice.

**To delete a message:** choose it with B, then hold A for a couple of seconds *without speaking* and let go.
The screen says "Nothing heard – msg cleared".
Mode, selected message and IR code, key, volume and settings are remembered too.

**Recording tips:** speak 10–15cm from the stick in a normal voice. The recording is cleaned up automatically:
silence trimmed, hiss between words turned down, level evened out.

### Keyboard (USB or Bluetooth)

Keys: **Space, Enter, Up, Down, Left, Right** (B-click to choose). In Grid 3 / Mind Express,
set switch input to the key shown on screen.

- **USB:** plug into the computer with a data cable. Nothing to pair, and it charges at the same time.
- **Bluetooth:** the StickS3 appears as **"Talking Switch XXXX"** – each stick has its own 4-character ID, shown on the
  KEYBOARD screen, so you can tell switches apart. Pair it from Windows, iPad or Android like a keyboard.
  To move it to a different computer or tablet, use *Forget BT devices* in SETTINGS.

It picks automatically: when a computer is using it as a USB keyboard, keys go over USB only;
otherwise over Bluetooth. A phone charger doesn't count, so Bluetooth keeps working while charging.
The screen says which is in use.

### Power saving

- The screen dims after 30s and switches off after 2 minutes without a staff button press.
  The big switch keeps working with the screen off.
- Bluetooth only runs in KEYBOARD mode (it starts after a second or so in that mode; pairing is remembered).
- **Sleep:** on battery, after the *Sleep after* time with no presses, the StickS3 sleeps – screen, speaker
  and radio off. The switch LED keeps glowing. **The big switch still works:** a press wakes it and does its
  job (a fraction of a second slower than normal). A or B wakes it for staff.
  It doesn't sleep in KEYBOARD mode over Bluetooth, because the connection would drop.
- **Auto power off** (off by default): after the chosen time with no presses it beeps three times and shows
  a warning; 30s later it powers off. Press any button to cancel. Press the power button to turn it back on.
- It never sleeps or powers off while plugged into USB.
- A "Low battery" warning appears at 15%.

### Press logging

Every student activation is written to the USB serial port (115200 baud) as
`press,<milliseconds since boot>,<mode>` – handy evidence for assessing
intentional communication. The screen also shows a press counter.

## Tuning in code

Most student settings are in the SETTINGS mode above. At the top of `TalkingSwitch.ino`:

| Setting | Default | What it does |
|---|---|---|
| `DEBOUNCE_MS` | 25 | Contact bounce filter |
| `MAX_SECONDS` | 10 | Longest recording per message |
| `MIC_PGA` | 8 | Microphone analogue gain (3dB steps, 0–10). Lower it if loud voices sound distorted |
| `SCREEN_DIM_MS` / `SCREEN_OFF_MS` | 30000 / 120000 | Screen dims, then switches off, when idle |
| `BLE_NAME` | "Talking Switch" | Bluetooth name; each stick's own ID is added to it |

## Troubleshooting

- **Big switch does nothing:** swap `PIN_KEY` (10) and `PIN_LED` (9) – the Grove wire order varies between units.
- **Recordings hiss or distort:** hiss – speak closer and lower Recording boost; distortion on loud voices – lower `MIC_PGA`.
- **IR only works close up:** the StickS3's IR LED is small (it's in the end of the stick). Point that end straight at
  the device – its IR window is usually on the front – ideally within 3–5m. In a dark sensory room, bouncing off a
  white wall or ceiling can help. For longer range, an IR repeater/extender near the device works with any remote.
- **IR learns nothing:** make sure the remote is 38kHz (most TV remotes are) and point it at the StickS3's IR window from **30cm to 1m** – closer than 30cm can scramble the code. Very long air-con codes may be cut short.
- **Quiet playback:** B-hold for volume; for a noisy classroom add a small external speaker.
- **Using a standard 3.5mm switch instead:** wire the jack's tip to `PIN_KEY` and sleeve to GND on the Grove connector.

## Notes

- Compile-checked against esp32 core 3.3.11 with M5Unified 0.2.23 / M5GFX 0.2.30, in both USB modes.
  Tested on hardware: switch, modes and press logging. Not yet tested: IR, USB keyboard, sleep, auto power-off.
- This is a DIY device, not a certified AT product. Check it's robust and safe for each student before use.
