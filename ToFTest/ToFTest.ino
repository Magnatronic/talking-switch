// =====================================================================
//  ToFTest - try the M5Stack Unit ToF4M (VL53L1X) as a touch-free switch
// ---------------------------------------------------------------------
//  A separate test sketch (not the ChatterSwitch firmware): plug the ToF4M
//  into the StickS3's Grove port instead of the Unit Key (not both, and not
//  a switch on the mono jack - they share the Grove pins) and upload this.
//
//  The screen shows the distance, a scrolling graph of the last few
//  seconds with the trigger line (yellow), and how many "presses" it has
//  counted. One reading closer than the trigger distance counts as a press
//  (with a beep); two readings back out let go. The header turns green
//  while "pressed".
//
//    A click .... next trigger distance (5, 10, 15, 20, 30, 50 cm)
//    B click .... reset the count, the graph and the "lost" figure
//    Hold B ..... Short range (up to ~1.3 m, better in sunlight) / Long
//                 (up to ~4 m)
//
//  Readings also go to the USB serial port (115200) for the Arduino IDE's
//  Serial Plotter, as  distance_cm:<cm>,trigger_cm:<cm>,pressed:<0 or 10>
//  (open the plotter after the stick has started - its USB port disappears
//  while it restarts).
//
//  Needs the "VL53L1X" library by Pololu (Arduino Library Manager).
//  Board settings: the same as the ChatterSwitch firmware.
// =====================================================================

#include <M5Unified.h>
#include <Wire.h>
#include <VL53L1X.h>

VL53L1X tof;
M5Canvas canvas(&M5.Display);

static const int TRIGGERS_CM[] = {5, 10, 15, 20, 30, 50};
static const int NUM_TRIGGERS = sizeof(TRIGGERS_CM) / sizeof(TRIGGERS_CM[0]);
static const int HYSTERESIS_MM = 15;  // move this much further out to let go (stops flicker at the line)
static const int PRESS_READINGS = 1;  // readings in a row inside the line to press (1 catches a quick wave)
static const int RELEASE_READINGS = 2; // ...and outside it to let go (so one stray reading can't)
static const int GRAPH_MAX_CM = 100;  // the graph's top

int triggerIdx = 1;          // 10 cm
bool longRange = false;
bool sensorOk = false;
int distMm = -1;             // latest reading (-1 = nothing in range / invalid)
bool pressed = false;
int streak = 0;              // readings in a row on the other side of the line
uint32_t presses = 0;
int nearest = -1, furthest = -1;
uint32_t readings = 0, invalid = 0, rateCount = 0, rateStart = 0;
float rateHz = 0;
static const int GRAPH_W = 224;       // readings shown (one per pixel, ~4.5 s at 50/s)
int16_t history[GRAPH_W];             // mm, -1 = none
bool historyPressed[GRAPH_W];
int historyPos = 0;

void setRange() {
  tof.setDistanceMode(longRange ? VL53L1X::Long : VL53L1X::Short);
  // Short: 20 ms readings, ~50 a second, so quick movements aren't missed.
  // Long needs longer readings to reach further.
  tof.setMeasurementTimingBudget(longRange ? 33000 : 20000);  // microseconds per reading
  tof.startContinuous(longRange ? 33 : 20);                   // ms between readings
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
  presses = 0;
  nearest = furthest = -1;
  readings = invalid = 0;
  for (int i = 0; i < GRAPH_W; i++) { history[i] = -1; historyPressed[i] = false; }
}

void draw() {
  const int W = canvas.width();
  canvas.fillScreen(TFT_BLACK);
  // header: green while pressed; the range and readings per second on the right
  canvas.fillRect(0, 0, W, 24, pressed ? TFT_GREEN : canvas.color565(90, 90, 90));
  canvas.setTextColor(pressed ? TFT_BLACK : TFT_WHITE);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.drawString(pressed ? "PRESSED" : "ToF TEST", 6, 12);
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

  // the graph: the last few seconds, oldest on the left; closer = lower
  const int gx = (W - GRAPH_W) / 2, gy = 58, gh = 40;
  canvas.drawRect(gx - 1, gy - 1, GRAPH_W + 2, gh + 2, TFT_DARKGREY);
  auto yOf = [&](int mm) { return gy + gh - 1 - min(gh - 1, (int)((long)mm * (gh - 1) / (GRAPH_MAX_CM * 10))); };
  int ty = yOf(TRIGGERS_CM[triggerIdx] * 10);
  canvas.drawFastHLine(gx, ty, GRAPH_W, TFT_YELLOW);
  int prevY = -1;
  for (int i = 0; i < GRAPH_W; i++) {
    int k = (historyPos + i) % GRAPH_W;  // oldest first
    if (history[k] < 0) { prevY = -1; continue; }
    int y = yOf(history[k]);
    uint16_t col = historyPressed[k] ? TFT_GREEN : TFT_CYAN;
    if (prevY >= 0) canvas.drawLine(gx + i - 1, prevY, gx + i, y, col);
    else canvas.drawPixel(gx + i, y, col);
    prevY = y;
  }

  // details and buttons
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_YELLOW);
  int bad = readings ? (int)(invalid * 100 / readings) : 0;
  canvas.drawString("Trigger " + String(TRIGGERS_CM[triggerIdx]) + " cm - " + String(presses) + " presses - "
                    + String(bad) + "% lost", W / 2, 108);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("A trigger  B reset  hold B range", W / 2, 125);
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true;
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  M5.Speaker.setVolume(180);
  M5.BtnB.setHoldThresh(800);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  resetStats();
  sensorOk = startSensor();
  rateStart = millis();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasClicked()) triggerIdx = (triggerIdx + 1) % NUM_TRIGGERS;
  if (M5.BtnB.wasClicked()) resetStats();
  if (M5.BtnB.wasHold() && sensorOk) { longRange = !longRange; tof.stopContinuous(); setRange(); }

  if (sensorOk && tof.dataReady()) {
    int mm = tof.read(false);
    bool valid = !tof.timeoutOccurred() && tof.ranging_data.range_status == VL53L1X::RangeValid;
    readings++;
    rateCount++;
    if (!valid) invalid++;
    distMm = valid ? mm : -1;
    if (valid) {
      if (nearest < 0 || mm < nearest) nearest = mm;
      if (mm > furthest) furthest = mm;
    }
    // pressed: closer than the trigger line; let go: back out past it plus the hysteresis
    const int line = TRIGGERS_CM[triggerIdx] * 10;
    bool other = pressed ? (!valid || mm > line + HYSTERESIS_MM) : (valid && mm < line);
    streak = other ? streak + 1 : 0;
    if (streak >= (pressed ? RELEASE_READINGS : PRESS_READINGS)) {
      streak = 0;
      pressed = !pressed;
      if (pressed) { presses++; M5.Speaker.tone(1500, 60); }
    }
    history[historyPos] = distMm;
    historyPressed[historyPos] = pressed;
    historyPos = (historyPos + 1) % GRAPH_W;
    // for the Serial Plotter: name:value pairs (pressed is drawn as 0 or 10)
    Serial.printf("distance_cm:%.1f,trigger_cm:%d,pressed:%d\n", distMm < 0 ? 0.0f : distMm / 10.0f,
                  TRIGGERS_CM[triggerIdx], pressed ? 10 : 0);
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
  delay(2);
}
