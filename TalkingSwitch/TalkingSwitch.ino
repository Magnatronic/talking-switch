// =====================================================================
//  TalkingSwitch - M5StickS3 + M5Stack Unit Key accessibility switch
// ---------------------------------------------------------------------
//  One big switch, four jobs (modes), named after what they hold:
//    QUICK     The 4 Quick messages. "Messages" setting:
//                Staff pick - plays the selected one (BIGmack style). Play
//                  style: Tap / Hold to play / Latch. With Tap, keep holding
//                  the switch to move on to the next Quick message.
//                Student scans - the student chooses one by scanning (below).
//    TOPICS    The 4 Topics of 4 messages, by scanning. Choose from:
//                One topic - the messages in the topic staff selected;
//                All topics - the topics first, then that topic's messages
//                  plus "Back". "Offer Quick / Control / My device" add those
//                  as choices next to the topics: the Quick messages and the
//                  IR codes are offered like a topic's messages, and My
//                  device changes to KEYBOARD for the student's AAC device.
//    KEYBOARD  Acts as a USB or Bluetooth keyboard key, e.g. Space/Enter
//              for Grid 3 or Mind Express switch access. Key goes down on
//              press and up on release, so dwell and hold-to-scan settings
//              in the AAC software still work. When a computer is using it
//              as a USB keyboard, keys go over USB only; otherwise they go
//              over Bluetooth. (Never both, so no double presses.)
//              "No USB" setting: Bluetooth, or a backup mode - with no USB
//              connection (the AAC device isn't there) it changes to QUICK
//              or TOPICS by itself, with Bluetooth off, and comes back to
//              KEYBOARD when USB is connected again.
//    CONTROL   Sends the selected learned infrared (IR) code (TV, fibre
//              optics, bubble tube), and plays that code's own sound if it
//              has one (e.g. "Bubbles!"). "IR codes" setting: Staff pick /
//              Repeat while held / Student scans (the student chooses).
//  Scanning: the switch offers the choices one at a time (LED colour, name
//  on screen, a quiet spoken prompt) and the student picks one, which then
//  plays. Choosing: Press twice - a press starts, the next chooses;
//  Hold & release - hold to step through, let go to choose.
//
//  Each Quick message, topic message and IR code is up to 5 s, with an
//  optional name. The setup page turns the names into the short spoken
//  prompts used when scanning.
//    SETTINGS  General settings (volume, modes, press timing, screen, power,
//              Bluetooth). "Modes" can turn KEYBOARD and/or CONTROL off for a
//              talking-only switch: A then skips them (key and codes are kept).
//              Each mode has its own settings too, as the last item B steps to.
//              The big switch keeps doing the previous mode's job.
//
//  Staff controls on the StickS3 - one rule everywhere (the bottom line of
//  the screen always shows what they do; when the screen is dim or off, the
//  first press only wakes it):
//    A click .... next mode (in a mode's settings: close them)
//    B click .... next: Quick message / topic / key / IR code, then the
//                 mode's "Settings" item; in settings, the next setting
//    Hold A ..... do it: QUICK record the message, CONTROL learn the code,
//                 on "Settings" open them, in settings change the value
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

// Firmware version. FW_API goes up whenever the setup page needs to change
// with it (the page shows a warning if the numbers don't match).
static const char* FW_VERSION = "27 Sep 2026";
static const int   FW_API     = 1;

// ---------------------------------------------------------------------
//  Settings you may want to change
// ---------------------------------------------------------------------
static const int PIN_KEY   = 10;   // Unit Key switch signal (Grove)
static const int PIN_LED   = 9;    // Unit Key SK6812 LED data (Grove)
static const int PIN_IR_TX = 46;   // StickS3 IR transmitter
static const int PIN_IR_RX = 42;   // StickS3 IR receiver

static const uint32_t DEBOUNCE_MS = 25;    // ignore contact bounce
static const uint32_t LONG_REPEAT_MS = 2000; // QUICK long press: keep moving on every this long while held

static const uint32_t SAMPLE_RATE = 16000; // recording sample rate (Hz)
static const uint32_t MAX_SECONDS = 5;     // longest message / IR code sound
static const uint32_t PROMPT_SECONDS = 2;  // longest scanning prompt
static const int      NUM_QUICK   = 4;     // Quick messages (QUICK)
static const int      NUM_TOPICS  = 4;     // Topics (TOPICS)...
static const int      PER_TOPIC   = 4;     // ...of up to 4 messages
static const int      NUM_SLOTS   = 4;     // IR codes (and the 4 item colours)
// Sound numbers (the setup page uses the same ones):
//   0-3 Quick messages, 4-19 topic messages (topic t, item k = 4 + t*4 + k),
//   20-23 IR codes' sounds, 24-39 topic message prompts, 40-43 topic prompts,
//   44-47 IR code prompts, 48 the "Back" prompt, 49-52 Quick message prompts,
//   53 the "Stop" prompt, 54 "Control", 55 "My device", 56 "Quick"
static const int SND_TOPIC = NUM_QUICK, SND_IR = SND_TOPIC + NUM_TOPICS * PER_TOPIC, SND_PMSG = SND_IR + NUM_SLOTS,
                 SND_PTOPIC = SND_PMSG + NUM_TOPICS * PER_TOPIC, SND_PIR = SND_PTOPIC + NUM_TOPICS,
                 SND_PBACK = SND_PIR + NUM_SLOTS, SND_PQUICK = SND_PBACK + 1,
                 SND_PSTOP = SND_PQUICK + NUM_QUICK, SND_PIRMENU = SND_PSTOP + 1, SND_PTALKER = SND_PIRMENU + 1,
                 SND_PQMENU = SND_PTALKER + 1, NUM_SOUNDS = SND_PQMENU + 1;
// Name numbers: 0-3 Quick, 4-19 topic messages, 20-23 IR codes (as sounds), 24-27 topics
static const int NAME_TOPIC = SND_PMSG, NUM_NAMES = NAME_TOPIC + NUM_TOPICS;
static const uint8_t  MIC_PGA     = 8;     // mic analogue gain, 3dB steps (0-10). Lower if loud voices distort

static const char*    BLE_NAME      = "Talking Switch"; // Bluetooth name; the last 4 characters of the
                                                        // stick's address are added, e.g. "Talking Switch 7B70"
static const uint8_t  VOLUMES[]     = {128, 180, 220, 255}; // output power is volume squared: ~25/50/75/100%
static const uint8_t  SPK_GAIN      = 8;   // speaker magnification: 8 = volume 4 is exactly full scale
                                           // (M5Unified's StickS3 default of 1 is 18dB quieter)
static const int      IR_REPEATS    = 2;   // extra copies of the IR code (helps at the edge of range)
static const float    IR_DUTY       = 0.5f; // carrier duty: 0.5 = most energy per pulse (remotes often use 0.33)
static const uint32_t IR_REPEAT_MS  = 110; // CONTROL, Repeat held: resend this often while held
static const uint32_t STATUS_MS     = 2500; // how long status messages stay on screen

// Power saving
// normal screen brightness: the "Brightness" setting (lower saves battery)
static const uint8_t  SCREEN_DIMMED = 16;     // faint glow when idle
static const uint32_t SCREEN_DIM_MS = 30000;  // dim the screen after this long without a staff button press
static const uint32_t SCREEN_OFF_MS = 120000; // then switch it off completely
static const uint32_t SETTINGS_EXIT_MS = 30000; // SETTINGS goes back to the previous mode after this long untouched
static const uint32_t BACKUP_MS = 10000;        // KEYBOARD with no USB this long: change to the backup mode
static const int      LOW_BATTERY   = 15;     // warn below this battery %

// ---------------------------------------------------------------------
//  Modes and keys
// ---------------------------------------------------------------------
enum Mode : uint8_t { M_SPEAK, M_CHOOSE, M_KEYBOARD, M_IR, M_SETTINGS, M_COUNT };
static const char* MODE_NAMES[M_COUNT] = {"QUICK", "TOPICS", "KEYBOARD", "CONTROL", "SETTINGS"};
// Mode colours for the switch LED (dim idle glow)
static const uint8_t MODE_RGB[M_COUNT][3] = {
  {0, 40, 0}, {0, 30, 40}, {30, 0, 40}, {40, 20, 0}, {0, 0, 0}
};
// QUICK, TOPICS and IR: the LED shows which message / topic / code it's on
static const uint8_t SLOT_RGB[NUM_SLOTS][3] = {{0, 40, 0}, {0, 25, 40}, {30, 0, 40}, {40, 20, 0}};
// Mode colours for the screen header, and whether it needs dark text
static const uint8_t SCREEN_RGB[M_COUNT][3] = {
  {0, 160, 70}, {0, 140, 160}, {140, 60, 220}, {240, 130, 0}, {90, 90, 90}
};
static const bool HEADER_DARK_TEXT[M_COUNT] = {false, false, false, true, false};

enum KbOut : uint8_t { OUT_NONE, OUT_BLE, OUT_USB };  // where keyboard presses go
enum PlayStyle : uint8_t { PLAY_TAP, PLAY_HOLD, PLAY_LATCH };
// A choice offered while scanning (declared here, before any function)
enum ScanKind : uint8_t { SK_GROUP, SK_MSG, SK_BACK, SK_IR, SK_STOP, SK_IRMENU, SK_TALKER, SK_QMENU };
struct ScanItem { uint8_t kind, idx; };

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
enum SettingId : uint8_t {
  S_VOLUME, S_BSOUND, S_MODES, S_SPEAK_CHOOSE, S_PLAY, S_HOLD, S_CHOOSE_FROM, S_OFFER_QUICK, S_OFFER_CONTROL, S_OFFER_DEVICE, S_IR_CHOOSE, S_ACCESS, S_SCAN_SPEED, S_SCAN_ROUNDS, S_STOP,
  S_KEY_ACTION, S_PRESS_SOUND, S_NO_USB,
  S_ACCEPT, S_LOCKOUT, S_BRIGHT, S_WAKE, S_SLEEP, S_AUTOOFF, S_LOUD, S_FORGET, S_ABOUT,
  S_COUNT
};
// Which settings screens a setting appears on (a setting can be on several)
enum SettingGroup : uint8_t { G_GENERAL = 1, G_SPEAK = 2, G_CHOOSE = 4, G_KEYBOARD = 8, G_IR = 16 };
struct Setting {
  const char* name;       // shown on screen
  const char* key;        // Preferences key (nullptr = an action, not a stored value)
  uint8_t count, def;     // number of choices, default choice
  uint32_t values[6];
  const char* labels[6];
  uint8_t groups;         // SettingGroup bits
};
static const Setting SETTINGS[S_COUNT] = {
  {"Volume",              "vol",       4, 2, {0, 1, 2, 3}, {"1", "2", "3", "4"}, G_GENERAL},
  // staff pressing B through messages / topics / codes: say each one (silent if empty), or nothing
  {"B button sound",      "s_bsound",  2, 0, {0, 1}, {"Say it", "Off"}, G_GENERAL},
  // which modes this switch uses (QUICK and TOPICS always)
  {"Modes",               "s_modes",   4, 0, {0, 1, 2, 3}, {"All", "No KEYBOARD", "No CONTROL", "Talking only"}, G_GENERAL},
  // QUICK: play the Quick message staff selected, or the student chooses one by scanning
  {"Messages",            "s_spkmode", 2, 0, {0, 1}, {"Staff pick", "Student scans"}, G_SPEAK},
  // QUICK: Tap = a press plays the whole message; Hold to play = plays (looping)
  // only while the switch is held; Latch = one press starts it looping, the next stops it
  {"Play style",          "s_play",    3, 0, {0, 1, 2}, {"Tap", "Hold to play", "Latch"}, G_SPEAK},
  // QUICK, Tap: keep holding the switch this long to move to the next Quick message
  {"Hold for next msg",   "s_hold",    5, 2, {0, 1000, 1500, 2000, 3000}, {"Off", "1 s", "1.5 s", "2 s", "3 s"}, G_SPEAK},
  // TOPICS: the messages in the topic staff selected (B button); or the topics
  // first, then a topic's messages
  {"Choose from",         "s_topics",  2, 0, {0, 1}, {"One topic", "All topics"}, G_CHOOSE},
  // TOPICS, All topics: more choices next to the topics - the Quick messages,
  // the IR codes ("Control") and "My device" (changes to KEYBOARD)
  {"Offer Quick",         "s_offq",    2, 0, {0, 1}, {"Off", "On"}, G_CHOOSE},
  {"Offer Control",       "s_offctl",  2, 0, {0, 1}, {"Off", "On"}, G_CHOOSE},
  {"Offer My device",     "s_offdev",  2, 0, {0, 1}, {"Off", "On"}, G_CHOOSE},
  // IR: send the selected code; the same, repeating while held; or the student chooses by scanning
  {"IR codes",            "s_irmode",  3, 0, {0, 1, 2}, {"Staff pick", "Repeat held", "Student scans"}, G_IR},
  // scanning: press to start, press to choose; or hold to step, let go to choose
  {"Choosing",            "s_access",  2, 0, {0, 1}, {"Press twice", "Hold & release"}, G_SPEAK | G_CHOOSE | G_IR},
  // scanning: how long each choice is offered, and how many times round before stopping
  {"Scan speed",          "s_scanspd", 5, 2, {1500, 2000, 3000, 4000, 5000}, {"1.5 s", "2 s", "3 s", "4 s", "5 s"}, G_SPEAK | G_CHOOSE | G_IR},
  {"Scan rounds",         "s_scanrnd", 3, 1, {1, 2, 3}, {"1", "2", "3"}, G_SPEAK | G_CHOOSE | G_IR},
  // scanning: offer "Stop" last, so the student can choose none of them
  {"Stop choice",         "s_stop",    2, 0, {0, 1}, {"Off", "On"}, G_SPEAK | G_CHOOSE | G_IR},
  // KEYBOARD: Momentary = key held while the switch is held; Latch = one press
  // holds the key down, the next press lets it go
  {"Key action",          "s_keyact",  2, 0, {0, 1}, {"Momentary", "Latch"}, G_KEYBOARD},
  // KEYBOARD and IR: a sound on each student press (they're silent otherwise)
  {"Press sound",         "s_psound",  3, 0, {0, 1, 2}, {"Off", "Click", "Beep"}, G_KEYBOARD | G_IR},
  // KEYBOARD with no USB connection: keys go over Bluetooth, or the switch
  // talks instead (backup mode, Bluetooth off) until USB is back
  {"No USB",              "s_nousb",   3, 0, {0, 1, 2}, {"Bluetooth", "QUICK", "TOPICS"}, G_KEYBOARD},
  // press must be held this long to count - filters accidental brushes
  {"Press must last",     "s_accept",  5, 0, {0, 100, 250, 500, 1000}, {"Instant", "0.1 s", "0.25 s", "0.5 s", "1 s"}, G_GENERAL},
  // ignore new presses this soon after the last one - filters tremor/repeats
  {"Ignore repeats for",  "s_lockout", 5, 2, {0, 200, 400, 800, 1500}, {"Off", "0.2 s", "0.4 s", "0.8 s", "1.5 s"}, G_GENERAL},
  // screen brightness while in use; lower saves battery
  {"Brightness",          "s_bright",  3, 1, {40, 128, 220}, {"Low", "Medium", "High"}, G_GENERAL},
  {"Switch wakes screen", "s_wake",    2, 0, {0, 1}, {"No", "Yes"}, G_GENERAL},
  // on battery, sleep after this many idle minutes; the big switch still works
  {"Sleep after",         "s_sleep",   4, 2, {0, 2, 5, 15}, {"Off", "2 min", "5 min", "15 min"}, G_GENERAL},
  // on battery, power off completely after this many idle minutes
  {"Auto power off",      "s_autooff", 4, 0, {0, 30, 60, 120}, {"Never", "30 min", "60 min", "2 hours"}, G_GENERAL},
  // soft-clip compression of new recordings (value / 10); louder but harsher
  {"Recording boost",     "s_loud",    4, 2, {10, 15, 20, 30}, {"Off", "Low", "Medium", "High"}, G_GENERAL},
  // action: hold A twice clears all remembered Bluetooth devices
  {"Forget BT devices",   nullptr,     1, 0, {0}, {"B: forget all"}, G_GENERAL},
  // information: the firmware version, storage used and presses
  {"About",               nullptr,     1, 0, {0}, {""}, G_GENERAL},
};
uint8_t settingChoice[S_COUNT];
uint32_t setting(SettingId id) { return SETTINGS[id].values[settingChoice[id]]; }
uint8_t volume() { return VOLUMES[settingChoice[S_VOLUME]]; }

// ---------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------
Preferences prefs;
uint8_t mode = M_SPEAK;
uint8_t slot = 0;        // QUICK: selected Quick message (0-3)
uint8_t group = 0;       // TOPICS: topic staff selected / the student is choosing in
uint8_t irSlot = 0;      // selected IR code
int irSound(int i) { return SND_IR + i; }  // sound (and name) number of IR code i
int topicMsg(int t, int k) { return SND_TOPIC + t * PER_TOPIC + k; }  // sound of topic t's message k
uint8_t keyIdx = 0;      // selected keyboard key (NUM_KEYS = the custom key)
// Custom key/shortcut set from the setup page, e.g. Win+H.
// Modifier bits: 1 Ctrl, 2 Shift, 4 Alt, 8 Win (HID left-hand modifiers).
uint8_t customMods = 0, customUsage = 0;
String  customName;
int keyCount() { return NUM_KEYS + (customUsage ? 1 : 0); }
bool keyIsCustom() { return keyIdx >= NUM_KEYS; }
String keyName() { return keyIsCustom() ? customName : String(KEYS[keyIdx].name); }
uint32_t pressCount = 0; // student activations since boot
KbOut    keyDownOut = OUT_NONE; // where the current key-down went (the key-up goes there too)
bool     keyLatched = false;    // Latch key action: key is being held down
bool     playLatched = false;   // Latch play style: message is looping
// Scanning (TOPICS, and IR with "Student scans")
ScanItem scanItems[NUM_TOPICS + 4];  // top: Quick, 4 topics, Control, My device, Stop  // the choices on offer: topics, messages (+ Back) or IR codes, + Stop
int      scanCount = 0;
bool     scanning = false;      // offering choices now
bool     scanInGroup = false;   // TOPICS: offering a topic's messages (true) or the topics (false)
enum ScanSub : uint8_t { SUB_TOPIC, SUB_CONTROL, SUB_QUICK };
uint8_t  scanSub = SUB_TOPIC;   // ...and with scanInGroup, which: a topic, the IR codes or the Quick messages
bool     gateShown = false;     // staff have stepped (B) to the mode's "Settings" item
bool     modeSettingsOpen = false; // ...and opened it (hold A)
int8_t   scanPos = 0;           // choice being offered
int      scanStepsLeft = 0;
uint32_t scanNext = 0;          // when to offer the next one
uint32_t scanWaitUntil = 0;     // Hold & release: after picking a group, wait this long for the next hold

static const size_t MAX_SAMPLES = SAMPLE_RATE * MAX_SECONDS;
static const size_t PROMPT_SAMPLES = SAMPLE_RATE * PROMPT_SECONDS;
size_t soundMax(int i) { return i < SND_PMSG ? MAX_SAMPLES : PROMPT_SAMPLES; }
int16_t* slotBuf[NUM_SOUNDS];  // see the sound numbers above
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
String   slotName[NUM_NAMES];  // optional names (see name numbers above), set on the setup page
String   voiceId, voiceSpeed;  // typed-speech voice for this switch, chosen on the setup page
String bleName;  // the switch's name: the one staff gave it on the setup page, or BLE_NAME plus this stick's ID
String devName;  // the name staff gave it ("" = the default)
String stickId;  // last 4 characters of this stick's Bluetooth address, e.g. "7B70"
String defaultName() { return String(BLE_NAME) + " " + stickId; }
// A typed message's words and the voice they were spoken in ("<voice> <speed>"),
// so the setup page can remake it in a new voice. Empty for recorded ones.
String   slotWords[SND_PMSG], slotWordsVoice[SND_PMSG];
String   statusMsg = "";
uint16_t statusColor = TFT_YELLOW;
uint32_t statusUntil = 0;
float    recProgress = -1;     // 0..1 while recording, else -1
uint8_t  prevMode = M_SPEAK;   // in SETTINGS, the big switch keeps doing this mode's job
bool     backupOn = false;     // in the backup mode because KEYBOARD has no USB
uint32_t backupSince = 0;      // KEYBOARD without USB since (0 = not counting)
uint8_t  settingIdx = 0;       // setting shown in SETTINGS mode
uint32_t forgetConfirmUntil = 0; // "Forget BT devices": waiting for the second B press

M5Canvas canvas(&M5.Display);
bool canvasOk = false;

void drawScreen();  // defined in the Display section

// ---------------------------------------------------------------------
//  LED
// ---------------------------------------------------------------------
// What the big switch does: in SETTINGS it carries on with the previous mode.
bool inSettings() { return mode == M_SETTINGS; }         // the general SETTINGS mode
bool settingsView() { return inSettings() || modeSettingsOpen; }
uint8_t studentMode() { return inSettings() ? prevMode : mode; }

// LED colour for a choice being offered
const uint8_t* scanColour(const ScanItem& it) {
  static const uint8_t WHITE[3] = {30, 30, 30}, RED[3] = {40, 0, 0};
  switch (it.kind) {
    case SK_STOP: return RED;
    case SK_IRMENU: return MODE_RGB[M_IR];
    case SK_TALKER: return MODE_RGB[M_KEYBOARD];
    case SK_QMENU: return MODE_RGB[M_SPEAK];
    case SK_GROUP:
    case SK_IR: return SLOT_RGB[it.idx];
    case SK_MSG: return SLOT_RGB[it.idx < SND_TOPIC ? it.idx : (it.idx - SND_TOPIC) % PER_TOPIC];
    default: return WHITE;  // Back
  }
}

void ledShow(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_LED, r, g, b); }
void ledIdle() {
  uint8_t m = studentMode();
  const uint8_t* c = scanning ? scanColour(scanItems[scanPos])
                   : m == M_SPEAK ? SLOT_RGB[slot] : m == M_CHOOSE ? SLOT_RGB[group]
                   : m == M_IR ? SLOT_RGB[irSlot] : MODE_RGB[m];
  int k = (keyLatched || playLatched || scanning) ? 3 : 1;  // brighter while latched on / scanning
  ledShow(c[0] * k, c[1] * k, c[2] * k);
}

// ---------------------------------------------------------------------
//  Storage
// ---------------------------------------------------------------------
String slotPath(int i) { return String("/s") + i + ".raw"; }

// Older firmware stored the 4 messages and 4 IR sounds under other names.
// (Only matters if the recording space wasn't re-formatted by an upload.)
void migrateFiles() {
  static const char* OLD[] = {"/msg", "/irs"};
  const int NEW[] = {0, SND_IR};
  for (int d = 0; d < 2; d++)
    for (int k = 0; k < NUM_SLOTS; k++) {
      String from = String(OLD[d]) + k + ".raw";
      if (LittleFS.exists(from) && !LittleFS.exists(slotPath(NEW[d] + k))) LittleFS.rename(from, slotPath(NEW[d] + k));
    }
}

String nameKey(int i) { return "nm" + String(i); }

void saveSettings() {
  prefs.putUChar("mode3", backupOn ? M_KEYBOARD : studentMode());  // never a SETTINGS page or the backup
  prefs.putUChar("slot", slot);
  prefs.putUChar("topic", group);
  prefs.putUChar("irslot", irSlot);
  prefs.putUChar("key", keyIdx);
}

void loadSettings() {
  if (prefs.isKey("mode3")) {
    mode = prefs.getUChar("mode3", M_SPEAK) % M_COUNT;
  } else if (prefs.isKey("mode2")) {  // previous firmware: SPEAK, KEYBOARD, IR
    static const uint8_t OLD[] = {M_SPEAK, M_KEYBOARD, M_IR};
    mode = OLD[prefs.getUChar("mode2", 0) % 3];
  } else {                            // older: SPEAK, STEPS, KEYBOARD, IR, SPEAK+IR
    static const uint8_t OLD[] = {M_SPEAK, M_SPEAK, M_KEYBOARD, M_IR, M_IR};
    mode = OLD[prefs.getUChar("mode", 0) % 5];
  }
  if (inSettings()) mode = M_SPEAK;
  slot   = prefs.getUChar("slot", 0) % NUM_QUICK;
  group  = prefs.getUChar("topic", 0) % NUM_TOPICS;
  irSlot = prefs.getUChar("irslot", 0) % NUM_SLOTS;
  customMods  = prefs.getUChar("ck_mod", 0);
  customUsage = prefs.getUChar("ck_use", 0);
  customName  = prefs.getString("ck_name", "");
  voiceId     = prefs.getString("voice", "");
  voiceSpeed  = prefs.getString("vspeed", "");
  devName     = prefs.getString("devname", "");
  keyIdx = prefs.getUChar("key", 0) % keyCount();
  // older firmware's names: "name0-3" messages (now Quick), "name4-7" IR codes
  for (int i = 0; i < 2 * NUM_SLOTS; i++) {
    String old = "name" + String(i);
    if (!prefs.isKey(old.c_str())) continue;
    prefs.putString(nameKey(i < NUM_SLOTS ? i : irSound(i - NUM_SLOTS)).c_str(), prefs.getString(old.c_str(), ""));
    prefs.remove(old.c_str());
  }
  for (int i = 0; i < NUM_NAMES; i++) slotName[i] = prefs.getString(nameKey(i).c_str(), "");
  for (int i = 0; i < S_COUNT; i++) {
    if (!SETTINGS[i].key) { settingChoice[i] = 0; continue; }
    settingChoice[i] = prefs.getUChar(SETTINGS[i].key, SETTINGS[i].def);
    if (settingChoice[i] >= SETTINGS[i].count) settingChoice[i] = SETTINGS[i].def;
  }
  // older firmware's "Choose from": Quick msgs / A topic / Topics. Scanning the
  // Quick messages is now QUICK's "Messages: Student scans".
  if (prefs.isKey("s_choose")) {
    uint8_t old = prefs.getUChar("s_choose", 0);
    if (old == 0 && mode == M_CHOOSE) {
      mode = M_SPEAK;
      prefs.putUChar("mode3", mode);
      settingChoice[S_SPEAK_CHOOSE] = 1;
      prefs.putUChar(SETTINGS[S_SPEAK_CHOOSE].key, 1);
    } else if (old > 0) {
      settingChoice[S_CHOOSE_FROM] = old - 1;
      prefs.putUChar(SETTINGS[S_CHOOSE_FROM].key, old - 1);
    }
    prefs.remove("s_choose");
  }
  if (prefs.isKey("s_extra")) {
    uint8_t old = prefs.getUChar("s_extra", 0);
    settingChoice[S_OFFER_CONTROL] = old & 1 ? 1 : 0;
    settingChoice[S_OFFER_DEVICE] = old & 2 ? 1 : 0;
    prefs.putUChar(SETTINGS[S_OFFER_CONTROL].key, settingChoice[S_OFFER_CONTROL]);
    prefs.putUChar(SETTINGS[S_OFFER_DEVICE].key, settingChoice[S_OFFER_DEVICE]);
    prefs.remove("s_extra");
  }
  if (!modeOn(mode)) mode = M_SPEAK;
}

void loadSlots() {
  for (int i = 0; i < NUM_SOUNDS; i++) {
    slotLen[i] = 0;
    File f = LittleFS.open(slotPath(i), "r");
    if (!f) continue;
    size_t n = min((size_t)f.size() / 2, soundMax(i));
    slotLen[i] = f.read((uint8_t*)slotBuf[i], n * 2) / 2;
    f.close();
  }
}

String wordsPath(int i) { return String("/w") + i + ".txt"; }

// Typed words for message i (first line the voice, then the words); "" clears them.
void setWords(int i, const String& voice, const String& words) {
  if (i < 0 || i >= SND_PMSG) return;
  slotWords[i] = words;
  slotWordsVoice[i] = words.length() ? voice : "";
  if (!words.length()) { LittleFS.remove(wordsPath(i)); return; }
  File f = LittleFS.open(wordsPath(i), "w");
  if (f) { f.print(voice + "\n" + words); f.close(); }
}

void loadWords() {
  for (int i = 0; i < SND_PMSG; i++) {
    slotWords[i] = slotWordsVoice[i] = "";
    File f = LittleFS.open(wordsPath(i), "r");
    if (!f) continue;
    slotWordsVoice[i] = f.readStringUntil('\n');
    slotWords[i] = f.readString();
    f.close();
  }
}

// A new sound replaces any typed words (the page sets them again for typed ones).
// Recording space used (flash), and nearly full (over 90%).
int storagePercent() {
  size_t total = LittleFS.totalBytes();
  return total ? (int)(LittleFS.usedBytes() * 100 / total) : 0;
}
bool storageNearlyFull() { return storagePercent() >= 90; }

bool saveSlot(int i) {
  setWords(i, "", "");
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
  setStatus("Recording " + titleOf(i) + "\nRelease A to stop", TFT_RED, 60000);
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
  M5.Speaker.setVolume(volume());
  recProgress = -1;

  slotLen[i] = pos;
  tidyRecording(i);
  if (slotLen[i] == 0) {
    LittleFS.remove(slotPath(i));
    setWords(i, "", "");
    setStatus("Nothing heard\n" + titleOf(i) + " cleared");
  } else {
    bool ok = saveSlot(i);
    if (!ok) setStatus("Save failed!\nStorage may be full", TFT_RED);
    else if (storageNearlyFull()) setStatus("Saved - storage nearly full\nDelete sounds you don't use", TFT_ORANGE, 6000);
    else setStatus("Saved\n" + titleOf(i), TFT_GREEN);
    playSlot(i);
  }
  ledIdle();
}

// Next recorded Quick message after `from` (wrapping), or -1.
int nextQuick(int from) {
  for (int k = 1; k <= NUM_QUICK; k++) {
    int m = (from + k) % NUM_QUICK;
    if (slotLen[m]) return m;
  }
  return -1;
}

// Next learned IR code after `from` (wrapping), or -1.
int nextIr(int from) {
  for (int k = 1; k <= NUM_SLOTS; k++) {
    int i = (from + k) % NUM_SLOTS;
    if (irLen[i]) return i;
  }
  return -1;
}

bool topicUsed(int t) {
  for (int k = 0; k < PER_TOPIC; k++) if (slotLen[topicMsg(t, k)]) return true;
  return false;
}

// "Bubbles", or "Quick 2" / "Message 2" / "IR code 2" when it has no name (sound number).
String titleOf(int snd) {
  if (slotName[snd].length()) return slotName[snd];
  if (snd < SND_TOPIC) return "Quick " + String(snd + 1);
  if (snd < SND_IR) return "Message " + String((snd - SND_TOPIC) % PER_TOPIC + 1);
  return "IR code " + String(snd - SND_IR + 1);
}
String topicTitle(int t) {
  return slotName[NAME_TOPIC + t].length() ? slotName[NAME_TOPIC + t] : "Topic " + String(t + 1);
}

// Audio cue when a message / code is chosen: its sound, or a short tone.
void slotCue(int i) {
  if (slotLen[i]) playSlot(i);
  else M5.Speaker.tone(1200, 60);
}

// Staff pressing B to step through: say the message / topic / code sound, if
// it has one - or nothing, with "B button sound: Off". (No beep when empty:
// the screen says so.)
void staffCue(int snd) { if (setting(S_BSOUND) == 0 && slotLen[snd]) playSlot(snd); }

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
  if (studentMode() != M_KEYBOARD || backupMode() != M_COUNT) {  // with a backup mode, Bluetooth stays off
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

// Whether a mode is in use ("Modes" setting). QUICK, TOPICS and SETTINGS always are.
bool modeOn(uint8_t m) {
  const uint8_t c = setting(S_MODES);
  if (m == M_KEYBOARD) return c == 0 || c == 2;
  if (m == M_IR) return c == 0 || c == 1;
  return true;
}

// The mode KEYBOARD changes to with no USB (M_COUNT = none: use Bluetooth).
uint8_t backupMode() {
  if (!modeOn(M_KEYBOARD)) return M_COUNT;
#if HAS_USB_HID
  switch (setting(S_NO_USB)) {
    case 1: return M_SPEAK;
    case 2: return M_CHOOSE;
  }
#endif
  return M_COUNT;
}

void setStudentMode(uint8_t m) {
  if (inSettings()) prevMode = m; else mode = m;
}

// KEYBOARD with no USB for BACKUP_MS: change to the backup mode, and back
// to KEYBOARD when USB returns (not in the middle of a student's choice).
void manageBackup(uint32_t now) {
  const bool usb = usbHostActive();
  const uint8_t to = backupMode();
  if (backupOn) {
    if ((usb || to == M_COUNT) && !scanning && !swActive) {
      backupOn = false;
      stopPlay();
      setStudentMode(M_KEYBOARD);
      ledIdle();
      setStatus("USB connected\nBack to KEYBOARD", TFT_GREEN);
      beep(1200, 60); beep(1800, 60);
      needRedraw = true;
    }
    return;
  }
  // count only while KEYBOARD is on screen and staff aren't busy with it
  if (to == M_COUNT || usb || mode != M_KEYBOARD || modeSettingsOpen || gateShown || swActive || keyLatched) {
    if (backupSince) { backupSince = 0; needRedraw = true; }
    return;
  }
  if (!backupSince) backupSince = now;
  static uint32_t lastSec = 0;
  uint32_t sec = (now - backupSince) / 1000;
  if (sec != lastSec) { lastSec = sec; needRedraw = true; }  // the countdown on screen
  if (now - backupSince < BACKUP_MS) return;
  backupSince = 0;
  releaseKey();
  backupOn = true;
  mode = to;
  scanInGroup = false;
  ledIdle();
  setStatus("No USB\n" + String(MODE_NAMES[to]) + " until it's back", TFT_ORANGE);
  beep(1800, 60); beep(1200, 60);
  needRedraw = true;
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
  M5.Speaker.setVolume(volume());
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
static const int PILL_W = 122;  // top-right pill: volume and battery

void drawBattery(lgfx::LovyanGFX& c) {
  const int W = c.width();
  c.fillRoundRect(W - PILL_W - 4, 5, PILL_W, HEADER_H - 10, 6, TFT_BLACK);
  // volume: a speaker, then 4 equal bars (hollow above the current level)
  const int sx = W - PILL_W + 1, sy = HEADER_H / 2;
  c.fillRect(sx, sy - 2, 3, 5, TFT_WHITE);
  c.fillTriangle(sx + 2, sy, sx + 7, sy - 5, sx + 7, sy + 5, TFT_WHITE);
  for (int i = 0; i < (int)sizeof(VOLUMES); i++) {
    int x = sx + 12 + i * 5, y = sy - 6;
    if (i <= settingChoice[S_VOLUME]) c.fillRect(x, y, 3, 12, TFT_WHITE);
    else c.drawRect(x, y, 3, 12, TFT_DARKGREY);
  }
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

// Centred text in the largest of `fonts` that fits `maxW`; if even the
// smallest is too wide, it's shortened with "..".
void drawFitIn(lgfx::LovyanGFX& c, const String& s, int y, uint16_t col,
               std::initializer_list<const lgfx::IFont*> fonts, int maxW) {
  String t = s;
  for (auto f : fonts) {
    c.setFont(f);
    if (c.textWidth(t) <= maxW) break;
  }
  if (c.textWidth(t) > maxW) {
    while (t.length() && c.textWidth(t + "..") > maxW) t.remove(t.length() - 1);
    t += "..";
  }
  c.setTextColor(col);
  c.setTextDatum(middle_center);
  c.drawString(t, c.width() / 2, y);
}

// Large centred text, stepping down the font size until it fits the width.
void drawFit(lgfx::LovyanGFX& c, const String& s, int y, uint16_t col) {
  drawFitIn(c, s, y, col, {&fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b, &fonts::FreeSansBold9pt7b}, c.width() - 12);
}

void drawCentered(lgfx::LovyanGFX& c, const String& s, int y, const lgfx::IFont* font, uint16_t col) {
  c.setFont(font);
  c.setTextColor(col);
  c.setTextDatum(middle_center);
  c.drawString(s, c.width() / 2, y);
}

// Every screen uses the same three lines (settings too):
//   line 1 - the main thing: big, white (bold 18pt, or smaller to fit)
//   line 2 - its state or what to do: FreeSans 9pt - green ready,
//            orange a problem, yellow what to do
//   line 3 - details: Font2, light grey (orange in the backup mode)
static const int LINE1_Y = 52, LINE2_Y = 80, LINE3_Y = 104;
static const uint16_t C_READY = TFT_GREEN, C_PROBLEM = TFT_ORANGE, C_DO = TFT_YELLOW, C_DETAIL = TFT_LIGHTGREY;

void line1(lgfx::LovyanGFX& c, const String& s, uint16_t col = TFT_WHITE) { drawFit(c, s, LINE1_Y, col); }
void line2(lgfx::LovyanGFX& c, const String& s, uint16_t col) {
  drawFitIn(c, s, LINE2_Y, col, {&fonts::FreeSans9pt7b, &fonts::Font2}, c.width() - 12);
}
void line3(lgfx::LovyanGFX& c, const String& s, uint16_t col = C_DETAIL) {
  drawFitIn(c, s, LINE3_Y, col, {&fonts::Font2}, c.width() - 12);
}

// Line 3 while in the backup mode: "Backup - ..." in orange.
String backupTag() { return backupOn ? "Backup - " : ""; }
void detailLine(lgfx::LovyanGFX& c, const String& s) { line3(c, backupTag() + s, backupOn ? C_PROBLEM : C_DETAIL); }

// A choice being offered while scanning.
void drawOffer(lgfx::LovyanGFX& c) {
  line1(c, scanTitle(scanItems[scanPos]));
  line2(c, holdToChoose() ? "Let go to choose" : "Press to choose", C_DO);
}

// Scanning before a scan starts: how many choices, how to start, and where
// they come from - or, with nothing to choose, what to do about it.
void drawChoices(lgfx::LovyanGFX& c, int n, const String& what, const String& from, const String& empty) {
  if (!n) {
    line1(c, "Empty", C_PROBLEM);
    line2(c, empty, C_DO);
  } else {
    line1(c, String(n) + (n == 1 ? what.substring(0, what.length() - 1) : what));
    line2(c, holdToChoose() ? "Hold to start" : "Press to start", C_DO);
  }
  detailLine(c, "From: " + from);
}

int countTopics() { int n = 0; for (int t = 0; t < NUM_TOPICS; t++) n += topicUsed(t); return n; }
int countQuick() { int n = 0; for (int i = 0; i < NUM_QUICK; i++) n += slotLen[i] ? 1 : 0; return n; }
int countIr() { int n = 0; for (int i = 0; i < NUM_SLOTS; i++) n += irLen[i] ? 1 : 0; return n; }
String plural(int n, const char* one, const char* many) { return String(n) + " " + (n == 1 ? one : many); }

void drawMain(lgfx::LovyanGFX& c) {
  const int W = c.width();
  if (gateShown) {  // staff stepped to this mode's "Settings" item
    line1(c, "Settings");
    line2(c, "Hold A to open", C_DO);
    return;
  }
  if (scanning) { drawOffer(c); return; }
  switch (mode) {
    case M_SPEAK:
      // the Quick message B and hold A (record) work on - also when the student scans
      line1(c, titleOf(slot));
      if (playLatched) line2(c, "Playing - press to stop", C_DO);
      else if (slotLen[slot]) line2(c, String(slotLen[slot] / (float)SAMPLE_RATE, 1) + " s recorded", C_READY);
      else line2(c, "Empty - hold A to record", C_PROBLEM);
      detailLine(c, speakScanning() ? "Student scans " + plural(countQuick(), "message", "messages")
                                    : "Quick " + String(slot + 1) + " of " + String(NUM_QUICK));
      break;
    case M_CHOOSE: {
      // what the student will choose from, and how many choices there are
      int n = 0;
      String from, what = " messages";
      if (topicsByStudent() && !scanInGroup) {
        n = topCount();
        from = "Topics";
        what = topCount() > countTopics() ? " choices" : " topics";
      } else if (scanSub == SUB_CONTROL && topicsByStudent()) {
        n = countIr();
        from = "Control";
        what = " IR codes";
      } else if (scanSub == SUB_QUICK && topicsByStudent()) {
        n = countQuick();
        from = "Quick";
      } else {
        for (int k = 0; k < PER_TOPIC; k++) n += slotLen[topicMsg(group, k)] ? 1 : 0;
        from = topicTitle(group);
      }
      drawChoices(c, n, what, from, "Add them on the setup page");
      break;
    }
    case M_KEYBOARD: {
      // the key, with an arrow for the arrow keys, in line 1's fonts
      String name = keyName();
      Arrow arrow = keyIsCustom() ? A_NONE : KEYS[keyIdx].arrow;
      const lgfx::IFont* fonts[] = {&fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b, &fonts::FreeSansBold9pt7b};
      int tw = 0;
      for (auto f : fonts) {
        c.setFont(f);
        tw = c.textWidth(name) + (arrow ? 30 : 0);
        if (tw <= W - 12) break;
      }
      int x = W / 2 - tw / 2;
      if (arrow) { drawArrow(c, arrow, x + 10, LINE1_Y, 10, rgb(SCREEN_RGB[mode])); x += 30; }
      c.setTextColor(TFT_WHITE);
      c.setTextDatum(middle_left);
      c.drawString(name, x, LINE1_Y);
      KbOut o = kbOutput();
      if (keyLatched) line2(c, "Key held - press to let go", C_DO);
      else if (backupSince) {
        int left = (int)((BACKUP_MS - min(BACKUP_MS, millis() - backupSince) + 999) / 1000);
        line2(c, "No USB - " + String(MODE_NAMES[backupMode()]) + " in " + String(left) + " s", C_PROBLEM);
      } else if (o == OUT_NONE) line2(c, "Not connected", C_PROBLEM);
      else line2(c, o == OUT_USB ? "Sending by USB" : "Sending by Bluetooth", C_READY);
      line3(c, backupMode() != M_COUNT ? String("No USB: ") + MODE_NAMES[backupMode()] : "Bluetooth: " + bleName);
      break;
    }
    case M_IR: {
      // the IR code B and hold A (learn) work on - also when the student scans
      int snd = irSound(irSlot);
      line1(c, titleOf(snd));
      if (!irLen[irSlot]) line2(c, "No code - hold A to learn", C_PROBLEM);
      else line2(c, slotLen[snd] ? "Code learned, with sound" : "Code learned", C_READY);
      line3(c, irScanning() ? "Student scans " + plural(countIr(), "IR code", "IR codes")
                            : "IR code " + String(irSlot + 1) + " of " + String(NUM_SLOTS));
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
  drawFitIn(c, l1, y1, fg, {&fonts::FreeSansBold12pt7b, &fonts::FreeSansBold9pt7b}, W - 24);
  if (l2.length()) drawFitIn(c, l2, y1 + 24, fg, {&fonts::FreeSans9pt7b, &fonts::Font2}, W - 24);
  if (bar) {  // recording time used
    int by = y1 + (l2.length() ? 42 : 22), bw = W - 40;
    c.drawRect(20, by, bw, 8, fg);
    c.fillRect(20, by, (int)(bw * recProgress), 8, fg);
  }
}

// The settings shown now: general SETTINGS, or the open mode's own.
uint8_t settingsMask() {
  static const uint8_t MASK[M_COUNT] = {G_SPEAK, G_CHOOSE, G_KEYBOARD, G_IR, G_GENERAL};
  return MASK[mode];
}

// Settings on the current screen, leaving out ones that do nothing with the
// current choices (e.g. scan speed when staff pick the IR code).
bool settingShown(int i) {
  if (!(SETTINGS[i].groups & settingsMask())) return false;
  bool scan = mode == M_SPEAK ? speakScanning() : mode == M_IR ? irScanning() : true;
  switch (i) {
    case S_PLAY: return !speakScanning();
    case S_HOLD: return !speakScanning() && setting(S_PLAY) == PLAY_TAP;
    case S_ACCESS: case S_SCAN_SPEED: return scan;
    case S_SCAN_ROUNDS: return scan && !holdToChoose();
    case S_STOP: return scan;
    case S_OFFER_QUICK: return topicsByStudent();
    case S_OFFER_CONTROL: return topicsByStudent() && modeOn(M_IR);
    case S_OFFER_DEVICE: return topicsByStudent() && modeOn(M_KEYBOARD);
    case S_NO_USB: return HAS_USB_HID;  // needs USB Mode: USB-OTG (TinyUSB)
    case S_FORGET: return modeOn(M_KEYBOARD);  // Bluetooth is only for KEYBOARD
  }
  return true;
}

void drawSettings(lgfx::LovyanGFX& c) {
  const Setting& st = SETTINGS[settingIdx];
  int n = 0, pos = 0;
  for (int i = 0; i < S_COUNT; i++)
    if (settingShown(i)) { n++; if (i <= settingIdx) pos++; }
  // the setting's name, its value (big), then where you are
  drawFitIn(c, st.name, LINE1_Y, TFT_WHITE, {&fonts::FreeSansBold12pt7b, &fonts::FreeSansBold9pt7b}, c.width() - 12);
  if (settingIdx == S_ABOUT) {
    drawFit(c, FW_VERSION, LINE2_Y, C_DO);
    line3(c, "Storage " + String(storagePercent()) + "% used - " + plural(pressCount, "press", "presses"),
          storageNearlyFull() ? C_PROBLEM : C_DETAIL);
    return;
  }
  bool confirm = settingIdx == S_FORGET && forgetConfirmUntil;
  drawFit(c, confirm ? "Hold A again" : settingIdx == S_FORGET ? "Hold A to forget" : st.labels[settingChoice[settingIdx]],
          LINE2_Y, confirm ? C_PROBLEM : C_DO);
  line3(c, (inSettings() ? "Switch does " + String(MODE_NAMES[prevMode]) : String(MODE_NAMES[mode]) + " settings")
             + " - " + String(pos) + " of " + String(n));
}

// What the buttons do on this screen (the same words everywhere).
String buttonGuide() {
  if (settingsView()) {
    if (settingIdx == S_FORGET) return "B next  hold A forget  A " + String(inSettings() ? "mode" : "close");
    if (settingIdx == S_ABOUT) return "B next  A " + String(inSettings() ? "mode" : "close");
    return "B next  hold A change  A " + String(inSettings() ? "mode" : "close");
  }
  if (gateShown) return "B first  hold A open  A mode";
  switch (mode) {
    case M_SPEAK: return "B next  hold A record  A mode";
    case M_CHOOSE: return "B next topic  A mode";
    case M_KEYBOARD: return "B next key  A mode";
    case M_IR: return "B next code  hold A learn  A mode";
  }
  return "A mode";
}

void drawFooter(lgfx::LovyanGFX& c) {
  const int W = c.width(), H = c.height();
  c.drawFastHLine(0, FOOTER_Y, W, TFT_DARKGREY);
  String g = buttonGuide();
  drawFitIn(c, g, (FOOTER_Y + H) / 2 + 1, C_DETAIL, {&fonts::Font2}, W - 6);
}

void drawScreen() {
  lgfx::LovyanGFX& c = canvasOk ? (lgfx::LovyanGFX&)canvas : (lgfx::LovyanGFX&)M5.Display;
  const int W = c.width(), H = c.height();
  c.startWrite();
  c.fillScreen(TFT_BLACK);

  // Header: mode name on the mode colour, battery on the right
  c.fillRect(0, 0, W, HEADER_H, rgb(SCREEN_RGB[mode]));
  // a mode's own settings: "SETTINGS" on that mode's colour. Bold 9pt for
  // every title, so KEYBOARD fits beside the volume and battery.
  String title = modeSettingsOpen ? String("SETTINGS") : String(MODE_NAMES[mode]);
  c.setFont(&fonts::FreeSansBold9pt7b);
  c.setTextColor(HEADER_DARK_TEXT[mode] ? TFT_BLACK : TFT_WHITE);
  c.setTextDatum(middle_left);
  c.drawString(title, 6, HEADER_H / 2 + 1);
  drawBattery(c);

  if (statusMsg.length() && millis() < statusUntil) drawStatus(c);
  else if (settingsView()) drawSettings(c);
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
  M5.Display.setBrightness(setting(S_BRIGHT));
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
  if (M5.Speaker.isPlaying() || scanning) return false;
  return true;
}

void lightSleep(uint32_t idle) {
  if (inSettings()) { mode = prevMode; needRedraw = true; }
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
  M5.Speaker.setVolume(volume());
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
// ---------------------------------------------------------------------
//  Choosing by scanning (TOPICS mode, and QUICK and IR with "Student scans").
//  The switch offers each choice in turn - LED colour, name on screen and a
//  quiet spoken prompt - and the student picks one:
//    Press twice ..... a press starts the offers, the next press chooses
//    Hold & release .. hold to step through the offers, let go to choose
//  TOPICS with "Choose from: All topics" first offers the topics, then the
//  chosen topic's messages plus "Back"; with "One topic" it offers the
//  messages in the topic staff selected.
// ---------------------------------------------------------------------
static const uint8_t PROMPT_VOLUME = 150;  // channel volume for prompts (255 = as loud as messages)

bool speakScanning() { return setting(S_SPEAK_CHOOSE) == 1; }
bool topicsByStudent() { return setting(S_CHOOSE_FROM) == 1; }
bool holdToChoose() { return setting(S_ACCESS) == 1; }
bool irScanning() { return setting(S_IR_CHOOSE) == 2; }
bool irRepeat() { return setting(S_IR_CHOOSE) == 1; }
bool scanModeActive() {
  return studentMode() == M_CHOOSE || (studentMode() == M_IR && irScanning())
      || (studentMode() == M_SPEAK && speakScanning());
}

String scanTitle(const ScanItem& it) {
  switch (it.kind) {
    case SK_GROUP: return topicTitle(it.idx);
    case SK_MSG: return titleOf(it.idx);
    case SK_BACK: return "Back";
    case SK_STOP: return "Stop";
    case SK_IRMENU: return "Control";
    case SK_TALKER: return "My device";
    case SK_QMENU: return "Quick";
    default: return titleOf(irSound(it.idx));
  }
}

void playPrompt(const ScanItem& it) {
  int p;
  switch (it.kind) {
    case SK_GROUP: p = SND_PTOPIC + it.idx; break;
    case SK_MSG:   p = it.idx < SND_TOPIC ? SND_PQUICK + it.idx : SND_PMSG + (it.idx - SND_TOPIC); break;
    case SK_BACK:  p = SND_PBACK; break;
    case SK_STOP:  p = SND_PSTOP; break;
    case SK_IRMENU: p = SND_PIRMENU; break;
    case SK_TALKER: p = SND_PTALKER; break;
    case SK_QMENU: p = SND_PQMENU; break;
    default:       p = slotLen[SND_PIR + it.idx] ? SND_PIR + it.idx : irSound(it.idx);  // IR code's own sound is short
  }
  // no prompt made yet (e.g. set up on the stick): the message itself, quietly - the
  // next offer cuts it off, so a long one just gives its first few words
  if (!slotLen[p] && it.kind == SK_MSG && slotLen[it.idx]) p = it.idx;
  if (!slotLen[p]) { M5.Speaker.tone(it.kind == SK_STOP ? 400 : it.kind == SK_BACK ? 600 : 1200, 60); return; }
  M5.Speaker.setChannelVolume(1, PROMPT_VOLUME);
  M5.Speaker.playRaw(slotBuf[p], slotLen[p], SAMPLE_RATE, false, 1, 1, true);
}

bool stopChoice() { return setting(S_STOP) == 1; }

// Fills scanItems with what the student can choose from right now, then "Stop".
void buildScanList() {
  fillScanList();
  if (stopChoice() && scanCount) scanItems[scanCount++] = {SK_STOP, 0};
}

void fillScanList() {
  scanCount = 0;
  if (studentMode() == M_IR) {
    for (int i = 0; i < NUM_SLOTS; i++) if (irLen[i]) scanItems[scanCount++] = {SK_IR, (uint8_t)i};
    return;
  }
  if (studentMode() == M_SPEAK) {
    for (int i = 0; i < NUM_QUICK; i++) if (slotLen[i]) scanItems[scanCount++] = {SK_MSG, (uint8_t)i};
    return;
  }
  int topics = 0;
  for (int t = 0; t < NUM_TOPICS; t++) topics += topicUsed(t);
  const int top = topCount();
  if (!topicsByStudent()) { scanInGroup = true; scanSub = SUB_TOPIC; }
  if (!scanInGroup && top == 1 && topics == 1)  // only one topic: go straight to its messages
    for (int t = 0; t < NUM_TOPICS; t++) if (topicUsed(t)) { group = t; scanInGroup = true; scanSub = SUB_TOPIC; }
  if (!scanInGroup) {
    if (offerQuickMenu()) scanItems[scanCount++] = {SK_QMENU, 0};
    for (int t = 0; t < NUM_TOPICS; t++) if (topicUsed(t)) scanItems[scanCount++] = {SK_GROUP, (uint8_t)t};
    if (offerIrMenu()) scanItems[scanCount++] = {SK_IRMENU, 0};
    if (offerTalker()) scanItems[scanCount++] = {SK_TALKER, 0};
    return;
  }
  if (scanSub == SUB_CONTROL) {
    for (int i = 0; i < NUM_SLOTS; i++) if (irLen[i]) scanItems[scanCount++] = {SK_IR, (uint8_t)i};
  } else if (scanSub == SUB_QUICK) {
    for (int i = 0; i < NUM_QUICK; i++) if (slotLen[i]) scanItems[scanCount++] = {SK_MSG, (uint8_t)i};
  } else {
    for (int k = 0; k < PER_TOPIC; k++)
      if (slotLen[topicMsg(group, k)]) scanItems[scanCount++] = {SK_MSG, (uint8_t)topicMsg(group, k)};
  }
  if (topicsByStudent() && top > 1 && scanCount) scanItems[scanCount++] = {SK_BACK, 0};
}

// TOPICS, All topics, "Offer ...": the Quick messages and the IR codes (if
// there are any), and "My device"
bool offerQuickMenu() { return topicsByStudent() && setting(S_OFFER_QUICK) && countQuick(); }
bool offerIrMenu() { return topicsByStudent() && setting(S_OFFER_CONTROL) && modeOn(M_IR) && countIr(); }
bool offerTalker() { return topicsByStudent() && setting(S_OFFER_DEVICE) && modeOn(M_KEYBOARD); }

// How many choices the top of TOPICS offers (topics and extras)
int topCount() {
  int n = offerQuickMenu() + offerIrMenu() + offerTalker();
  for (int t = 0; t < NUM_TOPICS; t++) n += topicUsed(t);
  return n;
}

void stopScan() {
  if (!scanning) return;
  scanning = false;
  M5.Speaker.stop(1);
  ledIdle();
  needRedraw = true;
}

void offer(int pos) {
  scanPos = pos;
  scanNext = millis() + setting(S_SCAN_SPEED);
  playPrompt(scanItems[pos]);
  ledIdle();
  needRedraw = true;
}

void startScan() {
  scanning = true;
  scanWaitUntil = 0;
  scanStepsLeft = scanCount * setting(S_SCAN_ROUNDS);
  offer(0);
}

// The student picked a choice.
void choose(ScanItem it) {
  stopScan();
  switch (it.kind) {
    case SK_QMENU:
    case SK_IRMENU:
    case SK_GROUP:
      if (it.kind == SK_GROUP) group = it.idx;
      scanInGroup = true;
      scanSub = it.kind == SK_IRMENU ? SUB_CONTROL : it.kind == SK_QMENU ? SUB_QUICK : SUB_TOPIC;
      saveSettings();
      buildScanList();
      if (holdToChoose()) scanWaitUntil = millis() + 10000;  // wait for the next hold
      else if (scanCount) startScan();                      // straight on to its messages
      break;
    case SK_BACK:
      scanInGroup = false;
      buildScanList();
      if (!holdToChoose() && scanCount) startScan();
      break;
    case SK_TALKER:  // over to the student's AAC device
      scanInGroup = false;
      backupOn = false;
      setStudentMode(M_KEYBOARD);
      saveSettings();
      ledIdle();
      beep(1200, 60); beep(1800, 60);
      break;
    case SK_STOP:  // none of them: the next press starts again from the top
      if (topicsByStudent()) scanInGroup = false;
      scanWaitUntil = 0;
      break;
    case SK_MSG:
      if (topicsByStudent()) scanInGroup = false;  // next time, start from the topics
      if (it.idx < SND_TOPIC) { slot = it.idx; saveSettings(); }  // a Quick message: QUICK uses it too
      ledIdle();
      playSlot(it.idx);
      break;
    case SK_IR:
      if (topicsByStudent()) scanInGroup = false;  // TOPICS's Control: next time, start from the top
      irSlot = it.idx;
      saveSettings();
      ledIdle();
      if (!irSend(irSlot, IR_REPEATS)) beep(300, 150);
      else if (slotLen[irSound(irSlot)]) playSlot(irSound(irSlot));
      else pressSound();
      break;
  }
  needRedraw = true;
}

// Big switch pressed in a scanning mode.
void scanActivate() {
  if (scanning && !holdToChoose()) { choose(scanItems[scanPos]); return; }
  buildScanList();
  if (scanCount == 0) { beep(300, 150); return; }
  if (scanCount == 1 && !holdToChoose()) { choose(scanItems[0]); return; }  // only one choice: just do it
  startScan();
}

// Big switch let go: with Hold & release, that chooses.
void scanDeactivate() {
  if (holdToChoose() && scanning) choose(scanItems[scanPos]);
}

void updateScan(uint32_t now) {
  // Hold & release: after picking a group, go back to the groups if no hold comes
  if (!scanning && scanWaitUntil && (int32_t)(now - scanWaitUntil) >= 0) {
    scanWaitUntil = 0;
    if (topicsByStudent()) scanInGroup = false;
    needRedraw = true;
  }
  if (!scanning || (int32_t)(now - scanNext) < 0) return;
  if (!holdToChoose() && --scanStepsLeft <= 0) {  // no choice made: stop
    stopScan();
    if (topicsByStudent()) scanInGroup = false;
    return;
  }
  offer((scanPos + 1) % scanCount);
}

void onActivate() {
  pressCount++;
  markActivity();
  if (setting(S_WAKE)) wakeScreen();
  Serial.printf("press,%lu,%s\n", (unsigned long)millis(), MODE_NAMES[studentMode()]);
  ledShow(90, 90, 90);
  ledFlashUntil = millis() + 250;

  const uint8_t style = setting(S_PLAY);
  if (scanModeActive()) { scanActivate(); needRedraw = true; return; }
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

// QUICK long press: move to the next Quick message and play it as a cue.
void advanceMessage() {
  int s = nextQuick(slot);
  if (s < 0 || s == slot) return;  // nothing else to move to
  slot = s;
  saveSettings();
  slotCue(slot);
  const uint8_t* c = SLOT_RGB[slot];
  ledShow(c[0] * 2, c[1] * 2, c[2] * 2);
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
  if (scanModeActive()) { scanDeactivate(); return; }
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
  // Still holding in QUICK: step through the other messages
  uint32_t longPress = setting(S_HOLD);
  uint8_t m = studentMode();
  if (swActive && longPress && setting(S_PLAY) == PLAY_TAP && m == M_SPEAK && !speakScanning()) {
    uint32_t wait = swLastAdvance == swLastActivation ? longPress : LONG_REPEAT_MS;
    if (now - swLastAdvance >= wait) { swLastAdvance = now; advanceMessage(); }
  }
  // CONTROL, "Repeat held": keep sending while held, like holding a remote button
  static uint32_t lastIrRepeat = 0;
  if (swActive && m == M_IR && irRepeat() && irLen[irSlot]
      && now - swLastActivation >= 400 && now - lastIrRepeat >= IR_REPEAT_MS) {
    lastIrRepeat = now;
    irSend(irSlot, 0);
  }
}

// ---------------------------------------------------------------------
//  Staff buttons
// ---------------------------------------------------------------------
// Side effects of changing a setting (on the stick or from the setup page).
void settingChanged(int i) {
  if (i == S_KEY_ACTION) releaseKey();
  if (i == S_PLAY) stopPlay();
  if (i == S_CHOOSE_FROM || i == S_ACCESS || i == S_IR_CHOOSE || i == S_SPEAK_CHOOSE || i == S_STOP
      || i == S_OFFER_QUICK || i == S_OFFER_CONTROL || i == S_OFFER_DEVICE) { stopScan(); scanInGroup = false; }
  if (i == S_VOLUME) { M5.Speaker.setVolume(volume()); M5.Speaker.tone(1000, 100); }
  if (i == S_BRIGHT && scr == SCR_ON) M5.Display.setBrightness(setting(S_BRIGHT));
  if (i == S_MODES) {
    stopScan();
    scanInGroup = false;
    if (!modeOn(prevMode)) prevMode = M_SPEAK;
    if (!modeOn(mode)) {  // the mode it was in is now off
      releaseKey();
      backupOn = false;
      mode = M_SPEAK;
      saveSettings();
      ledIdle();
    }
  }
}

// Next setting shown on the current settings screen after `from` (wrapping).
int nextSettingInView(int from) {
  for (int k = 1; k <= S_COUNT; k++) {
    int i = (from + k) % S_COUNT;
    if (settingShown(i)) return i;
  }
  return from;
}

// Settings screens: B = next setting, hold A = change it (saved straight away)
void settingsButtons() {
  if (M5.BtnB.wasClicked()) { settingIdx = nextSettingInView(settingIdx); forgetConfirmUntil = 0; }
  if (M5.BtnA.wasHold() && settingIdx == S_FORGET) {
    if (forgetConfirmUntil && millis() < forgetConfirmUntil) {
      forgetConfirmUntil = 0;
      forgetBluetooth();
      setStatus("Forgotten\nAlso remove it on the computer", TFT_GREEN, 5000);
    } else {
      forgetConfirmUntil = millis() + 4000;
    }
  } else if (M5.BtnA.wasHold() && SETTINGS[settingIdx].key) {  // (About has nothing to change)
    uint8_t& ch = settingChoice[settingIdx];
    ch = (ch + 1) % SETTINGS[settingIdx].count;
    prefs.putUChar(SETTINGS[settingIdx].key, ch);
    settingChanged(settingIdx);
  }
  needRedraw = true;
}

// B click: the next item in this mode, then its "Settings" item, then the first.
void nextItem() {
  if (gateShown) {
    gateShown = false;
    switch (mode) {
      case M_SPEAK: stopPlay(); slot = 0; staffCue(slot); break;
      case M_CHOOSE: scanInGroup = false; group = 0; staffCue(SND_PTOPIC + group); break;
      case M_KEYBOARD: releaseKey(); keyIdx = 0; break;
      case M_IR: irSlot = 0; staffCue(irSound(irSlot)); break;
    }
  } else {
    switch (mode) {
      case M_SPEAK:
        stopPlay();
        if (slot == NUM_QUICK - 1) gateShown = true; else staffCue(++slot);
        break;
      case M_CHOOSE:
        scanInGroup = false;
        if (group == NUM_TOPICS - 1) gateShown = true;
        else { ++group; staffCue(SND_PTOPIC + group); }
        break;
      case M_KEYBOARD:
        releaseKey();
        if (keyIdx == keyCount() - 1) gateShown = true; else keyIdx++;
        break;
      case M_IR:
        if (irSlot == NUM_SLOTS - 1) gateShown = true; else { irSlot++; staffCue(irSound(irSlot)); }
        break;
    }
  }
  saveSettings();
  ledIdle();
  needRedraw = true;
}

void pollStaffButtons() {
  bool any = M5.BtnA.wasHold() || M5.BtnA.wasClicked()
          || M5.BtnB.wasHold() || M5.BtnB.wasClicked();
  if (!any) return;
  markActivity();
  stopScan();
  if (wakeScreen()) return;  // screen was dim/off: this press only wakes it
  if (settingsView() && (M5.BtnA.wasHold() || M5.BtnB.wasClicked())) { settingsButtons(); return; }

  if (M5.BtnA.wasHold()) {
    if (gateShown) {  // open this mode's settings
      modeSettingsOpen = true;
      settingIdx = nextSettingInView(S_COUNT - 1);
    } else if (mode == M_IR) {
      learnIr(irSlot);
    } else if (mode == M_SPEAK) {
      recordSlot(slot);
    }
    lastInteraction = millis();
    needRedraw = true;
    return;
  }
  if (M5.BtnA.wasClicked()) {  // not wasSingleClicked: that waits to rule out a double-click
    if (modeSettingsOpen) {    // close the mode's settings, back to its "Settings" item
      modeSettingsOpen = false;
      needRedraw = true;
      return;
    }
    if (swActive) { swActive = false; onDeactivate(); }
    releaseKey();
    stopPlay();
    if (!inSettings()) prevMode = mode;
    do mode = (mode + 1) % M_COUNT; while (!modeOn(mode));
    if (!inSettings()) backupOn = false;  // staff chose a mode
    gateShown = false;
    if (inSettings()) settingIdx = nextSettingInView(S_COUNT - 1);
    scanInGroup = false;
    saveSettings();
    ledIdle();
    needRedraw = true;
  }
  if (M5.BtnB.wasClicked()) nextItem();
}

// ---------------------------------------------------------------------
//  USB setup page (setup.html in Chrome/Edge talks to us over the USB
//  serial port). One text command per line; replies start with '@'.
//  Audio goes as raw 16-bit little-endian mono samples at SAMPLE_RATE.
//    INFO                 -> @INFO {json}
//    MODE n / SLOT n (Quick 0-3) / TOPIC n / IRSLOT n / KEY n / VOL n / SET <key> <choice>
//    PLAY n / STOP / DEL n / UP / DOWN   (n = sound number, see the top)
//    NAME n <text>        (n = name number, see the top) / FORGET
//    VOICE <id> <speed>   (the setup page's typed-speech voice for this switch)
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
  auto yes = [](bool b) { return String(b ? "true" : "false"); };
  auto words = [](int i) {  // a typed message's words and voice
    return slotWords[i].length() ? ",\"tx\":" + jsonStr(slotWords[i]) + ",\"tv\":" + jsonStr(slotWordsVoice[i]) : String();
  };
  auto msg = [&](int i) {  // a Quick message: length, name, and whether it has a prompt
    return "{\"len\":" + String(slotLen[i]) + ",\"pr\":" + yes(slotLen[SND_PQUICK + i]) + ",\"name\":" + jsonStr(slotName[i]) + words(i) + "}";
  };
  String j = "@INFO {\"name\":" + jsonStr(bleName) + ",\"defname\":" + jsonStr(defaultName());
  j += ",\"rate\":" + String(SAMPLE_RATE) + ",\"maxSec\":" + String(MAX_SECONDS) + ",\"promptSec\":" + String(PROMPT_SECONDS);
  j += ",\"mode\":" + String(studentMode()) + ",\"slot\":" + String(slot) + ",\"topic\":" + String(group)
       + ",\"irSlot\":" + String(irSlot);
  j += ",\"key\":" + String(keyIdx) + ",\"vol\":" + String(settingChoice[S_VOLUME]) + ",\"vols\":" + String(sizeof(VOLUMES));
  j += ",\"bat\":" + String(batLevel) + ",\"chg\":" + yes(batCharging) + ",\"bt\":" + yes(bleConnected);
  j += ",\"usbhid\":" + yes(HAS_USB_HID);
  j += ",\"ver\":" + jsonStr(FW_VERSION) + ",\"api\":" + String(FW_API) + ",\"presses\":" + String(pressCount);
  j += ",\"fsUsed\":" + String((unsigned long)LittleFS.usedBytes()) + ",\"fsTotal\":" + String((unsigned long)LittleFS.totalBytes());
  j += ",\"voice\":" + jsonStr(voiceId) + ",\"vspeed\":" + jsonStr(voiceSpeed);
  j += ",\"modes\":[";
  for (int i = 0; i < M_SETTINGS; i++) j += (i ? "," : "") + jsonStr(MODE_NAMES[i]);
  j += "],\"modesOn\":[";
  for (int i = 0; i < M_SETTINGS; i++) j += (i ? "," : "") + yes(modeOn(i));
  j += "],\"keys\":[";
  for (int i = 0; i < NUM_KEYS; i++) j += (i ? "," : "") + jsonStr(KEYS[i].name);
  if (customUsage) j += "," + jsonStr(customName);
  j += "],\"ckey\":{\"mods\":" + String(customMods) + ",\"usage\":" + String(customUsage) + ",\"name\":" + jsonStr(customName) + "}";
  j += ",\"quick\":[";
  for (int i = 0; i < NUM_QUICK; i++) j += String(i ? "," : "") + msg(i);
  j += "],\"topics\":[";
  for (int t = 0; t < NUM_TOPICS; t++) {
    j += String(t ? "," : "") + "{\"name\":" + jsonStr(slotName[NAME_TOPIC + t]) + ",\"pr\":" + yes(slotLen[SND_PTOPIC + t]) + ",\"msgs\":[";
    for (int k = 0; k < PER_TOPIC; k++) {
      int m = topicMsg(t, k);
      j += String(k ? "," : "") + "{\"len\":" + String(slotLen[m]) + ",\"pr\":" + yes(slotLen[SND_PMSG + (m - SND_TOPIC)])
           + ",\"name\":" + jsonStr(slotName[m]) + words(m) + "}";
    }
    j += "]}";
  }
  j += "],\"irs\":[";
  for (int i = 0; i < NUM_SLOTS; i++)
    j += String(i ? "," : "") + "{\"ir\":" + yes(irLen[i]) + ",\"len\":" + String(slotLen[irSound(i)])
         + ",\"pr\":" + yes(slotLen[SND_PIR + i]) + ",\"name\":" + jsonStr(slotName[irSound(i)]) + words(irSound(i)) + "}";
  j += "],\"back\":{\"pr\":" + yes(slotLen[SND_PBACK]) + "},\"stop\":{\"pr\":" + yes(slotLen[SND_PSTOP]) + "}";
  j += ",\"irmenu\":{\"pr\":" + yes(slotLen[SND_PIRMENU]) + "},\"talker\":{\"pr\":" + yes(slotLen[SND_PTALKER]) + "}";
  j += ",\"qmenu\":{\"pr\":" + yes(slotLen[SND_PQMENU]) + "}";
  j += ",\"settings\":[";
  bool first = true;
  for (int i = 0; i < S_COUNT; i++) {
    const Setting& st = SETTINGS[i];
    if (!st.key) continue;  // actions have their own buttons on the page
    j += String(first ? "" : ",") + "{\"key\":" + jsonStr(st.key) + ",\"name\":" + jsonStr(st.name);
    j += ",\"groups\":" + String(st.groups) + ",\"cur\":" + String(settingChoice[i]) + ",\"choices\":[";
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
  if (samples == 0 || samples > soundMax(i)) return reply("bad length");
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
  bool okSlot = n >= 0 && n < NUM_SLOTS;   // IR code number
  bool okSnd = n >= 0 && n < NUM_SOUNDS;   // sound number
  markActivity();
  if (cmd != "INFO") stopScan();           // the page checks in regularly: don't interrupt a student
  needRedraw = true;

  if (cmd == "INFO") {
    static uint32_t lastInfo = 0;
    if (!lastInfo || millis() - lastInfo > 60000) setStatus("Setup page\nconnected", TFT_GREEN);
    lastInfo = millis();
    sendInfo();
  } else if (cmd == "MODE") {
    if (n < 0 || n >= M_SETTINGS) return reply("bad mode");
    if (!modeOn(n)) return reply("that mode is turned off (Settings: Modes)");
    releaseKey();
    stopPlay();
    mode = prevMode = n;
    backupOn = false;
    saveSettings();
    ledIdle();
    reply();
  } else if (cmd == "SLOT") {
    if (n < 0 || n >= NUM_QUICK) return reply("bad message");
    stopPlay();
    slot = n;
    saveSettings();
    ledIdle();
    reply();
  } else if (cmd == "TOPIC") {
    if (n < 0 || n >= NUM_TOPICS) return reply("bad topic");
    group = n;
    scanInGroup = false;
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
    settingChoice[S_VOLUME] = n;
    prefs.putUChar(SETTINGS[S_VOLUME].key, n);
    settingChanged(S_VOLUME);
    reply();
  } else if (cmd == "SET") {
    int sp2 = arg.indexOf(' ');
    String key = arg.substring(0, sp2);
    int v = arg.substring(sp2 + 1).toInt();
    for (int i = 0; i < S_COUNT; i++) {
      if (!SETTINGS[i].key || key != SETTINGS[i].key) continue;
      if (sp2 < 0 || v < 0 || v >= SETTINGS[i].count) return reply("bad choice");
      settingChoice[i] = v;
      prefs.putUChar(SETTINGS[i].key, v);
      settingChanged(i);
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
    setWords(n, "", "");
    reply();
  } else if (cmd == "WORDS") {
    // WORDS <sound> <voice> <speed> <words>: the words a typed message says
    if (n < 0 || n >= SND_PMSG) return reply("bad sound");
    int a = arg.indexOf(' '), b = arg.indexOf(' ', a + 1), c = arg.indexOf(' ', b + 1);
    if (a < 0 || b < 0 || c < 0) return reply("bad words");
    setWords(n, arg.substring(a + 1, c).substring(0, 60), arg.substring(c + 1).substring(0, 200));
    reply();
  } else if (cmd == "NAME") {
    if (n < 0 || n >= NUM_NAMES) return reply("bad name number");
    int sp2 = arg.indexOf(' ');
    String t = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    t.trim();
    t = t.substring(0, 24);
    slotName[n] = t;
    prefs.putString(nameKey(n).c_str(), t);
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
  } else if (cmd == "VOICE") {
    // just remembered for the setup page; the switch itself only plays sounds
    int sp2 = arg.indexOf(' ');
    voiceId = (sp2 < 0 ? arg : arg.substring(0, sp2)).substring(0, 48);
    voiceSpeed = sp2 < 0 ? "" : arg.substring(sp2 + 1).substring(0, 8);
    prefs.putString("voice", voiceId);
    prefs.putString("vspeed", voiceSpeed);
    reply();
  } else if (cmd == "DEVNAME") {
    // DEVNAME <name>: the switch's name for Bluetooth and USB ("" = the default)
    String t = arg;
    t.trim();
    devName = t.substring(0, 20);
    prefs.putString("devname", devName);
    bleName = devName.length() ? devName : defaultName();
    if (bleRunning) bleEnd();  // KEYBOARD restarts Bluetooth with the new name
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
  M5.Display.setBrightness(setting(S_BRIGHT));

  prefs.begin("tswitch", false);
  loadSettings();
  M5.Speaker.setVolume(volume());

  bool havePsram = psramFound();
  for (int i = 0; i < NUM_SOUNDS; i++) {
    slotBuf[i] = (int16_t*)(havePsram ? ps_malloc(soundMax(i) * 2) : nullptr);
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
  stickId = id;
  bleName = devName.length() ? devName : defaultName();

  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvasOk = canvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;

  if (!LittleFS.begin(true)) setStatus("Storage error", TFT_RED, 10000);
  migrateFiles();
  loadSlots();
  loadWords();
  loadIr();

#if HAS_USB_HID
  // the name computers (and the setup page's connect list) show
  USB.productName(bleName.c_str());
  USB.manufacturerName("Magnatronic");
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
  if (inSettings() && now - lastInteraction > SETTINGS_EXIT_MS) { mode = prevMode; ledIdle(); needRedraw = true; }
  if ((modeSettingsOpen || gateShown) && now - lastInteraction > SETTINGS_EXIT_MS) {
    modeSettingsOpen = gateShown = false;
    needRedraw = true;
  }
  updateScan(now);
  manageBackup(now);
  manageBle(now);
  managePower(now);
  if (needRedraw && scr != SCR_OFF) drawScreen();
  delay(2);
}
