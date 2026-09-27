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
//             is learnt slowly while not pressed, so it copes with the
//             student shifting position - for small movements like a
//             finger.
//  The screen shows the distance, a scrolling graph of the last few
//  seconds (it zooms to fit, so small movements show) with the trigger
//  line in yellow, and how many presses it has counted. The header turns
//  green while "pressed", with a beep for each press.
//
//    A click .... next trigger size (distance in LINE, movement in MOVE)
//    Hold A ..... LINE / MOVE
//    B click .... reset the count, the graph and the "lost" figure
//    Hold B ..... Short range (up to ~1.3 m, faster, better in sunlight) /
//                 Long (up to ~4 m, slower)
//
//  Tips: the sensor isn't reliable closer than about 4 cm, and it sees a
//  cone about 27 degrees wide - so a finger works best 5-10 cm away, where
//  it fills more of the view (further away a finger is only part of what
//  it sees).
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

static const int LINES_CM[] = {5, 6, 8, 10, 15, 20, 30, 50};  // LINE: press closer than this
static const int MOVES_MM[] = {5, 8, 10, 15, 20, 30};         // MOVE: press on a movement this big
static const int NUM_LINES = sizeof(LINES_CM) / sizeof(LINES_CM[0]);
static const int NUM_MOVES = sizeof(MOVES_MM) / sizeof(MOVES_MM[0]);
static const int PRESS_READINGS = 1;    // readings in a row past the line to press (1 catches a quick wave)
static const int RELEASE_READINGS = 2;  // ...and back to let go (so one stray reading can't)
static const float REST_FOLLOW = 0.02f; // MOVE: how quickly the resting distance follows (per reading)
static const int GRAPH_W = 224;         // readings shown, one per pixel (~3.5-4.5 s)
static const int GRAPH_MIN_SPAN = 30;   // the graph shows at least this many mm top to bottom

bool moveMode = false;
int lineIdx = 3;             // 10 cm
int moveIdx = 1;             // 8 mm
bool longRange = false;
bool sensorOk = false;
int distMm = -1;             // latest reading (-1 = nothing in range / unreliable)
float restMm = -1;           // MOVE: the resting distance (-1 = not learnt yet)
bool pressed = false;
int streak = 0;              // readings in a row on the other side of the line
uint32_t presses = 0;
uint32_t readings = 0, lost = 0, rateCount = 0, rateStart = 0;
float rateHz = 0;
int16_t history[GRAPH_W];    // mm, -1 = none
int16_t historyLine[GRAPH_W];  // the trigger line at that reading
bool historyPressed[GRAPH_W];
int historyPos = 0;

void setRange() {
  tof.stopContinuous();
  tof.setDistanceMode(longRange ? VL53L1X::Long : VL53L1X::Short);
  // Short: 15 ms readings (~65 a second, if the sensor accepts it, else 20 ms)
  // so quick movements aren't missed. Long needs longer readings to reach further.
  uint32_t budget = longRange ? 33000 : 15000;
  if (!tof.setMeasurementTimingBudget(budget)) { budget = 20000; tof.setMeasurementTimingBudget(budget); }
  tof.startContinuous(budget / 1000);
}

bool startSensor() {
  M5.Ex_I2C.release();
  int sda = M5.getPin(m5::pin_name_t::port_a_sda), scl = M5.getPin(m5::pin_name_t::port_a_scl);
  Wire.begin(sda, scl, 400000);
  tof.setBus(&Wire);
  tof.setTimeout(500);
  if (!tof.init()) return false;
  setRange();
  return true;
}

void resetStats() {
  presses = readings = lost = 0;
  restMm = -1;
  pressed = false;
  streak = 0;
  for (int i = 0; i < GRAPH_W; i++) { history[i] = historyLine[i] = -1; historyPressed[i] = false; }
}

// The trigger line now: a distance (LINE), or the resting distance minus the movement (MOVE).
int lineMm() {
  if (!moveMode) return LINES_CM[lineIdx] * 10;
  return restMm < 0 ? -1 : (int)restMm - MOVES_MM[moveIdx];
}
// How far back past the line to let go: 10% of the distance (at least 4 mm) in
// LINE; half the movement in MOVE.
int marginMm() { return moveMode ? MOVES_MM[moveIdx] / 2 : max(4, LINES_CM[lineIdx]); }

String sizeText() { return moveMode ? String(MOVES_MM[moveIdx]) + " mm move" : String(LINES_CM[lineIdx]) + " cm"; }

void draw() {
  const int W = canvas.width();
  canvas.fillScreen(TFT_BLACK);
  // header: green while pressed; the way it presses, the range and readings a second
  canvas.fillRect(0, 0, W, 24, pressed ? TFT_GREEN : canvas.color565(90, 90, 90));
  canvas.setTextColor(pressed ? TFT_BLACK : TFT_WHITE);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.drawString(pressed ? "PRESSED" : moveMode ? "MOVE" : "LINE", 6, 12);
  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(middle_right);
  canvas.drawString(String(longRange ? "Long " : "Short ") + String(rateHz, 0) + "/s", W - 6, 12);

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
  canvas.setTextColor(distMm < 0 ? TFT_DARKGREY : TFT_WHITE);
  canvas.drawString(distMm < 0 ? "--" : String(distMm / 10.0f, 1) + " cm", W / 2, 41);

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
  int bad = readings ? (int)(lost * 100 / readings) : 0;
  canvas.drawString(sizeText() + " - " + String(presses) + " presses - " + String(bad) + "% lost", W / 2, 108);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("A size  hold A mode  B reset  hold B range", W / 2, 125);
}

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
  if (M5.BtnA.wasClicked()) {
    if (moveMode) moveIdx = (moveIdx + 1) % NUM_MOVES;
    else lineIdx = (lineIdx + 1) % NUM_LINES;
  }
  if (M5.BtnA.wasHold()) { moveMode = !moveMode; resetStats(); }
  if (M5.BtnB.wasClicked()) resetStats();
  if (M5.BtnB.wasHold() && sensorOk) { longRange = !longRange; setRange(); resetStats(); }

  if (sensorOk && tof.dataReady()) {
    int mm = tof.read(false);
    // close up, good readings come back as "min range clipped"; keep those too
    uint8_t st = tof.ranging_data.range_status;
    bool valid = !tof.timeoutOccurred() && (st == VL53L1X::RangeValid || st == VL53L1X::RangeValidMinRangeClipped
                                            || st == VL53L1X::RangeValidNoWrapCheckFail);
    readings++;
    rateCount++;
    if (!valid) lost++;
    distMm = valid ? mm : -1;

    // MOVE: learn the resting distance while not pressed (straight away the first time)
    if (moveMode && valid && !pressed) restMm = restMm < 0 ? mm : restMm + (mm - restMm) * REST_FOLLOW;
    if (moveMode && !valid && !pressed) restMm = -1;  // hand gone: learn again when it's back

    // press: past the line; let go: back past it plus the margin
    const int line = lineMm();
    bool other = line < 0 ? false
               : pressed ? (!valid || mm > line + marginMm())
                         : (valid && mm < line);
    streak = other ? streak + 1 : 0;
    if (streak >= (pressed ? RELEASE_READINGS : PRESS_READINGS)) {
      streak = 0;
      pressed = !pressed;
      if (pressed) { presses++; M5.Speaker.tone(1500, 60); }
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
