// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// R13 - PSRAM phase, step 1: INIT + VERIFY (no memory relocation).
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
//   would only duplicate psram_initialize_internal(), so R13 keeps the
//   SDK stage and adds exactly two things the SDK cannot know:
//
//   1. RETIME: the runtime stage computes the M1 clock divisor at the
//      boot-time sys clock (125MHz), but the emulator raises clk_sys to
//      251.75MHz for the TMDS bit clock.  Nothing touches CS1 in between,
//      so zxPsramProbe() (called right after ZxSpectrumVideoInit())
//      rewrites only the QMI_M1_TIMING divisor/rxdelay/max-select/
//      min-deselect fields with the same conservative APS6404 formula as
//      the SDK's psram_configure_params(): divisor for <=133MHz, +1
//      rxdelay when the divided clock is >100MHz, 8ms max-select,
//      18ns min-deselect -> 251.75MHz/2 = 125.875MHz quad bus, rxdelay 3.
//
//   2. VERIFY: a 1MB address-dependent marker fill + read-back through
//      the non-cacheable CS1 alias 0x13000000 (mapped XIP window: CS1
//      cacheable = 0x11000000, non-cacheable/non-alloc = 0x13000000;
//      8MB -> 0x117FFFFF / 0x137FFFFF), then the 0xA5A5A5A5 / 0x5A5A5A5A
//      address-wrap markers at offset 0 and offset 8MB-4: a smaller
//      aliased window (wrong devinfo size / wrong chip) makes the top
//      write land on offset 0 and the 0xA5 read-back fails.
//
//   3. NO RELOCATION: no __in_psram variables here - the SDK stage sees
//      an empty __psram region, BSS/heap stay in the SRAM banks exactly
//      as in R12.

#include "pico.h"

#if defined(OLIMEX_RP2350PC) && PICO_RP2350

#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
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

// Same conservative APS6404 re-timing as the SDK's psram_configure_params()
// (hardware_psram/psram.c), applied straight to the M1 timing register
// (read-modify-write, the COOLDOWN=1 / PAGEBREAK=1024 fields installed by
// the SDK init are preserved).
static void __not_in_flash_func(zx_psram_retime)(void) {
  const uint32_t clock_hz = clock_get_hz(clk_sys);
  const uint32_t max_hz = 133u * 1000000u; // APS6404L-3SQR max
  uint32_t divisor = (clock_hz + max_hz - 1) / max_hz;
  if (divisor == 1 && clock_hz > 100000000u) {
    divisor = 2; // rxdelay 2 would be too late above 100MHz
  }
  uint32_t rxdelay = divisor;
  if (clock_hz / divisor > 100000000u) {
    rxdelay += 1;
  }
  // max-select in units of 64 sys clocks (8ms), min-deselect in sys
  // clocks minus ceil(divisor/2) (18ns) - 64-bit maths, femtoseconds
  const uint64_t period_fs = 1000000000000000ull / clock_hz;
  const uint32_t max_select =
    (uint32_t)((8000ull * 1000000ull) / (64ull * period_fs));
  const uint32_t min_deselect =
    (uint32_t)((18ull * 1000000ull + period_fs - 1) / period_fs
               - (divisor + 1) / 2);
  qmi_hw->m[1].timing =
    (qmi_hw->m[1].timing
     & ~(QMI_M1_TIMING_CLKDIV_BITS | QMI_M1_TIMING_RXDELAY_BITS
         | QMI_M1_TIMING_MAX_SELECT_BITS | QMI_M1_TIMING_MIN_DESELECT_BITS))
    | (divisor << QMI_M1_TIMING_CLKDIV_LSB)
    | (rxdelay << QMI_M1_TIMING_RXDELAY_LSB)
    | (max_select << QMI_M1_TIMING_MAX_SELECT_LSB)
    | (min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB);
}

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

  // 1: retime for the 251.75MHz sys clock before any mapped access.
  zx_psram_retime();

  // 2: verify through the non-cacheable CS1 alias.
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

  // 3: reported size (devinfo CS1) and the pass flag for the menu row.
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
