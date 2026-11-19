// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <stdbool.h>

// On-board ES8311 low-power audio codec bring-up for the Olimex RP2350-PC.
//
// Phase 2 (playback only, headphone jack): the codec runs as an I2S SLAVE
// behind our PIO master (LRCK=GP29, BCLK=GP30, playback data = DSDIN/SDAI
// = GP31) and is programmed over I2C0 (SDA=GP32, SCL=GP33, addr 0x18,
// 400 kHz).  Clocking uses the datasheet MCLK-free mode: the internal
// clock tree derives from SCLK (BCLK) itself, so GP23 (CLOCK_GPOUT1
// MCLK) is deliberately left untouched.
//
// The codec power LDO enable (PWR_EN#, GP22) polarity is not 100%
// audited; init probes the codec over I2C with the pin HIGH first (R43
// 0R fit + "PWR_EN" naming suggest an active-high enable) and falls back
// to LOW if the chip does not answer.  Whichever polarity makes the
// codec ACK is remembered.
//
// Return: true when the codec answered on the I2C bus (registers
// written), false when no codec was found (the I2S master still runs and
// audio plays into the void; nothing else in the system is affected).

bool zxEs8311Init();

// Result of the last zxEs8311Init() probe.
bool zxEs8311Present();
