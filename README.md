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
partition layout automatically (2MB for the firmware, ~5.9MB for recordings). Keep it next to the `.ino`.

`build_opt.h` (also next to the `.ino`) names the USB device **"Talking Switch"** by **Magnatronic**, so it shows
up under that name when connecting from the setup page (instead of the board's default "ESP32S3_DEV").

**Changing the partition layout wipes the recordings and learned IR codes** on the stick at the next upload
(names and settings are kept) – record them again afterwards.

If the screen says **"No PSRAM!"**, change PSRAM to *QSPI PSRAM* and upload again.

In TinyUSB mode the StickS3 still shows up as a COM port and uploads normally reset it
automatically. If an upload fails, put the StickS3 into download mode
(see M5Stack's StickS3 docs for the button sequence) and upload again.

## Setup page (USB)

The setup page is `docs/index.html`, published with **GitHub Pages** so staff just open a link in **Chrome or
Edge** (Firefox and Safari can't talk to USB devices). Plug the StickS3 in, close the Arduino Serial Monitor,
click **Connect** and pick the switch. The switch is set up over the USB cable – no Wi-Fi – and recordings and
typed words stay on the computer.

The page has a tab for each mode – SPEAK, CHOOSE, KEYBOARD, IR – then **Voice** and **Settings**, like the
modes on the switch. The tab of the mode the switch is in has a green dot; any other mode's tab has a **Use** button to switch
to it. Each mode's tab has that mode's settings at the top, in the same order as on the switch (settings that do
nothing with the current choices are hidden), and what it uses below.

- **SPEAK:** Messages (Staff pick / Student scans), then Play style and Hold for next, or the scanning settings
  when the student scans; the 4 Quick messages. For each: a name, ▶ Play, **Add / Change** (**type the words** –
  spoken in the switch's voice; Enter makes the speech – record with the computer's microphone or a headset, or
  upload an audio file; it plays straight away, then *Save to the switch*), Delete, and *Select* (the one SPEAK
  plays). Typing starts from the message's words, or its name; a typed message with no name is named after its
  words.
- **CHOOSE:** Choose from, Choosing, Scan speed, Scan rounds; the 4 **topics** of up to 4 messages each (up to 5
  seconds per message). Name each topic and message – the names become the spoken prompts. With *Choose from: One
  topic*, *Use this topic* picks it.
- **KEYBOARD:** the key, Key action, Press sound; a **custom key or shortcut** such as Win+H (dictation); the
  Bluetooth name.
- **IR:** IR codes (and the scanning settings when students scan), Press sound; 4 remote-control codes, separate
  from the messages. For each: a name, **Learn / Test / Delete**, and an optional **sound** (made the same three
  ways) that the switch says as it sends the code.
- **Voice:** the switch's **voice** and speed for typed messages and scanning prompts (natural: Emma, Isabella,
  George, Fable; quick: Cori, Alba, Southern English female, Northern English male). It's stored on the switch, so
  every computer uses the same one. Changing it remakes the prompts, and **Remake in this voice** remakes the typed
  messages made in another voice (the switch keeps each typed message's words; recorded and uploaded ones don't
  change).
- **Settings:** the switch's SETTINGS in the same order – Volume, Press must last, Ignore repeats, Switch wakes
  screen, Sleep after, Auto power off, Recording boost, Forget BT devices. Recording boost also sets the loudness
  of messages made on the page.
- **Names become prompts:** naming a message, topic or IR code makes its short spoken prompt for scanning, in
  the switch's voice (plus a "Back" prompt when students choose topics).

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

The switch holds **4 Quick messages** (SPEAK), **4 Topics of 4 messages** (CHOOSE) and **4 IR codes** (IR), each
up to 5 seconds. Each IR code can have its own sound, e.g. IR code 1 "Bubbles" turns the bubble tube on *and* says
"Bubbles!".

**Student:** press the big switch. What happens depends on the mode.

| Mode | Big switch does | LED glow |
|---|---|---|
| SPEAK | *Messages* setting: **Staff pick** – plays the selected Quick message (see *Play style* for hold-to-play and latch; with Tap, keep holding to move to the next Quick message). **Student scans** – the student chooses one of the Quick messages by scanning (see below) | message colour: 1 green, 2 blue, 3 purple, 4 orange |
| CHOOSE | The student **chooses** from the Topics by scanning, see below | colour of the choice on offer |
| KEYBOARD | Holds down the chosen key while pressed | purple |
| IR | Sends the selected IR code, and says its sound if it has one. *IR codes* setting: Staff pick / Repeat held (keeps sending while held, like a remote's volume button) / Student scans (the student chooses by scanning) | code colour |
| SETTINGS | Keeps doing whatever the previous mode did | as previous mode |

### Staff buttons – one rule everywhere

The bottom line of the screen always shows what the buttons do right now.

| Button | Does |
|---|---|
| **A click** | Next mode (SPEAK → CHOOSE → KEYBOARD → IR → SETTINGS). In a mode's settings: close them |
| **B click** | Next: Quick message / topic / key / IR code, then the mode's **Settings** item, then back to the first. In settings: the next setting |
| **Hold A** | Do it: SPEAK – record the selected Quick message (release to stop); IR – learn the selected code (remote 30cm+ from the end of the stick, within 8s); on **Settings** – open them; in settings – change the value |

When the screen is dim or off, the first press of A or B only wakes it.

### Scanning: choice-making with one switch

With SPEAK or IR set to **Student scans**, and in CHOOSE, the switch offers the choices one at a time – each with
its LED colour, its name on screen and a short **spoken prompt** played quietly ("Toast"… "Crisps"… "Yoghurt") –
and the student picks one, which then plays in full (or, in IR, is sent).

- **SPEAK, Messages: Student scans** – it offers the 4 Quick messages. The simplest start.
- **CHOOSE, Choose from: One topic** (default) – it offers the messages in the topic staff selected (B on the switch, or *Use this
  topic* on the setup page), e.g. "Snack time".
- **CHOOSE, Choose from: All topics** – it offers the **topics** first ("Snack time"… "Music"…), then the chosen topic's
  messages and **"Back"** (to return to the topics).
- **Choosing: Press twice** – a press starts the offers, the next press chooses. With no choice it stops after the
  set number of *Scan rounds*.
- **Choosing: Hold & release** – hold the switch to step through the offers, let go to choose. After choosing a
  topic, hold again for its messages.

Only topics and messages that are set up are offered, so two messages make a simple two-way choice. Prompts are
made by the setup page from the **names**. A message without a prompt – e.g. one just recorded on the stick – plays
itself quietly as its prompt (cut short by the next offer), so scanning works straight away with no setup page:
record the Quick messages (SPEAK, hold A) and set SPEAK's *Messages: Student scans*.

### Settings

Each mode has its own settings – press B until the screen shows **Settings**, then hold A to open them – and the
**SETTINGS** mode holds the general ones. In any settings: **B** = next setting, **hold A** = change the value,
**A** = close (or next mode). Changes are saved straight away; settings close by themselves after 30s untouched.
Settings that do nothing with the current choices are left out (e.g. Scan speed when staff pick).
All of these are on the setup page too, on each mode's tab and the Settings tab.

| Where | Setting | Choices (default **bold**) | What it does |
|---|---|---|---|
| General | Volume | 1, 2, **3**, 4 | How loud messages play |
| General | Press must last | **Instant**, 0.1s, 0.25s, 0.5s, 1s | Filters accidental brushes |
| General | Ignore repeats for | Off, 0.2s, **0.4s**, 0.8s, 1.5s | Filters tremor and bounces after a press |
| General | Switch wakes screen | **No**, Yes | Whether student presses light up the screen |
| General | Sleep after | Off, 2 min, **5 min**, 15 min | On battery, with no presses: sleep to save power. The big switch still works (see below) |
| General | Auto power off | **Never**, 30 min, 60 min, 2 hours | On battery, with no presses: turn off completely, e.g. at the end of the day |
| General | Recording boost | Off, Low, **Medium**, High | Makes recordings made on the switch louder, at the cost of some harshness |
| General | Forget BT devices | hold A, then again to confirm | Clears every paired computer/tablet. Also remove the switch in that device's Bluetooth settings |
| SPEAK | Messages | **Staff pick**, Student scans | *Staff pick*: plays the selected Quick message. *Student scans*: the student chooses one by scanning (then Choosing, Scan speed and Scan rounds apply instead of Play style and Hold for next msg) |
| SPEAK | Play style | **Tap**, Hold to play, Latch | *Tap*: a press plays the whole message. *Hold to play*: plays (looping) only while the switch is held – classic cause and effect. *Latch*: one press starts it looping, the next press stops it |
| SPEAK | Hold for next msg | Off, 1s, **1.5s**, 2s, 3s | With Tap: how long the student holds the switch to move to the next Quick message. Turn off for students who can't let go quickly |
| CHOOSE | Choose from | **One topic**, All topics | See above |
| SPEAK, CHOOSE, IR | Choosing | **Press twice**, Hold & release | See above |
| SPEAK, CHOOSE, IR | Scan speed | 1.5s, 2s, **3s**, 4s, 5s | How long each choice is offered |
| SPEAK, CHOOSE, IR | Scan rounds | 1, **2**, 3 | Press twice: how many times round before it stops by itself |
| KEYBOARD | Key action | **Momentary**, Latch | *Momentary* holds the key while the switch is held; *Latch* – one press holds the key down, the next lets it go. The screen says "Key held" and the LED glows brighter while latched |
| KEYBOARD, IR | Press sound | **Off**, Click, Beep | A sound on each press, as feedback (IR: only for codes with no sound of their own) |
| IR | IR codes | **Staff pick**, Repeat held, Student scans | See the IR mode above |

### The screen

- **Top bar:** the mode, in its colour ("Settings" on that colour while its settings are open); on the right, the
  volume (a speaker and 4 bars) and the battery (% and icon; green bolt = charging, red = low).
- **Middle:** what the switch will do – the Quick message, the topic CHOOSE will offer (or the choice on offer while
  scanning), the key and whether it's sending by USB or Bluetooth, or the IR code. With *Student scans*, SPEAK and
  IR still show the selected message or code (the one B and hold A work on), with "Student scans…" underneath.
- **Bottom:** what the buttons do right now.
- A white frame appears while the big switch is held down.

Recordings are trimmed and boosted automatically, stored in flash, and survive power-off.

**To delete a Quick message:** select it (B in SPEAK), then hold A for a couple of seconds *without speaking* and
let go. The screen says "Nothing heard – cleared". Or use Delete on the setup page (topic messages are made there).
Mode, selected Quick message, topic and IR code, key and settings are remembered too.

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
