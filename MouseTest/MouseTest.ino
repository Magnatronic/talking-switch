// =====================================================================
//  MouseTest - try the StickS3's motion sensor as a mouse
// ---------------------------------------------------------------------
//  A separate test sketch (not the ChatterSwitch firmware). Worn on the
//  head (headband or cap) or held in the hand: turning left/right moves
//  the pointer left/right, tipping down/up moves it down/up.
//
//  Clicks: the big switch (Unit Key or a jack switch on Grove, GPIO10) is
//  the left button - held down while the switch is, so it can drag - or
//  the right button (Switch setting). Dwell click: keeping the pointer
//  within a small area for the Dwell time clicks once; move away to arm
//  it again.
//
//  Calibrate (hold A, and at the first start):
//    1. Keep still  - learns the sensor's zero point and which way is up
//    2. Tip down    - nod down (head) or tip the front down (hand), then
//                     back; this learns the up/down direction, whatever
//                     way round the stick is worn or held
//  Each start after that only needs the "Keep still" part. While still
//  it also keeps correcting its zero point, so the pointer doesn't creep.
//
//    A click .... pause / move (a paused pointer stays put; the switch
//                 still clicks)
//    Hold A ..... calibrate
//    B click .... next speed
//    Hold B ..... settings: B next, hold A change, A close
//
//  Output: over USB when a computer is using it, otherwise Bluetooth, with
//  the same identity as ChatterSwitch ("ChatterSwitch XXXX"), so a device
//  already paired with ChatterSwitch works without pairing again.
//
//  Rates also go to the USB serial port (115200) for the Arduino IDE's
//  Serial Plotter, as  x:<deg/s>,y:<deg/s>
//
//  Board settings: sketch.yaml (the same as the ChatterSwitch firmware).
// =====================================================================

#include <M5Unified.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <esp_mac.h>
#if defined(CONFIG_NIMBLE_ENABLED)
#include "host/ble_hs.h"
#else
#include <BLESecurity.h>
#endif

#if ARDUINO_USB_MODE == 0  // "USB Mode: USB-OTG (TinyUSB)"
#include "USB.h"
#include "USBHIDMouse.h"
#define HAS_USB_HID 1
USBHIDMouse UsbMouse;
#else
#define HAS_USB_HID 0
#define MOUSE_LEFT  0x01
#define MOUSE_RIGHT 0x02
#endif

// (declared before any function, for the Arduino IDE's automatic prototypes)
enum Out { OUT_NONE, OUT_BLE, OUT_USB };  // where mouse reports go
enum CalState { CAL_NONE, CAL_STILL, CAL_TIP };

M5Canvas canvas(&M5.Display);
Preferences prefs;

static const int PIN_KEY = 10;              // big switch (Grove yellow wire), low when pressed
static const uint32_t DEBOUNCE_MS = 25;
static const float STILL_DPS = 6.0f;         // Keep still: gyro may wander this much (deg/s)
static const uint32_t STILL_MS = 1000;       // ...for this long
static const float TIP_DEG = 12.0f;          // Tip down: this far is enough to learn the direction
static const uint32_t TIP_TIMEOUT_MS = 10000;
static const uint32_t SEND_MS = 10;          // send movement at most 100 times a second

// ---------------------------------------------------------------------
//  Settings (hold B), remembered after power-off
// ---------------------------------------------------------------------
enum SettingId { T_SPEED, T_ACCEL, T_STEADY, T_SMOOTH, T_DWELL, T_AREA, T_SWITCH, T_FLIPX, T_FLIPY, T_COUNT };
struct Setting { const char* key; const char* name; uint8_t count, cur; float values[8]; const char* labels[8]; };
Setting SETTINGS[T_COUNT] = {
  // pointer counts per degree of turn (the computer's pointer speed also applies)
  {"speed",  "Speed",      6, 3, {10, 15, 20, 30, 45, 70}, {"1", "2", "3", "4", "5", "6"}},
  // faster turns go further: extra speed per 100 deg/s
  {"accel",  "Speed-up",   4, 2, {0, 1, 2, 4}, {"Off", "Low", "Medium", "High"}},
  // turns slower than this are ignored (deg/s): steadies tremor and drift
  {"steady", "Steady",     4, 1, {0, 1.5f, 3, 6}, {"Off", "Low", "Medium", "High"}},
  // smooths out shakes (time constant, ms), at the cost of a little lag
  {"smooth", "Smoothing",  4, 1, {0, 30, 60, 120}, {"Off", "Low", "Medium", "High"}},
  // dwell click: time in one place (ms, 0 = off)
  {"dwell",  "Dwell click", 6, 0, {0, 800, 1000, 1500, 2000, 3000}, {"Off", "0.8 s", "1 s", "1.5 s", "2 s", "3 s"}},
  // dwell click: how far the pointer may wander and still count (counts)
  {"area",   "Dwell area", 3, 1, {15, 30, 60}, {"Small", "Medium", "Large"}},
  {"switch", "Switch",     2, 0, {MOUSE_LEFT, MOUSE_RIGHT}, {"Left click", "Right click"}},
  {"flipx",  "Flip left/right", 2, 0, {0, 1}, {"Off", "On"}},
  {"flipy",  "Flip up/down",    2, 0, {0, 1}, {"Off", "On"}},
};
float val(SettingId i) { return SETTINGS[i].values[SETTINGS[i].cur]; }
const char* label(SettingId i) { return SETTINGS[i].labels[SETTINGS[i].cur]; }
bool shown(int i) { return i != T_AREA || val(T_DWELL) > 0; }

void loadSettings() {
  for (auto& st : SETTINGS) st.cur = min<uint8_t>(prefs.getUChar(st.key, st.cur), st.count - 1);
}
void saveSetting(int i) { prefs.putUChar(SETTINGS[i].key, SETTINGS[i].cur); }

// ---------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------
CalState cal = CAL_NONE;
bool fullCal = false;        // this calibration includes Tip down
bool haveAxes = false;       // up and down directions known (saved)
float bias[3] = {0, 0, 0};   // gyro zero point (deg/s)
float up[3] = {0, 0, 1};     // "up" in the stick's axes (unit)
float down[3] = {1, 0, 0};   // the axis tipping down turns about (unit, at right angles to up)
float calSumG[3], calSumA[3], calMin[3], calMax[3];
int calN = 0;
uint32_t calStart = 0;
float tipAngle[3];           // Tip down: turn so far (deg)

bool paused = false;
bool settingsOpen = false;
int settingIdx = 0;
float rateX = 0, rateY = 0;  // smoothed turn rates (deg/s): right +, down +
float accX = 0, accY = 0;    // movement not sent yet (counts)
float posX = 0, posY = 0;    // where the pointer has gone (counts), for dwell
float dwellX = 0, dwellY = 0;
uint32_t dwellStart = 0;
bool dwellArmed = false;
uint32_t stillSince = 0;
uint32_t lastSample = 0, lastSend = 0;
uint8_t buttons = 0;
int batLevel = -1;
String status;               // short message on screen
uint32_t statusUntil = 0;

// Big switch
bool swRaw = false, swStable = false;
uint32_t swChange = 0;

// ---------------------------------------------------------------------
//  Output: USB when a computer is using us, else Bluetooth
// ---------------------------------------------------------------------
// The same report map as ChatterSwitch (keyboard, report 1; mouse, report 2),
// so devices paired with ChatterSwitch keep working.
static const uint8_t HID_REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,  // Keyboard, report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,  // modifiers
  0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,              // reserved byte
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,  // LEDs
  0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73,  // 6 keys
  0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
  0xC0,
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,  // Mouse, report ID 2
  0x09, 0x01, 0xA1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00,  // 3 buttons
  0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x05, 0x81, 0x03,              // padding
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,  // X, Y, wheel
  0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
  0xC0, 0xC0
};

BLEHIDDevice* hid = nullptr;
BLECharacteristic* mouseInput = nullptr;
volatile bool bleConnected = false;
String bleName;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true; }
  void onDisconnect(BLEServer*) override { bleConnected = false; BLEDevice::startAdvertising(); }
};

void bleBegin() {
  BLEDevice::init(bleName.c_str());
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  hid = new BLEHIDDevice(server);
  hid->inputReport(1);  // the keyboard report (unused here)
  mouseInput = hid->inputReport(2);
  hid->manufacturer()->setValue("DIY AT");
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
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
}

volatile bool usbSuspended = false;
#if HAS_USB_HID
void onUsbEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (id == ARDUINO_USB_SUSPEND_EVENT) usbSuspended = true;
  else if (id == ARDUINO_USB_RESUME_EVENT || id == ARDUINO_USB_STARTED_EVENT) usbSuspended = false;
}
#endif

Out output() {
#if HAS_USB_HID
  if ((bool)USB && !usbSuspended) return OUT_USB;
#endif
  return bleConnected ? OUT_BLE : OUT_NONE;
}

// One mouse report: the buttons now and a movement (counts, -127..127).
void sendMouse(int8_t dx, int8_t dy) {
  Out o = output();
  if (o == OUT_BLE && mouseInput) {
    uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, 0};
    mouseInput->setValue(report, sizeof(report));
    mouseInput->notify();
  }
#if HAS_USB_HID
  if (o == OUT_USB) {
    if (dx || dy) UsbMouse.move(dx, dy);
    else UsbMouse.buttons(buttons);
  }
#endif
}

void setButton(uint8_t b, bool down) {
  uint8_t was = buttons;
  buttons = down ? buttons | b : buttons & ~b;
  if (buttons != was) sendMouse(0, 0);
}

void click(uint8_t b) {
  setButton(b, true);
  delay(15);
  setButton(b, false);
}

// ---------------------------------------------------------------------
//  Motion
// ---------------------------------------------------------------------
float dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float norm(const float* a) { return sqrtf(dot(a, a)); }

void setStatus(const String& s, uint32_t ms = 2000) { status = s; statusUntil = millis() + ms; }

// Keep still: start (again) collecting samples.
void restartStill() {
  cal = CAL_STILL;
  calN = 0;
  calStart = millis();
  for (int k = 0; k < 3; k++) { calSumG[k] = calSumA[k] = 0; calMin[k] = 1e9f; calMax[k] = -1e9f; }
}

void startCalibration(bool full) {
  fullCal = full;
  restartStill();
  setButton(buttons, false);
  M5.Speaker.tone(1000, 60);
}

void saveAxes() {
  prefs.putBytes("up", up, sizeof(up));
  prefs.putBytes("down", down, sizeof(down));
}

void loadAxes() {
  haveAxes = prefs.getBytes("up", up, sizeof(up)) == sizeof(up)
          && prefs.getBytes("down", down, sizeof(down)) == sizeof(down);
}

// One gyro (deg/s) and accelerometer (g) sample while calibrating.
void calibrateSample(const float* g, const float* a, float dt) {
  if (cal == CAL_STILL) {
    for (int k = 0; k < 3; k++) {
      calSumG[k] += g[k];
      calSumA[k] += a[k];
      calMin[k] = min(calMin[k], g[k]);
      calMax[k] = max(calMax[k], g[k]);
      if (calMax[k] - calMin[k] > STILL_DPS) { restartStill(); return; }  // moved: start again
    }
    calN++;
    if (millis() - calStart < STILL_MS) return;
    for (int k = 0; k < 3; k++) { bias[k] = calSumG[k] / calN; up[k] = calSumA[k] / calN; }
    float n = norm(up);
    for (int k = 0; k < 3; k++) up[k] /= n;
    if (fullCal || !haveAxes) {
      cal = CAL_TIP;
      calStart = millis();
      for (int k = 0; k < 3; k++) tipAngle[k] = 0;
      M5.Speaker.tone(1500, 60);
    } else {
      cal = CAL_NONE;
      M5.Speaker.tone(2000, 60);
      setStatus("Ready");
    }
    return;
  }
  // CAL_TIP: add up the turn, leaving out any turn about "up"
  float w[3] = {g[0] - bias[0], g[1] - bias[1], g[2] - bias[2]};
  float u = dot(w, up);
  for (int k = 0; k < 3; k++) tipAngle[k] += (w[k] - u * up[k]) * dt;
  float n = norm(tipAngle);
  if (n >= TIP_DEG) {
    for (int k = 0; k < 3; k++) down[k] = tipAngle[k] / n;
    haveAxes = true;
    saveAxes();
    cal = CAL_NONE;
    M5.Speaker.tone(2000, 60);
    setStatus("Ready");
  } else if (millis() - calStart > TIP_TIMEOUT_MS) {
    cal = CAL_NONE;
    M5.Speaker.tone(400, 150);
    setStatus(haveAxes ? "No tip seen - kept the old one" : "No tip seen - hold A to try again", 4000);
  }
}

// One sample while running: turn rates -> pointer movement.
void moveSample(const float* g, float dt) {
  float w[3] = {g[0] - bias[0], g[1] - bias[1], g[2] - bias[2]};
  // keep correcting the zero point while really still (drift)
  if (fabsf(w[0]) < 1.5f && fabsf(w[1]) < 1.5f && fabsf(w[2]) < 1.5f) {
    if (!stillSince) stillSince = millis();
    if (millis() - stillSince > 1000) for (int k = 0; k < 3; k++) bias[k] += w[k] * 0.005f;
  } else stillSince = 0;

  // turning about "up" (anticlockwise seen from above = left) and tipping down
  float x = -dot(w, up), y = dot(w, down);
  if (val(T_FLIPX)) x = -x;
  if (val(T_FLIPY)) y = -y;
  const float tau = val(T_SMOOTH) / 1000.0f;
  const float k = tau > 0 ? dt / (tau + dt) : 1.0f;
  rateX += (x - rateX) * k;
  rateY += (y - rateY) * k;
  if (paused || !haveAxes) return;

  auto speed = [&](float r) {
    const float d = val(T_STEADY);
    float m = fabsf(r) - d;
    if (m <= 0) return 0.0f;
    float gain = val(T_SPEED) * (1 + val(T_ACCEL) * m / 100.0f);
    return (r < 0 ? -m : m) * gain;  // counts per second
  };
  float mx = speed(rateX) * dt, my = speed(rateY) * dt;
  accX += mx; accY += my;
  posX += mx; posY += my;
}

void sendMovement() {
  if (millis() - lastSend < SEND_MS) return;
  int dx = (int)constrain(accX, -127.0f, 127.0f), dy = (int)constrain(accY, -127.0f, 127.0f);
  if (!dx && !dy) return;
  lastSend = millis();
  accX -= dx; accY -= dy;
  if (output() != OUT_NONE) sendMouse(dx, dy);
}

// Dwell: in one small area for the dwell time -> one click; move away to arm again.
float dwellProgress() {
  if (!val(T_DWELL) || !dwellArmed || paused) return 0;
  return min(1.0f, (millis() - dwellStart) / val(T_DWELL));
}

void checkDwell() {
  if (!val(T_DWELL) || paused || cal != CAL_NONE) { dwellArmed = false; dwellX = posX; dwellY = posY; return; }
  float dx = posX - dwellX, dy = posY - dwellY;
  if (dx * dx + dy * dy > val(T_AREA) * val(T_AREA)) {  // moved: a new place
    dwellX = posX; dwellY = posY;
    dwellStart = millis();
    dwellArmed = true;
    return;
  }
  if (dwellArmed && millis() - dwellStart >= (uint32_t)val(T_DWELL)) {
    dwellArmed = false;
    click(MOUSE_LEFT);
    M5.Speaker.tone(2500, 20);
  }
}

// ---------------------------------------------------------------------
//  Big switch and buttons
// ---------------------------------------------------------------------
void pollSwitch() {
  bool raw = digitalRead(PIN_KEY) == LOW;
  if (raw != swRaw) { swRaw = raw; swChange = millis(); }
  if (millis() - swChange < DEBOUNCE_MS || swRaw == swStable) return;
  swStable = swRaw;
  if (cal != CAL_NONE) return;
  setButton((uint8_t)val(T_SWITCH), swStable);
  if (swStable) M5.Speaker.tone(3000, 15);
}

int nextShown(int from) {
  for (int k = 1; k <= T_COUNT; k++) { int i = (from + k) % T_COUNT; if (shown(i)) return i; }
  return from;
}

void pollButtons() {
  if (settingsOpen) {
    if (M5.BtnB.wasClicked()) settingIdx = nextShown(settingIdx);
    if (M5.BtnA.wasHold()) {
      Setting& st = SETTINGS[settingIdx];
      st.cur = (st.cur + 1) % st.count;
      saveSetting(settingIdx);
    }
    if (M5.BtnA.wasClicked()) settingsOpen = false;
    return;
  }
  if (M5.BtnA.wasHold()) startCalibration(true);
  else if (M5.BtnA.wasClicked()) {
    if (cal != CAL_NONE) { cal = CAL_NONE; setStatus("Cancelled"); }
    else { paused = !paused; accX = accY = 0; }
  }
  if (M5.BtnB.wasClicked()) {
    Setting& st = SETTINGS[T_SPEED];
    st.cur = (st.cur + 1) % st.count;
    saveSetting(T_SPEED);
    setStatus(String("Speed ") + label(T_SPEED), 1200);
  }
  if (M5.BtnB.wasHold()) { settingsOpen = true; settingIdx = 0; }
}

// ---------------------------------------------------------------------
//  Screen
// ---------------------------------------------------------------------
void drawSettings(int W) {
  int n = 0, pos = 0;
  for (int i = 0; i < T_COUNT; i++) if (shown(i)) { n++; if (i <= settingIdx) pos++; }
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawString(SETTINGS[settingIdx].name, W / 2, 50);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(TFT_YELLOW);
  canvas.drawString(label((SettingId)settingIdx), W / 2, 80);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_LIGHTGREY);
  canvas.drawString(String(pos) + " of " + String(n), W / 2, 104);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("B next  hold A change  A close", W / 2, 125);
}

void draw() {
  const int W = canvas.width();
  canvas.fillScreen(TFT_BLACK);
  const bool calibrating = cal != CAL_NONE;
  uint16_t head = calibrating ? TFT_ORANGE : paused ? canvas.color565(90, 90, 90) : canvas.color565(0, 140, 160);
  canvas.fillRect(0, 0, W, 24, head);
  canvas.setTextColor(TFT_WHITE);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.drawString(settingsOpen ? "SETTINGS" : calibrating ? "CALIBRATE" : paused ? "PAUSED" : "MOUSE", 6, 12);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(middle_right);
  Out o = output();
  canvas.drawString(String(o == OUT_USB ? "USB" : o == OUT_BLE ? "Bluetooth" : "Not connected")
                    + (batLevel >= 0 ? "  " + String(batLevel) + "%" : ""), W - 6, 12);

  if (settingsOpen) { drawSettings(W); return; }

  // line 1: what's happening
  String l1, l2;
  uint16_t c2 = TFT_LIGHTGREY;
  if (cal == CAL_STILL) { l1 = "Keep still"; l2 = "Learning the zero point"; }
  else if (cal == CAL_TIP) { l1 = "Tip down"; l2 = "Nod down, or tip the front down"; }
  else if (status.length() && millis() < statusUntil) { l1 = status; }
  else if (!haveAxes) { l1 = "Not calibrated"; l2 = "Hold A to calibrate"; c2 = TFT_ORANGE; }
  else if (paused) { l1 = "Paused"; l2 = "A to move again"; }
  else {
    l1 = swStable ? (val(T_SWITCH) == MOUSE_LEFT ? "Left held" : "Right held") : "Moving";
    l2 = String("Speed ") + label(T_SPEED) + "  Dwell " + label(T_DWELL);
    if (!HAS_USB_HID && output() == OUT_NONE) { l2 = "No USB mouse: set USB Mode to TinyUSB"; c2 = TFT_ORANGE; }
  }
  canvas.setTextDatum(middle_left);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawString(l1, 8, 46);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(c2);
  canvas.drawString(l2, 8, 72);

  // dwell progress
  float p = dwellProgress();
  if (p > 0) {
    canvas.drawRect(8, 86, 150, 8, TFT_DARKGREY);
    canvas.fillRect(9, 87, (int)(148 * p), 6, TFT_GREEN);
  }

  // the turn right now: a dot in a box (centre = still, grey ring = Steady)
  const int bx = W - 48, by = 36, bs = 72, cx = bx + bs / 2, cy = by + bs / 2;
  canvas.drawRect(bx, by, bs, bs, TFT_DARKGREY);
  const float full = 60.0f;  // deg/s at the edge
  int steady = (int)(val(T_STEADY) / full * bs / 2);
  if (steady > 1) canvas.drawCircle(cx, cy, steady, canvas.color565(70, 70, 70));
  int dx = constrain((int)(rateX / full * bs / 2), -bs / 2 + 3, bs / 2 - 3);
  int dy = constrain((int)(rateY / full * bs / 2), -bs / 2 + 3, bs / 2 - 3);
  canvas.fillCircle(cx + dx, cy + dy, 3, swStable ? TFT_GREEN : TFT_CYAN);

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("A pause  hold A calibrate  B speed", W / 2 - 24, 118);
}

// ---------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true;
  cfg.internal_imu = true;
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  M5.Speaker.setVolume(160);
  M5.BtnA.setHoldThresh(600);
  M5.BtnB.setHoldThresh(600);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  M5.Ex_I2C.release();  // the Grove pins are the switch
  pinMode(PIN_KEY, INPUT_PULLUP);

  prefs.begin("mousetest", false);
  loadSettings();
  loadAxes();

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char id[6];
  snprintf(id, sizeof(id), "%02X%02X", mac[4], mac[5]);
  bleName = String("ChatterSwitch ") + id;

#if HAS_USB_HID
  USB.onEvent(onUsbEvent);
  UsbMouse.begin();
  USB.begin();
#endif
  bleBegin();

  if (M5.Imu.getType() == m5::imu_none) { setStatus("No motion sensor", 60000); return; }
  startCalibration(!haveAxes);
  lastSample = micros();
}

void loop() {
  M5.update();
  pollButtons();
  pollSwitch();

  if (M5.Imu.update()) {
    uint32_t now = micros();
    float dt = min((now - lastSample) / 1e6f, 0.05f);
    lastSample = now;
    auto d = M5.Imu.getImuData();
    float g[3] = {d.gyro.x, d.gyro.y, d.gyro.z}, a[3] = {d.accel.x, d.accel.y, d.accel.z};
    if (cal != CAL_NONE) calibrateSample(g, a, dt);
    else moveSample(g, dt);
  }
  if (cal == CAL_NONE) { sendMovement(); checkDwell(); }

  static uint32_t lastPlot = 0;
  if (millis() - lastPlot >= 20) {
    lastPlot = millis();
    Serial.printf("x:%.1f,y:%.1f\n", rateX, rateY);
  }
  static uint32_t lastBat = 0;
  if (millis() - lastBat > 5000 || !lastBat) { lastBat = millis(); batLevel = M5.Power.getBatteryLevel(); }

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 50) {
    lastDraw = millis();
    draw();
    canvas.pushSprite(0, 0);
  }
  delay(1);
}
