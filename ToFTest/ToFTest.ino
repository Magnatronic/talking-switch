// =====================================================================
//  ToFTest - try the M5Stack Unit ToF4M (VL53L1X) as a touch-free switch
// ---------------------------------------------------------------------
//  A separate test sketch (not the ChatterSwitch firmware): plug the ToF4M
//  into the StickS3's Grove port instead of the Unit Key (not both, and not
//  a switch on the mono jack - they share the Grove pins) and upload this.
//
//  Two ways to "press":
//    LINE ... closer than a set distance (5-50 cm). One reading inside
//             counts; two readings back out past a small margin (10% of
//             the distance, at least 4 mm) let go.
//    MOVE ... a movement of a set size (5-30 mm) towards the sensor from
//             where the hand or finger is resting. The resting distance
//             is learnt while not pressed ("Follow" speed), so it copes
//             with the student shifting position - for small movements
//             like a finger. After it has been pressed for the "Settle"
//             time, where the finger is becomes the new resting place and
//             it lets go (without another press), so a finger that relaxes
//             closer doesn't leave it stuck pressed.
//  The screen shows the distance, a scrolling graph of the last few
//  seconds (it zooms to fit, so small movements show) with the trigger
//  line in yellow, and how many presses it has counted. The header turns
//  green while "pressed", with a beep for each press.
//
//    A click .... next size (distance in LINE, movement in MOVE)
//    Hold A ..... LINE / MOVE
//    B click .... reset the count, the graph and the "lost" figure
//    Hold B ..... settings: B next, hold A change, A close
//
//  Settings: Mode, Distance / Movement, Settle, Follow, Ignore beyond,
//  Field of view, Smoothing, Too close, Range (see SETTINGS below).
//
//  Tips: the sensor isn't reliable closer than about 4 cm, and it sees a
//  cone about 27 degrees wide ("Field of view" narrows it) - so a finger
//  works best 5-10 cm away, where it fills more of the view.
//
//  Readings also go to the USB serial port (115200) for the Arduino IDE's
//  Serial Plotter, as  distance_cm:<cm>,trigger_cm:<cm>,pressed:<0 or 10>
//  (open the plotter after the stick has started - its USB port disappears
//  while it restarts).
//
//  Needs the "VL53L1X" library by Pololu (Arduino Library Manager).
//  Board settings: sketch.yaml (the same as the ChatterSwitch firmware).
// =====================================================================

#include <M5Unified.h>
#include <Wire.h>
#include <VL53L1X.h>

VL53L1X tof;
M5Canvas canvas(&M5.Display);

// ---------------------------------------------------------------------
//  Settings (changed on the stick: hold B)
// ---------------------------------------------------------------------
enum SettingId { T_MODE, T_LINE, T_MOVE, T_SETTLE, T_FOLLOW, T_BEYOND, T_FOV, T_SMOOTH, T_CLOSE, T_RANGE, T_COUNT };
struct Setting { const char* name; uint8_t count, cur; int values[8]; const char* labels[8]; };
Setting SETTINGS[T_COUNT] = {
  {"Mode",          2, 1, {0, 1}, {"LINE", "MOVE"}},
  // LINE: press closer than this (mm)
  {"Distance",      8, 3, {50, 60, 80, 100, 150, 200, 300, 500}, {"5 cm", "6 cm", "8 cm", "10 cm", "15 cm", "20 cm", "30 cm", "50 cm"}},
  // MOVE: press on a movement this big towards the sensor (mm)
  {"Movement",      6, 1, {5, 8, 10, 15, 20, 30}, {"5 mm", "8 mm", "10 mm", "15 mm", "20 mm", "30 mm"}},
  // MOVE: pressed this long -> where the hand is becomes the resting place (ms)
  {"Settle",        5, 2, {0, 1000, 2000, 3000, 5000}, {"Off", "1 s", "2 s", "3 s", "5 s"}},
  // MOVE: how quickly the resting distance follows drift (per 1000 readings)
  {"Follow",        3, 1, {8, 20, 50}, {"Slow", "Medium", "Fast"}},
  // anything further than this counts as nothing there (mm, 0 = off)
  {"Ignore beyond", 4, 0, {0, 200, 300, 500}, {"Off", "20 cm", "30 cm", "50 cm"}},
  // the sensor's view: how many of its 16x16 zones it uses
  {"Field of view", 3, 0, {16, 10, 6}, {"Wide", "Medium", "Narrow"}},
  // take the middle of the last 3 readings (removes single spikes, ~30 ms later)
  {"Smoothing",     2, 0, {1, 3}, {"Off", "3 readings"}},
  // closer than the sensor can measure (about 4 cm): pressed, or nothing there
  {"Too close",     2, 0, {1, 0}, {"Pressed", "Nothing"}},
  // Short: up to ~1.3 m, faster (~65 a second), better in sunlight. Long: ~4 m, slower
  {"Range",         2, 0, {0, 1}, {"Short", "Long"}},
};
int val(SettingId i) { return SETTINGS[i].values[SETTINGS[i].cur]; }
const char* label(SettingId i) { return SETTINGS[i].labels[SETTINGS[i].cur]; }
bool moveMode() { return val(T_MODE) == 1; }
bool shown(int i) {  // settings that do something with the current choices
  if (i == T_LINE) return !moveMode();
  if (i == T_MOVE || i == T_SETTLE || i == T_FOLLOW) return moveMode();
  return true;
}

static const int PRESS_READINGS = 1;    // readings in a row past the line to press (1 catches a quick wave)
static const int RELEASE_READINGS = 2;  // ...and back to let go (so one stray reading can't)
static const int LOST_LET_GO = 10;      // while pressed, this many unreliable readings in a row (~150 ms) let go
static const int MIN_MM = 40;           // closer than this isn't reliable ("Too close")
static const int GRAPH_W = 224;         // readings shown, one per pixel (~3.5-4.5 s)
static const int GRAPH_MIN_SPAN = 30;   // the graph shows at least this many mm top to bottom

// ---------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------
bool settingsOpen = false;
int settingIdx = 0;
bool sensorOk = false;
int distMm = -1;             // latest reading used (-1 = nothing there / unreliable)
bool tooClose = false;       // ...closer than the sensor can measure
float restMm = -1;           // MOVE: the resting distance (-1 = not learnt yet)
bool pressed = false;
int streak = 0;              // readings in a row on the other side of the line
uint32_t pressedAt = 0;      // MOVE, Settle: when the press started...
float holdAvg = -1;          // ...and where the hand has been lately (a short average)
int lostRun = 0;             // unreliable readings in a row
int recent[3], recentN = 0;  // Smoothing: the last 3 readings
uint32_t presses = 0;
uint32_t readings = 0, lost = 0, rateCount = 0, rateStart = 0;
float rateHz = 0;
int16_t history[GRAPH_W];      // mm, -1 = none
int16_t historyLine[GRAPH_W];  // the trigger line at that reading
bool historyPressed[GRAPH_W];
int historyPos = 0;

// ---------------------------------------------------------------------
//  Sensor
// ---------------------------------------------------------------------
void applySensorSettings() {
  bool longRange = val(T_RANGE) == 1;
  tof.stopContinuous();
  tof.setDistanceMode(longRange ? VL53L1X::Long : VL53L1X::Short);
  // Short: 15 ms readings (~65 a second, if the sensor accepts it, else 20 ms)
  // so quick movements aren't missed. Long needs longer readings to reach further.
  uint32_t budget = longRange ? 33000 : 15000;
  if (!tof.setMeasurementTimingBudget(budget)) { budget = 20000; tof.setMeasurementTimingBudget(budget); }
  tof.setROISize(val(T_FOV), val(T_FOV));
  tof.startContinuous(budget / 1000);
}

bool startSensor() {
  M5.Ex_I2C.release();
  int sda = M5.getPin(m5::pin_name_t::port_a_sda), scl = M5.getPin(m5::pin_name_t::port_a_scl);
  Wire.begin(sda, scl, 400000);
  tof.setBus(&Wire);
  tof.setTimeout(500);
  if (!tof.init()) return false;
  applySensorSettings();
  return true;
}

void resetStats() {
  presses = readings = lost = 0;
  restMm = -1;
  pressed = false;
  streak = 0;
  holdAvg = -1;
  lostRun = 0;
  recentN = 0;
  for (int i = 0; i < GRAPH_W; i++) { history[i] = historyLine[i] = -1; historyPressed[i] = false; }
}

// The trigger line now: a distance (LINE), or the resting distance minus the movement (MOVE).
int lineMm() {
  if (!moveMode()) return val(T_LINE);
  return restMm < 0 ? -1 : (int)restMm - val(T_MOVE);
}
// How far back past the line to let go: 10% of the distance (at least 4 mm) in
// LINE; half the movement in MOVE.
int marginMm() { return moveMode() ? val(T_MOVE) / 2 : max(4, val(T_LINE) / 10); }

// One reading from the sensor -> the distance used (-1 = nothing there), after
// Too close, Ignore beyond and Smoothing.
int useReading(int mm, uint8_t st, bool timeout, bool& unreliable) {
  unreliable = false;
  tooClose = false;
  // close up, good readings come back as "min range clipped"; keep those
  bool valid = !timeout && (st == VL53L1X::RangeValid || st == VL53L1X::RangeValidMinRangeClipped
                            || st == VL53L1X::RangeValidNoWrapCheckFail);
  if (!timeout && (st == VL53L1X::MinRangeFail || (valid && mm < MIN_MM))) {
    tooClose = true;
    recentN = 0;
    return val(T_CLOSE) ? MIN_MM / 2 : -1;  // "Pressed": as if very close
  }
  if (!valid) { unreliable = true; return -1; }
  if (val(T_BEYOND) && mm > val(T_BEYOND)) { recentN = 0; return -1; }  // too far: nothing there
  if (val(T_SMOOTH) == 3) {
    if (recentN < 3) recent[recentN++] = mm;
    else { recent[0] = recent[1]; recent[1] = recent[2]; recent[2] = mm; }
    if (recentN == 3) {
      int a = recent[0], b = recent[1], c = recent[2];
      mm = max(min(a, b), min(max(a, b), c));  // the middle one
    }
  }
  return mm;
}

// ---------------------------------------------------------------------
//  Screen
// ---------------------------------------------------------------------
String sizeText() {
  if (!moveMode()) return String("Line ") + label(T_LINE);
  return String("Move ") + label(T_MOVE) + ", settle " + label(T_SETTLE);
}

void drawSettings() {
  const int W = canvas.width();
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
  canvas.drawString("Sensor settings - " + String(pos) + " of " + String(n), W / 2, 104);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("B next  hold A change  A close", W / 2, 125);
}

void draw() {
  const int W = canvas.width();
  canvas.fillScreen(TFT_BLACK);
  // header: green while pressed; the range, readings a second and lost on the right
  canvas.fillRect(0, 0, W, 24, pressed ? TFT_GREEN : canvas.color565(90, 90, 90));
  canvas.setTextColor(pressed ? TFT_BLACK : TFT_WHITE);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.drawString(settingsOpen ? "SETTINGS" : pressed ? "PRESSED" : label(T_MODE), 6, 12);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(middle_right);
  int bad = readings ? (int)(lost * 100 / readings) : 0;
  canvas.drawString(String(label(T_RANGE)) + " " + String(rateHz, 0) + "/s  " + String(bad) + "% lost", W - 6, 12);

  if (settingsOpen) { drawSettings(); return; }
  canvas.setTextDatum(middle_center);
  if (!sensorOk) {
    canvas.setTextColor(TFT_ORANGE);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("No sensor", W / 2, 56);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("Plug the ToF4M into Grove, then restart", W / 2, 86);
    return;
  }

  // the distance, big
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(distMm < 0 && !tooClose ? TFT_DARKGREY : TFT_WHITE);
  canvas.drawString(tooClose ? "< 4 cm" : distMm < 0 ? "--" : String(distMm / 10.0f, 1) + " cm", W / 2, 41);

  // the graph: oldest on the left, closer = lower, zoomed to what's shown
  const int gx = (W - GRAPH_W) / 2, gy = 58, gh = 40;
  int lo = INT16_MAX, hi = -1;
  for (int i = 0; i < GRAPH_W; i++) {
    if (history[i] >= 0) { lo = min(lo, (int)history[i]); hi = max(hi, (int)history[i]); }
    if (historyLine[i] >= 0) { lo = min(lo, (int)historyLine[i]); hi = max(hi, (int)historyLine[i]); }
  }
  if (hi < 0) { lo = 0; hi = 1000; }
  if (hi - lo < GRAPH_MIN_SPAN) { int mid = (lo + hi) / 2; lo = mid - GRAPH_MIN_SPAN / 2; hi = mid + GRAPH_MIN_SPAN / 2; }
  const int pad = (hi - lo) / 10 + 2;
  lo -= pad;
  hi += pad;
  auto yOf = [&](int mm) { return gy + gh - 1 - constrain((int)((long)(mm - lo) * (gh - 1) / (hi - lo)), 0, gh - 1); };
  canvas.drawRect(gx - 1, gy - 1, GRAPH_W + 2, gh + 2, TFT_DARKGREY);
  int prevY = -1, prevLine = -1;
  for (int i = 0; i < GRAPH_W; i++) {
    int k = (historyPos + i) % GRAPH_W;  // oldest first
    if (historyLine[k] >= 0) {  // the trigger line (it moves with the resting distance in MOVE)
      int ly = yOf(historyLine[k]);
      if (prevLine >= 0) canvas.drawLine(gx + i - 1, prevLine, gx + i, ly, TFT_YELLOW);
      prevLine = ly;
    } else prevLine = -1;
    if (history[k] < 0) { prevY = -1; continue; }
    int y = yOf(history[k]);
    uint16_t col = historyPressed[k] ? TFT_GREEN : TFT_CYAN;
    if (prevY >= 0) canvas.drawLine(gx + i - 1, prevY, gx + i, y, col);
    else canvas.drawPixel(gx + i, y, col);
    prevY = y;
  }
  canvas.setFont(&fonts::Font0);  // the graph's scale, top and bottom
  canvas.setTextColor(TFT_DARKGREY);
  canvas.setTextDatum(top_left);
  canvas.drawString(String(hi / 10.0f, 1), gx + 1, gy + 1);
  canvas.setTextDatum(bottom_left);
  canvas.drawString(String(lo / 10.0f, 1), gx + 1, gy + gh - 1);

  // details and buttons
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_YELLOW);
  canvas.drawString(sizeText() + " - " + String(presses) + " presses", W / 2, 108);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("A size  hold A mode  hold B settings", W / 2, 125);
}

// ---------------------------------------------------------------------
//  Buttons
// ---------------------------------------------------------------------
int nextShown(int from) {
  for (int k = 1; k <= T_COUNT; k++) { int i = (from + k) % T_COUNT; if (shown(i)) return i; }
  return from;
}

void buttons() {
  if (settingsOpen) {
    if (M5.BtnB.wasClicked()) settingIdx = nextShown(settingIdx);
    if (M5.BtnA.wasHold()) {
      Setting& st = SETTINGS[settingIdx];
      st.cur = (st.cur + 1) % st.count;
      if (settingIdx == T_FOV || settingIdx == T_RANGE) applySensorSettings();
      resetStats();
    }
    if (M5.BtnA.wasClicked()) settingsOpen = false;
    return;
  }
  if (M5.BtnA.wasClicked()) {
    Setting& st = SETTINGS[moveMode() ? T_MOVE : T_LINE];
    st.cur = (st.cur + 1) % st.count;
    resetStats();
  }
  if (M5.BtnA.wasHold()) { SETTINGS[T_MODE].cur ^= 1; resetStats(); }
  if (M5.BtnB.wasClicked()) resetStats();
  if (M5.BtnB.wasHold()) { settingsOpen = true; settingIdx = 0; }
}

// ---------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true;
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  M5.Speaker.setVolume(180);
  M5.BtnA.setHoldThresh(800);
  M5.BtnB.setHoldThresh(800);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  resetStats();
  sensorOk = startSensor();
  rateStart = millis();
}

void loop() {
  M5.update();
  buttons();

  if (sensorOk && tof.dataReady()) {
    int raw = tof.read(false);
    bool unreliable;
    int mm = useReading(raw, tof.ranging_data.range_status, tof.timeoutOccurred(), unreliable);
    bool here = mm >= 0;
    readings++;
    rateCount++;
    if (unreliable) lost++;
    lostRun = unreliable ? lostRun + 1 : 0;
    distMm = mm;

    // MOVE: learn the resting distance while not pressed (straight away the first
    // time); when nothing's there, learn it again when something comes back
    if (moveMode() && !pressed) {
      if (!here) restMm = -1;
      else if (restMm < 0) restMm = mm;
      else restMm += (mm - restMm) * val(T_FOLLOW) / 1000.0f;
    }

    // press: past the line; let go: back past it plus the margin, or nothing
    // there. While pressed, a few unreliable readings don't let go - only
    // ~150 ms of them do; moving back lets go straight away.
    const int line = lineMm();
    bool other;
    if (line < 0) other = false;
    else if (!pressed) other = here && mm < line;
    else if (here) other = mm > line + marginMm();
    else other = unreliable ? lostRun >= LOST_LET_GO : true;
    streak = other ? streak + 1 : 0;
    if (streak >= (pressed ? RELEASE_READINGS : PRESS_READINGS)) {
      streak = 0;
      pressed = !pressed;
      pressedAt = millis();
      holdAvg = -1;
      if (pressed) { presses++; M5.Speaker.tone(1500, 60); }
    }
    // MOVE: pressed for the settle time -> where the hand is now (a short
    // average) becomes the resting place: let go, without counting a press. It
    // doesn't wait for the hand to be still - a relaxed finger creeps.
    if (moveMode() && pressed && here) holdAvg = holdAvg < 0 ? mm : holdAvg + (mm - holdAvg) * 0.15f;
    if (moveMode() && pressed && val(T_SETTLE) && holdAvg >= 0 && millis() - pressedAt >= (uint32_t)val(T_SETTLE)) {
      pressed = false;
      streak = 0;
      restMm = holdAvg;
      holdAvg = -1;
      M5.Speaker.tone(600, 30);  // a quiet low tick: settled
    }

    history[historyPos] = distMm;
    historyLine[historyPos] = line;
    historyPressed[historyPos] = pressed;
    historyPos = (historyPos + 1) % GRAPH_W;
    // for the Serial Plotter: name:value pairs (pressed is drawn as 0 or 10)
    Serial.printf("distance_cm:%.1f,trigger_cm:%.1f,pressed:%d\n", distMm < 0 ? 0.0f : distMm / 10.0f,
                  line < 0 ? 0.0f : line / 10.0f, pressed ? 10 : 0);
  }
  if (millis() - rateStart >= 1000) {
    rateHz = rateCount * 1000.0f / (millis() - rateStart);
    rateCount = 0;
    rateStart = millis();
  }

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 50) {
    lastDraw = millis();
    draw();
    canvas.pushSprite(0, 0);
  }
  delay(1);
}
