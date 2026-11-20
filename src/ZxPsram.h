// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// R16 - PSRAM phase, step 1: INIT + VERIFY (no memory relocation yet).
//
// Olimex RP2350-PC: APS6404L-3SQR-SN 8MB QSPI PSRAM on QMI CS1n (GP8),
// sharing the QSPI CLK/SD0-3 with the boot flash.  The INIT is a faithful
// port of Paul Robson's rp2350-modules PSRAM code
// (modules/psram/library/rp2350pc_psram.c), which runs at RUNTIME - here
// right after ZxSpectrumVideoInit() raised clk_sys to the 251.75MHz TMDS
// rate - computing the QMI M1 (and flash M0) timing from the LIVE system
// clock, with no SDK pre-main stage, no devinfo writes and no
// hardware_psram link (the KEY difference vs the failed R13/R14 SDK
// approach).  The probe reports the result in the menu as an UNGATED
// user-facing status row ("PSRAM: 8MB OK"/"PSRAM: FAIL"), Olimex-only.
#ifndef _ZXPSRAM_H
#define _ZXPSRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Full Robson-style init (ID probe, flash M0 retime + forced XIP read,
// QPI enable, QMI M1 formats/timing at the current clk_sys, WRITABLE_M1)
// then the 1MB marker fill + 0xA5/0x5A address-wrap verify.  Call once
// from main() after the video init has raised the clock.  Safe to call
// before the render loop starts (no concurrent CS1 traffic then).
void zxPsramProbe(void);

// 1 = the boot probe PASSED, 0 = not yet probed or FAILED
uint8_t zxPsramOk(void);

// Detected PSRAM size in MiB (8 on the Olimex board when OK)
uint8_t zxPsramSizeMb(void);

#ifdef __cplusplus
}
#endif

#endif
