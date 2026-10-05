// cx_sleep.h
//
// Notecarrier CX host sleep: STM32 STOP2, woken by the Notecard's ATTN pin.
//
// On the Notecarrier CX the Notecard cannot switch the host's power. Its EN
// pin enables the 3.3 V VIO rail shared by the host MCU, Qwiic, 3V3_OUT, and
// the Notecard's own I/O, so ATTN -> EN browns out the whole board instead of
// sleeping the host. The pattern that works: jumper ATTN to a host GPIO, ask
// the Notecard to hold ATTN low for N seconds with card.attn "sleep", put the
// STM32 in STOP2 (~1-2 uA, RAM retained), and wake on the ATTN rising edge.
// Execution resumes in place; no cold boot, no state to persist.
//
// Wiring:  Notecarrier CX ATTN (P1 pin 3) ---- jumper ---- D5 (P1 pin 14)
// Build:   Tools > USB support (if available) > None. With the USB CDC stack
//          enabled and no USB host attached (battery power, or the USB-C switch
//          in NC), the USB wakeup interrupt exits STOP2 immediately.
// Library: STM32duino Low Power (+ its dependency STM32duino RTC).
//
// Usage:
//   #include "cx_sleep.h"
//   void setup() { ...; notecard.begin(); ...; cxSleepBegin(); }
//   void loop()  { sample(); cxSleepUntilAttn(notecard, seconds, NULL, &dbgSerial); }
//
// See https://dev.blues.io/example-apps/samples/putting-a-host-to-sleep-between-sensor-readings/

#pragma once

#include <Arduino.h>
#include <Notecard.h>
#include <STM32LowPower.h>

#ifndef CX_ATTN_PIN
#define CX_ATTN_PIN D5
#endif

// Nothing to do in the ISR: waking up is the side effect.
static void cxAttnIsr() {}

// Call once from setup(), after notecard.begin().
static inline void cxSleepBegin() {
  // ATTN is driven push-pull by the Notecard; no pull resistor needed (one
  // would just leak current against it).
  pinMode(CX_ATTN_PIN, INPUT);
  LowPower.begin();
  LowPower.attachInterruptWakeup(CX_ATTN_PIN, cxAttnIsr, RISING, DEEP_SLEEP_MODE);
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
  // result. Right after a cold boot the Notecard may not be ready yet.
  J *req = notecard.newRequest("card.attn");
  char modestr[64];
  strlcpy(modestr, "sleep", sizeof(modestr));
  if (modes != NULL && modes[0]) {
    strlcat(modestr, ",", sizeof(modestr));
    strlcat(modestr, modes, sizeof(modestr));
  }
  JAddStringToObject(req, "mode", modestr);
  JAddNumberToObject(req, "seconds", seconds);
  if (!notecard.sendRequestWithRetry(req, 5)) {
    return false;
  }

  // Don't sleep until ATTN has actually gone low, or the rising edge we're
  // waiting for may already be behind us.
  uint32_t start = millis();
  while (digitalRead(CX_ATTN_PIN) == HIGH && (millis() - start) < 5000) {
    delay(10);
  }
  if (digitalRead(CX_ATTN_PIN) == HIGH) {
    return false;  // ATTN isn't reaching the host; check the ATTN -> D5 jumper
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
