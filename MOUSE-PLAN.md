# MOUSE mode – research and plan

This is where the ChatterSwitch MOUSE mode stands and what comes next, so work can pick up from here.
We build it **one step at a time**, testing each step on the stick before starting the next.
New ideas can be tried in `MouseTest/` first.

**Targets:** Windows PCs and iPads (USB, or Bluetooth with the same pairing as KEYBOARD).
**Users:** college; the stick is worn on the head (headband or cap) or on a wrist or hand.

## Status

| Step | What | State |
|---|---|---|
| 1 | MOUSE mode in ChatterSwitch (from MouseTest), hold to pause, setup page tab | **Done, tested 30 Sep 2026** (commits 369e680, a447b3f; on by default) |
| 2 | Training on the user's own movements, speeds set by Calibrate, live picture on the page | **Built 30 Sep–1 Oct 2026, to test** (no turn limit, see below) |
| 3 | Freeze on click, double-click help, ignore jerks, Centre | Planned |
| 4 | Tilt (joystick) style for very small movements | Planned |
| 5 | True screen position (absolute pointer) | Only if 2–4 aren't enough |
| 6 | GAME mode: movement as a thumbstick, for PC and Xbox | Planned, after step 4 |
| – | Head gestures (Quha style) | Idea, see the end |

## What we found (research, 30 Sep 2026)

**The edge problem.** The stick sends "move by this much", like any mouse, so it never knows where the pointer is.
At the edge of the screen the pointer stops but the head keeps turning; turning back moves the pointer straight
away, so "straight ahead" has shifted and the user ends up looking sideways at the screen.
- The commercial gyro head mice (**Quha Zono**, **GlassOuse**) behave the same way. Their manuals present it as the
  way to re-centre: push the pointer into the edge on purpose and keep turning. Fine as a trick, a fault by accident.
- Quha also has head gestures for pause, scroll and **cursor centring**, separate horizontal/vertical speed, and a tremor filter.

**Comfortable movement.** Comfortable head turn is about ±30°, and the maximum is about ±55°. Nodding range is smaller.
People with CP often move further one way than the other. One person testing a head-worn IMU joystick had a sore
neck after 10 minutes, so keep the movement needed small and make pausing easy.
- The current default (Speed 4 = 30 counts/°) needs about ±32° to cross a 1920-pixel screen, right at the comfortable limit.

**Two ways to map movement to the pointer:**
- **Follow (position/velocity of the turn → pointer movement):** what we have now. Fast.
- **Tilt / joystick (angle from rest → pointer speed):** hold a tilt and the pointer keeps going; back to rest
  and it stops. It needs only a few degrees of movement and has no edge problem. It doesn't drift because it uses
  gravity. A RESNA study of 15 wheelchair users (8 with CP) found it slower but clearly **more accurate: 86% vs 69%**.
  A 4-direction, fixed-speed variant exists for users with poor control.

**Wrist use.** The movement is different: a wrist is more likely to roll or bend than turn about the vertical.
So calibration must learn *whatever* two movements the user can make, not assume a head turn.

**True screen position (absolute HID).** The stick could say "pointer at x,y", so head angle maps exactly to
screen position. It works on Windows over USB, but reports over Bluetooth are mixed and iPad support is unclear.
It would also need a new report map, so every device would have to pair again. Last resort.

**Other gaps:** pressing the switch jogs the pointer; spasms throw it; the user needs to pause without help
(talking, resting, looking away).

Sources: [Quha Zono X](https://www.quha.com/products/quha-zono-x/),
[GlassOuse FAQ](https://glassouse.com/faq/),
[GlassOuse V1.4 manual](https://glassouse.com/wp-content/uploads/2022/09/GlassOuse-V1.4-user-manual.pdf),
[RESNA: joystick mouse emulation](https://www.resna.org/sites/default/files/legacy/conference/proceedings/2004/Papers/Research/CAC/Joymouse.html),
[Head-mouse, joystick mode](https://arxiv.org/html/2006.13503v1),
[Hackaday: IMU joysticks (UCPLA)](https://hackaday.io/project/173454-2020-hdp-dream-team-ucpla/log/180274-exploring-alternative-computer-input-devices-imu-joysticks),
[Neck range of motion and head control](https://www.researchgate.net/publication/9036081_Neck_range_of_motion_and_use_of_computer_head_control),
[Heads Will Roll: head pointing](https://dl.acm.org/doi/10.1145/3694907.3765916).

## Decisions made

- Build into ChatterSwitch as MOUSE mode; **on by default** (SETTINGS *MOUSE mode: Off* hides it). Keep MouseTest for experiments.
- Stick buttons follow the usual rule: A = next mode, **hold A = pause/move**, B steps to **Calibrate**, then Settings.
- **Pause with the switch:** *Hold to pause* setting (Off, 1, **2**, 3 s). With it on, a short press
  clicks on release; Off = the button is held while the switch is (drag). One switch input only (Grove GPIO10);
  a second switch on the other Grove wire (GPIO9, the Unit Key LED) is possible later if needed.
- **Training covers all four directions** (right, left, down, up), each as far as is comfortable, so each side
  gets its own range.
- Keep relative mouse reports (no re-pairing).
- **No turn limit or automatic re-centring for now** (decided 30 Sep 2026). The edge is the re-centring, as on the
  Quha and GlassOuse: push into the edge until the head is comfortable, or pause and move back. It also lets people
  with a small or one-sided range "ratchet" across the screen. Add the turn limit later as an option only if testing
  shows people losing the pointer.

## Step 1 – what exists now (TalkingSwitch.ino)

- Mode `M_MOUSE` (between CONTROL and SETTINGS), colour pink, settings group `G_MOUSE`.
- The mouse section after `releaseKey()` has calibration, movement, dwell and switch handling.
  Main functions: `updateMouse()` (called from `loop`), `mouseCalSample()`, `mouseMoveSample()`,
  `mouseSendMovement()`, `mouseDwell()`, `mouseSwitch()`, `mousePause()`, `mouseStartCal()`.
- Calibration: *Keep still* (gyro zero point plus "up" from gravity), then *Tip down*, which learns the up/down turn axis.
  Left/right is always the turn about "up". The axes are saved in prefs `m_up` and `m_down`. Each start only needs
  *Keep still*, unless the stick is tilted more than 25° from last time.
- Settings: `m_speed`, `m_accel`, `m_steady`, `m_smooth`, `m_dwell`, `m_area`, `m_switch`, `m_hold`, `m_flipx`,
  `m_flipy`, plus the shared `s_psound`. `s_mouse` turns the mode on and off.
- Serial commands `MCAL` and `MPAUSE 1|0`. INFO has
  `mouse:{cal, step, paused, imu}`. FW_API / PAGE_API = 3.
- Bluetooth runs in MOUSE too; no light sleep in MOUSE.
- Setup page: MOUSE tab with a Calibrate button (it watches the stick every second while calibrating), a Pause/Move
  button, the connection, and the settings panel.

## Step 2 – training (built; the design as planned, without the turn limit)

**What was built:** the 5-step training below (`MC_REST`, `MC_RIGHT`, `MC_LEFT`, `MC_DOWN`, `MC_UP` in
`mouseCalSample()`); the axes come from Right − Left and Down − Up (up/down made at right angles to left/right),
ranges from each peak (a missed Left or Up copies the other side). Prefs `m_up`, `m_x`, `m_y`, `m_rng` (the old `m_down` is unused, so it asks to calibrate
once). Movement is still "follow". **Speed (changed 1 Oct 2026, after research):** two ordinary settings,
*Speed left/right* (`m_spdx`) and *Speed up/down* (`m_spdy`), in counts per degree (8–100). Calibrate sets them
from the whole span (right+left, down+up) so it crosses about 1920 × 1080 counts, picks the nearest choice, and
shows them ("Speed 6 / 4"); they can be changed after. This is how Enable Viacam does it (Quha also has separate
H/V speeds). No product found sets left and right separately, and head-pointer research puts the best gain well
below a hand mouse's, so the speed must stay adjustable. Per-side speeds were tried and dropped; an optional
*Even out sides* can come later if a strongly one-sided user needs it. Flip settings removed (training learns the
directions). Small wear changes at start turn the axes with "up". Setup page: live picture (`MSTREAM 1|0`,
`mou,<step>,<x×10>,<y×10>,<paused>` lines), INFO `mouse.range`. FW_API / PAGE_API = 4.
The picture's dot is the angle since the last Keep still, pause or `MZERO` (the page's Centre button), so it drifts
slowly (about 0.07°/s measured) and doesn't follow edge pushes. FW_API / PAGE_API = 6 since the two speed settings.
Stream checked on the stick 30 Sep 2026: right, left, down, up all come out the right way.

The original design (turn limit parts not built):

**Training routine** (replaces Keep still + Tip down; the stick talks you through it; also started from the page):
1. **Rest** – sit comfortably, keep still. Learns the gyro zero point, gravity, and the **rest position = centre**
   (which need not be straight ahead).
2. **Right** as far as is comfortable, and back → the left/right axis (whatever movement it is: head turn, wrist
   roll…) and the right range (the peak angle).
3. **Left**, and back → the left range (and confirms the axis).
4. **Down**, and back → the up/down axis and the down range.
5. **Up**, and back → the up range.

For each step: it starts when the movement passes a small threshold, records the peak angle along the axis,
and finishes when the stick is back near rest and still. If nothing happens within about 10 s, it says so and
uses a small default range, or keeps the old one. Beeps and the screen show each step.
Save the axes, the ranges and the rest position in prefs.

**Turn limit (fixes the edge problem):** keep an angle from centre on each axis. The pointer only moves while that
angle is inside the trained range. Past the range, the pointer waits until the user comes back inside.
- Map each side's range to slightly **more** than half the screen (about 1.2×). Then reaching the limit always
  pins the pointer to that screen edge, and **every visit to an edge puts head and pointer back in step**. This
  works even with Windows/iPad pointer acceleration.
- **Speed** becomes "how much of the screen the trained range covers" (e.g. Reach: 80/100/120%), so each side
  gets its own gain from its own range. Speed-up may need to be off, or limited, in this style.
- **Drift:** up/down can be corrected from gravity (a complementary filter) when the axis is a tilt. Left/right
  relies on the zero-point correction plus the edge re-sync.
- **Centre:** make "where I am now" the new straight ahead. Idea to test: resuming from pause keeps the pointer
  where it is and takes the current position as matching it.

**Setup page:** the training driven from the page, with a live picture of the movement and the learnt ranges
(the stick streams angles, like the ToF sensor's `SENSE` stream).

**To test:** on the head and on a wrist; Windows and iPad; asymmetric range (pretend one side is stiff);
turning past the edge and back should no longer leave the user looking sideways.

## Step 3 – filters

- **Freeze on click:** ignore movement for a moment (about 0.3 s) when the switch is pressed, so the press doesn't jog the pointer.
- **Double-click help** (Quha's "double-click assistant"): after a click the pointer stays still for a short time
  (setting, e.g. Off / 0.5 / 1 s), so a second click lands in the same place. It doesn't double-click by itself.
  Could share a setting with freeze on click.
- **Ignore jerks:** a sudden movement faster than a limit (a spasm) is ignored.
- **Centre:** put the pointer in the middle of the screen. The stick doesn't know where the pointer is, so: push it
  hard into the top-left corner, then move it half of `MS_SPAN_X` / `MS_SPAN_Y` (the computer's pointer speed
  makes this only roughly the middle, like Quha's "central area"). Started by a switch action (e.g. a choice in
  *Hold to pause*: Pause / Centre) or from the page.
- Up/down and left/right speed are separate already (step 2).

## Step 4 – tilt (joystick) style

A *Pointer style* setting: **Follow** (steps 1–2) or **Tilt**.
- Tilt: angle from the trained rest position → pointer speed, with a dead zone around rest (sized from the Rest
  step's wobble) and a top speed. It uses the accelerometer, so it doesn't drift.
- Option: 4 directions only, at a fixed speed, for poor control.
- It needs a steady rest position, so there is "set rest position" (the Rest step).

## Step 5 – true screen position (only if needed)

An absolute pointer report, USB first. It needs a changed report map, so paired devices would have to pair again,
and iPad support must be checked. Try it in MouseTest first.

## Step 6 – GAME mode (thumbstick, PC and Xbox)

A new mode (like MOUSE, hidden unless turned on): head or hand movement is a **thumbstick**, the switch is a button.
Goal: games on a **Windows PC** and on an **Xbox** (through the Xbox Adaptive Controller).

**Why movement suits a thumbstick:** a thumbstick reports a *position* (centre to fully pushed), like a head
angle; a mouse reports movement. So angle from rest → stick position, and back at rest = centred. No edge problem.
- **Calibrate is the same as MOUSE:** Keep still = centre; Right, Left, Down, Up = full push each way. Here the
  per-side ranges *are* used (dropped for MOUSE), so a stiff side still reaches full push.
- **Dead zone** around rest (sized from the Keep still wobble) and a **response curve** (fine near the middle,
  full at the comfortable limit). Settings: dead zone, curve, which stick (left / right), the switch's button.
- **Drift:** nod and wrist tilt are corrected from gravity (accelerometer). A head turn is gyro only (about
  0.07°/s measured), so: slowly re-centre while still inside the dead zone, plus Centre on a pause / switch hold.
- Mostly the same code as step 4 (tilt style), so do step 4 first.

**PC:** a USB HID joystick/gamepad works in Windows with no driver (DirectInput). Games that only take Xbox
controllers (XInput) need Steam Input or similar to map it. Bluetooth gamepad possible later, but it changes the
Bluetooth report map, so paired devices would pair again.

**Xbox:** an Xbox only accepts controllers with Microsoft's security chip, so not directly. The **Xbox Adaptive
Controller (XAC)** accepts USB HID joysticks on its USB ports: X,Y axes → left stick, Z,RZ → right stick, hat →
d-pad, buttons → XAC buttons. Copilot lets it share one player with another controller.
Known from open-source ESP32-S3 XAC joysticks (one is for M5Stack):
- the HID report must be laid out the way XAC expects (signed vs unsigned axes: unsigned has centre at 127,127);
- XAC firmware June 2024 or newer;
- XAC's USB ports give at most 100 mA unless the XAC has its 5 V 2 A supply (the stick charges from USB, so
  check the current, or run it on battery);
- **to check:** whether XAC accepts our composite USB device (keyboard + mouse + serial + gamepad) or needs a
  joystick-only USB device. If joystick-only, GAME mode restarts the stick's USB as a joystick (no setup page
  over USB while in GAME mode).

**Plan:** a `GameTest/` sketch first (USB joystick from the stick's movement; test on PC, then XAC), then GAME
mode in ChatterSwitch.

Sources: [ESP32 gamepad for XAC (esp32beans)](https://github.com/esp32beans/ESP32_gamepad),
[M5Stack USB joystick (esp32beans)](https://github.com/esp32beans/M5Stack_Touch_USB_Joystick),
[dinput_tinyusb for XAC](https://github.com/controllercustom/dinput_tinyusb),
[Arduino forum: joystick for XAC](https://forum.arduino.cc/t/joystick-for-xbox-adaptive-controller/1325039).

## Idea for later – head gestures (Quha Zono 2)

From the [Quha Zono 2 manual](https://www.zyteq.com.au/uploads/PDF/Access/Quha-Zono-2-User-Manual-v1.0-English.pdf):
every gesture starts with holding still for about a second, then a quick back-and-forth, e.g. **pause** = right,
left, right, left; **exit pause** = right, left. Gestures for scroll and centring too.
- The pointer does move during a gesture; back-and-forth ends where it started. We could also undo the wiggle:
  the stick knows the movement it sent since the still second, so once a gesture is recognised it sends the
  opposite and the pointer is back exactly.
- **Doubts:** quick back-and-forth needs good control (hard for many people with CP), and jerky movement could
  trigger it by accident. Hold to pause already covers pausing. So: optional and off by default, and only if
  users who can't use the switch to pause need it.

## Hardware and build notes

- StickS3 IMU: BMI270 (6-axis, no compass), so left/right is the gyro only. M5Unified `M5.Imu`, `cfg.internal_imu = true`.
- IDE Tools: ESP32S3 Dev Module, **USB CDC On Boot: Enabled**, USB Mode: USB-OTG (TinyUSB), 8MB, Huge APP, OPI PSRAM.
  The IDE keeps these between sketches. If they're wrong, the build fails or USB mouse/serial goes missing.
- Page and firmware go together: raise FW_API and PAGE_API when the serial protocol or INFO changes.
