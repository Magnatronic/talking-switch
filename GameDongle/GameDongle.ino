// GameDongle - the ChatterSwitch GAME dongle (MOUSE-PLAN.md, step 7).
//
// An M5Stack AtomS3U in a USB port of the Xbox Adaptive Controller (or a PC): a USB
// gamepad laid out the way the XAC takes it, moved by a ChatterSwitch over ESP-NOW.
//
// First version: the power test. It is the gamepad and listens for ESP-NOW all the
// time (the most power it will take). Its button presses gamepad button 1, so it can
// be checked in the XAC's Properties without a stick. The stick doesn't send yet.
//
// LED: blue = waiting for a stick, green = a stick is sending, white = button held.
// Serial (USB): INFO -> its MAC address and how many packets came.
//
// Arduino IDE: board M5AtomS3, USB Mode: USB-OTG (TinyUSB), USB CDC On Boot: Enabled
// (sketch.yaml has them for arduino-cli).

#include "USB.h"
#include "USBHID.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#if ARDUINO_USB_MODE != 0
#error "Set Tools > USB Mode: USB-OTG (TinyUSB) - the gamepad needs it"
#endif

static const int      BUTTON_PIN = 41;     // the AtomS3U's button (pressed = LOW)
static const uint8_t  CHANNEL = 1;         // ESP-NOW channel (the stick uses the same)
static const uint32_t LOST_MS = 500;       // nothing from the stick this long: stick centred, buttons up
static const uint8_t  LED_DIM = 10;        // LED brightness (0-255); low, for the XAC's 100 mA

// The XAC gamepad (the same as ChatterSwitch's GAME over USB): one report with no report ID,
// X, Y, Z, Rz 0-255 (128 = middle), a hat, 12 buttons = 6 bytes. Written flat (one
// collection, no output report), as the core's report map check needs.
static const uint8_t XAC_REPORT_DESC[] = {
  0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,              // Generic Desktop, Joystick, Application
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x35, 0x00, 0x46, 0xFF, 0x00,
  0x75, 0x08, 0x95, 0x04, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,  // X Y Z Rz
  0x25, 0x07, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x09, 0x39, 0x81, 0x42,  // hat 0-7, null
  0x65, 0x00, 0x25, 0x01, 0x45, 0x01, 0x75, 0x01, 0x95, 0x0C,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x81, 0x02,  // buttons 1-12
  0xC0
};
class XacGamepad : public USBHIDDevice {
  USBHID hid;
public:
  XacGamepad() { hid.addDevice(this, sizeof(XAC_REPORT_DESC)); }
  uint16_t _onGetDescriptor(uint8_t* dst) override {
    memcpy(dst, XAC_REPORT_DESC, sizeof(XAC_REPORT_DESC));
    return sizeof(XAC_REPORT_DESC);
  }
  void begin() { hid.begin(); }
  bool ready() { return hid.ready(); }
  // sticks -127..127; button bits 0-11; the hat (d-pad) always centred
  bool send(int8_t lx, int8_t ly, int8_t rx, int8_t ry, uint16_t buttons) {
    uint8_t r[6] = {(uint8_t)(lx + 128), (uint8_t)(ly + 128), (uint8_t)(rx + 128), (uint8_t)(ry + 128),
                    (uint8_t)(0x0F | (buttons << 4)), (uint8_t)(buttons >> 4)};
    return hid.SendReport(0, r, sizeof(r));
  }
};
// A global: USB starts before setup (CDC On Boot) and the core adds the gamepad when it's made.
XacGamepad gamepad;

// What the stick sends (ChatterSwitch GAME, Send by: Wireless)
struct __attribute__((packed)) GamePacket {
  uint8_t magic;      // 'G'
  uint8_t version;    // 1
  int8_t lx, ly, rx, ry;
  uint16_t buttons;   // bit 0 = button 1
};

portMUX_TYPE rxLock = portMUX_INITIALIZER_UNLOCKED;
GamePacket rxPacket = {};
uint32_t rxAt = 0, rxCount = 0;

// Runs in the Wi-Fi task: keep the newest packet.
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(GamePacket) || data[0] != 'G' || data[1] != 1) return;
  portENTER_CRITICAL(&rxLock);
  memcpy(&rxPacket, data, sizeof(GamePacket));
  rxAt = millis();
  rxCount++;
  portEXIT_CRITICAL(&rxLock);
}

void setup() {
  USB.usbPower(100);  // what the XAC's ports give without its power supply (asking for 500 also works)
  setCpuFrequencyMhz(80);  // the least Wi-Fi allows; saves power
  Serial.begin(115200);
  gamepad.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);  // only for ESP-NOW: no network is joined or made
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onReceive);
  else Serial.println("ESP-NOW failed to start");
}

void loop() {
  GamePacket p;
  uint32_t at, count;
  portENTER_CRITICAL(&rxLock);
  p = rxPacket;
  at = rxAt;
  count = rxCount;
  portEXIT_CRITICAL(&rxLock);
  const bool live = count && millis() - at < LOST_MS;
  if (!live) p = GamePacket{'G', 1, 0, 0, 0, 0, 0};  // no stick: centred, buttons up
  const bool button = digitalRead(BUTTON_PIN) == LOW;
  if (button) p.buttons |= 1;

  // the report, when something changed
  static GamePacket sent = {'G', 1, 0, 0, 0, 0, 0xFFFF};
  if (memcmp(&p, &sent, sizeof(p)) && gamepad.ready() && gamepad.send(p.lx, p.ly, p.rx, p.ry, p.buttons)) sent = p;

  static int8_t ledWas = -1;
  const int8_t led = button ? 2 : live ? 1 : 0;
  if (led != ledWas) {
    ledWas = led;
    if (led == 2) rgbLedWrite(RGB_BUILTIN, LED_DIM, LED_DIM, LED_DIM);
    else if (led == 1) rgbLedWrite(RGB_BUILTIN, 0, LED_DIM, 0);
    else rgbLedWrite(RGB_BUILTIN, 0, 0, LED_DIM);
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "INFO")
      Serial.printf("@INFO {\"dongle\":1,\"mac\":\"%s\",\"packets\":%lu,\"live\":%s}\n",
                    WiFi.macAddress().c_str(), (unsigned long)count, live ? "true" : "false");
  }
  delay(2);
}
