/*
 * BasicSensorPayload.ino — InsightBasicBoard library example
 * Author: Eric Obeng (Profesir), Erictronics Systems
 * Date: 2026-06-01
 *
 * Demonstrates the two core patterns:
 *   1. registerPayload()        — automatic telemetry (data OUT to dashboard)
 *   2. registerCommandHandler() — receiving commands  (data IN from dashboard)
 *
 * Three of the five value-type helpers are used:
 *   D(v) — decimal (float) value (e.g. sensor reading)
 *   I(v) — int16_t value (e.g. integer count or status code)
 *   B(v) — bool value (e.g. GPS fix flag)
 *
 * Key convention
 * ──────────────
 * Every key must be exactly 3 characters, uppercase alphanumeric (A–Z, 0–9).
 * The comm board resolves keys using its own codebook:
 *   TMP → "Temperature (°C)"
 *   HUM → "Relative Humidity (%)"
 *   FIX → "GPS Fix Acquired"
 *
 * An invalid key (wrong length, lowercase, non-alphanumeric) is caught at
 * compile time — the code will not build. No runtime check is needed.
 *
 * Dashboard widgets
 * ─────────────────
 * Add a Relay widget with command key "LED" to toggle the on-board LED from
 * the web dashboard. The relay widget sends "LED:1" (ON) or "LED:0" (OFF),
 * which the command handler below receives as key="LED", value="1" or "0".
 *
 * Hardware
 * ────────
 * ATMega328P payload board connected to the InsightBasic Core Comm Board via
 * hardware UART (pins 0 RX / 1 TX). Sensors simulated with analogRead here.
 * Pins A6 and A7 are available for your own sensor use.
 */

#include <InsightBasicBoard.h>

InsightBasicBoard board;

static const uint8_t LED_PIN = 13;

// ── Payload provider callback
// ───────────────────────────────────────────────── Called by the library
// whenever the comm board sends "GET", or automatically every intervalMs
// milliseconds (set in registerPayload below). Keep this fast — read from
// globals or sensor libraries, never block here.
void buildPayload(PayloadField* fields, uint8_t* count) {
  float temperature = (analogRead(A0) / 1023.0f) * 100.0f;  // simulated °C
  float humidity = (analogRead(A1) / 1023.0f) * 100.0f;     // simulated %
  bool gpsFix = (analogRead(A2) > 512);  // simulated fix flag

  fields[0] = {"TMP", D(temperature)};  // float → {"TMP":23.50}
  fields[1] = {"HUM", D(humidity)};     // float → {"HUM":61.20}
  fields[2] = {"FIX", B(gpsFix)};       // bool  → {"FIX":true}
  *count = 3;
}

// ── Command handler callback
// ───────────────────────────────────────────────── Called when the comm board
// forwards a KEY:VALUE command from the web dashboard. System commands (PING,
// RESET, GET) are handled internally and never reach this callback.
//
// key   — 3-char command key matching the widget's configured command key
// value — everything after the colon (e.g. "1", "0"); empty string if bare cmd
void onCommand(const char* key, const char* value) {
  if (strcmp(key, "LED") == 0) {
    digitalWrite(LED_PIN, atoi(value) ? HIGH : LOW);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  board.begin(115200);

  // Register the payload provider — auto-transmit every 1000 ms
  board.registerPayload(buildPayload, 1000);

  // Register the command handler — receives KEY:VALUE commands from widgets
  board.registerCommandHandler(onCommand);
}

void loop() {
  // Handles periodic auto-transmission, inbound commands (PING, RESET, GET),
  // and user commands (KEY:VALUE) — all non-blocking, no delay() needed.
  board.update();
}
