/*
 * CommandControl.ino — InsightBasicBoard library example
 * Author: Eric Obeng (Profesir), Erictronics Systems
 * Date: 2026-06-19
 *
 * Demonstrates registerCommandHandler() — receiving commands from the web
 * dashboard's input widgets and driving actuators on the ATmega.
 *
 * Each dashboard widget type sends a KEY:VALUE string over WebSocket → ESP32 →
 * UART. The library splits on the first ':' and delivers (key, value) to the
 * callback registered below. The key is always a 3-char uppercase code matching
 * the widget's configured command key.
 *
 * ── Widget → UART payload → onCommand() mapping ─────────────────────────────
 *  Widget type       │ UART payload       │ key   │ value
 * ───────────────────┼────────────────────┼───────┼────────────────
 *  Relay / Latch SW  │ RLY:1  or  RLY:0   │ "RLY" │ "1" or "0"
 *  Button            │ FAN:1              │ "FAN" │ "1"
 *  Buzzer (hold)     │ BUZ:1  or  BUZ:0   │ "BUZ" │ "1" or "0"
 *  Momentary Switch  │ BTN:1  or  BTN:0   │ "BTN" │ "1" or "0"
 *  Servo slider      │ SRV:90             │ "SRV" │ "90"
 *  PWM slider        │ PWM:127            │ "PWM" │ "127"
 *  PWM w/ channel    │ PWM:1,200          │ "PWM" │ "1,200"
 *  RGB color picker  │ RGB:255,0,128      │ "RGB" │ "255,0,128"
 *  Serial terminal   │ (free-form text)   │ varies│ varies
 * ───────────────────┴────────────────────┴───────┴────────────────
 *
 * Hardware
 * ────────
 * Pin 13 — on-board LED   (relay / latch switch demo)
 * Pin  9 — servo signal   (servo slider demo)
 * Pin  6 — PWM output     (PWM slider demo)
 * Pin  3 — buzzer         (buzzer widget demo)
 * Pins 10,11,A0 — RGB LED (RGB picker demo, common-cathode)
 *
 * Adapt pin assignments and add #includes for your actual hardware (e.g.
 * Servo.h). This example uses raw analogWrite / digitalWrite to keep
 * dependencies minimal.
 */

#include <InsightBasicBoard.h>

InsightBasicBoard board;

// ── Pin assignments (adapt to your wiring) ──────────────────────────────────
static const uint8_t LED_PIN   = 13;
static const uint8_t SERVO_PIN = 9;
static const uint8_t PWM_PIN   = 6;
static const uint8_t BUZ_PIN   = 3;
static const uint8_t RGB_R_PIN = 10;
static const uint8_t RGB_G_PIN = 11;
static const uint8_t RGB_B_PIN = A0;  // use a PWM-capable pin on your board

// ── Helper: parse "R,G,B" CSV string into three bytes ───────────────────────
static void parseRGB(const char* csv, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = (uint8_t)atoi(csv);
  const char* p = strchr(csv, ',');
  if (!p) return;
  g = (uint8_t)atoi(p + 1);
  p = strchr(p + 1, ',');
  if (!p) return;
  b = (uint8_t)atoi(p + 1);
}

// ── Helper: parse "channel,value" CSV into channel + value ──────────────────
static void parseChanVal(const char* csv, uint8_t& chan, uint8_t& val) {
  chan = (uint8_t)atoi(csv);
  const char* p = strchr(csv, ',');
  if (p) val = (uint8_t)atoi(p + 1);
}

// ── Command handler callback ────────────────────────────────────────────────
// Called for every KEY:VALUE command from the web dashboard.
// System commands (PING, RESET, GET) are handled internally by the library
// and never reach this callback.
void onCommand(const char* key, const char* value) {

  // Relay widget / Latch switch — "RLY:1" or "RLY:0"
  if (strcmp(key, "RLY") == 0) {
    digitalWrite(LED_PIN, atoi(value) ? HIGH : LOW);
  }

  // Servo slider — "SRV:90" (angle 0–180)
  else if (strcmp(key, "SRV") == 0) {
    // Map 0–180 to ~544–2400 µs pulse using analogWrite as placeholder.
    // Replace with Servo.write(atoi(value)) when using the Servo library.
    uint8_t angle = (uint8_t)constrain(atoi(value), 0, 180);
    analogWrite(SERVO_PIN, map(angle, 0, 180, 0, 255));
  }

  // PWM slider — "PWM:127" (0–255) or "PWM:1,200" (channel,value)
  else if (strcmp(key, "PWM") == 0) {
    if (strchr(value, ',')) {
      uint8_t chan = 0, val = 0;
      parseChanVal(value, chan, val);
      // Route by channel number — extend this switch for more channels
      if (chan == 0) analogWrite(PWM_PIN, val);
    } else {
      analogWrite(PWM_PIN, (uint8_t)constrain(atoi(value), 0, 255));
    }
  }

  // Buzzer (hold) — "BUZ:1" (press) or "BUZ:0" (release)
  else if (strcmp(key, "BUZ") == 0) {
    if (atoi(value)) {
      tone(BUZ_PIN, 1000);
    } else {
      noTone(BUZ_PIN);
    }
  }

  // Momentary switch — "BTN:1" (down) or "BTN:0" (up)
  else if (strcmp(key, "BTN") == 0) {
    digitalWrite(LED_PIN, atoi(value) ? HIGH : LOW);
  }

  // RGB color picker — "RGB:255,0,128"
  else if (strcmp(key, "RGB") == 0) {
    uint8_t r = 0, g = 0, b = 0;
    parseRGB(value, r, g, b);
    analogWrite(RGB_R_PIN, r);
    analogWrite(RGB_G_PIN, g);
    analogWrite(RGB_B_PIN, b);
  }

  // Button (one-shot) — "FAN:1" or bare "FAN"
  else if (strcmp(key, "FAN") == 0) {
    digitalWrite(LED_PIN, atoi(value) ? HIGH : LOW);
  }
}

void setup() {
  pinMode(LED_PIN,   OUTPUT);
  pinMode(SERVO_PIN, OUTPUT);
  pinMode(PWM_PIN,   OUTPUT);
  pinMode(BUZ_PIN,   OUTPUT);
  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT);

  board.begin(115200);
  board.registerCommandHandler(onCommand);
}

void loop() {
  board.update();
}
