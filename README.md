# InsightBasicBoard

Lightweight UART communication library for ATMega328-based InsightBasic payload boards. Handles outbound JSON telemetry, chunked packet framing, inbound command dispatch, and periodic auto-transmission — no third-party dependencies.

## Features

- **Pull-based payload registration** — define sensor data once via `registerPayload()`; the library handles when and how it is transmitted.
- **Periodic auto-transmission** — fires your payload callback automatically every N milliseconds, driven by `update()` in `loop()`.
- **On-demand GET** — the ESP32 comm board can request a fresh payload snapshot at any time by sending `GET`.
- **Chunked JSON framing** — payloads larger than 54 bytes are automatically split into 64-byte framed chunks (`<BEG>`/`~`/`<END>`) and reassembled by the comm board.
- **Compile-time key validation** — all 3-character field keys are validated at compile time via a `constexpr` constructor. Invalid keys cause a build error.
- **Inbound command dispatch** — `PING`, `RESET`, and `GET` are handled internally. User `KEY:VALUE` commands from dashboard widgets are forwarded to a registered `CommandHandler`.
- **USB serial logging** — optionally mirror transmitted packets to the hardware Serial (USB) port for debugging via `enableSerialLogging()`.
- **Zero dependencies** — JSON serialization is built in. No ArduinoJson required.

## Hardware Setup

| Pin | Function | Notes |
|-----|----------|-------|
| D4 (TX) | SoftwareSerial transmit | Sends JSON telemetry to ESP32 comm board |
| D5 (RX) | SoftwareSerial receive | Receives commands from ESP32 comm board |
| A6 | Available for user use | Analog-input only on ATMega328P |
| A7 | Available for user use | Analog-input only on ATMega328P |

> Pins D4 and D5 are reserved by the library. Use `InsightBasicBoard::isPinReserved(pin)` to check at runtime.

## Installation

### Arduino IDE

1. Download this repository as a `.zip` file.
2. In the Arduino IDE: **Sketch > Include Library > Add .ZIP Library...**
3. Select the downloaded `.zip` file.

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/ppi-repo/insightbasic.git
```

### Manual

Copy the `InsightBasicBoard` folder into your Arduino `libraries/` directory:
- **Windows:** `Documents\Arduino\libraries\`
- **macOS / Linux:** `~/Arduino/libraries/`

## Quick Start

```cpp
#include <InsightBasicBoard.h>

InsightBasicBoard board;

void buildPayload(PayloadField* fields, uint8_t* count) {
    fields[0] = {"TMP", D(readTemperature())};  // float
    fields[1] = {"ALT", I(readAltitude())};     // int16_t
    fields[2] = {"FIX", B(gps.hasFix())};       // bool
    *count = 3;
}

void onCommand(const char* key, const char* value) {
    if (strcmp(key, "LED") == 0) {
        digitalWrite(13, atoi(value) ? HIGH : LOW);
    }
}

void setup() {
    board.begin(115200);
    board.enableSerialLogging();              // mirror packets to USB Serial
    board.registerPayload(buildPayload, 1000); // auto-transmit every 1 s
    board.registerCommandHandler(onCommand);
}

void loop() {
    board.update();
}
```

## Key Convention

Every data field key must be **exactly 3 characters**, **uppercase alphanumeric** (`A-Z`, `0-9`). Keys are lookup codes — the comm board resolves them to human-readable labels using its own codebook.

| Valid | Invalid | Reason |
|-------|---------|--------|
| `TMP` | `temp` | lowercase |
| `TP1` | `TEMP` | 4 characters |
| `CO2` | `T1` | 2 characters |
| `ALT` | `T_1` | underscore not allowed |

Invalid keys produce a **compile-time error** — no runtime overhead.

## API Reference

### Value Helper Functions

Use these inside `PayloadField` initializers:

| Function | Type | JSON Output |
|----------|------|-------------|
| `D(float v)` | `VAL_FLOAT` | `23.50` |
| `I(int16_t v)` | `VAL_INT` | `142` |
| `B(bool v)` | `VAL_BOOL` | `true` / `false` |
| `C(char v)` | `VAL_CHAR` | `"A"` |
| `S(const char* v)` | `VAL_STR` | `"NOMINAL"` |

> `D()` is used instead of `F()` to preserve Arduino's `F()` PROGMEM macro.

**Edge cases:** `NaN`/`Inf` → `0.0`, non-printable char → `"?"`, `nullptr` string → `""`.

### InsightBasicBoard Class

#### `begin(uint32_t baud = 115200)`

Initializes SoftwareSerial at the given baud rate. Call once in `setup()`. After this call, pins D4 and D5 are reserved.

#### `registerPayload(PayloadProvider provider, uint32_t intervalMs = 0)`

Registers a payload provider callback. If `intervalMs > 0`, auto-transmits every `intervalMs` ms. If `0`, transmits only on `GET` requests. Call once in `setup()`.

#### `registerCommandHandler(CommandHandler handler)`

Registers a callback for `KEY:VALUE` commands from dashboard widgets. System commands (`PING`, `RESET`, `GET`) are handled internally and never forwarded.

#### `enableSerialLogging()`

Enables mirroring of transmitted JSON packets to the hardware Serial (USB) port. Initializes `Serial` at the same baud rate used in `begin()`. Call once in `setup()` after `begin()`.

#### `update()`

Call on every `loop()` iteration. Handles periodic auto-transmission and non-blocking UART command polling.

#### `send(const PayloadField* fields, uint8_t count, const char* cmd)`

Transmits fields immediately as JSON. Prefer `registerPayload()` for structured telemetry; use `send()` for one-off transmissions.

#### `static isPinReserved(uint8_t pin)`

Returns `true` if the pin is reserved by the library for UART (D4, D5).

### PayloadProvider Callback

```cpp
typedef void (*PayloadProvider)(PayloadField* fields, uint8_t* count);
```

Fill `fields[0..N-1]` with current sensor data and set `*count`. Max fields: `INSIGHT_MAX_FIELDS` (8). Must not block — read from globals or cached values.

### CommandHandler Callback

```cpp
typedef void (*CommandHandler)(const char* key, const char* value);
```

Called for `KEY:VALUE` commands from dashboard widgets:

| Widget | Payload | `key` | `value` |
|--------|---------|-------|---------|
| Relay / Latch | `RLY:1` | `"RLY"` | `"1"` or `"0"` |
| Servo slider | `SRV:90` | `"SRV"` | `"90"` |
| PWM slider | `PWM:127` | `"PWM"` | `"127"` |
| RGB picker | `RGB:255,0,128` | `"RGB"` | `"255,0,128"` |
| Buzzer (hold) | `BUZ:1` | `"BUZ"` | `"1"` or `"0"` |
| Button | `FAN:1` | `"FAN"` | `"1"` |

### System Commands

Handled internally by `update()` — never reach the `CommandHandler`:

| Command | Response |
|---------|----------|
| `PING` | `PONG\n` |
| `RESET` | Watchdog software reset (~15 ms) |
| `GET` | Transmits registered payload immediately |

## Packet Protocol

| Case | Format |
|------|--------|
| Single (data <= 54 B) | `<BEG>data<END>` |
| First chunk | `<BEG>data~` |
| Middle chunk(s) | `~data~` |
| Last chunk | `~data<END>` |

Chunk size: 64 bytes (ATMega328 UART TX buffer boundary).

## Memory Usage

| Resource | Usage |
|----------|-------|
| SRAM (instance) | ~47 bytes |
| SRAM (per field, stack) | 9 bytes x count |
| SRAM (TX buffer, stack) | ~210 bytes peak |
| Flash | ~1.2-1.8 KB |

## Examples

- **[BasicSensorPayload](examples/BasicSensorPayload/BasicSensorPayload.ino)** — `registerPayload()` + `registerCommandHandler()`. The recommended starting template.
- **[MultiChannelPayload](examples/MultiChannelPayload/MultiChannelPayload.ino)** — All 8 channels, all 5 value types, dual operating modes.
- **[CommandControl](examples/CommandControl/CommandControl.ino)** — Command-only: relay, servo, PWM, buzzer, RGB, button widgets.

## Dependencies

None. Uses only Arduino core and AVR-libc headers bundled with the Arduino IDE.

## Compatibility

- **Architecture:** AVR (ATMega328P)
- **Framework:** Arduino
- **Tested with:** Arduino IDE 2.x, PlatformIO

## License

MIT License — see [LICENSE](LICENSE) for full text.

Copyright (c) 2026 Profesir
