// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// R13 - PSRAM phase, step 1: INIT + VERIFY (no memory relocation yet).
//
// Olimex RP2350-PC: APS6404L-3SQR-SN 8MB QSPI PSRAM on QMI CS1n (GP8),
// sharing the QSPI CLK/SD0-3 with the boot flash.  The INIT itself is the
// pico-sdk 2.3.1 hardware_psram runtime stage (activated by
// PICO_PSRAM_CS_PIN / PICO_PSRAM_SIZE_BYTES in boards/olimex_rp2350pc.h);
// this module re-times QMI M1 for the emulator's 251.75MHz sys clock and
// runs the boot-time pattern/wrap verify.  The result is reported in the
// menu as an UNGATED user-facing status row ("PSRAM: 8MB OK"/"PSRAM:
// FAIL"), Olimex-only.
#ifndef _ZXPSRAM_H
#define _ZXPSRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Retime QMI M1 to the current clk_sys (251.75MHz -> 125.875MHz quad bus)
// and run the 1MB marker fill + 0xA5/0x5A address-wrap verify.  Call once
// from main() after the video init has raised the clock.  Safe to call
// before core1 launches (no concurrent CS1 traffic then).
void zxPsramProbe(void);

// 1 = the boot verify PASSED, 0 = not yet probed or FAILED
uint8_t zxPsramOk(void);

// Detected/configured PSRAM size in MiB (8 on the Olimex board when OK)
uint8_t zxPsramSizeMb(void);

#ifdef __cplusplus
}
#endif

#endif
