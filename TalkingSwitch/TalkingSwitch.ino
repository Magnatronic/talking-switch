// =====================================================================
//  TalkingSwitch - M5StickS3 + M5Stack Unit Key accessibility switch
// ---------------------------------------------------------------------
//  One big switch, three jobs:
//    SPEAK     Plays one recorded message (BIGmack style). Keep holding
//              the switch to move on to the next message. Play style
//              setting: Tap / Hold to play / Latch.
//    KEYBOARD  Acts as a USB or Bluetooth keyboard key, e.g. Space/Enter
//              for Grid 3 or Mind Express switch access. Key goes down on
//              press and up on release, so dwell and hold-to-scan settings
//              in the AAC software still work. When a computer is using it
//              as a USB keyboard, keys go over USB only; otherwise they go
//              over Bluetooth. (Never both, so no double presses.)
//    IR        Sends the selected learned infrared code (TV, fibre optics,
//              bubble tube), and plays that code's own sound if it has one
//              (e.g. "Bubbles!"). Hold to play: repeats the code while held.
//
//  There are 4 messages and, separately, 4 IR codes, each with an
//  optional name (and each IR code an optional sound).
//    SETTINGS  Staff settings: B = change, hold A = next setting.
//              The big switch keeps doing the previous mode's job.
//
//  Staff controls on the StickS3 (when the screen is dim or off, the
//  first press only wakes it):
//    Button A (KEY1) click ....... next mode, SETTINGS last
//    Button A (KEY1) hold ........ SPEAK: record a message while held
//                                  IR: learn the selected code
//    Button B (KEY2) click ....... SPEAK: choose message   IR: choose code
//                                  KEYBOARD: choose key
//    Button B (KEY2) hold ........ volume (4 levels)
//
//  Power saving: the screen dims, then switches off, when idle. Bluetooth
//  only runs in KEYBOARD mode. On battery, after the "Sleep after" time
//  with no presses, it sleeps (except in Bluetooth KEYBOARD mode); the
//  big switch still works and wakes it. Optionally it can also power off
//  completely after a longer time (press the power button to restart).
//
//  Switch access settings (press must last, ignore repeats) help students with
//  tremor or accidental presses. Every student activation is logged
//  on the USB serial port as "press,<ms since boot>,<mode>".
//
//  Hardware
//    - M5StickS3 (ESP32-S3), M5Unified library
//    - M5Stack Unit Key (U144) on the StickS3's Grove port
//      Grove white wire  -> GPIO10 -> key   (PIN_KEY)
//      Grove yellow wire -> GPIO9  -> LED   (PIN_LED)
//      If the key does nothing, swap PIN_KEY and PIN_LED.
//    - A standard 3.5mm AT switch can be wired to PIN_KEY and GND
//      instead of (or alongside) the Unit Key.
//
//  Arduino IDE settings: see README.md next to this folder.
// =====================================================================

#include <M5Unified.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_mac.h>
#if defined(CONFIG_NIMBLE_ENABLED)
#include "host/ble_hs.h"
#else
#include <BLESecurity.h>
#endif

#if ARDUINO_USB_MODE == 0  // "USB Mode: USB-OTG (TinyUSB)" selected
#include "USB.h"
#include "USBHIDKeyboard.h"
#define HAS_USB_HID 1
USBHIDKeyboard UsbKeyboard;
#else
#define HAS_USB_HID 0
#endif

// ---------------------------------------------------------------------
//  Settings you may want to change
// ---------------------------------------------------------------------
static const int PIN_KEY   = 10;   // Unit Key switch signal (Grove)
static const int PIN_LED   = 9;    // Unit Key SK6812 LED data (Grove)
static const int PIN_IR_TX = 46;   // StickS3 IR transmitter
static const int PIN_IR_RX = 42;   // StickS3 IR receiver

static const uint32_t DEBOUNCE_MS = 25;    // ignore contact bounce
static const uint32_t LONG_REPEAT_MS = 2000; // SPEAK long press: keep moving on every this long while held

static const uint32_t SAMPLE_RATE = 16000; // recording sample rate (Hz)
static const uint32_t MAX_SECONDS = 10;    // longest message per slot
static const int      NUM_SLOTS   = 4;     // messages, and separately IR codes
static const int      NUM_SOUNDS  = NUM_SLOTS * 2;  // sounds 0-3: messages, 4-7: the IR codes' sounds
static const uint8_t  MIC_PGA     = 8;     // mic analogue gain, 3dB steps (0-10). Lower if loud voices distort

static const char*    BLE_NAME      = "Talking Switch"; // Bluetooth name; the last 4 characters of the
                                                        // stick's address are added, e.g. "Talking Switch 7B70"
static const uint8_t  VOLUMES[]     = {128, 180, 220, 255}; // output power is volume squared: ~25/50/75/100%
static const uint8_t  SPK_GAIN      = 8;   // speaker magnification: 8 = volume 4 is exactly full scale
                                           // (M5Unified's StickS3 default of 1 is 18dB quieter)
static const int      IR_REPEATS    = 2;   // extra copies of the IR code (helps at the edge of range)
static const float    IR_DUTY       = 0.5f; // carrier duty: 0.5 = most energy per pulse (remotes often use 0.33)
static const uint32_t IR_REPEAT_MS  = 110; // IR mode, Hold to play: resend this often while held
static const uint32_t STATUS_MS     = 2500; // how long status messages stay on screen

// Power saving
static const uint8_t  SCREEN_BRIGHT = 128;    // normal screen brightness (0-255)
static const uint8_t  SCREEN_DIMMED = 16;     // faint glow when idle
static const uint32_t SCREEN_DIM_MS = 30000;  // dim the screen after this long without a staff button press
static const uint32_t SCREEN_OFF_MS = 120000; // then switch it off completely
static const uint32_t SETTINGS_EXIT_MS = 30000; // SETTINGS goes back to the previous mode after this long untouched
static const int      LOW_BATTERY   = 15;     // warn below this battery %

// ---------------------------------------------------------------------
//  Modes and keys
// ---------------------------------------------------------------------
enum Mode : uint8_t { M_SPEAK, M_KEYBOARD, M_IR, M_SETTINGS, M_COUNT };
static const char* MODE_NAMES[M_COUNT] = {"SPEAK", "KEYBOARD", "IR", "SETTINGS"};
// Mode colours for the switch LED (dim idle glow)
static const uint8_t MODE_RGB[M_COUNT][3] = {
  {0, 40, 0}, {30, 0, 40}, {40, 20, 0}, {0, 0, 0}
};
// SPEAK and IR: the LED shows which message / code is selected
static const uint8_t SLOT_RGB[NUM_SLOTS][3] = {{0, 40, 0}, {0, 25, 40}, {30, 0, 40}, {40, 20, 0}};
// Mode colours for the screen header, and whether it needs dark text
static const uint8_t SCREEN_RGB[M_COUNT][3] = {
  {0, 160, 70}, {140, 60, 220}, {240, 130, 0}, {90, 90, 90}
};
static const bool HEADER_DARK_TEXT[M_COUNT] = {false, false, true, false};

enum KbOut : uint8_t { OUT_NONE, OUT_BLE, OUT_USB };  // where keyboard presses go
enum PlayStyle : uint8_t { PLAY_TAP, PLAY_HOLD, PLAY_LATCH };

enum Arrow : uint8_t { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT };
struct KeyDef { const char* name; uint8_t usage; Arrow arrow; };
static const KeyDef KEYS[] = {
  {"SPACE", 0x2C, A_NONE}, {"ENTER", 0x28, A_NONE},
  {"UP",    0x52, A_UP},   {"DOWN",  0x51, A_DOWN},
  {"LEFT",  0x50, A_LEFT}, {"RIGHT", 0x4F, A_RIGHT}
};
static const int NUM_KEYS = sizeof(KEYS) / sizeof(KEYS[0]);

// ---------------------------------------------------------------------
//  Staff settings - changed in SETTINGS mode on the StickS3
//  and remembered after power-off. Each has a fixed list of choices.
// ---------------------------------------------------------------------
enum SettingId : uint8_t { S_PLAY, S_HOLD, S_ACCEPT, S_LOCKOUT, S_WAKE, S_SLEEP, S_AUTOOFF, S_LOUD, S_KEY_ACTION, S_PRESS_SOUND, S_FORGET, S_COUNT };
struct Setting {
  const char* name;       // shown on screen
  const char* key;        // Preferences key (nullptr = an action, not a stored value)
  uint8_t count, def;     // number of choices, default choice
  uint32_t values[6];
  const char* labels[6];
};
static const Setting SETTINGS[S_COUNT] = {
  // SPEAK: Tap = a press plays the whole message; Hold to play =
  // plays (looping) only while the switch is held; Latch = one press starts it
  // looping, the next stops it
  {"Play style",          "s_play",    3, 0, {0, 1, 2}, {"Tap", "Hold to play", "Latch"}},
  // SPEAK, Tap play style: keep holding the switch this long to move to the next message
  {"Hold for next msg",   "s_hold",    5, 2, {0, 1000, 1500, 2000, 3000}, {"Off", "1 s", "1.5 s", "2 s", "3 s"}},
  // press must be held this long to count - filters accidental brushes
  {"Press must last",     "s_accept",  5, 0, {0, 100, 250, 500, 1000}, {"Instant", "0.1 s", "0.25 s", "0.5 s", "1 s"}},
  // ignore new presses this soon after the last one - filters tremor/repeats
  {"Ignore repeats for",  "s_lockout", 5, 2, {0, 200, 400, 800, 1500}, {"Off", "0.2 s", "0.4 s", "0.8 s", "1.5 s"}},
  {"Switch wakes screen", "s_wake",    2, 0, {0, 1}, {"No", "Yes"}},
  // on battery, sleep after this many idle minutes; the big switch still works
  {"Sleep after",         "s_sleep",   4, 2, {0, 2, 5, 15}, {"Off", "2 min", "5 min", "15 min"}},
  // on battery, power off completely after this many idle minutes
  {"Auto power off",      "s_autooff", 4, 0, {0, 30, 60, 120}, {"Never", "30 min", "60 min", "2 hours"}},
  // soft-clip compression of new recordings (value / 10); louder but harsher
  {"Recording boost",     "s_loud",    4, 2, {10, 15, 20, 30}, {"Off", "Low", "Medium", "High"}},
  // KEYBOARD: Momentary = key held while the switch is held; Latch = one press
  // holds the key down, the next press lets it go
  {"Key action",          "s_keyact",  2, 0, {0, 1}, {"Momentary", "Latch"}},
  // KEYBOARD and IR modes: a sound on each student press (they're silent otherwise)
  {"Press sound",         "s_psound",  3, 0, {0, 1, 2}, {"Off", "Click", "Beep"}},
  // action: B twice clears all remembered Bluetooth devices
  {"Forget BT devices",   nullptr,     1, 0, {0}, {"B: forget all"}},
};
uint8_t settingChoice[S_COUNT];
uint32_t setting(SettingId id) { return SETTINGS[id].values[settingChoice[id]]; }

// ---------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------
Preferences prefs;
uint8_t mode = M_SPEAK;
uint8_t slot = 0;        // selected message
uint8_t irSlot = 0;      // selected IR code
int irSound(int i) { return NUM_SLOTS + i; }  // sound number of IR code i's sound
uint8_t keyIdx = 0;      // selected keyboard key (NUM_KEYS = the custom key)
// Custom key/shortcut set from the setup page, e.g. Win+H.
// Modifier bits: 1 Ctrl, 2 Shift, 4 Alt, 8 Win (HID left-hand modifiers).
uint8_t customMods = 0, customUsage = 0;
String  customName;
int keyCount() { return NUM_KEYS + (customUsage ? 1 : 0); }
bool keyIsCustom() { return keyIdx >= NUM_KEYS; }
String keyName() { return keyIsCustom() ? customName : String(KEYS[keyIdx].name); }
uint8_t volIdx = 2;      // selected volume
uint32_t pressCount = 0; // student activations since boot
KbOut    keyDownOut = OUT_NONE; // where the current key-down went (the key-up goes there too)
bool     keyLatched = false;    // Latch key action: key is being held down
bool     playLatched = false;   // Latch play style: message is looping

static const size_t MAX_SAMPLES = SAMPLE_RATE * MAX_SECONDS;
int16_t* slotBuf[NUM_SOUNDS];  // sounds: messages then IR-code sounds
size_t   slotLen[NUM_SOUNDS];

static const size_t IR_MAX = 192;
rmt_data_t irCode[NUM_SLOTS][IR_MAX];  // one learned IR code per slot
size_t irLen[NUM_SLOTS];

// Student switch
bool     swRaw = false, swStable = false, swPending = false, swActive = false;
uint32_t swRawChange = 0, swPressStart = 0, swLastActivation = 0, swLastAdvance = 0;

// Screen and power
enum ScreenState : uint8_t { SCR_ON, SCR_DIM, SCR_OFF };
ScreenState scr = SCR_ON;
uint32_t lastInteraction = 0;  // last staff button press (screen timeout)
uint32_t lastActivity = 0;     // last press of anything (auto power-off)
bool     offWarned = false;
int      batLevel = -1;
bool     batCharging = false;
bool     usbPower = false;
bool     lowBatWarned = false;

bool     needRedraw = true;
uint32_t ledFlashUntil = 0;
String   slotName[NUM_SOUNDS]; // optional names: messages 0-3, IR codes 4-7 (setup page)
String   statusMsg = "";
uint16_t statusColor = TFT_YELLOW;
uint32_t statusUntil = 0;
float    recProgress = -1;     // 0..1 while recording, else -1
uint8_t  prevMode = M_SPEAK;   // in SETTINGS, the big switch keeps doing this mode's job
uint8_t  settingIdx = 0;       // setting shown in SETTINGS mode
uint32_t forgetConfirmUntil = 0; // "Forget BT devices": waiting for the second B press

M5Canvas canvas(&M5.Display);
bool canvasOk = false;

void drawScreen();  // defined in the Display section

// ---------------------------------------------------------------------
//  LED
// ---------------------------------------------------------------------
// What the big switch does: in SETTINGS it carries on with the previous mode.
uint8_t studentMode() { return mode == M_SETTINGS ? prevMode : mode; }

void ledShow(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_LED, r, g, b); }
void ledIdle() {
  uint8_t m = studentMode();
  const uint8_t* c = m == M_SPEAK ? SLOT_RGB[slot] : m == M_IR ? SLOT_RGB[irSlot] : MODE_RGB[m];
  int k = (keyLatched || playLatched) ? 3 : 1;  // brighter while something is latched on
  ledShow(c[0] * k, c[1] * k, c[2] * k);
}

// ---------------------------------------------------------------------
//  Storage
// ---------------------------------------------------------------------
String slotPath(int i) {
  return i < NUM_SLOTS ? String("/msg") + i + ".raw" : String("/irs") + (i - NUM_SLOTS) + ".raw";
}

void saveSettings() {
  prefs.putUChar("mode2", studentMode());  // never save SETTINGS as the mode
  prefs.putUChar("slot", slot);
  prefs.putUChar("irslot", irSlot);
  prefs.putUChar("key", keyIdx);
  prefs.putUChar("vol", volIdx);
}

void loadSettings() {
  if (prefs.isKey("mode2")) {
    mode = prefs.getUChar("mode2", M_SPEAK) % M_COUNT;
  } else {
    // older firmware's modes were SPEAK, STEPS, KEYBOARD, IR, SPEAK+IR
    static const uint8_t OLD[] = {M_SPEAK, M_SPEAK, M_KEYBOARD, M_IR, M_IR};
    mode = OLD[prefs.getUChar("mode", 0) % 5];
  }
  if (mode == M_SETTINGS) mode = M_SPEAK;
  slot   = prefs.getUChar("slot", 0) % NUM_SLOTS;
  irSlot = prefs.getUChar("irslot", 0) % NUM_SLOTS;
  customMods  = prefs.getUChar("ck_mod", 0);
  customUsage = prefs.getUChar("ck_use", 0);
  customName  = prefs.getString("ck_name", "");
  keyIdx = prefs.getUChar("key", 0) % keyCount();
  volIdx = prefs.getUChar("vol", 2) % sizeof(VOLUMES);
  for (int i = 0; i < NUM_SOUNDS; i++) slotName[i] = prefs.getString(("name" + String(i)).c_str(), "");
  for (int i = 0; i < S_COUNT; i++) {
    if (!SETTINGS[i].key) { settingChoice[i] = 0; continue; }
    settingChoice[i] = prefs.getUChar(SETTINGS[i].key, SETTINGS[i].def);
    if (settingChoice[i] >= SETTINGS[i].count) settingChoice[i] = SETTINGS[i].def;
  }
}

void loadSlots() {
  for (int i = 0; i < NUM_SOUNDS; i++) {
    slotLen[i] = 0;
    File f = LittleFS.open(slotPath(i), "r");
    if (!f) continue;
    size_t n = min((size_t)f.size() / 2, MAX_SAMPLES);
    slotLen[i] = f.read((uint8_t*)slotBuf[i], n * 2) / 2;
    f.close();
  }
}

bool saveSlot(int i) {
  File f = LittleFS.open(slotPath(i), "w");
  if (!f) return false;
  size_t w = f.write((uint8_t*)slotBuf[i], slotLen[i] * 2);
  f.close();
  return w == slotLen[i] * 2;
}

String irPath(int i) { return String("/ir") + i + ".bin"; }

void loadIr() {
  // older firmware kept one code in /ir.bin: it becomes slot 1's
  if (LittleFS.exists("/ir.bin") && !LittleFS.exists(irPath(0))) LittleFS.rename("/ir.bin", irPath(0));
  for (int i = 0; i < NUM_SLOTS; i++) {
    irLen[i] = 0;
    File f = LittleFS.open(irPath(i), "r");
    if (!f) continue;
    size_t n = min((size_t)f.size() / sizeof(rmt_data_t), IR_MAX);
    irLen[i] = f.read((uint8_t*)irCode[i], n * sizeof(rmt_data_t)) / sizeof(rmt_data_t);
    f.close();
  }
}

void saveIr(int i) {
  File f = LittleFS.open(irPath(i), "w");
  if (!f) return;
  f.write((uint8_t*)irCode[i], irLen[i] * sizeof(rmt_data_t));
  f.close();
}

void deleteIr(int i) {
  irLen[i] = 0;
  LittleFS.remove(irPath(i));
}

// ---------------------------------------------------------------------
//  Audio
// ---------------------------------------------------------------------
// Status messages may have two lines separated by '\n'.
void setStatus(const String& s, uint16_t color = TFT_YELLOW, uint32_t ms = STATUS_MS) {
  statusMsg = s;
  statusColor = color;
  statusUntil = millis() + ms;
  needRedraw = true;
}

void beep(uint16_t freq, uint32_t ms) {
  M5.Speaker.tone(freq, ms);
  delay(ms + 20);
}

float blockRms(const int16_t* p, size_t len) {
  float sum = 0;
  for (size_t k = 0; k < len; k++) sum += (float)p[k] * p[k];
  return sqrtf(sum / len);
}

// Trim silence at the start and end, then boost quiet recordings.
void tidyRecording(int i) {
  int16_t* b = slotBuf[i];
  size_t n = slotLen[i];
  if (n == 0) return;
  // Remove bass the small speaker can't reproduce, so the level goes into
  // sound it can play (two one-pole high-pass filters, ~200Hz)
  for (int pass = 0; pass < 2; pass++) {
    float px = b[0], py = 0;
    for (size_t k = 0; k < n; k++) {
      float y = 0.927f * (py + b[k] - px);
      px = b[k];
      py = y;
      b[k] = (int16_t)y;
    }
  }
  const int16_t gate = 600;
  size_t start = 0, end = n;
  while (start < n && abs(b[start]) < gate) start++;
  while (end > start && abs(b[end - 1]) < gate) end--;
  if (end <= start) { slotLen[i] = 0; return; }
  // keep a little breathing room either side
  start = start > SAMPLE_RATE / 20 ? start - SAMPLE_RATE / 20 : 0;
  end = min(n, (size_t)(end + SAMPLE_RATE / 10));
  memmove(b, b + start, (end - start) * 2);
  n = end - start;
  int32_t peak = 1;
  for (size_t k = 0; k < n; k++) peak = max(peak, (int32_t)abs(b[k]));
  float gain = min(16.0f, 28000.0f / peak);
  if (gain > 1.05f)
    for (size_t k = 0; k < n; k++) b[k] = (int16_t)constrain(b[k] * gain, -32767.0f, 32767.0f);
  // Noise gate: turn down 10ms blocks much quieter than the speech, so the
  // hiss in pauses isn't lifted by the compression below
  const size_t BLK = SAMPLE_RATE / 100;
  float maxRms = 1;
  for (size_t s0 = 0; s0 < n; s0 += BLK) maxRms = max(maxRms, blockRms(b + s0, min(BLK, n - s0)));
  float g = 1;
  for (size_t s0 = 0; s0 < n; s0 += BLK) {
    size_t len = min(BLK, n - s0);
    float target = blockRms(b + s0, len) < maxRms * 0.08f ? 0.15f : 1.0f;
    for (size_t k = 0; k < len; k++) {
      g += (target - g) * (target > g ? 0.02f : 0.002f);  // open fast, close gently
      b[s0 + k] = (int16_t)(b[s0 + k] * g);
    }
  }
  // Soft-clip compression: lifts the quieter parts of speech towards full scale
  const float loud = setting(S_LOUD) / 10.0f;
  if (loud > 1.0f) {
    const float norm = 32767.0f / tanhf(loud);
    for (size_t k = 0; k < n; k++) b[k] = (int16_t)(norm * tanhf(b[k] * (loud / 32767.0f)));
  }
  // Gentle treble cut (~4kHz) to take the harsh edge off hiss and "s" sounds
  float y = b[0];
  for (size_t k = 0; k < n; k++) { y += 0.79f * (b[k] - y); b[k] = (int16_t)y; }
  slotLen[i] = n;
}

void playSlot(int i) {
  if (slotLen[i] == 0) { beep(300, 150); return; }
  M5.Speaker.playRaw(slotBuf[i], slotLen[i], SAMPLE_RATE, false, 1, 0, true);
}

void playSlotLoop(int i) {
  if (slotLen[i] == 0) { beep(300, 150); return; }
  M5.Speaker.playRaw(slotBuf[i], slotLen[i], SAMPLE_RATE, false, ~0u, 0, true);  // ~0u = forever
}

// Stops a looping/latched message (mode or slot changes, etc.).
void stopPlay() {
  if (playLatched) { playLatched = false; ledIdle(); }
  M5.Speaker.stop(0);
}

// Records into slot i for as long as Button A stays held.
void recordSlot(int i) {
  stopPlay();
  recProgress = 0;
  setStatus("Recording msg " + String(i + 1) + "\nRelease A to stop", TFT_RED, 60000);
  drawScreen();
  ledShow(80, 0, 0);
  beep(1500, 80);
  while (M5.Speaker.isPlaying()) delay(1);
  M5.Speaker.end();
  M5.Mic.begin();
  // M5Unified sets the ES8311 codec to minimum analogue gain and maximum
  // digital gain, which amplifies hiss. Swap that round.
  M5.In_I2C.writeRegister8(0x18, 0x14, 0x10 | MIC_PGA, 100000);  // Mic1 input, PGA gain
  M5.In_I2C.writeRegister8(0x18, 0x17, 0xBF, 100000);            // ADC digital volume 0dB

  const size_t CHUNK = 512;
  size_t pos = 0;
  uint32_t lastDraw = millis();
  while (pos + CHUNK <= MAX_SAMPLES) {
    M5.Mic.record(slotBuf[i] + pos, CHUNK, SAMPLE_RATE);
    pos += CHUNK;
    M5.update();
    if (!M5.BtnA.isPressed()) break;
    if (millis() - lastDraw > 250) {  // time-left bar
      lastDraw = millis();
      recProgress = (float)pos / MAX_SAMPLES;
      drawScreen();
    }
  }
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUMES[volIdx]);
  recProgress = -1;

  slotLen[i] = pos;
  tidyRecording(i);
  if (slotLen[i] == 0) {
    LittleFS.remove(slotPath(i));
    setStatus("Nothing heard\nMsg " + String(i + 1) + " cleared");
  } else {
    bool ok = saveSlot(i);
    setStatus(ok ? "Saved msg " + String(i + 1) : String("Save failed!"), ok ? TFT_GREEN : TFT_RED);
    playSlot(i);
  }
  ledIdle();
}

// Whether a slot has something for the current mode to use.
bool slotUsed(int i) {
  switch (studentMode()) {
    case M_IR: return irLen[i];
    default: return slotLen[i];
  }
}

// Next used slot after 'from' (wrapping round), or -1 if none.
int nextUsedSlot(int from) {
  for (int i = 1; i <= NUM_SLOTS; i++) {
    int s = (from + i) % NUM_SLOTS;
    if (slotUsed(s)) return s;
  }
  return -1;
}

// "Bubbles", or "Message 2" / "IR code 2" when it has no name (sound number).
String titleOf(int snd) {
  if (slotName[snd].length()) return slotName[snd];
  return snd < NUM_SLOTS ? "Message " + String(snd + 1) : "IR code " + String(snd - NUM_SLOTS + 1);
}

// Audio cue when a message / code is chosen: its sound, or a short tone.
void slotCue(int i) {
  if (slotLen[i]) playSlot(i);
  else M5.Speaker.tone(1200, 60);
}

// ---------------------------------------------------------------------
//  Keyboard output: USB when a computer is using us, else Bluetooth
// ---------------------------------------------------------------------
static const uint8_t HID_REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,  // Keyboard, report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,  // modifiers
  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,              // reserved byte
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,  // LEDs
  0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73,  // 6 keys
  0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
  0xC0
};

BLEHIDDevice* hid = nullptr;
BLECharacteristic* kbInput = nullptr;
volatile bool bleConnected = false;
String bleName;  // BLE_NAME plus this stick's ID, set in setup()
volatile bool bleRunning = false;  // Bluetooth is only switched on in KEYBOARD mode

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true; needRedraw = true; }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    needRedraw = true;
    if (bleRunning) BLEDevice::startAdvertising();
  }
};

void bleBegin() {
  BLEDevice::init(bleName.c_str());
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  hid = new BLEHIDDevice(server);
  kbInput = hid->inputReport(1);
  hid->manufacturer()->setValue("DIY AT");
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  // Bond with the host so it reconnects automatically after power cycles
#if defined(CONFIG_NIMBLE_ENABLED)
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#else
  BLESecurity* security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);
#endif
  hid->reportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
  hid->startServices();
  hid->setBatteryLevel(100);
  BLEAdvertising* adv = server->getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->start();
  bleRunning = true;
  needRedraw = true;
}

void bleEnd() {
  if (keyDownOut == OUT_BLE) releaseKey();
  bleRunning = false;           // first, so the disconnect callback doesn't re-advertise
  BLEDevice::deinit(false);     // false: keeps it possible to start again later
  hid = nullptr;                // owned by the deleted server
  kbInput = nullptr;
  bleConnected = false;
  needRedraw = true;
}

// Clear every remembered Bluetooth device (NimBLE keeps them in NVS).
// If Bluetooth is on it's stopped first; manageBle() restarts it.
void forgetBluetooth() {
  if (bleRunning) bleEnd();
  Preferences bonds;
  if (bonds.begin("nimble_bond", false)) {
    bonds.clear();
    bonds.end();
  }
}

// Start Bluetooth once KEYBOARD mode has been selected for a moment (so
// clicking past it doesn't), and stop it when leaving. Pairing is kept.
void manageBle(uint32_t now) {
  static uint32_t keyboardSince = 0;
  if (studentMode() != M_KEYBOARD) {
    keyboardSince = 0;
    if (bleRunning && !swActive) bleEnd();
    return;
  }
  if (!keyboardSince) keyboardSince = now;
  if (!bleRunning && now - keyboardSince >= 1500) bleBegin();
}

// USB counts as active only if a computer has set us up as a USB device,
// the bus isn't suspended (cable pulled or computer asleep - the S3 isn't
// told about unplugging, but the bus going quiet shows up as a suspend),
// and the power chip still sees 5V on the USB socket.
volatile bool usbSuspended = false;
bool usbVbus = true;

#if HAS_USB_HID
void onUsbEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (id == ARDUINO_USB_SUSPEND_EVENT) usbSuspended = true;
  else if (id == ARDUINO_USB_RESUME_EVENT || id == ARDUINO_USB_STARTED_EVENT) usbSuspended = false;
  needRedraw = true;
}
#endif

bool usbHostActive() {
#if HAS_USB_HID
  return (bool)USB && !usbSuspended && usbVbus;
#else
  return false;
#endif
}

// Called often: reads the USB 5V from the power chip while USB looks active.
void checkVbus(uint32_t now) {
#if HAS_USB_HID
  static uint32_t last = 0;
  if (!(bool)USB || now - last < 300) return;
  last = now;
  int16_t mv = M5.Power.getVBUSVoltage();
  usbVbus = mv < 0 || mv > 4000;  // < 0: not readable on this board, so don't rely on it
#endif
}

KbOut kbOutput() {
  if (usbHostActive()) return OUT_USB;
  return bleConnected ? OUT_BLE : OUT_NONE;
}

// The key-up always goes to wherever the key-down went.
uint8_t keyDownUsage = 0, keyDownMods = 0;

void sendKey(bool down) {
  if (down) {
    keyDownOut = kbOutput();
    keyDownUsage = keyIsCustom() ? customUsage : KEYS[keyIdx].usage;
    keyDownMods = keyIsCustom() ? customMods : 0;
  }
  if (keyDownOut == OUT_BLE && kbInput) {
    uint8_t report[8] = {(uint8_t)(down ? keyDownMods : 0), 0, (uint8_t)(down ? keyDownUsage : 0), 0, 0, 0, 0, 0};
    kbInput->setValue(report, sizeof(report));
    kbInput->notify();
  }
#if HAS_USB_HID
  if (keyDownOut == OUT_USB) {
    // modifiers are HID usages 0xE0 + bit number
    if (down) {
      for (int b = 0; b < 8; b++) if (keyDownMods & (1 << b)) UsbKeyboard.pressRaw(0xE0 + b);
      UsbKeyboard.pressRaw(keyDownUsage);
    } else {
      UsbKeyboard.releaseRaw(keyDownUsage);
      for (int b = 0; b < 8; b++) if (keyDownMods & (1 << b)) UsbKeyboard.releaseRaw(0xE0 + b);
    }
  }
#endif
  if (!down) keyDownOut = OUT_NONE;
}

// Let go of the key whatever state it's in (mode/key changes, Bluetooth off).
void releaseKey() {
  keyLatched = false;
  sendKey(false);
}

// ---------------------------------------------------------------------
//  Infrared learn and send (raw timings, works with most remotes)
// ---------------------------------------------------------------------
bool irLearn(int slotNo, uint32_t timeoutMs) {
  if (!rmtInit(PIN_IR_RX, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, 1000000)) return false;
  rmtSetRxMinThreshold(PIN_IR_RX, 2);       // ignore glitches under 2us
  rmtSetRxMaxThreshold(PIN_IR_RX, 12000);   // 12ms of silence ends the code
  rmt_data_t buf[IR_MAX];
  size_t n = IR_MAX;
  bool ok = rmtRead(PIN_IR_RX, buf, &n, timeoutMs);
  rmtDeinit(PIN_IR_RX);
  if (!ok) return false;
  // Reject noise: a real remote code has several pulses of 100us or more.
  size_t real = 0;
  for (size_t k = 0; k < n; k++) if (buf[k].duration0 >= 100) real++;
  Serial.printf("ir,learn,%u symbols,%u real", (unsigned)n, (unsigned)real);
  for (size_t k = 0; k < min(n, (size_t)4); k++) Serial.printf(",%u/%u", buf[k].duration0, buf[k].duration1);
  Serial.println();
  if (n < 8 || real < 6) return false;
  // The receiver output is active-low; invert so "1" means carrier on.
  for (size_t k = 0; k < n; k++) {
    buf[k].level0 = !buf[k].level0;
    buf[k].level1 = !buf[k].level1;
  }
  memcpy(irCode[slotNo], buf, n * sizeof(rmt_data_t));
  irLen[slotNo] = n;
  saveIr(slotNo);
  return true;
}

// Sends a slot's IR code plus 'repeats' extra copies. False if it has none.
bool irSend(int i, int repeats) {
  if (!irLen[i]) return false;
  for (int r = 0; r <= repeats; r++) {
    rmtWrite(PIN_IR_TX, irCode[i], irLen[i], 500);
    if (r < repeats) delay(40);
  }
  return true;
}

// Learn into slot i with on-screen prompts (staff button or setup page).
bool learnIr(int i) {
  setStatus("Point remote here\nfrom 30cm+, press button", TFT_ORANGE, 10000);
  drawScreen();
  ledShow(80, 40, 0);
  M5.Speaker.end();  // StickS3: IR receive doesn't work while the speaker amp is on
  bool ok = irLearn(i, 8000);
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUMES[volIdx]);
  if (ok) setStatus("IR code learned\n" + titleOf(irSound(i)), TFT_GREEN);
  else setStatus("No IR code heard\nHold remote 30cm-1m away", TFT_RED);
  if (ok) beep(2000, 80); else beep(300, 150);
  ledIdle();
  return ok;
}

// ---------------------------------------------------------------------
//  Display (240x135, drawn off-screen then pushed in one go)
// ---------------------------------------------------------------------
static const int HEADER_H = 34;
static const int FOOTER_Y = 113;

uint16_t rgb(const uint8_t c[3]) { return M5.Display.color565(c[0], c[1], c[2]); }

void drawArrow(lgfx::LovyanGFX& c, Arrow a, int cx, int cy, int s, uint16_t col) {
  switch (a) {
    case A_UP:    c.fillTriangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, col); break;
    case A_DOWN:  c.fillTriangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, col); break;
    case A_LEFT:  c.fillTriangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, col); break;
    case A_RIGHT: c.fillTriangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, col); break;
    default: break;
  }
}

// Battery pill in the top-right corner: "78%" and an icon.
void drawBattery(lgfx::LovyanGFX& c) {
  const int W = c.width();
  c.fillRoundRect(W - 84, 5, 80, HEADER_H - 10, 6, TFT_BLACK);
  const int bw = 26, bh = 13, bx = W - 38, by = HEADER_H / 2 - bh / 2;
  c.drawRoundRect(bx, by, bw, bh, 3, TFT_WHITE);
  c.fillRect(bx + bw, by + 4, 3, bh - 8, TFT_WHITE);
  c.setFont(&fonts::FreeSansBold9pt7b);
  c.setTextDatum(middle_right);
  if (batLevel < 0) {
    c.setTextColor(TFT_LIGHTGREY);
    c.drawString("--", bx - 5, HEADER_H / 2);
    return;
  }
  bool low = batLevel <= LOW_BATTERY && !batCharging;
  uint16_t fill = batCharging ? TFT_GREEN : low ? TFT_RED : TFT_WHITE;
  c.fillRect(bx + 2, by + 2, max(1, (bw - 4) * batLevel / 100), bh - 4, fill);
  if (batCharging) {  // lightning bolt
    int mx = bx + bw / 2, my = by + bh / 2;
    c.fillTriangle(mx + 2, by + 1, mx - 5, my + 1, mx, my + 1, TFT_BLACK);
    c.fillTriangle(mx - 2, by + bh - 1, mx + 5, my - 1, mx, my - 1, TFT_BLACK);
  }
  c.setTextColor(low ? TFT_RED : TFT_WHITE);
  c.drawString(String(batLevel) + "%", bx - 5, HEADER_H / 2);
}

// Large centred text, stepping down the font size until it fits the width.
void drawFit(lgfx::LovyanGFX& c, const String& s, int y, uint16_t col) {
  const lgfx::IFont* fonts[] = {&fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b, &fonts::FreeSansBold9pt7b};
  for (auto f : fonts) {
    c.setFont(f);
    if (c.textWidth(s) <= c.width() - 12 || f == fonts[2]) break;
  }
  c.setTextColor(col);
  c.setTextDatum(middle_center);
  c.drawString(s, c.width() / 2, y);
}

void drawCentered(lgfx::LovyanGFX& c, const String& s, int y, const lgfx::IFont* font, uint16_t col) {
  c.setFont(font);
  c.setTextColor(col);
  c.setTextDatum(middle_center);
  c.drawString(s, c.width() / 2, y);
}

void drawMain(lgfx::LovyanGFX& c) {
  const int W = c.width();
  const uint16_t hint = TFT_LIGHTGREY;
  uint16_t accent = rgb(SCREEN_RGB[mode]);
  switch (mode) {
    case M_SPEAK: {
      drawFit(c, titleOf(slot), 60, TFT_WHITE);
      String l = slotLen[slot] ? String(slotLen[slot] / (float)SAMPLE_RATE, 1) + " s recorded"
                               : String("Empty - hold A to record");
      if (playLatched) drawCentered(c, "Playing - press to stop", 94, &fonts::FreeSansBold9pt7b, TFT_YELLOW);
      else drawCentered(c, l, 94, &fonts::FreeSans9pt7b, slotLen[slot] ? TFT_GREEN : hint);
      break;
    }
    case M_KEYBOARD: {
      String name = keyName();
      Arrow arrow = keyIsCustom() ? A_NONE : KEYS[keyIdx].arrow;
      const lgfx::IFont* fonts[] = {&fonts::FreeSansBold24pt7b, &fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b};
      int tw = 0;
      for (auto f : fonts) {
        c.setFont(f);
        tw = c.textWidth(name) + (arrow ? 34 : 0);
        if (tw <= W - 12) break;
      }
      int x = W / 2 - tw / 2;
      if (arrow) { drawArrow(c, arrow, x + 12, 56, 12, accent); x += 34; }
      c.setTextColor(TFT_WHITE);
      c.setTextDatum(middle_left);
      c.drawString(name, x, 56);
      KbOut o = kbOutput();
      if (keyLatched) drawCentered(c, "Key held - press to let go", 91, &fonts::FreeSansBold9pt7b, TFT_YELLOW);
      else drawCentered(c, o == OUT_USB ? "Sending by USB" : o == OUT_BLE ? "Sending by Bluetooth" : "Not connected",
                        91, &fonts::FreeSans9pt7b, o == OUT_NONE ? TFT_ORANGE : TFT_GREEN);
      drawCentered(c, "BT name: " + bleName, 106, &fonts::Font2, TFT_LIGHTGREY);
      break;
    }
    case M_IR: {
      int snd = irSound(irSlot);
      drawFit(c, titleOf(snd), 54, TFT_WHITE);
      drawCentered(c, !irLen[irSlot] ? "No IR code" : slotLen[snd] ? "Code ready + sound" : "Code ready", 84,
                   &fonts::FreeSans9pt7b, irLen[irSlot] ? TFT_GREEN : TFT_ORANGE);
      drawCentered(c, irLen[irSlot] ? "B: next code   Hold A: re-learn" : "Hold A, then press the remote", 104,
                   &fonts::Font2, hint);
      break;
    }
  }
}

void drawStatus(lgfx::LovyanGFX& c) {
  const int W = c.width();
  c.fillRoundRect(6, HEADER_H + 5, W - 12, FOOTER_Y - HEADER_H - 10, 8, statusColor);
  uint16_t fg = (statusColor == TFT_RED) ? TFT_WHITE : TFT_BLACK;
  int nl = statusMsg.indexOf('\n');
  String l1 = nl < 0 ? statusMsg : statusMsg.substring(0, nl);
  String l2 = nl < 0 ? "" : statusMsg.substring(nl + 1);
  int mid = (HEADER_H + FOOTER_Y) / 2;
  bool bar = recProgress >= 0;
  int y1 = l2.length() ? mid - 12 : mid;
  if (bar) y1 -= 8;
  c.setFont(&fonts::FreeSansBold12pt7b);
  if (c.textWidth(l1) > W - 24) c.setFont(&fonts::FreeSansBold9pt7b);
  c.setTextColor(fg);
  c.setTextDatum(middle_center);
  c.drawString(l1, W / 2, y1);
  if (l2.length()) drawCentered(c, l2, y1 + 24, &fonts::FreeSans9pt7b, fg);
  if (bar) {  // recording time used
    int by = y1 + (l2.length() ? 42 : 22), bw = W - 40;
    c.drawRect(20, by, bw, 8, fg);
    c.fillRect(20, by, (int)(bw * recProgress), 8, fg);
  }
}

void drawSettings(lgfx::LovyanGFX& c) {
  const Setting& st = SETTINGS[settingIdx];
  drawCentered(c, st.name, 54, &fonts::FreeSansBold12pt7b, TFT_WHITE);
  bool confirm = settingIdx == S_FORGET && forgetConfirmUntil;
  drawCentered(c, confirm ? "Press B again" : st.labels[settingChoice[settingIdx]], 86,
               &fonts::FreeSansBold18pt7b, confirm ? TFT_ORANGE : TFT_YELLOW);
  drawCentered(c, String(settingIdx + 1) + "/" + String(S_COUNT) + "   Switch does: " + MODE_NAMES[prevMode],
               106, &fonts::Font2, TFT_DARKGREY);
}

void drawFooter(lgfx::LovyanGFX& c) {
  const int W = c.width(), H = c.height();
  const int y = (FOOTER_Y + H) / 2 + 1;
  c.drawFastHLine(0, FOOTER_Y, W, TFT_DARKGREY);
  if (mode == M_SETTINGS) {
    drawCentered(c, "B: change   Hold A: next   A: exit", y, &fonts::Font2, TFT_LIGHTGREY);
    return;
  }
  // volume bars
  for (int i = 0; i < (int)sizeof(VOLUMES); i++) {
    int h = 4 + i * 3;
    int x = 8 + i * 6;
    if (i <= volIdx) c.fillRect(x, y + 7 - h, 4, h, TFT_WHITE);
    else c.drawRect(x, y + 7 - h, 4, h, TFT_DARKGREY);
  }
  c.setFont(&fonts::Font2);
  c.setTextDatum(middle_left);
  int x = 44;
#if HAS_USB_HID
  c.setTextColor(usbHostActive() ? TFT_GREEN : TFT_DARKGREY);
  c.drawString("USB", x, y);
  x += c.textWidth("USB") + 8;
#endif
  c.setTextColor(bleConnected ? TFT_GREEN : TFT_DARKGREY);
  c.drawString("BT", x, y);
  c.setTextColor(TFT_LIGHTGREY);
  c.setTextDatum(middle_right);
  c.drawString("Presses " + String(pressCount), W - 6, y);
}

void drawScreen() {
  lgfx::LovyanGFX& c = canvasOk ? (lgfx::LovyanGFX&)canvas : (lgfx::LovyanGFX&)M5.Display;
  const int W = c.width(), H = c.height();
  c.startWrite();
  c.fillScreen(TFT_BLACK);

  // Header: mode name on the mode colour, battery on the right
  c.fillRect(0, 0, W, HEADER_H, rgb(SCREEN_RGB[mode]));
  c.setFont(&fonts::FreeSansBold12pt7b);
  c.setTextColor(HEADER_DARK_TEXT[mode] ? TFT_BLACK : TFT_WHITE);
  c.setTextDatum(middle_left);
  c.drawString(MODE_NAMES[mode], 8, HEADER_H / 2 + 1);
  drawBattery(c);

  if (statusMsg.length() && millis() < statusUntil) drawStatus(c);
  else if (mode == M_SETTINGS) drawSettings(c);
  else drawMain(c);
  drawFooter(c);

  // White frame while the student switch is held down
  if (swStable)
    for (int i = 0; i < 3; i++) c.drawRect(i, i, W - 2 * i, H - 2 * i, TFT_WHITE);

  c.endWrite();
  if (canvasOk) canvas.pushSprite(0, 0);
  needRedraw = false;
}

// ---------------------------------------------------------------------
//  Power saving
// ---------------------------------------------------------------------
// Returns true if the screen was dim or off (so the press only wakes it).
bool wakeScreen() {
  lastInteraction = millis();
  if (scr == SCR_ON) return false;
  if (scr == SCR_OFF) M5.Display.wakeup();
  M5.Display.setBrightness(SCREEN_BRIGHT);
  scr = SCR_ON;
  needRedraw = true;
  return true;
}

void markActivity() {
  lastActivity = millis();
  if (offWarned) { offWarned = false; statusMsg = ""; needRedraw = true; }
}

void readBattery() {
  int lvl = M5.Power.getBatteryLevel();
  bool chg = M5.Power.isCharging() == m5::Power_Class::is_charging;
  bool usb = chg || M5.Power.getVBUSVoltage() > 4000 || usbHostActive();
  if (lvl != batLevel || chg != batCharging || usb != usbPower) needRedraw = true;
  batLevel = lvl;
  batCharging = chg;
  usbPower = usb;
  if (batLevel >= 0 && batLevel <= LOW_BATTERY && !usbPower) {
    if (!lowBatWarned) {
      lowBatWarned = true;
      wakeScreen();
      setStatus("Low battery\nPlease charge me", TFT_ORANGE, 8000);
    }
  } else if (usbPower || batLevel > LOW_BATTERY + 5) {
    lowBatWarned = false;
  }
}

void powerDown() {
  if (swActive) { swActive = false; sendKey(false); }
  saveSettings();
  wakeScreen();
  setStatus("Turning off\nPower button to restart", TFT_ORANGE, 10000);
  drawScreen();
  beep(1200, 120); beep(900, 120); beep(600, 200);
  ledShow(0, 0, 0);
  delay(500);
  M5.Power.powerOff();
}

// Light sleep: screen, speaker and radio off, processor paused. The big
// switch (or A/B) wakes it and a switch press still does its job; the
// switch LED keeps its glow. Not used with Bluetooth on (it would drop
// the connection) or when plugged into USB.
bool canSleep(uint32_t idle) {
  uint32_t mins = setting(S_SLEEP);
  if (!mins || idle < mins * 60000UL) return false;
  if (bleRunning || studentMode() == M_KEYBOARD) return false;
  if (swStable || swActive || M5.BtnA.isPressed() || M5.BtnB.isPressed()) return false;
  if (M5.Speaker.isPlaying()) return false;
  return true;
}

void lightSleep(uint32_t idle) {
  if (mode == M_SETTINGS) { mode = prevMode; needRedraw = true; }
  if (scr != SCR_OFF) {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    scr = SCR_OFF;
  }
  M5.Speaker.end();  // codec and amplifier off
  ledIdle();         // the LED holds its colour while we sleep

  // switch, button A, button B: all read low when pressed
  const gpio_num_t pins[] = {(gpio_num_t)PIN_KEY, GPIO_NUM_11, GPIO_NUM_12};
  for (auto p : pins) gpio_wakeup_enable(p, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  // wake in time to give the auto power-off warning
  uint32_t offMin = setting(S_AUTOOFF);
  if (offMin) {
    uint32_t warnAt = offMin * 60000UL - 30000;
    esp_sleep_enable_timer_wakeup((uint64_t)(warnAt > idle ? warnAt - idle : 1000) * 1000ULL);
  }

  esp_light_sleep_start();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (auto p : pins) gpio_wakeup_disable(p);
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUMES[volIdx]);
}

void managePower(uint32_t now) {
  if (scr == SCR_ON && now - lastInteraction > SCREEN_DIM_MS) {
    M5.Display.setBrightness(SCREEN_DIMMED);
    scr = SCR_DIM;
  }
  if (scr == SCR_DIM && now - lastInteraction > SCREEN_OFF_MS) {
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    scr = SCR_OFF;
  }

  if (usbPower) { lastActivity = now; return; }  // plugged in: never sleep or auto-off
  uint32_t idle = now - lastActivity;
  const uint32_t offMin = setting(S_AUTOOFF), warn = 30000;
  if (offMin) {
    const uint32_t limit = offMin * 60000UL;
    if (idle >= limit) {
      powerDown();
    } else if (idle >= limit - warn && !offWarned) {
      offWarned = true;
      wakeScreen();
      setStatus("Turning off soon\nPress any button", TFT_ORANGE, warn);
      drawScreen();
      beep(900, 100); beep(900, 100); beep(900, 100);
    }
  }
  if (!offWarned && canSleep(idle)) lightSleep(idle);
}

// ---------------------------------------------------------------------
//  Student switch actions
// ---------------------------------------------------------------------
void onActivate() {
  pressCount++;
  markActivity();
  if (setting(S_WAKE)) wakeScreen();
  Serial.printf("press,%lu,%s\n", (unsigned long)millis(), MODE_NAMES[studentMode()]);
  ledShow(90, 90, 90);
  ledFlashUntil = millis() + 250;

  const uint8_t style = setting(S_PLAY);
  switch (studentMode()) {
    case M_SPEAK:
      if (style == PLAY_LATCH) {
        playLatched = !playLatched;
        if (playLatched) playSlotLoop(slot); else M5.Speaker.stop(0);
      } else if (style == PLAY_HOLD) {
        playSlotLoop(slot);
      } else {
        playSlot(slot);
      }
      break;
    case M_KEYBOARD:
      if (setting(S_KEY_ACTION) == 1) {  // latch: each press toggles the key
        keyLatched = !keyLatched;
        sendKey(keyLatched);
      } else {
        sendKey(true);
      }
      pressSound();
      break;
    case M_IR:
      if (!irSend(irSlot, IR_REPEATS)) { beep(300, 150); break; }
      if (slotLen[irSound(irSlot)]) playSlot(irSound(irSlot));  // the code's own sound, if any
      else pressSound();
      break;
  }
  needRedraw = true;
}

// Long press (SPEAK, IR): move to the next message / code and play its
// sound as a cue. IR is only sent by a real press.
void advanceMessage() {
  bool ir = studentMode() == M_IR;
  uint8_t& sel = ir ? irSlot : slot;
  int s = nextUsedSlot(sel);
  if (s < 0 || s == sel) return;  // nothing else to move to
  sel = s;
  saveSettings();
  slotCue(ir ? irSound(sel) : sel);
  ledShow(SLOT_RGB[sel][0] * 2, SLOT_RGB[sel][1] * 2, SLOT_RGB[sel][2] * 2);
  ledFlashUntil = millis() + 500;
  needRedraw = true;
}

// Optional feedback sound for the silent modes. Non-blocking.
void pressSound() {
  switch (setting(S_PRESS_SOUND)) {
    case 1: M5.Speaker.tone(3000, 15); break;
    case 2: M5.Speaker.tone(1000, 80); break;
  }
}

void onDeactivate() {
  if (studentMode() == M_KEYBOARD && !keyLatched) sendKey(false);
  // Hold to play: letting go stops the message
  if (setting(S_PLAY) == PLAY_HOLD && studentMode() != M_KEYBOARD && studentMode() != M_IR) M5.Speaker.stop(0);
}

void pollStudentSwitch() {
  uint32_t now = millis();
  bool raw = digitalRead(PIN_KEY) == LOW;
  if (raw != swRaw) { swRaw = raw; swRawChange = now; }
  if (now - swRawChange >= DEBOUNCE_MS && swRaw != swStable) {
    swStable = swRaw;
    needRedraw = true;
    if (swStable) {
      swPressStart = now;
      swPending = true;
    } else {
      swPending = false;
      if (swActive) { swActive = false; onDeactivate(); }
    }
  }
  if (swPending && swStable && now - swPressStart >= setting(S_ACCEPT)) {
    swPending = false;
    if (now - swLastActivation >= setting(S_LOCKOUT)) {
      swLastActivation = swLastAdvance = now;
      swActive = true;
      onActivate();
    }
  }
  // Still holding in SPEAK: step through the other messages
  uint32_t longPress = setting(S_HOLD);
  uint8_t m = studentMode();
  if (swActive && longPress && setting(S_PLAY) == PLAY_TAP && (m == M_SPEAK || m == M_IR)) {
    uint32_t wait = swLastAdvance == swLastActivation ? longPress : LONG_REPEAT_MS;
    if (now - swLastAdvance >= wait) { swLastAdvance = now; advanceMessage(); }
  }
  // IR, Hold to play: keep sending while held, like holding a remote button
  static uint32_t lastIrRepeat = 0;
  if (swActive && m == M_IR && setting(S_PLAY) == PLAY_HOLD && irLen[irSlot]
      && now - swLastActivation >= 400 && now - lastIrRepeat >= IR_REPEAT_MS) {
    lastIrRepeat = now;
    irSend(irSlot, 0);
  }
}

// ---------------------------------------------------------------------
//  Staff buttons
// ---------------------------------------------------------------------
// SETTINGS mode: hold A = next setting, B = next choice (saved straight away)
void settingsButtons() {
  if (M5.BtnA.wasHold()) { settingIdx = (settingIdx + 1) % S_COUNT; forgetConfirmUntil = 0; }
  if (M5.BtnB.wasClicked() && settingIdx == S_FORGET) {
    if (forgetConfirmUntil && millis() < forgetConfirmUntil) {
      forgetConfirmUntil = 0;
      forgetBluetooth();
      setStatus("Forgotten\nAlso remove it on the computer", TFT_GREEN, 5000);
    } else {
      forgetConfirmUntil = millis() + 4000;
    }
  } else if (M5.BtnB.wasClicked()) {
    if (settingIdx == S_KEY_ACTION) releaseKey();
    if (settingIdx == S_PLAY) stopPlay();
    uint8_t& ch = settingChoice[settingIdx];
    ch = (ch + 1) % SETTINGS[settingIdx].count;
    prefs.putUChar(SETTINGS[settingIdx].key, ch);
  }
  needRedraw = true;
}

void pollStaffButtons() {
  bool any = M5.BtnA.wasHold() || M5.BtnA.wasClicked()
          || M5.BtnB.wasHold() || M5.BtnB.wasClicked();
  if (!any) return;
  markActivity();
  if (wakeScreen()) return;  // screen was dim/off: this press only wakes it
  if (mode == M_SETTINGS && (M5.BtnA.wasHold() || M5.BtnB.wasClicked())) { settingsButtons(); return; }

  if (M5.BtnA.wasHold()) {
    if (mode == M_IR) learnIr(irSlot);
    else if (mode == M_SPEAK) recordSlot(slot);
    lastInteraction = millis();
    return;
  }
  if (M5.BtnA.wasClicked()) {  // not wasSingleClicked: that waits to rule out a double-click
    if (swActive) { swActive = false; onDeactivate(); }
    releaseKey();
    stopPlay();
    if (mode != M_SETTINGS) prevMode = mode;
    mode = (mode + 1) % M_COUNT;
    if (mode == M_SETTINGS) settingIdx = 0;
    saveSettings();
    ledIdle();
    needRedraw = true;
  }
  if (M5.BtnB.wasHold()) {
    volIdx = (volIdx + 1) % sizeof(VOLUMES);
    M5.Speaker.setVolume(VOLUMES[volIdx]);
    beep(1000, 100);
    saveSettings();
    needRedraw = true;
    return;
  }
  if (M5.BtnB.wasClicked()) {
    switch (mode) {
      case M_KEYBOARD: releaseKey(); keyIdx = (keyIdx + 1) % keyCount(); break;
      case M_IR: irSlot = (irSlot + 1) % NUM_SLOTS; slotCue(irSound(irSlot)); ledIdle(); break;
      default: stopPlay(); slot = (slot + 1) % NUM_SLOTS; slotCue(slot); ledIdle(); break;
    }
    saveSettings();
    needRedraw = true;
  }
}

// ---------------------------------------------------------------------
//  USB setup page (setup.html in Chrome/Edge talks to us over the USB
//  serial port). One text command per line; replies start with '@'.
//  Audio goes as raw 16-bit little-endian mono samples at SAMPLE_RATE.
//    INFO                 -> @INFO {json}
//    MODE n / SLOT n / IRSLOT n / KEY n / VOL n / SET <key> <choice>
//    PLAY n / STOP / DEL n / NAME n <text> / FORGET
//      (sound numbers n: 0-3 messages, 4-7 the IR codes' sounds and names)
//    IRLEARN n (waits up to 8s for a remote) / IRSEND n / IRDEL n  (n 0-3)
//    CKEY <mods> <usage> <label>  (custom key; usage 0 removes it)
//    UP n <samples>       -> @READY, then the samples in 4KB pieces, each
//                            answered with @A; finally @OK
//    DOWN n               -> @DATA n <samples>, the samples, then @OK
//  Anything else on the port (e.g. "press,..." lines) is just logging.
// ---------------------------------------------------------------------
String jsonStr(const String& v) {
  String o = "\"";
  for (char ch : v) {
    if (ch == '"' || ch == '\\') o += '\\';
    if ((uint8_t)ch >= 32) o += ch;
  }
  return o + "\"";
}

void sendInfo() {
  String j = "@INFO {\"name\":" + jsonStr(bleName);
  j += ",\"rate\":" + String(SAMPLE_RATE) + ",\"maxSec\":" + String(MAX_SECONDS);
  j += ",\"mode\":" + String(studentMode()) + ",\"slot\":" + String(slot) + ",\"irSlot\":" + String(irSlot);
  j += ",\"key\":" + String(keyIdx) + ",\"vol\":" + String(volIdx) + ",\"vols\":" + String(sizeof(VOLUMES));
  j += ",\"bat\":" + String(batLevel) + ",\"chg\":" + String(batCharging ? "true" : "false");
  j += ",\"bt\":" + String(bleConnected ? "true" : "false");
  j += ",\"modes\":[";
  for (int i = 0; i < M_SETTINGS; i++) j += (i ? "," : "") + jsonStr(MODE_NAMES[i]);
  j += "],\"keys\":[";
  for (int i = 0; i < NUM_KEYS; i++) j += (i ? "," : "") + jsonStr(KEYS[i].name);
  if (customUsage) j += "," + jsonStr(customName);
  j += "],\"ckey\":{\"mods\":" + String(customMods) + ",\"usage\":" + String(customUsage) + ",\"name\":" + jsonStr(customName) + "}";
  j += ",\"slots\":[";
  for (int i = 0; i < NUM_SLOTS; i++)
    j += String(i ? "," : "") + "{\"len\":" + String(slotLen[i]) + ",\"name\":" + jsonStr(slotName[i]) + "}";
  j += "],\"irs\":[";
  for (int i = 0; i < NUM_SLOTS; i++)
    j += String(i ? "," : "") + "{\"ir\":" + String(irLen[i] ? "true" : "false") + ",\"len\":" + String(slotLen[irSound(i)])
         + ",\"name\":" + jsonStr(slotName[irSound(i)]) + "}";
  j += "],\"settings\":[";
  bool first = true;
  for (int i = 0; i < S_COUNT; i++) {
    const Setting& st = SETTINGS[i];
    if (!st.key) continue;  // actions have their own buttons on the page
    j += String(first ? "" : ",") + "{\"key\":" + jsonStr(st.key) + ",\"name\":" + jsonStr(st.name);
    j += ",\"cur\":" + String(settingChoice[i]) + ",\"choices\":[";
    for (int k = 0; k < st.count; k++) j += (k ? "," : "") + jsonStr(st.labels[k]);
    j += "]}";
    first = false;
  }
  j += "]}";
  Serial.println(j);
}

void reply(const char* err = nullptr) {
  if (err) { Serial.print("@ERR "); Serial.println(err); }
  else Serial.println("@OK");
}

// Receives a recording from the page straight into a slot.
void receiveSlot(int i, size_t samples) {
  if (samples == 0 || samples > MAX_SAMPLES) return reply("bad length");
  stopPlay();
  M5.Speaker.stop();
  Serial.println("@READY");
  // The page sends 4KB at a time and waits for "@A" before the next, so
  // the USB receive buffer can never overflow.
  const size_t ACK_EVERY = 4096;
  uint8_t* dst = (uint8_t*)slotBuf[i];
  size_t want = samples * 2, got = 0;
  uint32_t last = millis();
  while (got < want && millis() - last < 3000) {
    int a = Serial.available();
    if (a > 0) {
      size_t chunkEnd = min(want, (got / ACK_EVERY + 1) * ACK_EVERY);
      got += Serial.readBytes(dst + got, min((size_t)a, chunkEnd - got));
      if (got == chunkEnd) Serial.println("@A");
      last = millis();
    } else {
      delay(1);
    }
  }
  if (got < want) {
    loadSlots();  // put back what was there
    return reply("timeout");
  }
  slotLen[i] = samples;
  reply(saveSlot(i) ? nullptr : "save failed");
}

void sendSlot(int i) {
  Serial.printf("@DATA %d %u\n", i, (unsigned)slotLen[i]);
  const uint8_t* src = (const uint8_t*)slotBuf[i];
  size_t left = slotLen[i] * 2;
  uint32_t last = millis();
  while (left && millis() - last < 3000) {
    size_t w = Serial.write(src, min(left, (size_t)512));
    if (w) { src += w; left -= w; last = millis(); }
    else delay(1);
  }
  Serial.println();
  reply(left ? "send failed" : nullptr);
}

void handleCommand(String line) {
  line.trim();
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  cmd.toUpperCase();
  int n = arg.toInt();
  bool okSlot = n >= 0 && n < NUM_SLOTS;   // message / IR code number
  bool okSnd = n >= 0 && n < NUM_SOUNDS;   // sound number
  markActivity();
  needRedraw = true;

  if (cmd == "INFO") {
    static uint32_t lastInfo = 0;
    if (!lastInfo || millis() - lastInfo > 60000) setStatus("Setup page\nconnected", TFT_GREEN);
    lastInfo = millis();
    sendInfo();
  } else if (cmd == "MODE") {
    if (n < 0 || n >= M_SETTINGS) return reply("bad mode");
    releaseKey();
    stopPlay();
    mode = prevMode = n;
    saveSettings();
    ledIdle();
    reply();
  } else if (cmd == "SLOT") {
    if (!okSlot) return reply("bad slot");
    stopPlay();
    slot = n;
    saveSettings();
    ledIdle();
    reply();
  } else if (cmd == "IRSLOT") {
    if (!okSlot) return reply("bad slot");
    irSlot = n;
    saveSettings();
    ledIdle();
    reply();
  } else if (cmd == "KEY") {
    if (n < 0 || n >= keyCount()) return reply("bad key");
    releaseKey();
    keyIdx = n;
    saveSettings();
    reply();
  } else if (cmd == "VOL") {
    if (n < 0 || n >= (int)sizeof(VOLUMES)) return reply("bad volume");
    volIdx = n;
    M5.Speaker.setVolume(VOLUMES[volIdx]);
    saveSettings();
    reply();
  } else if (cmd == "SET") {
    int sp2 = arg.indexOf(' ');
    String key = arg.substring(0, sp2);
    int v = arg.substring(sp2 + 1).toInt();
    for (int i = 0; i < S_COUNT; i++) {
      if (!SETTINGS[i].key || key != SETTINGS[i].key) continue;
      if (sp2 < 0 || v < 0 || v >= SETTINGS[i].count) return reply("bad choice");
      if (i == S_KEY_ACTION) releaseKey();
      if (i == S_PLAY) stopPlay();
      settingChoice[i] = v;
      prefs.putUChar(SETTINGS[i].key, v);
      return reply();
    }
    reply("unknown setting");
  } else if (cmd == "PLAY") {
    if (!okSnd) return reply("bad sound");
    stopPlay();
    playSlot(n);
    reply();
  } else if (cmd == "STOP") {
    stopPlay();
    reply();
  } else if (cmd == "DEL") {
    if (!okSnd) return reply("bad sound");
    stopPlay();
    slotLen[n] = 0;
    LittleFS.remove(slotPath(n));
    reply();
  } else if (cmd == "NAME") {
    if (!okSnd) return reply("bad sound");
    int sp2 = arg.indexOf(' ');
    String t = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    t.trim();
    t = t.substring(0, 24);
    slotName[n] = t;
    prefs.putString(("name" + String(n)).c_str(), t);
    reply();
  } else if (cmd == "UP") {
    if (!okSnd) return reply("bad sound");
    int sp2 = arg.indexOf(' ');
    receiveSlot(n, sp2 < 0 ? 0 : (size_t)arg.substring(sp2 + 1).toInt());
  } else if (cmd == "DOWN") {
    if (!okSnd) return reply("bad sound");
    sendSlot(n);
  } else if (cmd == "CKEY") {
    // CKEY <mods> <usage> <label>   (usage 0 removes the custom key)
    int a = arg.indexOf(' '), b = arg.indexOf(' ', a + 1);
    int mods = arg.substring(0, a).toInt();
    int usage = a < 0 ? 0 : arg.substring(a + 1, b < 0 ? arg.length() : b).toInt();
    String label = b < 0 ? "" : arg.substring(b + 1);
    label.trim();
    if (mods < 0 || mods > 255 || usage < 0 || usage > 0xE7) return reply("bad key");
    if (usage && !label.length()) return reply("needs a label");
    releaseKey();
    customMods = usage ? mods : 0;
    customUsage = usage;
    customName = label.substring(0, 16);
    prefs.putUChar("ck_mod", customMods);
    prefs.putUChar("ck_use", customUsage);
    prefs.putString("ck_name", customName);
    if (usage) keyIdx = NUM_KEYS;            // select it straight away
    else if (keyIdx >= NUM_KEYS) keyIdx = 0;
    saveSettings();
    reply();
  } else if (cmd == "IRLEARN") {
    if (!okSlot) return reply("bad slot");
    wakeScreen();
    reply(learnIr(n) ? nullptr : "no IR code heard");
  } else if (cmd == "IRSEND") {
    if (!okSlot) return reply("bad slot");
    reply(irSend(n, IR_REPEATS) ? nullptr : "no IR code in that slot");
  } else if (cmd == "IRDEL") {
    if (!okSlot) return reply("bad slot");
    deleteIr(n);
    reply();
  } else if (cmd == "FORGET") {
    forgetBluetooth();
    reply();
  } else {
    reply("unknown command");
  }
}

void pollSerial() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      if (line.length()) handleCommand(line);
      line = "";
    } else if (ch != '\r' && line.length() < 200) {
      line += ch;
    }
  }
}

// ---------------------------------------------------------------------
//  Setup and loop
// ---------------------------------------------------------------------
void setup() {
  setCpuFrequencyMhz(160);  // plenty for audio + Bluetooth, uses less power than 240
  auto cfg = M5.config();
  cfg.external_imu = false;
  cfg.external_rtc = false;
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5.begin(cfg);
  M5.Ex_I2C.release();  // make sure the Grove pins are free for the Unit Key
  auto mic = M5.Mic.config();
  mic.over_sampling = 4;  // average more readings per sample: less hiss
  M5.Mic.config(mic);
  // M5Unified gives the StickS3 speaker a magnification of 1, which plays a
  // full-scale sound at about 12% (-18dB). Applied at the next begin().
  M5.Speaker.end();
  auto spk = M5.Speaker.config();
  spk.magnification = SPK_GAIN;
  M5.Speaker.config(spk);
  M5.Speaker.begin();
  Serial.setRxBufferSize(16384);  // room for recordings sent from the setup page
  Serial.begin(115200);

  M5.BtnA.setHoldThresh(800);
  M5.BtnB.setHoldThresh(800);

  pinMode(PIN_KEY, INPUT_PULLUP);
  rmtInit(PIN_IR_TX, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000);
  // carrier_level false = carrier on the HIGH (mark) symbols; duty is a 0-1 fraction
  rmtSetCarrier(PIN_IR_TX, true, false, 38000, IR_DUTY);
  // The IR LED is driven straight from the pin: maximum drive (~40mA) for range
  gpio_set_drive_capability((gpio_num_t)PIN_IR_TX, GPIO_DRIVE_CAP_3);
  rmtSetEOT(PIN_IR_TX, 0);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(SCREEN_BRIGHT);

  prefs.begin("tswitch", false);
  loadSettings();
  M5.Speaker.setVolume(VOLUMES[volIdx]);

  bool havePsram = psramFound();
  for (int i = 0; i < NUM_SOUNDS; i++) {
    slotBuf[i] = (int16_t*)(havePsram ? ps_malloc(MAX_SAMPLES * 2) : nullptr);
    slotLen[i] = 0;
  }
  if (!havePsram || !slotBuf[NUM_SOUNDS - 1]) {
    M5.Display.setTextSize(2);
    M5.Display.println("No PSRAM!\nCheck board\nsettings.");
    while (true) delay(1000);
  }

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char id[6];
  snprintf(id, sizeof(id), "%02X%02X", mac[4], mac[5]);
  bleName = String(BLE_NAME) + " " + id;

  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvasOk = canvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;

  if (!LittleFS.begin(true)) setStatus("Storage error", TFT_RED, 10000);
  loadSlots();
  loadIr();

#if HAS_USB_HID
  USB.onEvent(onUsbEvent);
  UsbKeyboard.begin();
  USB.begin();
#endif

  ledIdle();
  lastInteraction = lastActivity = millis();
  readBattery();
  drawScreen();
}

void loop() {
  M5.update();
  pollStudentSwitch();
  pollStaffButtons();
  pollSerial();

  uint32_t now = millis();
  if (ledFlashUntil && now >= ledFlashUntil) { ledFlashUntil = 0; ledIdle(); }
  if (statusMsg.length() && now >= statusUntil) { statusMsg = ""; needRedraw = true; }

  checkVbus(now);
  static bool lastUsb = false;
  bool u = usbHostActive();
  if (u != lastUsb) { lastUsb = u; needRedraw = true; }

  static uint32_t lastRefresh = 0;
  if (now - lastRefresh > 5000) { lastRefresh = now; readBattery(); }
  if (forgetConfirmUntil && now >= forgetConfirmUntil) { forgetConfirmUntil = 0; needRedraw = true; }
  if (mode == M_SETTINGS && now - lastInteraction > SETTINGS_EXIT_MS) { mode = prevMode; ledIdle(); needRedraw = true; }
  manageBle(now);
  managePower(now);
  if (needRedraw && scr != SCR_OFF) drawScreen();
  delay(2);
}
