// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// R14 - PSRAM phase, step 1: INIT + VERIFY (no memory relocation).
//
// R14 change: the R13 runtime QMI_M1_TIMING retime is REMOVED - it ran
// after the 251.75MHz clk_sys raise and disturbed the same QMI that the
// XIP flash code is executing through (video never came up on the
// auditor's board).  The SDK's boot-time (pre-main, 125MHz) PSRAM init
// timing is kept as-is.
//
// Olimex RP2350-PC hardware (auditor, 2026-11):
//   APS6404L-3SQR-SN 8MB QSPI PSRAM, CS = GP8 (QMI_CS1n), shares the QSPI
//   CLK/SD0-3 with the 16MB boot flash; 10k pull-up on R78.
//
// INIT approach - the SDK helper, not hand-rolled registers:
//   pico-sdk 2.3.1 ships hardware_psram; linking it (plus
//   PICO_PSRAM_CS_PIN=8 / PICO_PSRAM_SIZE_BYTES=8MB in the board header)
//   makes its runtime_init_setup_psram() run automatically during runtime
//   init, BEFORE main(): it programs flash_devinfo CS1, sends the
//   QUAD_ENABLE command, and installs the QMI M1 (CS1) quad read/write
//   formats + APS6404 timing (psram_configure_params defaults, including
//   the RP2350-E14 workaround).  Hand-rolling the QMI direct/serial setup
//   would only duplicate psram_initialize_internal(), so R14 keeps the
//   SDK stage and adds exactly two things the SDK cannot know:
//
//   1. VERIFY: a 1MB address-dependent marker fill + read-back through
//      the non-cacheable CS1 alias 0x13000000 (mapped XIP window: CS1
//      cacheable = 0x11000000, non-cacheable/non-alloc = 0x13000000;
//      8MB -> 0x117FFFFF / 0x137FFFFF), then the 0xA5A5A5A5 / 0x5A5A5A5A
//      address-wrap markers at offset 0 and offset 8MB-4: a smaller
//      aliased window (wrong devinfo size / wrong chip) makes the top
//      write land on offset 0 and the 0xA5 read-back fails.
//
//   2. NO RELOCATION: no __in_psram variables here - the SDK stage sees
//      an empty __psram region, BSS/heap stay in the SRAM banks exactly
//      as in R12.

#include "pico.h"

#if defined(OLIMEX_RP2350PC) && PICO_RP2350

#include "hardware/psram.h"
#include "ZxPsram.h"

// CS1 XIP aliases (RP2350 memory map, QMI CS1 = the second XIP region):
// cacheable/alloc at XIP_BASE + 16MB = 0x11000000; non-cacheable,
// non-allocating at + 16MB more = 0x13000000 (used for the verify so the
// shared XIP cache is not polluted by the test traffic).
#define ZXPSRAM_NOCACHE_BASE 0x13000000u
#define ZXPSRAM_BYTES        (8u * 1024u * 1024u)
#define ZXPSRAM_WORDS        (ZXPSRAM_BYTES / 4u)

static volatile uint8_t zx_psram_ok = 0;
static volatile uint8_t zx_psram_mb = 0;

// First 1MB quick fill/read with an address-dependent 0xA5/0x5A marker:
// word i = 0xA5A5____ where ____ = 0x5A5A ^ (i & 0xFFFF).
static int __not_in_flash_func(zx_psram_verify_1m)(volatile uint32_t *p) {
  const uint32_t words = 256u * 1024u;
  for (uint32_t i = 0; i < words; ++i) {
    p[i] = (0xA5A5u << 16) | (0x5A5Au ^ (i & 0xFFFFu));
  }
  for (uint32_t i = 0; i < words; ++i) {
    if (p[i] != ((0xA5A5u << 16) | (0x5A5Au ^ (i & 0xFFFFu)))) {
      return 0;
    }
  }
  return 1;
}

void __not_in_flash_func(zxPsramProbe)(void) {
  zx_psram_ok = 0;
  zx_psram_mb = 0;

  // The SDK hardware_psram runtime stage must have brought CS1 up
  // (flash_devinfo + QUAD_ENABLE + QMI M1 formats/timing).
  if (!psram_is_available()) {
    return;
  }

  // NO RETIME (R14): the runtime stage computed the M1 timing at the
  // boot-time sys clock (125MHz) and the emulator raises clk_sys to
  // 251.75MHz for the TMDS bit clock.  Rewriting QMI_M1_TIMING at runtime
  // disturbs the QMI that is also executing this XIP flash code and
  // killed the video (R13 "no monitor"), so the SDK's conservative
  // boot-time timing is kept as-is; a careful retime, if the PSRAM turns
  // out marginal, is separate future work.

  // 1: verify through the non-cacheable CS1 alias.
  volatile uint32_t *p = (volatile uint32_t *)ZXPSRAM_NOCACHE_BASE;
  if (!zx_psram_verify_1m(p)) {
    return;
  }

  // Address-wrap check: 0xA5A5A5A5 at offset 0, 0x5A5A5A5A at the top
  // word of the 8MB window.  If the mapped window were smaller than the
  // chip/devinfo claims (aliased), the top write lands on offset 0 and
  // the 0xA5 read-back fails; if the chip ignores the upper address
  // bits, the top read-back fails.
  p[0] = 0xA5A5A5A5u;
  p[ZXPSRAM_WORDS - 1] = 0x5A5A5A5Au;
  if (p[0] != 0xA5A5A5A5u) {
    return;
  }
  if (p[ZXPSRAM_WORDS - 1] != 0x5A5A5A5Au) {
    return;
  }

  // 2: reported size (devinfo CS1) and the pass flag for the menu row.
  zx_psram_mb = (uint8_t)(psram_get_size() >> 20); // 8
  zx_psram_ok = 1;
}

uint8_t zxPsramOk(void) {
  return zx_psram_ok;
}

uint8_t zxPsramSizeMb(void) {
  return zx_psram_mb;
}

#else // !OLIMEX_RP2350PC (incl. host builds): hardware-only stubs

#include "ZxPsram.h"

uint8_t zxPsramOk(void) {
  return 0;
}

uint8_t zxPsramSizeMb(void) {
  return 0;
}

#endif
