#include "InsightBasicBoard.h"

// ── Static members
// ─────────────────────────────────────────────────────────────

bool InsightBasicBoard::_serialActive = false;

// ── Constructor
// ────────────────────────────────────────────────────────────────

InsightBasicBoard::InsightBasicBoard()
    : _serial(INSIGHT_UART_RX_PIN, INSIGHT_UART_TX_PIN),
      _rxIdx(0),
      _provider(nullptr),
      _cmdHandler(nullptr),
      _providerCmd("DATA"),
      _intervalMs(0),
      _lastSendMs(0),
      _logToSerial(false),
      _baudRate(115200) {
  memset(_rxBuf, 0, sizeof(_rxBuf));
}

// ── Public API
// ─────────────────────────────────────────────────────────────────

void InsightBasicBoard::begin(uint32_t baud) {
  // SoftwareSerial::begin() does not reconfigure the TX pin — only the
  // constructor does.  Re-assert it here so begin() is safe to call after
  // external code (e.g. InsightBasicLabs::begin()) has reset the pin to INPUT.
  pinMode(INSIGHT_UART_TX_PIN, OUTPUT);
  digitalWrite(INSIGHT_UART_TX_PIN, HIGH);
  _serial.begin(baud);
  _serialActive = true;
  _baudRate = baud;
}

bool InsightBasicBoard::isPinReserved(uint8_t pin) {
  return _serialActive &&
         (pin == INSIGHT_UART_TX_PIN || pin == INSIGHT_UART_RX_PIN);
}

void InsightBasicBoard::update() {
  // ── Periodic auto-transmission ──────────────────────────────────────────
  // Checked before UART polling so a GET arriving in the same loop() tick
  // does not delay a due transmission by another full interval.
  if (_provider && _intervalMs > 0) {
    uint32_t now = millis();
    if (now - _lastSendMs >= _intervalMs) {
      _lastSendMs = now;
      _sendRegisteredPayload();
    }
  }

  // Non-blocking: drain all available bytes, accumulate into _rxBuf.
  // Dispatch on newline or carriage return; silently discard overflow
  // characters.
  while (_serial.available()) {
    char c = (char)_serial.read();
    if (c == '\n' || c == '\r') {
      _rxBuf[_rxIdx] = '\0';
      if (_rxIdx > 0) {
        _dispatch(_rxBuf);
      }
      _rxIdx = 0;
    } else if (_rxIdx < (uint8_t)(sizeof(_rxBuf) - 1)) {
      _rxBuf[_rxIdx++] = c;
    }
    // Characters beyond the buffer are silently discarded — overflow
    // protection.
  }
}

void InsightBasicBoard::send(const PayloadField* fields, uint8_t count,
                             const char* cmd) {
  if (!fields) return;
  if (count > INSIGHT_MAX_FIELDS) count = INSIGHT_MAX_FIELDS;

  char buf[INSIGHT_JSON_BUF_SIZE];
  _buildJson(buf, INSIGHT_JSON_BUF_SIZE, fields, count, cmd);
  _transmitChunked(buf);
}

void InsightBasicBoard::registerPayload(PayloadProvider provider,
                                        uint32_t intervalMs) {
  _provider = provider;
  _providerCmd = "PAYLOAD";
  _intervalMs = intervalMs;
  _lastSendMs = millis();  // start the interval clock now, not from boot
}

void InsightBasicBoard::registerCommandHandler(CommandHandler handler) {
  _cmdHandler = handler;
}

void InsightBasicBoard::enableSerialLogging() {
  _logToSerial = true;
  Serial.begin(_baudRate);
}

// ── JSON serialization
// ─────────────────────────────────────────────────────────

uint8_t InsightBasicBoard::_buildJson(char* buf, uint8_t bufSize,
                                      const PayloadField* fields,
                                      uint8_t count, const char* cmd) {
  uint8_t pos = 0;
  buf[pos++] = '{';

  // Inject "CMD":"<value>" as the first field.
  // pos > 1 is used throughout to detect whether a field has already been
  // written — avoids a separate bool on the stack.
  if (cmd && (uint8_t)(pos + 8) <= (uint8_t)(bufSize - 2)) {
    memcpy(buf + pos, "\"CMD\":", 6);
    pos += 6;
    pos += _writeStr(buf + pos, (uint8_t)(bufSize - pos - 2), cmd);
  }

  for (uint8_t i = 0; i < count; i++) {
    if (pos > 1) {
      if (pos >= (uint8_t)(bufSize - 2)) break;
      buf[pos++] = ',';
    }

    if ((uint8_t)(pos + 6) > (uint8_t)(bufSize - 2)) break;
    buf[pos++] = '"';
    buf[pos++] = fields[i].key.code[0];
    buf[pos++] = fields[i].key.code[1];
    buf[pos++] = fields[i].key.code[2];
    buf[pos++] = '"';
    buf[pos++] = ':';

    uint8_t room = (uint8_t)(bufSize - pos - 2);
    uint8_t written = 0;

    switch (fields[i].val.type) {
      case VAL_FLOAT:
        written = _writeFloat(buf + pos, room, fields[i].val.f);
        break;
      case VAL_INT:
        written = _writeInt(buf + pos, room, fields[i].val.i);
        break;
      case VAL_BOOL:
        written = _writeBool(buf + pos, room, fields[i].val.b);
        break;
      case VAL_CHAR:
        written = _writeChar(buf + pos, room, fields[i].val.c);
        break;
      case VAL_STR:
        written = _writeStr(buf + pos, room, fields[i].val.s);
        break;
    }
    pos += written;
  }

  buf[pos++] = '}';
  buf[pos] = '\0';
  return pos;
}

// ── Chunked framing transmission
// ───────────────────────────────────────────────
//
// Protocol:
//   Single packet (data ≤ 54 B):  <BEG>data<END>
//   First chunk:                  <BEG>data~
//   Middle chunk(s):              ~data~
//   Last chunk:                   ~data<END>
//
// Chunk size = 64 bytes (matches ATMega328 UART TX buffer).
// Constants INSIGHT_*_MAX define max data bytes per chunk type.

void InsightBasicBoard::_transmitChunked(const char* buf) {
  if (_logToSerial) Serial.println(buf);

  uint8_t len = (uint8_t)strlen(buf);

  // Fits in a single framed packet
  if (len <= INSIGHT_SINGLE_MAX) {
    _serial.print("<BEG>");
    _serial.print(buf);
    _serial.println("<END>");
    return;
  }

  uint8_t pos = 0;
  bool firstChunk = true;

  while (pos < len) {
    uint8_t remaining = len - pos;

    if (firstChunk) {
      uint8_t chunkLen = (remaining < INSIGHT_FIRST_MAX)
                             ? remaining
                             : (uint8_t)INSIGHT_FIRST_MAX;
      _serial.print("<BEG>");
      _serial.write(buf + pos, chunkLen);
      _serial.print("~");
      pos += chunkLen;
      firstChunk = false;

    } else if (remaining <= INSIGHT_LAST_MAX) {
      // Final chunk — all remaining data fits
      _serial.print("~");
      _serial.write(buf + pos, remaining);
      _serial.println("<END>");
      break;

    } else {
      // Middle chunk
      uint8_t chunkLen = (remaining < INSIGHT_MIDDLE_MAX)
                             ? remaining
                             : (uint8_t)INSIGHT_MIDDLE_MAX;
      _serial.print("~");
      _serial.write(buf + pos, chunkLen);
      _serial.print("~");
      pos += chunkLen;
    }
  }
}

// ── Inbound command handling
// ───────────────────────────────────────────────────

void InsightBasicBoard::_dispatch(const char* cmd) {
  if (strcmp(cmd, "PING") == 0) {
    _serial.print("PONG\n");

  } else if (strcmp(cmd, "RESET") == 0) {
    _softReset();

  } else if (strcmp(cmd, "GET") == 0) {
    // Comm board is requesting the current registered payload snapshot.
    // _lastSendMs is intentionally NOT updated here — a GET between two
    // periodic ticks does not reset the auto-transmission interval.
    _sendRegisteredPayload();

  } else if (_cmdHandler) {
    // Route KEY:VALUE commands to the user-registered handler.
    // If there's a colon, split into key + value; otherwise treat the
    // entire string as the key with an empty value.
    const char* colon = strchr(cmd, ':');
    if (colon) {
      // Null-terminate the key portion in a local copy
      char key[4] = {};
      uint8_t keyLen = (uint8_t)(colon - cmd);
      if (keyLen > 3) keyLen = 3;
      memcpy(key, cmd, keyLen);
      key[keyLen] = '\0';
      _cmdHandler(key, colon + 1);
    } else {
      _cmdHandler(cmd, "");
    }
  }
}

// ── Registered payload dispatch
// ────────────────────────────────────────────────

void InsightBasicBoard::_sendRegisteredPayload() {
  if (!_provider) return;

  // FieldKey has no default constructor (its constexpr ctor requires a const
  // char* argument), so a plain PayloadField[N] declaration is ill-formed.
  // Allocate raw storage instead and cast — the provider is responsible for
  // writing every slot it reports via *count, so no slot is ever read
  // uninitialised.
  uint8_t raw[sizeof(PayloadField) * INSIGHT_MAX_FIELDS];
  PayloadField* fields = reinterpret_cast<PayloadField*>(raw);
  uint8_t count = 0;
  _provider(fields, &count);

  if (count == 0) return;
  if (count > INSIGHT_MAX_FIELDS) count = INSIGHT_MAX_FIELDS;

  char buf[INSIGHT_JSON_BUF_SIZE];
  _buildJson(buf, INSIGHT_JSON_BUF_SIZE, fields, count, _providerCmd);
  _transmitChunked(buf);
}

// ── Hardware helpers
// ───────────────────────────────────────────────────────────

void InsightBasicBoard::_softReset() {
  wdt_enable(WDTO_15MS);
  while (true) {
  }  // spin until watchdog fires (~15 ms)
}
