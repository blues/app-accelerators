// host_sleep.h
//
// Swan + Notecarrier XI host sleep: STM32 STOP2, woken by the Notecard's ATTN pin.
//
// The Notecard cannot switch the Swan's power. The Notecarrier XI's EN pin is
// the Notecard's own enable — pulling it to GND turns the Notecard OFF — so it
// must never be used for host sleep. The pattern that works: wire the XI's
// ATTN header pin to a Swan GPIO, ask the Notecard to hold ATTN low for N
// seconds with card.attn "sleep", put the STM32U5 in STOP2 (a few uA, RAM
// retained), and wake on the ATTN rising edge. Execution resumes in place; no
// cold boot, no state to persist.
//
// Wiring:  Notecarrier XI ATTN (0.1" header) ---- wire ---- Swan D5
//          (plus the usual 3V3, GND, SDA, SCL between the XI headers and the
//          Swan). Leave the XI's EN pin unconnected.
// Build:   Tools > USB support (if available) > None. With the USB CDC stack
//          enabled and no USB host attached (battery power), the USB wakeup
//          interrupt exits STOP2 immediately.
// Library: STM32duino Low Power (+ its dependency STM32duino RTC). On the
//          STM32U5 LowPower.deepSleep() is STOP2, as on the L4.
//
// Usage:
//   #include "host_sleep.h"
//   void setup() { ...; notecard.begin(); ...; cxSleepBegin(); }
//   void loop()  { sample(); cxSleepUntilAttn(notecard, seconds, NULL, &debugSerial); }
//
// See https://dev.blues.io/example-apps/samples/putting-a-host-to-sleep-between-sensor-readings/

#pragma once

#include <Arduino.h>
#include <Notecard.h>
#include <STM32LowPower.h>

#ifndef HOST_ATTN_PIN
#define HOST_ATTN_PIN D5
#endif

// Nothing to do in the ISR: waking up is the side effect.
static void cxAttnIsr() {}

// Call once from setup(), after notecard.begin().
static inline void cxSleepBegin() {
  // ATTN is driven push-pull by the Notecard; no pull resistor needed (one
  // would just leak current against it).
  pinMode(HOST_ATTN_PIN, INPUT);
  LowPower.begin();
  LowPower.attachInterruptWakeup(HOST_ATTN_PIN, cxAttnIsr, RISING, DEEP_SLEEP_MODE);
}

// Ask the Notecard to hold ATTN low for `seconds` (optionally with extra
// card.attn modes such as "arm,motionchange" so other events can wake the host
// early), then enter STOP2 until ATTN rises.
//
// `log` is the Stream used for debug output, if any; it is flushed before
// sleeping. Returns true when the host slept and ATTN woke it. Returns false
// when the Notecard did not accept the request or ATTN never went low; the
// caller should treat that as "try again next loop", typically after a delay.
static inline bool cxSleepUntilAttn(Notecard &notecard, uint32_t seconds,
                                    const char *modes = NULL, Stream *log = NULL) {
  // The host stays alive, so use a request (not a command) and check the
  // result.
  J *req = notecard.newRequest("card.attn");
  char modestr[64];
  strlcpy(modestr, "sleep", sizeof(modestr));
  if (modes != NULL && modes[0]) {
    strlcat(modestr, ",", sizeof(modestr));
    strlcat(modestr, modes, sizeof(modestr));
  }
  JAddStringToObject(req, "mode", modestr);
  JAddNumberToObject(req, "seconds", seconds);
  if (!notecard.sendRequest(req)) {
    return false;
  }

  // Don't sleep until ATTN has actually gone low, or the rising edge we're
  // waiting for may already be behind us.
  uint32_t start = millis();
  while (digitalRead(HOST_ATTN_PIN) == HIGH && (millis() - start) < 5000) {
    delay(10);
  }
  if (digitalRead(HOST_ATTN_PIN) == HIGH) {
    return false;  // ATTN isn't reaching the host; check the ATTN -> D5 wire
  }

  // flush() only waits for the UART's TX ring buffer to drain; its final
  // TX-empty interrupt fires ~90 us later. Entering STOP2 with that interrupt
  // pending makes WFI return immediately, so give it time to be serviced.
  if (log != NULL) {
    log->flush();
  }
  delay(10);

  // STOP2 until the ATTN rising edge. Execution resumes on the next line.
  LowPower.deepSleep();
  return true;
}
