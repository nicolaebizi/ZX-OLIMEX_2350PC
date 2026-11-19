// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxEsDiag.h"

// Olimex R1 ES8311/I2S audio diag storage (ZX_ESDIAG, default OFF).
// ~32 bytes of .bss in the diag build, zero code and zero bytes in every
// other build.
#ifdef ZX_ESDIAG

static ZxEsDiag zxEsDiagState = {
  0xFF, // pwr: neither polarity known to ACK until a probe succeeds
  0,    // ack
  0,    // wrCount
  0xFF, // regFail
  0,    // sps
  0,    // smRun
  {0xFF, 0xFF, 0xFF, 0xFF} // rb: no readback until zxEs8311Init() fills it
};
static uint32_t zxEsDiagSecSamples; // sample pushes since the last latch
static uint32_t zxEsDiagSecFrames;  // frames since the last latch

const ZxEsDiag *zxEsDiag(void) { return &zxEsDiagState; }

void zxEsDiagPwr(uint8_t pol) { zxEsDiagState.pwr = pol; }

void zxEsDiagPwrNone(void) { zxEsDiagState.pwr = 0xFF; }

void zxEsDiagAck(bool acked) { zxEsDiagState.ack = acked ? 1 : 0; }

void zxEsDiagWr(uint8_t reg, bool ok) {
  ++zxEsDiagState.wrCount;
  if (!ok && zxEsDiagState.regFail == 0xFF) zxEsDiagState.regFail = reg;
}

void zxEsDiagSample(void) { ++zxEsDiagSecSamples; }

void zxEsDiagFrameEnd(void) {
  // Per-second counter: the PIO FIFO pushes of the last second, latched on
  // every 60th frame (~1 s at 640x480x60) and reset for the next second.
  if (++zxEsDiagSecFrames >= 60) {
    zxEsDiagState.sps = zxEsDiagSecSamples;
    zxEsDiagSecSamples = 0;
    zxEsDiagSecFrames = 0;
  }
}

void zxEsDiagSmRun(bool run) { zxEsDiagState.smRun = run ? 1 : 0; }

void zxEsDiagRb(uint8_t idx, uint8_t val) {
  if (idx < 4) zxEsDiagState.rb[idx] = val;
}

#endif
