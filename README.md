# InsightBasicBoard

Lightweight UART communication library for ATMega328-based InsightBasic payload boards. Handles outbound JSON telemetry, chunked packet framing, inbound command dispatch, and periodic auto-transmission -- no third-party dependencies.

---

## Table of Contents

- [Features](#features)
- [Hardware Setup](#hardware-setup)
- [Installation](#installation)
  - [Arduino IDE (ZIP)](#arduino-ide-zip)
  - [Arduino IDE (Manual)](#arduino-ide-manual)
  - [PlatformIO](#platformio)
- [Quick Start](#quick-start)
- [Key Convention](#key-convention)
  - [Compile-Time Enforcement](#compile-time-enforcement)
  - [Codebook Separation](#codebook-separation)
- [API Reference](#api-reference)
  - [InsightBasicBoard Class](#insightbasicboard-class)
    - [board.begin()](#boardbegin)
    - [board.registerPayload()](#boardregisterpayload)
    - [board.registerCommandHandler()](#boardregistercommandhandler)
    - [board.enableSerialLogging()](#boardenableseriallogging)
    - [board.update()](#boardupdate)
    - [board.send()](#boardsend)
    - [board.isPinReserved()](#boardispinreserved)
  - [Types](#types)
  - [Value Helper Functions](#value-helper-functions)
  - [PayloadProvider Callback](#payloadprovider-callback)
  - [CommandHandler Callback](#commandhandler-callback)
- [Inbound Command Handling](#inbound-command-handling)
  - [System Commands](#system-commands)
  - [User Commands (KEY:VALUE)](#user-commands-keyvalue)
- [Packet Protocol](#packet-protocol)
- [Memory Usage](#memory-usage)
- [Examples](#examples)
- [Dependencies](#dependencies)
- [Compatibility](#compatibility)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- **Pull-based payload registration** -- define sensor data once via `registerPayload()`; the library handles when and how it is transmitted.
- **Periodic auto-transmission** -- fires your payload callback automatically every N milliseconds, driven by `update()` in `loop()`.
- **On-demand GET** -- the ESP32 comm board can request a fresh payload snapshot at any time by sending `GET`.
- **Chunked JSON framing** -- payloads larger than 54 bytes are automatically split into 64-byte framed chunks (`<BEG>`/`~`/`<END>`) and reassembled by the comm board.
- **Compile-time key validation** -- all 3-character field keys are validated at compile time via a `constexpr` constructor. Invalid keys cause a build error.
- **Inbound command dispatch** -- `PING`, `RESET`, and `GET` are handled internally. User `KEY:VALUE` commands from dashboard widgets are forwarded to a registered `CommandHandler`.
- **USB serial logging** -- optionally mirror transmitted packets to the hardware Serial (USB) port for debugging via `enableSerialLogging()`.
- **Zero dependencies** -- JSON serialization is built in. No ArduinoJson required.

---

## Hardware Setup

| Pin | Function | Notes |
|-----|----------|-------|
| D4 (TX) | SoftwareSerial transmit | Sends JSON telemetry to ESP32 comm board |
| D5 (RX) | SoftwareSerial receive | Receives commands from ESP32 comm board |
| A6 | Available for user use | Analog-input only on ATMega328P |
| A7 | Available for user use | Analog-input only on ATMega328P |

> **Note:** Pins D4 and D5 are reserved by the library for SoftwareSerial communication. Use `InsightBasicBoard::isPinReserved(pin)` to check at runtime. Pins A6 and A7 on the ATMega328P are **analog-input only** -- never call `digitalRead()` or `digitalWrite()` on them.

---

## Installation

### Arduino IDE (ZIP)

1. Download this repository as a `.zip` file (click **Code > Download ZIP** on GitHub).
2. In the Arduino IDE go to **Sketch > Include Library > Add .ZIP Library...** and select the downloaded file.
3. The library now appears under **File > Examples > InsightBasicBoard**.

### Arduino IDE (Manual)

1. Download or clone this repository.
2. Copy the `InsightBasicBoard` folder into your Arduino libraries directory:
   - **Windows:** `Documents\Arduino\libraries\`
   - **macOS:** `~/Documents/Arduino/libraries/`
   - **Linux:** `~/Arduino/libraries/`
3. Restart the Arduino IDE.

Your libraries folder should look like this:

```
Arduino/
  libraries/
    InsightBasicBoard/
      src/
        InsightBasicBoard.h
        InsightBasicBoard.cpp
      examples/
        BasicSensorPayload/
          BasicSensorPayload.ino
        ...
      library.properties
      keywords.txt
      LICENSE
      README.md
```

### PlatformIO

**Option A -- Git dependency (recommended):**

Add the GitHub URL to your `platformio.ini`:

```ini
[env:your_board]
platform = atmelavr
board = uno
framework = arduino
lib_deps =
    https://github.com/ppi-repo/insightbasic.git
```

**Option B -- Local project library:**

Place the `InsightBasicBoard` folder inside your project's `lib/` directory. PlatformIO discovers it automatically -- no `lib_deps` entry needed.

---

## Quick Start

The recommended pattern combines `registerPayload()` for outbound telemetry with `registerCommandHandler()` for inbound commands from the web dashboard.

```cpp
#include <InsightBasicBoard.h>

InsightBasicBoard board;

// Called by the library automatically every 1000 ms,
// and on demand whenever the comm board sends "GET".
void buildPayload(PayloadField* fields, uint8_t* count) {
    fields[0] = {"TMP", D(readTemperature())};  // float  -> {"TMP":23.50}
    fields[1] = {"ALT", I(readAltitude())};     // int    -> {"ALT":142}
    fields[2] = {"FIX", B(gps.hasFix())};       // bool   -> {"FIX":true}
    *count = 3;
}

// Called when the comm board forwards a KEY:VALUE command from a dashboard widget.
void onCommand(const char* key, const char* value) {
    if (strcmp(key, "LED") == 0) digitalWrite(13, atoi(value) ? HIGH : LOW);
    if (strcmp(key, "SRV") == 0) myServo.write(atoi(value));
}

void setup() {
    pinMode(13, OUTPUT);
    board.begin(115200);
    board.enableSerialLogging();              // mirror packets to USB Serial

    board.registerPayload(buildPayload, 1000);
    board.registerCommandHandler(onCommand);
}

void loop() {
    board.update(); // all timing and command handling happens here
}
```

`loop()` needs nothing else. No `delay()`, no manual `send()` call, no timestamp bookkeeping.

---

## Key Convention

Every data field key must satisfy all three rules simultaneously:

1. **Exactly 3 characters** -- no more, no fewer.
2. **Uppercase alphanumeric only** -- characters `A-Z` and `0-9`. Lowercase letters, underscores, hyphens, and spaces are not permitted.
3. **Lookup codes, not labels** -- the comm board resolves a code like `TP1` to `"Temperature Sensor 1 (degC)"` using its own codebook. The payload board never embeds units or descriptions in the key itself.

**Valid examples:** `TMP`, `PRS`, `ALT`, `HUM`, `TP1`, `TP2`, `AC1`, `LAT`, `LNG`, `CO2`, `FIX`, `MOD`, `STS`

**Invalid examples:**

| Key | Reason |
|-----|--------|
| `temp` | lowercase characters |
| `TEMP` | 4 characters |
| `T1` | 2 characters |
| `T_1` | underscore not permitted |

### Compile-Time Enforcement

Key validation is performed by the `FieldKey` `constexpr` constructor using the constexpr-poison pattern. Any key string literal that violates the rules causes a **compile error at the call site** -- the firmware will not build. There is no runtime check, no silent failure, and no SRAM overhead.

```cpp
PayloadField fields[] = {
    {"TMP", D(23.5)},   // valid -- compiles
    {"temp", D(23.5)},  // COMPILE ERROR: lowercase not allowed
    {"TEMP", D(23.5)},  // COMPILE ERROR: 4 characters
    {"T1",  D(23.5)},   // COMPILE ERROR: 2 characters
};
```

### Codebook Separation

The payload board transmits raw 3-char codes. The comm board (or ground station software) holds the codebook that maps codes to human-readable labels, units, and display formatting. This separation keeps the payload firmware small and allows the codebook to be updated on the ground side without reflashing the payload board.

---

## API Reference

### InsightBasicBoard Class

---

#### `board.begin()`

```cpp
void begin(uint32_t baud = 115200);
```

Initialises SoftwareSerial at the given baud rate. Must be called once in `setup()` before any transmission or reception can occur. After this call, pins D4 (TX) and D5 (RX) are reserved for UART communication.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `baud` | `115200` | UART baud rate. Must match the comm board configuration. |

---

#### `board.registerPayload()`

```cpp
void registerPayload(PayloadProvider provider, uint32_t intervalMs = 0);
```

Registers a payload provider callback and configures transmission behaviour. Call once in `setup()` after `begin()`. Replaces any previously registered provider.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `provider` | -- | Pointer to the user's payload callback. Must not be `nullptr`. |
| `intervalMs` | `0` | Auto-transmission interval in milliseconds. `0` = GET-only mode (no automatic transmission). |

**Transmission modes:**

| `intervalMs` | Behaviour |
|--------------|-----------|
| `0` | GET-only -- payload is sent only when the comm board sends `GET`. |
| `> 0` | Periodic -- payload is sent automatically every `intervalMs` ms **and** on every `GET`. |

> **Interval seeding:** `_lastSendMs` is initialised to `millis()` at the moment `registerPayload()` is called. The first automatic transmission fires one full interval later -- not immediately on the next `update()` call.

> **GET independence:** A `GET` command received between two periodic ticks does **not** reset the interval clock. The next automatic transmission fires at the originally scheduled time.

---

#### `board.registerCommandHandler()`

```cpp
void registerCommandHandler(CommandHandler handler);
```

Registers a callback that is invoked when the comm board forwards a `KEY:VALUE` command from the web dashboard. System commands (`PING`, `RESET`, `GET`) are handled internally and never reach this callback.

Call once in `setup()` after `begin()`. Replaces any previously registered handler.

---

#### `board.enableSerialLogging()`

```cpp
void enableSerialLogging();
```

Enables mirroring of transmitted JSON packets to the hardware Serial (USB) port for debugging. Initialises `Serial` at the same baud rate used in `begin()`. Call once in `setup()` after `begin()`.

---

#### `board.update()`

```cpp
void update();
```

The library's main heartbeat. Must be called on **every iteration** of `loop()` -- missing calls will cause commands to be delayed or dropped and periodic transmissions to drift.

Two things happen on each call:

1. **Periodic auto-transmission check** -- if a provider is registered and `intervalMs > 0`, checks whether the interval has elapsed and fires a transmission if so.
2. **Non-blocking UART receive** -- drains all available bytes from SoftwareSerial, accumulates them into the internal 32-byte command buffer, and dispatches completed commands on newline or carriage return.

---

#### `board.send()`

```cpp
void send(const PayloadField* fields, uint8_t count, const char* cmd);
```

Serialises `fields[0..count-1]` to JSON and transmits immediately via the chunked framing protocol. `count` is silently clamped to `INSIGHT_MAX_FIELDS` (8).

> **Prefer `registerPayload()` for structured sensor data.** Use `send()` only for one-off or ad-hoc transmissions that fall outside the normal payload cycle.

| Parameter | Description |
|-----------|-------------|
| `fields` | Pointer to the first `PayloadField`. Must not be `nullptr`. |
| `count` | Number of fields to transmit. Clamped to 8. |
| `cmd` | String written as the `"CMD"` field in the JSON object. |

---

#### `board.isPinReserved()`

```cpp
static bool isPinReserved(uint8_t pin);
```

Returns `true` if the given pin is reserved by the library for UART communication (pins D4 and D5 after `begin()` has been called). Use this to avoid accidentally reconfiguring UART pins in your sketch.

---

### Types

#### `FieldKey`

```cpp
struct FieldKey {
    char code[4]; // 3-char key + null terminator
    constexpr FieldKey(const char* k);
};
```

A compile-time-enforced 3-character alphanumeric uppercase key. Constructed implicitly from a string literal inside a `PayloadField` initializer -- users never construct `FieldKey` directly. An invalid key literal produces a compile error.

#### `ValType`

```cpp
enum ValType : uint8_t {
    VAL_FLOAT,   // float       -- JSON number,  e.g. 23.50
    VAL_INT,     // int16_t     -- JSON integer, e.g. 142
    VAL_BOOL,    // uint8_t     -- JSON boolean, e.g. true / false
    VAL_CHAR,    // char        -- 1-char JSON string, e.g. "A"
    VAL_STR,     // const char* -- JSON string,  e.g. "NOMINAL"
};
```

Tags the active member of the `PayloadVal` union. Stored as `uint8_t` to minimise SRAM usage. Users never set `ValType` directly -- use the helper functions below.

#### `PayloadVal`

```cpp
struct PayloadVal {
    ValType type;
    union {
        float       f;   // VAL_FLOAT
        int16_t     i;   // VAL_INT
        uint8_t     b;   // VAL_BOOL  (0 = false, non-zero = true)
        char        c;   // VAL_CHAR  (single printable ASCII character)
        const char* s;   // VAL_STR   (pointer must outlive the send() call)
    };
};
```

A tagged union holding one value of any supported type. The union is 4 bytes (dominated by `float` / pointer); the tag adds 1 byte. Construct exclusively via the helper functions below.

> **`VAL_STR` lifetime:** The `const char*` passed to `S()` must remain valid for the entire duration of the library's internal `send` call. String literals (e.g. `S("OK")`) have indefinite lifetime and are always safe. Do **not** pass a pointer to a stack-local `char` array that may go out of scope before the transmission completes.

#### `PayloadField`

```cpp
struct PayloadField {
    FieldKey   key; // 3-char key, compile-time enforced
    PayloadVal val; // tagged union value
};
```

The only data type the user works with directly. Initialise as an aggregate:

```cpp
PayloadField fields[] = {
    {"TMP", D(23.5)},      // float  -> {"TMP":23.50}
    {"ALT", I(142)},       // int    -> {"ALT":142}
    {"FIX", B(true)},      // bool   -> {"FIX":true}
    {"MOD", C('A')},       // char   -> {"MOD":"A"}
    {"STS", S("NOMINAL")}, // string -> {"STS":"NOMINAL"}
};
```

---

### Value Helper Functions

Use these to construct `PayloadVal` values inside `PayloadField` initializers. They are plain inline functions -- zero flash overhead.

| Function | Stored type | JSON output example |
|----------|-------------|---------------------|
| `D(float v)` | `VAL_FLOAT` | `23.50` |
| `I(int16_t v)` | `VAL_INT` | `142` |
| `B(bool v)` | `VAL_BOOL` | `true` / `false` |
| `C(char v)` | `VAL_CHAR` | `"A"` |
| `S(const char* v)` | `VAL_STR` | `"NOMINAL"` |

> **Why `D()` and not `F()`?** The float helper is named `D()` (decimal) to preserve Arduino's built-in `F()` PROGMEM macro for string literals. Both can be used freely in the same sketch.

**Serialization edge cases handled automatically:**

| Input | Behaviour |
|-------|-----------|
| `NaN` or `Inf` passed to `D()` | Transmitted as `0.0` |
| Non-printable char passed to `C()` | Transmitted as `"?"` |
| `nullptr` passed to `S()` | Transmitted as `""` |

---

### PayloadProvider Callback

```cpp
typedef void (*PayloadProvider)(PayloadField* fields, uint8_t* count);
```

A user-defined function that fills a `PayloadField` array with the current sensor snapshot. The library calls this function whenever a transmission is due -- either on the auto-transmission interval or in response to a `GET` command.

| Parameter | Direction | Description |
|-----------|-----------|-------------|
| `fields` | out | Pre-allocated array of `INSIGHT_MAX_FIELDS` (8) entries. Write fields by index starting at 0. |
| `count` | out | Set `*count` to the number of fields written before returning. The library reads only up to `*count` entries. |

**Rules:**
- Must not block. Read from globals or cached sensor values -- never call `delay()` or wait on I2C/SPI inside the callback.
- Must write at least one field and set `*count >= 1`. If `*count` is left at 0, the library skips the transmission silently.
- Must not write more than `INSIGHT_MAX_FIELDS` (8) fields.

```cpp
void buildPayload(PayloadField* fields, uint8_t* count) {
    fields[0] = {"TMP", D(readTemp())};
    fields[1] = {"HUM", D(readHumidity())};
    fields[2] = {"FIX", B(gps.hasFix())};
    *count = 3;
}
```

---

### CommandHandler Callback

```cpp
typedef void (*CommandHandler)(const char* key, const char* value);
```

A user-defined function called when the comm board forwards a `KEY:VALUE` command from a web dashboard widget. System commands (`PING`, `RESET`, `GET`) are handled internally and never reach this callback.

| Parameter | Direction | Description |
|-----------|-----------|-------------|
| `key` | in | 3-character uppercase command key matching the widget's configured command key (e.g. `"SRV"`, `"RGB"`, `"RLY"`). Truncated to 3 chars if the sender sent more. |
| `value` | in | Everything after the first colon (e.g. `"90"`, `"255,0,0"`, `"1"`). Empty string `""` if the command had no colon (bare word). |

**Widget type -> value format:**

| Widget | Example payload | `key` | `value` |
|--------|----------------|-------|---------|
| Relay / Latch switch | `RLY:1` | `"RLY"` | `"1"` or `"0"` |
| Button | `FAN:1` | `"FAN"` | `"1"` (or `""` if no value) |
| Buzzer (hold) | `BUZ:1` | `"BUZ"` | `"1"` (press) or `"0"` (release) |
| Momentary switch | `BTN:0` | `"BTN"` | `"1"` (down) or `"0"` (up) |
| Servo slider | `SRV:90` | `"SRV"` | `"90"` (0-180) |
| PWM slider | `PWM:127` | `"PWM"` | `"127"` (0-255) |
| PWM w/ channel | `PWM:1,200` | `"PWM"` | `"1,200"` (channel,value) |
| RGB color picker | `RGB:255,0,128` | `"RGB"` | `"255,0,128"` (R,G,B) |
| Serial terminal | *(free-form)* | varies | varies |

```cpp
void onCommand(const char* key, const char* value) {
    if (strcmp(key, "SRV") == 0) myServo.write(atoi(value));
    if (strcmp(key, "RLY") == 0) digitalWrite(RELAY_PIN, atoi(value));
    if (strcmp(key, "RGB") == 0) {
        uint8_t r = atoi(value);
        const char* p = strchr(value, ',');
        uint8_t g = p ? atoi(p + 1) : 0;
        p = p ? strchr(p + 1, ',') : NULL;
        uint8_t b = p ? atoi(p + 1) : 0;
        analogWrite(R_PIN, r);
        analogWrite(G_PIN, g);
        analogWrite(B_PIN, b);
    }
}
```

---

## Inbound Command Handling

The library listens for newline-terminated ASCII commands sent by the comm board. Commands are dispatched by `update()` in the following priority:

### System Commands

Handled internally -- never forwarded to the user's `CommandHandler`.

| Command | Response | Notes |
|---------|----------|-------|
| `PING` | `PONG\n` | Connectivity check. |
| `RESET` | *(none -- board resets)* | Triggers a watchdog-based software reset (~15 ms). |
| `GET` | Registered payload JSON | Invokes the provider and transmits immediately. Does not reset the auto-transmission interval. |

### User Commands (KEY:VALUE)

Any command that is not `PING`, `RESET`, or `GET` is forwarded to the registered `CommandHandler` callback. The library splits on the first `:` character: everything before is the key (truncated to 3 chars), everything after is the value. If no colon is present, the entire string is the key and the value is an empty string.

> **Reserved key:** `GET` is reserved as a system command. Do not use it as a widget command key -- it will be intercepted by the library and never reach your callback.

**Buffer:** The inbound buffer is 32 bytes. Commands longer than 31 characters are silently truncated. All built-in commands and standard widget payloads (e.g. `RGB:255,255,255` = 15 chars) fit well within this limit.

---

## Packet Protocol

### Framing Tokens

| Token | Wire bytes | Role |
|-------|-----------|------|
| `<BEG>` | 5 | Start of message |
| `<END>` | 5 | End of message |
| `~` | 1 | Chunk continuation marker |

### Packet Formats

| Case | Wire format |
|------|-------------|
| Single packet (data <= 54 bytes) | `<BEG>data<END>` |
| First chunk | `<BEG>data~` |
| Middle chunk(s) | `~data~` |
| Last chunk | `~data<END>` |

The chunk size is **64 bytes** -- one ATMega328P UART TX buffer flush. Maximum data bytes per chunk type:

| Chunk type | Max data bytes | Calculation |
|------------|---------------|-------------|
| Single | 54 | 64 - 5 (`<BEG>`) - 5 (`<END>`) |
| First | 58 | 64 - 5 (`<BEG>`) - 1 (`~`) |
| Middle | 62 | 64 - 1 (`~`) - 1 (`~`) |
| Last | 58 | 64 - 1 (`~`) - 5 (`<END>`) |

All payload transmission -- whether triggered by the interval, a `GET`, or a direct `send()` call -- passes through the same `_transmitChunked()` pipeline. The comm board accumulates chunks, strips framing tokens, and concatenates data regions to reconstruct the full JSON object.

---

## Memory Usage

### Class instance (SRAM)

| Member | Type | Size |
|--------|------|------|
| `_rxBuf[32]` | `char[32]` | 32 bytes |
| `_rxIdx` | `uint8_t` | 1 byte |
| `_provider` | function pointer | 2 bytes (AVR) |
| `_cmdHandler` | function pointer | 2 bytes (AVR) |
| `_providerCmd` | `const char*` | 2 bytes (AVR) |
| `_intervalMs` | `uint32_t` | 4 bytes |
| `_lastSendMs` | `uint32_t` | 4 bytes |
| **Total** | | **~47 bytes** |

### Per-field on the user's stack

`PayloadField` = `FieldKey` (4 B) + `PayloadVal` tag (1 B) + union (4 B) = **9 bytes per field**. Eight fields = 72 bytes.

### Peak stack usage during transmission

`char buf[210]` is allocated on the stack inside `_sendRegisteredPayload()` and `send()` for the duration of the JSON build + transmit, then released. Peak stack usage is approximately **210 bytes** above baseline.

### Flash

The library contributes approximately **1.2-1.8 KB of flash** depending on which value types are used, compared to ~3-4 KB when using ArduinoJson.

---

## Examples

All examples live in `examples/` and open from **File > Examples > InsightBasicBoard** in the Arduino IDE.

| Sketch | What it does |
|--------|-------------|
| [BasicSensorPayload](examples/BasicSensorPayload/BasicSensorPayload.ino) | Demonstrates both core patterns: `registerPayload()` for automatic telemetry and `registerCommandHandler()` for receiving commands. The recommended starting template. |
| [MultiChannelPayload](examples/MultiChannelPayload/MultiChannelPayload.ino) | Fills all 8 channels and uses all five value helpers. Two operating modes selected by a digital pin, demonstrating runtime mode switching. |
| [CommandControl](examples/CommandControl/CommandControl.ino) | Focuses on `registerCommandHandler()` -- receiving commands from every web dashboard widget type (relay, servo, PWM, buzzer, RGB, button). Command-only, no payload provider. |

---

## Dependencies

This library has **no third-party dependencies**. JSON serialization is built in. AVR-libc headers (`avr/wdt.h`, `stdlib.h`, `string.h`, `math.h`) are bundled with every Arduino IDE installation targeting AVR boards.

---

## Compatibility

| Attribute | Value |
|-----------|-------|
| Microcontroller | ATmega328P |
| Boards tested | Arduino Uno, Arduino Nano, bare ATmega328P at 16 MHz |
| Framework | Arduino |
| Build system | Arduino IDE 1.8+, Arduino IDE 2.x, PlatformIO |
| UART | SoftwareSerial on D4 (TX) / D5 (RX) |

---

## Contributing

1. Fork this repository.
2. Create a feature branch (`git checkout -b feature/my-change`).
3. Commit your changes.
4. Push to the branch and open a Pull Request.

Please ensure all examples compile cleanly for the ATmega328P before submitting.

---

## License

MIT License -- see [LICENSE](LICENSE) for full text.

Copyright (c) 2026 Profesir

---

*Insight Basic Experimental Board -- designed by Erictronics.*
