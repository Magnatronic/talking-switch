# ChatterSwitch – M5StickS3 + Unit Key

Firmware for a big-button accessibility switch: a talking switch (Quick messages), choice-making with Topics,
a Bluetooth/USB keyboard key for an AAC device, and an IR remote (Control), all in one.

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

These settings are saved in `sketch.yaml` next to each sketch (`TalkingSwitch/` and `ToFTest/`): the Arduino IDE
picks them up when it opens the sketch, and `arduino-cli compile` uses them. If the Tools menu still shows something
else, set it to the table above.

Open `TalkingSwitch/TalkingSwitch.ino` (the sketch keeps its original name; Arduino needs the sketch folder and
`.ino` to share a name). The `partitions.csv` in that folder replaces the
partition layout automatically (2MB for the firmware, ~5.9MB for recordings). Keep it next to the `.ino`.

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

The page has a tab for each mode – QUICK, TOPICS, KEYBOARD, CONTROL – then **Voice** and **Settings**, like the
modes on the switch. The tab of the mode the switch is in has a green dot; any other mode's tab has a **Use** button to switch
to it. Each mode's tab has that mode's settings at the top, in the same order as on the switch (settings that do
nothing with the current choices are hidden), and what it uses below.

- **QUICK:** Messages (Staff pick / Student chooses), then Play style and Hold for next, or Choosing (Press twice /
  Hold & release / Count presses) with its settings; the 4 Quick messages. For each: a name, ▶ Play, **Add / Change** (**type the words** –
  spoken in the switch's voice; Enter makes the speech – record with the computer's microphone or a headset, or
  upload an audio file; it plays straight away, then *Save to the switch*), Delete, and *Select* (the one QUICK
  plays). Typing starts from the message's words, or its name; a typed message with no name is named after its
  words.
- **TOPICS:** Choose from, Choosing, Scan speed, Scan rounds; the 4 **topics** of up to 4 messages each (up to 5
  seconds per message). Name each topic and message – the names become the spoken prompts. With *Choose from: One
  topic*, *Use this topic* picks it.
- **KEYBOARD:** the key, Key action, Press sound; a **custom key or shortcut** such as Win+H (dictation); the
  Bluetooth name.
- **CONTROL:** IR codes (and the scanning settings when students scan), Press sound; 4 remote-control codes, separate
  from the messages. For each: a name, **Learn / Test / Delete**, and an optional **sound** (made the same three
  ways) that the switch says as it sends the code.
- **Voice:** the switch's **voice** and speed for typed messages and scanning prompts (natural: Emma, Isabella,
  George, Fable; quick: Cori, Alba, Southern English female, Northern English male). It's stored on the switch, so
  every computer uses the same one. Changing it remakes the prompts, and **Remake in this voice** remakes the typed
  messages made in another voice (the switch keeps each typed message's words; recorded and uploaded ones don't
  change).
- **Settings:** the switch's SETTINGS in the same order – Volume, Modes, Press must last, Ignore repeats,
  Brightness, Switch wakes screen, Sleep after, Auto power off, Recording boost, Forget BT devices. Recording boost
  also sets the loudness of messages made on the page. Then **Switch name**: a name (up to 10 characters) put in front of the
  switch's own for Bluetooth and the setup page's Connect list, e.g. "Sam" shows as "Sam ChatterSwitch 7B70" (it's always
  "ChatterSwitch" and the stick's ID). Unplug and plug the switch in
  again for computers to see a new name; Windows may keep the old one until the switch is removed in Device
  Manager. **About this switch** shows the firmware version, storage used (red when nearly full – delete sounds
  you don't use), student presses since it was turned on, and whether it can be a USB keyboard. If the page and
  the switch's firmware don't match, the page says **Update the switch** (or to reload the page).
- **Names become prompts:** naming a message, topic or IR code makes its short spoken prompt for scanning, in
  the switch's voice (plus "Back" when students choose topics, and "Stop" with Stop choice on).

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

The switch holds **4 Quick messages** (QUICK), **4 Topics of 4 messages** (TOPICS) and **4 IR codes** (CONTROL), each
up to 5 seconds. Each IR code can have its own sound, e.g. IR code 1 "Bubbles" turns the bubble tube on *and* says
"Bubbles!".

**Student:** press the big switch. What happens depends on the mode.

| Mode | Big switch does | LED glow |
|---|---|---|
| QUICK | *Messages* setting (and QUICK's *Choosing*): **Staff pick** – plays the selected Quick message (see *Play style* for hold-to-play and latch; with Tap, keep holding to move to the next Quick message). **Student chooses** – by scanning (see below), or by counting presses: 1–4 presses for message 1–4 | message colour: 1 green, 2 blue, 3 purple, 4 orange |
| TOPICS | The student **chooses** from the Topics by scanning, see below | colour of the choice on offer |
| KEYBOARD | Holds down the chosen key while pressed. With *No USB* set to QUICK or TOPICS it's also a **backup**: see below | purple |
| CONTROL | Sends the selected IR code, and says its sound if it has one. *IR codes* setting: Staff pick / Repeat held (keeps sending while held, like a remote's volume button) / Student scans (the student chooses by scanning) | code colour |
| SETTINGS | Keeps doing whatever the previous mode did | as previous mode |

### Just a talking switch

For a student who only needs it to talk, set SETTINGS **Modes: Talking only**: A then goes QUICK → TOPICS →
SETTINGS, and the setup page shows only QUICK, TOPICS, Voice and Settings.

### Backup mode: a switch for the AAC device, and a talker when it isn't there

With KEYBOARD's **No USB** set to QUICK or TOPICS, the switch works the student's AAC device over USB, and when
there's no USB connection (the device isn't there, is flat, or the cable is out) for 10 seconds it changes to
QUICK or TOPICS by itself, with a falling two-note sound and "No USB" on screen, so the student can still talk.
While counting down, the KEYBOARD screen says "No USB – QUICK in 8 s". In the backup mode the bottom line starts
with "Backup", and Bluetooth stays off, so it doesn't use extra battery (and the switch can sleep as usual).
When USB is connected again it goes back to KEYBOARD (rising two notes), once any choice the student is making is
finished. If it's asleep, that happens when it wakes. Choosing a mode with A (or on the setup page) ends the
backup. Fill the Quick messages and topics with what the student needs when their AAC device isn't there.

### Staff buttons – one rule everywhere

The bottom line of the screen always shows what the buttons do right now.

| Button | Does |
|---|---|
| **A click** | Next mode (QUICK → TOPICS → KEYBOARD → CONTROL → SETTINGS). In a mode's settings: close them |
| **B click** | Next: Quick message / topic / key / IR code, then the mode's **Settings** item, then back to the first. In settings: the next setting |
| **Hold A** in TOPICS | Opens the topic: **B** then steps through its messages (hold B hears one) and **A** closes it |
| **Hold B** | Hear it: QUICK – plays the selected Quick message; an open topic – the message; CONTROL – plays the code's sound (without sending it). B itself is silent, to save battery |
| **Hold A** | Do it: QUICK – record the selected Quick message (release to stop); CONTROL – learn the selected code (remote 30cm+ from the end of the stick, within 8s); on **Settings** – open them; in settings – change the value |

When the screen is dim or off, the first press of A or B only wakes it.

### Scanning: choice-making with one switch

With QUICK set to **Student chooses** (Press twice or Hold & release) or CONTROL to **Student scans**, and in TOPICS, the switch offers the choices one at a time – each with
its LED colour, its name on screen and a short **spoken prompt** played quietly ("Toast"… "Crisps"… "Yoghurt") –
and the student picks one, which then plays in full (or, in CONTROL, is sent).

- **QUICK, Messages: Student chooses** – it offers the 4 Quick messages. The simplest start.
- **TOPICS, Choose from: One topic** (default) – it offers the messages in the topic staff selected (B on the switch, or *Use this
  topic* on the setup page), e.g. "Snack time".
- **TOPICS, Choose from: All topics** – it offers the **topics** first ("Snack time"… "Music"…), then the chosen topic's
  messages and **"Back"** (to return to the topics).
- **Offer Quick / Offer Control / Offer My device** (with All topics) – more choices next to the topics, so TOPICS
  can be the student's home: **Quick** offers the Quick messages and **Control** the IR codes, like a topic's
  messages (then Back), so the student can say a Quick message or turn on the bubble tube or TV themselves; **My
  device** changes the switch to KEYBOARD so they can use their AAC device or computer.
  Once in KEYBOARD every press goes to the device, so the switch can't offer a way back: staff bring it back (A,
  or the setup page), or with *No USB: TOPICS* it comes back by itself when USB is unplugged.
- **Choosing: Press twice** – a press starts the offers, the next press chooses. With no choice it stops after the
  set number of *Scan rounds*.
- **Choosing: Hold & release** – hold the switch to step through the offers, let go to choose. After choosing a
  topic, hold again for its messages.
- **Stop choice: On** – every scan ends with **"Stop"** (LED red): the topics, a topic's messages (after "Back"),
  and QUICK's and CONTROL's choices. Choosing it ends the scan without doing anything; the next press starts again
  from the top. It lets the student say "none of these". Staff can also stop a scan with A or B on the stick.

Only topics and messages that are set up are offered, so two messages make a simple two-way choice. Prompts are
made by the setup page from the **names**. A message without a prompt – e.g. one just recorded on the stick – plays
itself quietly as its prompt (cut short by the next offer), so scanning works straight away with no setup page:
record the Quick messages (QUICK, hold A) and set QUICK's *Messages: Student chooses*.

### Settings

Each mode has its own settings – press B until the screen shows **Settings**, then hold A to open them – and the
**SETTINGS** mode holds the general ones. In any settings: **B** = next setting, **hold A** = change the value,
**A** = close (or next mode). Changes are saved straight away; settings close by themselves after 30s untouched.
Settings that do nothing with the current choices are left out (e.g. Scan speed when staff pick).
All of these are on the setup page too, on each mode's tab and the Settings tab.

| Where | Setting | Choices (default **bold**) | What it does |
|---|---|---|---|
| General | Volume | 1, 2, **3**, 4 | How loud messages play |
| General | Modes | **All**, No KEYBOARD, No CONTROL, Talking only | Turn KEYBOARD and/or CONTROL off for a switch that only talks: A skips them, the setup page hides their tabs, and their settings (and the matching Offer settings) go too. Their key and IR codes are kept |
| General | Press must last | **Instant**, 0.1s, 0.25s, 0.5s, 1s | Filters accidental brushes |
| General | Ignore repeats for | Off, 0.2s, **0.4s**, 0.8s, 1.5s | Filters tremor and bounces after a press |
| General | Brightness | Low, **Medium**, High | Screen brightness while in use. Low saves battery |
| General | Switch wakes screen | **No**, Yes | Whether student presses light up the screen |
| General | Sleep after | Off, 2 min, **5 min**, 15 min | On battery, with no presses: sleep to save power. The big switch still works (see below) |
| General | Auto power off | **Never**, 30 min, 60 min, 2 hours | On battery, with no presses: turn off completely, e.g. at the end of the day |
| General | Recording boost | Off, Low, **Medium**, High | Makes recordings made on the switch louder, at the cost of some harshness |
| General | Forget BT devices | hold A, then again to confirm | Clears every paired computer/tablet. Also remove the switch in that device's Bluetooth settings |
| General | About | – | The firmware version, how much recording storage is used, and student presses since it was turned on |
| QUICK | Messages | **Staff pick**, Student chooses | *Staff pick*: plays the selected Quick message (Play style and Hold for next msg apply). *Student chooses*: the student picks one – see QUICK's Choosing |
| QUICK | Choosing | **Press twice**, Hold & release, Count presses | Kept in step with TOPICS and CONTROL's Choosing (change Press twice / Hold & release in one and the other follows); only Count presses is QUICK's own – TOPICS and CONTROL (and the Quick choice inside TOPICS) always scan. *Press twice* / *Hold & release*: scanning, see above (Scan speed, Scan rounds, Stop choice apply). *Count presses*: press 1–4 times quickly for message 1–4 – each press ticks and the LED shows that message's colour; the message plays after the Press gap. Not for students with tremor or accidental double presses |
| QUICK | Press gap | 0.5s, **0.8s**, 1.2s, 1.6s, 2s | Count presses: how long without a press ends the count – longer for students who are slower to release and press again (every message waits this long before playing). Ignore repeats only filters bounces under 0.15s in this mode |
| QUICK | Play style | **Tap**, Hold to play, Latch | *Tap*: a press plays the whole message. *Hold to play*: plays (looping) only while the switch is held – classic cause and effect. *Latch*: one press starts it looping, the next press stops it |
| QUICK | Hold for next msg | Off, 1s, **1.5s**, 2s, 3s | With Tap: how long the student holds the switch to move to the next Quick message. Turn off for students who can't let go quickly |
| TOPICS | Choose from | **One topic**, All topics | See above |
| TOPICS | Offer Quick | **Off**, On | With All topics: offers "Quick" (the Quick messages) next to the topics, see above |
| TOPICS | Offer Control | **Off**, On | With All topics: offers "Control" (the IR codes) next to the topics |
| TOPICS | Offer My device | **Off**, On | With All topics: offers "My device" (changes to KEYBOARD) next to the topics |
| TOPICS, CONTROL | Choosing | **Press twice**, Hold & release | See above |
| QUICK, TOPICS, CONTROL | Scan speed | 1.5s, 2s, **3s**, 4s, 5s | How long each choice is offered |
| QUICK, TOPICS, CONTROL | Scan rounds | 1, **2**, 3 | Press twice: how many times round before it stops by itself |
| QUICK, TOPICS, CONTROL | Stop choice | **Off**, On | Offers "Stop" last, so the student can choose none of them |
| KEYBOARD | No USB | **Bluetooth**, QUICK, TOPICS | With no USB connection: *Bluetooth* sends keys over Bluetooth. *QUICK* / *TOPICS*: the backup mode (below). Only with USB Mode: USB-OTG (TinyUSB) |
| KEYBOARD | Key action | **Momentary**, Latch | *Momentary* holds the key while the switch is held; *Latch* – one press holds the key down, the next lets it go. The screen says "Key held" and the LED glows brighter while latched |
| KEYBOARD, CONTROL | Press sound | **Off**, Click, Beep | A sound on each press, as feedback (CONTROL: only for codes with no sound of their own) |
| CONTROL | IR codes | **Staff pick**, Repeat held, Student scans | See the CONTROL mode above |

### The screen

- **Top bar:** the mode, in its colour ("SETTINGS" on that colour while its settings are open); on the right, the
  volume (a speaker and 4 bars) and the battery (% and icon; green bolt = charging, red = low).
- **Middle:** three lines, the same on every screen – the main thing (big), its state or what to do (green ready,
  orange a problem, yellow what to do), and details (grey). What the switch will do – the Quick message, the topic TOPICS will offer (or the choice on offer while
  scanning), the key and whether it's sending by USB or Bluetooth, or the IR code. With *Student scans*, QUICK and
  CONTROL still show the selected message or code (the one B and hold A work on), with "Student scans…" underneath.
- **Bottom:** what the buttons do right now.
- A white frame appears while the big switch is held down.

Recordings are trimmed and boosted automatically, stored in flash, and survive power-off.

**To delete a Quick message:** select it (B in QUICK), then hold A for a couple of seconds *without speaking* and
let go. The screen says "Nothing heard – cleared". Or use Delete on the setup page (topic messages are made there).
Mode, selected Quick message, topic and IR code, key and settings are remembered too.

**Recording tips:** speak 10–15cm from the stick in a normal voice. The recording is cleaned up automatically:
silence trimmed, hiss between words turned down, level evened out.

### Keyboard (USB or Bluetooth)

Keys: **Space, Enter, Up, Down, Left, Right** (B-click to choose). In Grid 3 / Mind Express,
set switch input to the key shown on screen.

- **USB:** plug into the computer with a data cable. Nothing to pair, and it charges at the same time.
- **Bluetooth:** the StickS3 appears as **"ChatterSwitch XXXX"** – each stick has its own 4-character ID, shown on the
  KEYBOARD screen, so you can tell switches apart – with any *Switch name* from the setup page in front, e.g.
  "Sam ChatterSwitch 7B70". Pair it from Windows, iPad or Android like a keyboard.
  To move it to a different computer or tablet, use *Forget BT devices* in SETTINGS.

It picks automatically: when a computer is using it as a USB keyboard, keys go over USB only;
otherwise over Bluetooth. A phone charger doesn't count, so Bluetooth keeps working while charging.
The screen says which is in use.

### Power saving

- The screen dims after 30s and switches off after 2 minutes without a staff button press. Its brightness while
  in use is the *Brightness* setting (Low saves the most).
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
| `BLE_NAME` | "ChatterSwitch" | Default name (Bluetooth and USB); each stick's own ID is added to it |
| `FW_VERSION` / `FW_API` | "27 Sep 2026" / 1 | Version shown in About. Raise `FW_API` (and `PAGE_API` in `docs/index.html`) when the page and firmware must change together |

## Troubleshooting

- **Big switch does nothing:** swap `PIN_KEY` (10) and `PIN_LED` (9) – the Grove wire order varies between units.
- **Recordings hiss or distort:** hiss – speak closer and lower Recording boost; distortion on loud voices – lower `MIC_PGA`.
- **IR only works close up:** the StickS3's IR LED is small (it's in the end of the stick). Point that end straight at
  the device – its IR window is usually on the front – ideally within 3–5m. In a dark sensory room, bouncing off a
  white wall or ceiling can help. For longer range, an IR repeater/extender near the device works with any remote.
- **IR learns nothing:** make sure the remote is 38kHz (most TV remotes are) and point it at the StickS3's IR window from **30cm to 1m** – closer than 30cm can scramble the code. Very long air-con codes may be cut short.
- **Quiet playback:** B-hold for volume; for a noisy classroom add a small external speaker.
- **Using a standard 3.5mm switch instead:** wire the jack's tip to `PIN_KEY` and sleeve to GND on the Grove connector.

## ToF sensor test (touch-free switch)

`ToFTest/ToFTest.ino` is a separate test sketch for the M5Stack **Unit ToF4M** (VL53L1X) on the Grove port, to try it as
a touch-free switch (a hand, head or foot moving closer than a set distance) and to watch a student's movement. It
needs the **VL53L1X** library by Pololu (Library Manager). Unplug the Unit Key and any switch on the mono jack first –
they share the Grove pins. A = next trigger distance (5–50 cm), B = reset, hold B = Short / Long range. Readings go to
the serial port as `tof,<ms>,<mm>,<pressed>` for the Serial Plotter.

## Notes

- Compile-checked against esp32 core 3.3.11 with M5Unified 0.2.23 / M5GFX 0.2.30, in both USB modes.
  Tested on hardware: switch, modes and press logging. Not yet tested: IR, USB keyboard, sleep, auto power-off.
- This is a DIY device, not a certified AT product. Check it's robust and safe for each student before use.
