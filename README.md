# ChatterSwitch – M5StickS3 + Unit Key

Firmware for a big-button accessibility switch: a talking switch (Quick messages), choice-making with Topics,
a Bluetooth/USB keyboard key for an AAC device, and an IR remote (Control), all in one.

## What you need

- M5StickS3
- M5Stack Unit Key (U144) on the StickS3's Grove port – or a 3.5mm switch socket, or a Unit ToF4M distance sensor
  (see *Using a standard 3.5mm switch* and *Touch-free: the ToF sensor*)
- The printed base and cap (`unit-key-big-cap.scad`)
- Arduino IDE 2.x

## Arduino IDE setup

1. **Board package:** Boards Manager → install **esp32 by Espressif Systems**, version 3.3.0 or newer.
2. **Libraries:** Library Manager → install **M5Unified** (it pulls in M5GFX) and **VL53L1X** by Pololu (for the
   ToF distance sensor).
3. **Board:** *ESP32S3 Dev Module*, then set Tools to:

| Setting | Value |
|---|---|
| USB CDC On Boot | **Enabled** – the setup page talks to the switch over it (the firmware won't build without it) |
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

In TinyUSB mode the StickS3 still shows up as a COM port (the setup page's Connect list calls it "ESP32S3_DEV –
TinyUSB CDC") and uploads normally reset it automatically. If an upload fails, put the StickS3 into download mode
(see M5Stack's StickS3 docs for the button sequence) and upload again.

## Setup page (USB)

The setup page is `docs/index.html`, published with **GitHub Pages** so anyone setting it up just opens a link in **Chrome or
Edge** (Firefox and Safari can't talk to USB devices). Turn the StickS3 on and plug it in (don't hold the power
button while plugging it in – that starts it for programming), close the Arduino IDE, click **Connect** and pick
**ESP32S3_DEV – TinyUSB CDC**. The switch is set up over the USB cable – no Wi-Fi – and recordings and
typed words stay on the computer.

The page has a tab for each mode – QUICK, TOPICS, KEYBOARD, CONTROL, MOUSE – then **Voice** and **Settings**, like the
modes on the switch. The tab of the mode the switch is in has a green dot. At the top of each mode's tab is one line:
**In use** (or a **Use** button to switch to that mode), the mode's settings as short chips, and **⚙ settings**,
which opens them (click **?** next to a setting for a one-line explanation; settings that do nothing with the
current choices are hidden). Below that is what the mode uses.

- **Rows:** each message has a name, its length, **▶ Play** (on the switch), **Edit sound** / **+ Add sound**, and
  a **⋯** menu (Listen here, Download .wav, Delete). In QUICK and CONTROL, click a row (or its number) to select it.
- **Sounds, in three steps:** 1 *Make it* – **Type** the words (spoken in the switch's voice; Enter makes it),
  **Record** with the computer's microphone or a headset, or pick an **Audio file**; 2 *Check it* – it plays straight
  away, **▶ Listen** to hear it again; 3 **Save to switch**. It says *Not saved yet* until then, and asks before
  closing without saving. Typing starts from the message's words, or its name; a typed message with no name is
  named after its words.
- **QUICK:** Plays (Selected message / Scan: press twice / Scan: hold & release / Count presses) and the settings
  that go with it; the 4 Quick messages.
- **TOPICS:** Choose from, Choosing, Scan speed, Scan rounds, Stop choice; the 4 **topics** of up to 4 messages
  each (up to 5 seconds per message). With *Choose from: One topic*, **Use** picks the topic.
- **KEYBOARD:** the key (including Left click), a **custom key or shortcut** such as Win+H (dictation) and the
  Bluetooth name; settings: Key action, Press sound, No USB.
- **CONTROL:** 4 remote-control codes, separate from the messages. For each: a name, **Test / Learn**, Delete (⋯),
  and an optional **sound** that the switch says as it sends the code. Settings: IR codes (and scanning settings
  with *Scan*), Press sound.
- **MOUSE:** **Calibrate** (with what it's doing now), **Pause / Move**, and the connection; settings: Speed,
  Speed-up, Steady, Smoothing, Dwell click, Switch, Hold to pause, Press sound, Flip.
- **Voice:** the switch's **voice** and speed for typed messages and scanning prompts (natural: Emma, Isabella,
  George, Fable; quick: Cori, Alba, Southern English female, Northern English male). It's stored on the switch, so
  every computer uses the same one. Changing it remakes the prompts, and **Remake in this voice** remakes the typed
  messages made in another voice (recorded and uploaded ones don't change).
- **Settings:** the switch's SETTINGS in groups – **Sound** (Volume, Recording boost – which also sets the loudness
  of messages made on the page), **Presses** (Press must last, Ignore repeats), **ToF sensor** (only with one: its
  settings and a live **Sensor test** graph), **Screen and power** (Brightness, Press wakes screen, Sleep after,
  Auto power off), **Switch** (Modes; **Name**: up to 10 characters put in front of the Bluetooth name, e.g. "Sam"
  shows as "Sam ChatterSwitch 7B70"; Forget paired devices) and **About** (firmware, storage – red when nearly
  full, presses since it was turned on, input, USB keyboard). If the page and the switch's firmware don't match,
  the page says **Update the switch** (or to reload the page).
- **Names become prompts:** naming a message, topic or IR code makes its short spoken prompt for scanning, in
  the switch's voice (plus "Back" when choosing from all topics, and "Stop" with Stop choice on). A row says
  *no prompt* when one is missing.

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

**The big switch:** press it. What happens depends on the mode.

| Mode | Big switch does | LED glow |
|---|---|---|
| QUICK | *Plays* setting (and QUICK's *Choosing*): **Selected** – plays the selected Quick message (see *Play style* for hold-to-play and latch; with Tap, keep holding to move to the next Quick message). **Choose one** – by scanning (see below), or by counting presses: 1–4 presses for message 1–4 | message colour: 1 green, 2 blue, 3 purple, 4 orange |
| TOPICS | Messages are **chosen** from the Topics by scanning, see below | colour of the choice on offer |
| KEYBOARD | Holds down the chosen key while pressed. With *No USB* set to QUICK or TOPICS it's also a **backup**: see below | purple |
| CONTROL | Sends the selected IR code, and says its sound if it has one. *IR codes* setting: Selected / Repeat held (keeps sending while held, like a remote's volume button) / Scan (chosen by scanning) | code colour |
| MOUSE | Clicks the mouse button; head or hand movement moves the pointer. See *MOUSE* below | pink |
| SETTINGS | Keeps doing whatever the previous mode did | as previous mode |

### Touch-free: the ToF sensor

Plug an M5Stack **Unit ToF4M** distance sensor into the Grove port instead of the Unit Key (not together with the
Unit Key or a jack switch – they share the Grove pins) and restart: the switch finds it by itself and it becomes the
switch – every mode, scanning, Press must last and Ignore repeats work as with a button.

- **Line:** a press is coming closer than the *Distance*.
- **Move:** a press is a movement of the *Movement* size towards the sensor from where the hand or finger rests –
  the resting place is learnt as it goes (*Follow*), so it suits small movements like a finger and copes with the
  user shifting. After it has been pressed for the *Settle* time, where the finger is becomes the new resting
  place and it lets go.
- One reading past the line presses (a quick wave counts); it reads about 65 times a second.
- **Setting it up:** SETTINGS → *Sensor test* shows a live graph on the stick, and the setup page's Settings tab
  has a bigger live graph next to the sensor settings, with **Record** to download the readings as a spreadsheet
  file for assessment.
- A finger works best 5–10 cm away; closer than about 4 cm counts as pressed. Mount it rigidly, keep its window
  clean, and point it at a plain background.
- With the sensor there's no switch LED (feedback is on screen and by sound), and the switch doesn't sleep – the
  sensor can't wake it – so the battery runs down faster.

### Just a talking switch

For a switch that only needs to talk, set SETTINGS **Modes: Talking only**: A then goes QUICK → TOPICS →
SETTINGS, and the setup page shows only QUICK, TOPICS, Voice and Settings.

### MOUSE: head or hand movement moves the pointer

The stick's motion sensor works a mouse pointer (SETTINGS **MOUSE mode: Off** hides it), worn on the head (headband or
cap) or on a hand: turn left/right to move left/right, tip down/up to move down/up. It goes over USB, or Bluetooth
with the same pairing as KEYBOARD (on an iPad, if no pointer shows, turn on AssistiveTouch).

- **Calibrate:** the first time MOUSE starts, and whenever you choose it (B to **Calibrate**, hold A; or the setup
  page): *Keep still*, then *Tip down* – nod down, or tip the hand down, and back up. Wear it as it will be used;
  it works whichever way round it's worn. Each time MOUSE starts after that it only needs *Keep still* (a second),
  unless it's worn at a clearly different angle, when it asks for *Tip down* again.
- **Clicks:** the big switch is the left (or right, *Switch* setting) button. **Dwell click:** keep the pointer in
  one small area for the *Dwell click* time to click; move away to click again. A green bar on the screen fills up
  before it clicks.
- **Pause:** hold A, or **hold the switch** for the *Hold to pause* time (2 s): two falling notes = paused, two
  rising = moving. With Hold to pause on, a short press clicks when it's let go; set it Off to drag (the button is
  then held while the switch is).
- It doesn't sleep in MOUSE mode.
- Coming next: training it on the student's own movements (left, right, up and down as far as is comfortable), so
  the pointer and head stay in step at the screen edges, and a tilt (joystick) style for small movements.

### Backup mode: a switch for the AAC device, and a talker when it isn't there

With KEYBOARD's **No USB** set to QUICK or TOPICS, the switch works an AAC device over USB, and when
there's no USB connection (the device isn't there, is flat, or the cable is out) for 10 seconds it changes to
QUICK or TOPICS by itself, with a falling two-note sound and "No USB" on screen, so it can still talk.
While counting down, the KEYBOARD screen says "No USB – QUICK in 8 s". In the backup mode the bottom line starts
with "Backup", and Bluetooth stays off, so it doesn't use extra battery (and the switch can sleep as usual).
When USB is connected again it goes back to KEYBOARD (rising two notes), once any choice being made is
finished. If it's asleep, that happens when it wakes. Choosing a mode with A (or on the setup page) ends the
backup. Fill the Quick messages and topics with what's needed when the AAC device isn't there.

### Stick buttons – one rule everywhere

The bottom line of the screen always shows what the buttons do right now.

| Button | Does |
|---|---|
| **A click** | Next mode (QUICK → TOPICS → KEYBOARD → CONTROL → MOUSE → SETTINGS, skipping modes turned off in *Modes* / *MOUSE mode*). In a mode's settings, an open topic, About or Sensor test: close it |
| **B click** | Next: Quick message / topic / key / IR code / Calibrate (MOUSE), then the mode's **Settings** item, then back to the first. In an open topic: its next message. In settings: the next setting. B itself is silent, to save battery |
| **Hold A** | Do it: QUICK – record the selected Quick message (release to stop; if it already has one, the screen says "Hold A again to replace" – hold A again within 4s to record, B keeps it); TOPICS – open the topic, to look through its messages; CONTROL – learn the selected code (remote 30cm+ from the end of the stick, within 8s); MOUSE – pause / move the pointer, or on **Calibrate** start it; on **Settings** – open them; in settings – next value, or open About / Sensor test |
| **Hold B** | Hear it: QUICK – the selected Quick message; an open topic – the message; CONTROL – the code's sound (without sending it); in settings – back a value |

A hold is 0.6s. When the screen is off, the first press of A or B only wakes it; when it's just dim, presses work
as normal.

### Scanning: choice-making with one switch

With QUICK's *Plays* set to **Choose one** (Press twice or Hold & release) or CONTROL's *IR codes* to **Scan**, and in TOPICS, the switch offers the choices one at a time – each with
its LED colour, its name on screen and a short **spoken prompt** played quietly ("Toast"… "Crisps"… "Yoghurt") –
and one is picked, which then plays in full (or, in CONTROL, is sent).

- **QUICK, Plays: Choose one** – it offers the 4 Quick messages. The simplest start.
- **TOPICS, Choose from: One topic** (default) – it offers the messages in the selected topic (B on the switch, or *Use* on the
  setup page), e.g. "Snack time".
- **TOPICS, Choose from: All topics** – it offers the **topics** first ("Snack time"… "Music"…), then the chosen topic's
  messages and **"Back"** (to return to the topics).
- **Offer Quick / Offer Control / Offer My device** (with All topics) – more choices next to the topics, so TOPICS
  can be the home screen: **Quick** offers the Quick messages and **Control** the IR codes, like a topic's
  messages (then Back), so the user can say a Quick message or turn on the bubble tube or TV themselves; **My
  device** changes the switch to KEYBOARD so they can use their AAC device or computer.
  Once in KEYBOARD every press goes to the device, so the switch can't offer a way back: bring it back with A (or
  the setup page), or with *No USB: TOPICS* it comes back by itself when USB is unplugged.
- **Choosing: Press twice** – a press starts the offers, the next press chooses. With no choice it stops after the
  set number of *Scan rounds*.
- **Choosing: Hold & release** – hold the switch to step through the offers, let go to choose. After choosing a
  topic, hold again for its messages.
- **Stop choice: On** – every scan ends with **"Stop"** (LED red): the topics, a topic's messages (after "Back"),
  and QUICK's and CONTROL's choices. Choosing it ends the scan without doing anything; the next press starts again
  from the top. It lets the user say "none of these". A or B on the stick also stops a scan.

Only topics and messages that are set up are offered, so two messages make a simple two-way choice. Prompts are
made by the setup page from the **names**. A message without a prompt – e.g. one just recorded on the stick – plays
itself quietly as its prompt (cut short by the next offer), so scanning works straight away with no setup page:
record the Quick messages (QUICK, hold A) and set QUICK's *Plays: Choose one*.

### Settings

Each mode has its own settings – press B until the screen shows **Settings**, then hold A to open them – and the
**SETTINGS** mode holds the general ones. In any settings: **B** = next setting, **hold A** = next value, **hold B** = back a value,
**A** = close (or next mode). Changes are saved straight away; settings close by themselves after 30s untouched.
Settings that do nothing with the current choices are left out (e.g. Scan speed when the selected message plays).
All of these are on the setup page too, on each mode's tab and the Settings tab.

| Where | Setting | Choices (default **bold**) | What it does |
|---|---|---|---|
| General | Volume | 1, 2, **3**, 4 | How loud messages play |
| General | Modes | **All**, No KEYBOARD, No CONTROL, Talking only | Turn KEYBOARD and/or CONTROL off for a switch that only talks: A skips them, the setup page hides their tabs, and their settings (and the matching Offer settings) go too. Their key and IR codes are kept |
| General | MOUSE mode | Off, **On** | The MOUSE mode (see above) and its tab on the setup page. Off: A skips it and the tab is hidden |
| General | Press must last | **Instant**, 0.1s, 0.25s, 0.5s, 1s | Filters accidental brushes |
| General | Ignore repeats for | Off, 0.2s, **0.4s**, 0.8s, 1.5s | Filters tremor and bounces after a press |
| General (sensor) | Sensor mode | Line, **Move** | Only with a ToF sensor plugged in – see *Touch-free: the ToF sensor* |
| General (sensor) | Distance | 5, 6, 8, **10**, 15, 20, 30, 50 cm | Line: closer than this counts as a press |
| General (sensor) | Movement | 5, **8**, 10, 15, 20, 30 mm | Move: a movement this big towards the sensor counts as a press |
| General (sensor) | Settle | Off, 1, **2**, 3, 5 s | Move: after being pressed this long, where the finger is becomes the new resting place and it lets go. Off for users who hold presses on purpose |
| General (sensor) | Follow | Slow, **Medium**, Fast | Move: how quickly the resting place follows drift |
| General (sensor) | Ignore beyond | Off, 20, **30**, 50 cm | Anything further away counts as nothing there |
| General (sensor) | Sensor test | hold A | A live graph of the sensor with a press count; presses only beep there. B restarts the count, A closes |
| General | Brightness | Low, **Medium**, High | Screen brightness while in use. Low saves battery |
| General | Press wakes screen | **No**, Yes | Whether presses of the big switch light up the screen |
| General | Sleep after | Off, 2 min, **5 min**, 15 min | On battery, with no presses: sleep to save power. The big switch still works (see below) |
| General | Auto power off | **Never**, 30 min, 60 min, 2 hours | On battery, with no presses: turn off completely, e.g. at the end of the day |
| General | Recording boost | Off, Low, **Medium**, High | Makes recordings made on the switch louder, at the cost of some harshness |
| General | Forget BT devices | hold A, then again to confirm | Clears every paired computer/tablet. Also remove the switch in that device's Bluetooth settings |
| General | About | hold A to open | The firmware version, the switch's name, how much recording storage is used, presses since it was turned on, and the input (switch or ToF sensor). A closes |
| QUICK | Plays | **Selected**, Choose one | *Selected*: plays the selected Quick message (Play style and Hold for next msg apply). *Choose one*: one is chosen – see QUICK's Choosing. The setup page shows Plays and Choosing as one setting |
| QUICK | Choosing | **Press twice**, Hold & release, Count presses | Kept in step with TOPICS and CONTROL's Choosing (change Press twice / Hold & release in one and the other follows); only Count presses is QUICK's own – TOPICS and CONTROL (and the Quick choice inside TOPICS) always scan. *Press twice* / *Hold & release*: scanning, see above (Scan speed, Scan rounds, Stop choice apply). *Count presses*: press 1–4 times quickly for message 1–4 – each press ticks and the LED shows that message's colour; the message plays after the Press gap. Not for users with tremor or accidental double presses |
| QUICK | Press gap | 0.5s, **0.8s**, 1.2s, 1.6s, 2s | Count presses: how long without a press ends the count – longer for users who are slower to release and press again (every message waits this long before playing). Ignore repeats only filters bounces under 0.15s in this mode |
| QUICK | Play style | **Tap**, Hold to play, Latch | *Tap*: a press plays the whole message. *Hold to play*: plays (looping) only while the switch is held – classic cause and effect. *Latch*: one press starts it looping, the next press stops it |
| QUICK | Hold for next msg | Off, 1s, **1.5s**, 2s, 3s | With Tap: how long the switch is held to move to the next Quick message. Turn off if letting go quickly is hard |
| TOPICS | Choose from | **One topic**, All topics | See above |
| TOPICS | Offer Quick | **Off**, On | With All topics: offers "Quick" (the Quick messages) next to the topics, see above |
| TOPICS | Offer Control | **Off**, On | With All topics: offers "Control" (the IR codes) next to the topics |
| TOPICS | Offer My device | **Off**, On | With All topics: offers "My device" (changes to KEYBOARD) next to the topics |
| TOPICS, CONTROL | Choosing | **Press twice**, Hold & release | See above |
| QUICK, TOPICS, CONTROL | Scan speed | 1.5s, 2s, **3s**, 4s, 5s | How long each choice is offered |
| QUICK, TOPICS, CONTROL | Scan rounds | 1, **2**, 3 | Press twice: how many times round before it stops by itself |
| QUICK, TOPICS, CONTROL | Stop choice | **Off**, On | Offers "Stop" last, to choose none of them |
| KEYBOARD | No USB | **Bluetooth**, QUICK, TOPICS | What to do when the switch isn't plugged into a computer or AAC device by USB. *Bluetooth*: send the key over Bluetooth instead. *QUICK* / *TOPICS*: after 10 seconds, talk instead, using that mode – Bluetooth stays off to save battery. Plugging USB back in switches it back to KEYBOARD (see the backup mode above). Only with USB Mode: USB-OTG (TinyUSB) |
| KEYBOARD | Key action | **Momentary**, Latch | *Momentary* holds the key while the switch is held; *Latch* – one press holds the key down, the next lets it go. The screen says "Key held" and the LED glows brighter while latched |
| KEYBOARD, CONTROL, MOUSE | Press sound | **Off**, Click, Beep | A sound on each press, as feedback (CONTROL: only for codes with no sound of their own) |
| CONTROL | IR codes | **Selected**, Repeat held, Scan | See the CONTROL mode above |
| MOUSE | Speed | 1–6 (**4**) | How far the pointer goes per turn. The computer's pointer speed also applies |
| MOUSE | Speed-up | Off, Low, **Medium**, High | Faster turns go further, so slow turns stay precise |
| MOUSE | Steady | Off, **Low**, Medium, High | Ignores turns slower than this – steadies tremor and drift |
| MOUSE | Smoothing | Off, **Low**, Medium, High | Smooths out shakes, with a little lag |
| MOUSE | Dwell click | **Off**, 0.8, 1, 1.5, 2, 3 s | Time in one place to left-click |
| MOUSE | Dwell area | Small, **Medium**, Large | How far the pointer may wander and still count as one place |
| MOUSE | Switch | **Left click**, Right click | The button the big switch presses |
| MOUSE | Hold to pause | Off, 1 s, **2 s**, 3 s | Holding the switch this long pauses / moves the pointer; short presses click when let go. Off: the button is held while the switch is (drag) |
| MOUSE | Flip left/right, Flip up/down | **Off**, On | If the pointer goes the wrong way |

### The screen

- **Top bar:** the mode, in its colour ("SETTINGS" on that colour while its settings are open); on the right, the
  volume (a speaker and 4 bars) and the battery (% and icon; green bolt = charging, red = low).
- **Middle:** three lines, the same on every screen – the main thing (big), its state or what to do (green ready,
  orange a problem, yellow what to do), and details (grey). What the switch will do – the Quick message; in TOPICS the topic B is on and its messages (or the
  choice on offer while scanning); the key and whether it's sending by USB or Bluetooth; or the IR code. When scanning, QUICK and
  CONTROL still show the selected message or code (the one B and hold A work on), with "Scans…" underneath.
- **Bottom:** what the buttons do right now.
- A white frame appears while the big switch is held down.

Recordings are trimmed and boosted automatically, stored in flash, and survive power-off.

**To delete a Quick message:** select it (B in QUICK), then hold A, and hold it again ("Hold A again to replace") for a couple of seconds *without speaking*, and
let go. The screen says "Nothing heard – cleared". Or use ⋯ › Delete on the setup page (topic messages are made there).
Mode, selected Quick message, topic and IR code, key and settings are remembered too.

**Recording tips:** speak 10–15cm from the stick in a normal voice. The recording is cleaned up automatically:
silence trimmed, hiss between words turned down, level evened out.

### Keyboard (USB or Bluetooth)

Keys: **Space, Enter, Up, Down, Left, Right, Left click**, and a custom key or shortcut set on the setup page (B-click to choose). In Grid 3 / Mind Express,
set switch input to the key shown on screen.

**Left click** clicks the left mouse button wherever the pointer is (it doesn't move the pointer). With *Key action:
Latch* it holds the button down until the next press, for dragging. On an iPad the pointer only shows with
AssistiveTouch on.

- **USB:** plug into the computer with a data cable. Nothing to pair, and it charges at the same time.
- **Bluetooth:** the StickS3 appears as **"ChatterSwitch XXXX"** – each stick has its own 4-character ID, shown on the
  KEYBOARD screen, so you can tell switches apart – with any *Switch name* from the setup page in front, e.g.
  "Sam ChatterSwitch 7B70". Pair it from Windows, iPad or Android like a keyboard (it's a keyboard and mouse in
  one, for Left click).
  To move it to a different computer or tablet, use *Forget BT devices* in SETTINGS.
  **Paired before Left click was added (28 Sep 2026)?** Remove the switch in the device's Bluetooth settings, use
  *Forget BT devices* on the stick, and pair again – the device remembers it as keyboard-only.

It picks automatically: when a computer is using it as a USB keyboard, keys go over USB only;
otherwise over Bluetooth. A phone charger doesn't count, so Bluetooth keeps working while charging.
The screen says which is in use.

### Power saving

- The screen dims after 30s and switches off after 2 minutes without a press of A or B. Its brightness while
  in use is the *Brightness* setting (Low saves the most).
  The big switch keeps working with the screen off.
- Bluetooth only runs in KEYBOARD and MOUSE modes (it starts after a second or so in that mode; pairing is remembered).
- **Sleep:** on battery, after the *Sleep after* time with no presses, the StickS3 sleeps – screen, speaker
  and radio off. The switch LED keeps glowing. **The big switch still works:** a press wakes it and does its
  job (a fraction of a second slower than normal). A or B wakes it too.
  It doesn't sleep in KEYBOARD mode over Bluetooth, because the connection would drop, in MOUSE mode, or with the
  ToF sensor, which can't wake it.
- **Auto power off** (off by default): after the chosen time with no presses it beeps three times and shows
  a warning; 30s later it powers off. Press any button to cancel. Press the power button to turn it back on.
- It never sleeps or powers off while plugged into USB.
- A "Low battery" warning appears at 15%.

### Press logging

Every press of the big switch is written to the USB serial port (115200 baud) as
`press,<milliseconds since boot>,<mode>` – handy evidence for assessing
intentional communication. SETTINGS → About (and the setup page's About box) shows how many since it was turned on.

## Tuning in code

Most settings are in the SETTINGS mode above. At the top of `TalkingSwitch.ino`:

| Setting | Default | What it does |
|---|---|---|
| `DEBOUNCE_MS` | 25 | Contact bounce filter |
| `MAX_SECONDS` | 5 | Longest recording per message |
| `MIC_PGA` | 8 | Microphone analogue gain (3dB steps, 0–10). Lower it if loud voices sound distorted |
| `SCREEN_DIM_MS` / `SCREEN_OFF_MS` | 30000 / 120000 | Screen dims, then switches off, when idle |
| `BLE_NAME` | "ChatterSwitch" | Default Bluetooth name; each stick's own ID is added to it |
| `FW_VERSION` / `FW_API` | "30 Sep 2026" / 3 | Version shown in About. Raise `FW_API` (and `PAGE_API` in `docs/index.html`) when the page and firmware must change together |

## Troubleshooting

- **Won't compile, with errors about `io_pin_remap.h`, `pinMode` or `tone` in M5Unified:** the board is set to
  *Arduino Nano ESP32*. Choose **Tools → Board → esp32 → ESP32S3 Dev Module** and the settings above.
- **Big switch does nothing:** with a jack switch, try the other Grove wire (see below). With the Unit Key, swap
  `PIN_KEY` (10) and `PIN_LED` (9) in the code – the Grove wire order can vary. With the ToF sensor, check About
  says *Input: ToF sensor* – the switch only looks for it when it starts, so plug it in, then turn it on.
- **Recordings hiss or distort:** hiss – speak closer and lower Recording boost; distortion on loud voices – lower `MIC_PGA`.
- **IR only works close up:** the StickS3's IR LED is small (it's in the end of the stick). Point that end straight at
  the device – its IR window is usually on the front – ideally within 3–5m. In a dark sensory room, bouncing off a
  white wall or ceiling can help. For longer range, an IR repeater/extender near the device works with any remote.
- **IR learns nothing:** make sure the remote is 38kHz (most TV remotes are) and point it at the StickS3's IR window from **30cm to 1m** – closer than 30cm can scramble the code. Very long air-con codes may be cut short.
- **Quiet playback:** raise *Volume* in SETTINGS (or on the setup page's Settings tab); for a noisy classroom add
  a small external speaker.
- **After an upload the switch shows as "USB JTAG/serial debug unit" and doesn't answer:** it's waiting in download
  mode instead of running the firmware – press the stick's power button once to restart it. It then shows as
  "ESP32S3_DEV – TinyUSB CDC" on a different COM port.
- **The page says the switch didn't answer:** check *USB CDC On Boot: Enabled* and close the Arduino Serial Monitor.
  The Connect list always calls the switch "ESP32S3_DEV – TinyUSB CDC" (the switch's own name is used for Bluetooth).
- **Using a standard 3.5mm switch instead:** cut a Grove cable and wire a 3.5mm mono socket to two of its wires:
  **yellow** (the switch signal, GPIO10) and **black** (ground) – either way round, as a switch just joins them. Cut the
  red (5V) and white wires short and cover them. If it doesn't respond, colours vary between cables: try white instead
  of yellow. The thin wires hold better in screw terminals if twisted and folded back. Not together with the ToF sensor
  (it uses the same wires).

## ToF sensor test (touch-free switch)

`ToFTest/ToFTest.ino` is a separate test sketch for the M5Stack **Unit ToF4M** (VL53L1X) on the Grove port, to try it as
a touch-free switch (a hand, head or foot moving closer than a set distance) and to watch a user's movement. It
needs the **VL53L1X** library by Pololu (Library Manager). Unplug the Unit Key and any switch on the mono jack first –
they share the Grove pins. Two ways to press: **LINE** – closer than a set distance (5–50 cm); **MOVE** – a movement of 5–30 mm towards the
sensor from where the hand or finger rests (best for small movements like a finger). The screen shows a scrolling
graph that zooms to fit, with the trigger line. A = size, hold A = LINE / MOVE, B = reset, **hold B = settings**
(B next, hold A change, A close):

| Setting | Choices | What it does |
|---|---|---|
| Mode | LINE, **MOVE** | How a press is detected |
| Distance | 5, 6, 8, **10**, 15, 20, 30, 50 cm | LINE: press closer than this |
| Movement | 5, **8**, 10, 15, 20, 30 mm | MOVE: press on a movement this big towards the sensor |
| Settle | Off, 1, **2**, 3, 5 s | MOVE: after being pressed this long, where the finger is becomes the new resting place (so it can't stay stuck pressed) |
| Follow | Slow, **Medium**, Fast | MOVE: how quickly the resting place follows drift. Slow catches slow presses; Fast copes with restless movement |
| Ignore beyond | **Off**, 20, 30, 50 cm | Anything further away counts as nothing there – stops people passing by from pressing |
| Field of view | **Wide**, Medium, Narrow | Narrower ignores movement at the sides, but the finger must be right in front |
| Smoothing | **Off**, 3 readings | The middle of the last 3 readings: removes single spikes, ~30 ms later |
| Too close | **Pressed**, Nothing | Closer than the sensor can measure (~4 cm) |
| Range | **Short**, Long | Short: up to ~1.3 m, ~65 readings a second, better in sunlight. Long: ~4 m, slower |

The sensor isn't reliable closer than about 4 cm, and a finger works best 5–10 cm away. Mount it rigidly, keep its
window clean, and point it at a plain background. Readings also go to the serial port for the Serial Plotter – open it
after the stick has started.

## Mouse test (motion sensor as a mouse)

This is now ChatterSwitch's MOUSE mode (above); the test sketch stays for trying new ideas first.
`MouseTest/MouseTest.ino` is a separate test sketch: the StickS3's motion sensor moves the pointer, worn on the head
(headband or cap) or held in the hand. Turn left/right to move left/right, tip down/up to move down/up. It works over
USB, or Bluetooth as "ChatterSwitch XXXX" (the same identity as ChatterSwitch, so a paired device keeps working).

- **Clicks:** the big switch is the left button (held while the switch is, so it drags) or the right button
  (*Switch* setting). **Dwell click:** keep the pointer in one small area for the *Dwell* time to click; move away to
  click again.
- **Calibrate** (hold A; also at the first start): *Keep still*, then *Tip down* – nod down or tip the front down,
  then back. This works whichever way round the stick is worn – calibrate it the way it's worn. Later starts
  only need *Keep still*, unless it's worn at a clearly different angle, when it asks for *Tip down* again.
- **Buttons:** A = pause / move, hold A = calibrate, B = next speed, hold B = settings (B next, hold A change,
  A close).
- The screen shows the turn as a dot in a box (the grey ring is *Steady*). The rates also go to the Serial Plotter.

| Setting | Choices | What it does |
|---|---|---|
| Speed | 1–6 (**4**) | How far the pointer goes per turn. The computer's pointer speed also applies |
| Speed-up | Off, Low, **Medium**, High | Faster turns go further, so slow turns stay precise |
| Steady | Off, **Low**, Medium, High | Ignores turns slower than this – steadies tremor and drift |
| Smoothing | Off, **Low**, Medium, High | Smooths out shakes, with a little lag |
| Dwell click | **Off**, 0.8, 1, 1.5, 2, 3 s | Time in one place to click |
| Dwell area | Small, **Medium**, Large | How far the pointer may wander and still count as one place |
| Switch | **Left click**, Right click | What the big switch does |
| Flip left/right, Flip up/down | **Off**, On | If the pointer goes the wrong way |

## Notes

- Compile-checked against esp32 core 3.3.11 with M5Unified 0.2.23 / M5GFX 0.2.30 and VL53L1X 1.3.1, in both USB
  modes.
- Tested on hardware: the Unit Key and a 3.5mm jack switch, the modes and their settings, the setup page, and the
  ToF sensor (Line and Move), IR (learning and sending), Left click over USB. Not yet tested on hardware: Left click
  over Bluetooth, the USB keyboard, sleep, auto power-off and the backup
  mode.
- This is a DIY device, not a certified AT product. Check it's robust and safe for each user before use.
