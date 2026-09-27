// =====================================================================
//  ToFTest - try the M5Stack Unit ToF4M (VL53L1X) as a touch-free switch
// ---------------------------------------------------------------------
//  A separate test sketch (not the ChatterSwitch firmware): plug the ToF4M
//  into the StickS3's Grove port instead of the Unit Key (not both, and not
//  a switch on the mono jack - they share the Grove pins) and upload this.
//
//  The screen shows the distance, a bar with the trigger line, and how
//  many "presses" it has counted. Moving closer than the trigger distance
//  counts as a press (with a beep); moving back out lets go. The switch's
//  header turns green while "pressed".
//
//    A click .... next trigger distance (5, 10, 15, 20, 30, 50 cm)
//    B click .... reset the count and the nearest / furthest readings
//    Hold B ..... Short range (up to ~1.3 m, better in sunlight) / Long
//                 (up to ~4 m)
//
//  Readings are also sent on the USB serial port (115200) as
//    tof,<ms since boot>,<mm or -1>,<pressed 0/1>
//  so they can be logged or plotted (Arduino IDE Serial Plotter).
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
static const int CONFIRM = 2;         // readings in a row needed to press / let go
static const int BAR_MAX_CM = 100;    // the bar's full width

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

void setRange() {
  tof.setDistanceMode(longRange ? VL53L1X::Long : VL53L1X::Short);
  tof.setMeasurementTimingBudget(longRange ? 50000 : 33000);  // microseconds per reading
  tof.startContinuous(longRange ? 50 : 35);                   // ms between readings
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
}

void draw() {
  const int W = canvas.width(), H = canvas.height();
  canvas.fillScreen(TFT_BLACK);
  // header: green while pressed
  canvas.fillRect(0, 0, W, 30, pressed ? TFT_GREEN : canvas.color565(90, 90, 90));
  canvas.setTextColor(pressed ? TFT_BLACK : TFT_WHITE);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.drawString(pressed ? "PRESSED" : "ToF TEST", 6, 15);
  canvas.setTextDatum(middle_right);
  canvas.drawString(longRange ? "Long" : "Short", W - 6, 15);

  canvas.setTextDatum(middle_center);
  if (!sensorOk) {
    canvas.setTextColor(TFT_ORANGE);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.drawString("No sensor", W / 2, 58);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("Plug the ToF4M into Grove, then restart", W / 2, 88);
    return;
  }

  // the distance, big
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(distMm < 0 ? TFT_DARKGREY : TFT_WHITE);
  canvas.drawString(distMm < 0 ? "--" : String(distMm / 10.0f, 1) + " cm", W / 2, 50);

  // bar: 0 to BAR_MAX_CM, with the trigger line
  const int bx = 8, by = 72, bw = W - 16, bh = 10;
  canvas.drawRect(bx, by, bw, bh, TFT_DARKGREY);
  if (distMm >= 0) {
    int fill = min(bw - 2, (int)((long)distMm * (bw - 2) / (BAR_MAX_CM * 10)));
    canvas.fillRect(bx + 1, by + 1, fill, bh - 2, pressed ? TFT_GREEN : TFT_CYAN);
  }
  int tx = bx + 1 + TRIGGERS_CM[triggerIdx] * (bw - 2) / BAR_MAX_CM;
  canvas.fillRect(tx - 1, by - 4, 3, bh + 8, TFT_YELLOW);

  // details
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_YELLOW);
  canvas.drawString("Trigger " + String(TRIGGERS_CM[triggerIdx]) + " cm - " + String(presses) + " presses", W / 2, 96);
  canvas.setTextColor(TFT_LIGHTGREY);
  String range = nearest < 0 ? String("--") : String(nearest / 10.0f, 1) + "-" + String(furthest / 10.0f, 1) + " cm";
  int bad = readings ? (int)(invalid * 100 / readings) : 0;
  canvas.drawString(range + "  " + String(rateHz, 0) + "/s  " + String(bad) + "% lost", W / 2, 112);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("A trigger  B reset  hold B range", W / 2, 127);
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
    if (streak >= CONFIRM) {
      streak = 0;
      pressed = !pressed;
      if (pressed) { presses++; M5.Speaker.tone(1500, 60); }
    }
    Serial.printf("tof,%lu,%d,%d\n", (unsigned long)millis(), distMm, pressed ? 1 : 0);
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
