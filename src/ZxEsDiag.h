// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <stdint.h>

// Olimex R1 ES8311/I2S audio diag (ZX_ESDIAG, default OFF): on-screen
// evidence for the "menu says I2S, headphone jack silent" fault, since the
// user has no UART access.  Collected by ZxEs8311.cpp (GP22 PWR_EN#
// polarity probe, I2C ACK at 0x18, init register-write count + first
// failure) and ZxSpectrumAudioI2s.cpp (live PIO FIFO sample pushes, PIO SM
// running flag), latched per second on the DVI render-loop frame wrap,
// read by the menu row "E83: ...".  Everything (struct, storage,
// functions) compiles out unless the build passes -DZX_ESDIAG=ON, so
// release binaries are untouched.
#ifdef ZX_ESDIAG

typedef struct ZxEsDiag {
  uint8_t  pwr;     // (a) GP22 PWR_EN# polarity that ACKed (0/1), 0xFF = neither
  uint8_t  ack;     // (b) I2C probe ACK at 0x18 (1 = codec answered)
  uint32_t wrCount; // (c) init register writes attempted
  uint8_t  regFail; // (c) first failed init register write, 0xFF = none
  uint32_t sps;     // (d) PIO FIFO sample pushes in the last second (60 frames)
  uint8_t  smRun;   // (e) I2S PIO SM running flag (set at i2s_audio_init)
  uint8_t  rb[8];   // (f) readback of R01/R02/R11/R12/R13/R14/R32/R17 after init (0xFF = read failed)
} ZxEsDiag;

const ZxEsDiag *zxEsDiag(void);
void zxEsDiagPwr(uint8_t pol);          // (a) this GP22 polarity ACKed (0/1)
void zxEsDiagPwrNone(void);             // (a) neither polarity ACKed
void zxEsDiagAck(bool acked);           // (b) final I2C ACK state at 0x18
void zxEsDiagWr(uint8_t reg, bool ok);  // (c) one init register write result
void zxEsDiagSample(void);              // (d) one stereo sample -> PIO FIFO
void zxEsDiagFrameEnd(void);            // (d) frame wrap -> per-second latch
void zxEsDiagSmRun(bool run);           // (e) PIO SM enabled flag
void zxEsDiagRb(uint8_t idx, uint8_t val); // (f) one register readback, idx 0..7 = R01/R02/R11/R12/R13/R14/R32/R17

#endif
