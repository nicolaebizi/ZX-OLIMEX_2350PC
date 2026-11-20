// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// R16 - PSRAM init PORTED from Paul Robson's rp2350-modules
// (modules/psram/library/rp2350pc_psram.c - the PROVEN APS6404 init for
// the Olimex RP2350PC; Adafruit-derived code, verbatim sequence).
//
// THE KEY DIFFERENCE vs the failed R13/R14 SDK approach:
//   Robson initialises the PSRAM at RUNTIME, from the application, AFTER
//   the final system clock is set - one clean init whose QMI M1 timing is
//   computed from the LIVE clock_get_hz(clk_sys).  Ours (R13/R14) ran the
//   SDK's runtime_init_setup_psram PRE-MAIN at the 125MHz boot clock,
//   leaving M1 timed for the wrong clock; the R13 attempt to rewrite M1
//   at runtime killed the video.
//   Robson's design, now ported faithfully:
//     (a) WHEN: an explicit init call from main() (runtime), NEVER
//         pre-main; here it runs right after ZxSpectrumVideoInit() has
//         raised clk_sys to the 251.75MHz TMDS rate, so timing is
//         computed once, for the final clock;
//     (b) HOW: direct-mode QMI ID probe (0x9F, CS1, quad escape 0xF5),
//         QPI-enable (0x35) via direct mode, then the full QMI M1
//         timing/rfmt/rcmd/wfmt/wcmd programming - NOT the SDK's
//         flash-devinfo route (no devinfo writes at all);
//     (c) TIMING: divisor for a 133MHz max at the current clk_sys,
//         rxdelay = divisor (+1 if the divided clock is >100MHz), 8ms
//         max-select in 64-sys-clock units, 18ns min-deselect;
//     (d) FLASH: Robson DOES retime QMI M0 (the flash!) for the current
//         clock and forces an XIP read to apply it (SetQMITiming) inside
//         this same init, so flash XIP stays valid at the raised clock;
//         every QMI-touching function runs __no_inline_not_in_flash_func
//         (executes from SRAM while QMI is being programmed);
//     (e) MAP: cacheable CS1 XIP window 0x11000000, writes enabled with
//         XIP_CTRL_WRITABLE_M1.
//   Consequence for the build: NO hardware_psram link, NO PICO_PSRAM_*
//   board defines, NO pre-main SDK stage.  NO memory relocation either -
//   no __in_psram variables; BSS/heap stay in the SRAM banks as in R12.

#include "pico.h"

#if defined(OLIMEX_RP2350PC) && PICO_RP2350

#include <stddef.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "ZxPsram.h"

// CS1 XIP aliases (RP2350 memory map, QMI CS1 = the second XIP region):
// cacheable/alloc at XIP_BASE + 16MB = 0x11000000 (the Robson address);
// non-cacheable, non-allocating at + 16MB more = 0x13000000 (used here
// for the verify so the shared XIP cache is not polluted by test
// traffic).
#define ZXPSRAM_NOCACHE_BASE 0x13000000u
#define ZXPSRAM_BYTES        (8u * 1024u * 1024u)
#define ZXPSRAM_WORDS        (ZXPSRAM_BYTES / 4u)

static volatile uint8_t zx_psram_ok = 0;
static volatile uint8_t zx_psram_mb = 0;

// ---------------------------------------------------------------------------
// Robson rp2350-modules: PSRHW_SetQMITiming (verbatim, renamed).
// ---------------------------------------------------------------------------
static void __no_inline_not_in_flash_func(zx_psram_set_qmi_timing)(void) {
    // Make sure flash is deselected - QMI doesn't appear to have a busy flag(!)
    while ((ioqspi_hw->io[1].status & IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) != IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) {
        ;
    }

    // Use the minimum divisor assuming a 133MHz flash.
    // RX delay equal to the divisor means sampling at the same time as the next falling edge of SCK after the
    // falling edge that generated the data.  This is pretty tight at 133MHz but seems to work with the Winbond flash chips.
    const int max_flash_freq = 133000000;
    const int divisor = (clock_get_hz(clk_sys) + max_flash_freq - 1) / max_flash_freq;
    const int rxdelay = divisor;
    // Upstream writes m[0] (flash) using the QMI_M1_* field offsets: the
    // M0/M1 TIMING registers are positionally identical (Robson verbatim).
    qmi_hw->m[0].timing = (1 << QMI_M0_TIMING_COOLDOWN_LSB) |
        rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
        divisor << QMI_M1_TIMING_CLKDIV_LSB;

    // Force a read through XIP to ensure the timing is applied
    volatile uint32_t *ptr = (volatile uint32_t *)0x14000000;
    (void)*ptr;
}

// ---------------------------------------------------------------------------
// Robson rp2350-modules: PSRHW_Detect (verbatim, renamed).
// ---------------------------------------------------------------------------
static size_t __no_inline_not_in_flash_func(zx_psram_detect)(void) {
    int psram_size = 0;

    uint32_t intr_stash = save_and_disable_interrupts();

    // Try and read the PSRAM ID via direct_csr.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;

    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
    }

    // Exit out of QMI in case we've inited already
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    // Transmit as quad.
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xf5;

    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
    }

    (void)qmi_hw->direct_rx;

    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    // Read the id
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;

    for (size_t i = 0; i < 7; i++)
    {
        if (i == 0) {
            qmi_hw->direct_tx = 0x9f;
        } else {
            qmi_hw->direct_tx = 0xff;
        }

        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {
        }

        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
        }

        if (i == 5) {
            kgd = qmi_hw->direct_rx;
        } else if (i == 6) {
            eid = qmi_hw->direct_rx;
        } else {
            (void)qmi_hw->direct_rx;
        }
    }

    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    if (kgd == 0x5D) {
        psram_size = 1024 * 1024; // 1 MiB
        uint8_t size_id = eid >> 5;
        if (eid == 0x26 || size_id == 2) {
            psram_size *= 8; // 8 MiB
        } else if (size_id == 0) {
            psram_size *= 2; // 2 MiB
        } else if (size_id == 1) {
            psram_size *= 4; // 4 MiB
        }
    }

    restore_interrupts(intr_stash);
    return psram_size;
}

// ---------------------------------------------------------------------------
// Robson rp2350-modules: PSRHW_Initialise (verbatim, renamed).
// ---------------------------------------------------------------------------
static size_t __no_inline_not_in_flash_func(zx_psram_initialise)(uint cs_pin) {
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    size_t psram_size = zx_psram_detect();

    if (!psram_size) {
        return 0;
    }

    zx_psram_set_qmi_timing();

    // Enable direct mode, PSRAM CS, clkdiv of 10.
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB | \
        QMI_DIRECT_CSR_EN_BITS | \
        QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
        ;
    }

    // Enable QPI mode on the PSRAM
    const uint CMD_QPI_EN = 0x35;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;

    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
        ;
    }

    // Set PSRAM timing for APS6404
    //
    // Using an rxdelay equal to the divisor isn't enough when running the APS6404 close to 133MHz.
    // So: don't allow running at divisor 1 above 100MHz (because delay of 2 would be too late),
    // and add an extra 1 to the rxdelay if the divided clock is > 100MHz (i.e. sys clock > 200MHz).
    const int max_psram_freq = 133000000;
    const int clock_hz = clock_get_hz(clk_sys);
    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) {
        divisor = 2;
    }
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) {
        rxdelay += 1;
    }

    // - Max select must be <= 8us.  The value is given in multiples of 64 system clocks.
    // - Min deselect must be >= 18ns.  The value is given in system clock cycles - ceil(divisor / 2).
    const int clock_period_fs = 1000000000000000ll / clock_hz;
    const int max_select = (125 * 1000000) / clock_period_fs;  // 125 = 8000ns / 64
    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
        QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
        max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
        rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
        divisor << QMI_M1_TIMING_CLKDIV_LSB;

    // Set PSRAM commands and formats
    qmi_hw->m[1].rfmt =
        QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB | \
            QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB | \
            QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB | \
            QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB | \
            QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB | \
            QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB | \
            6 << QMI_M0_RFMT_DUMMY_LEN_LSB;

    qmi_hw->m[1].rcmd = 0xEB;

    qmi_hw->m[1].wfmt =
        QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB | \
            QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB | \
            QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB | \
            QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB | \
            QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB | \
            QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;

    qmi_hw->m[1].wcmd = 0x38;

    // Disable direct mode
    qmi_hw->direct_csr = 0;

    // Enable writes to PSRAM
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);

    return psram_size;
}

// ---------------------------------------------------------------------------
// Probe: the Robson init at the SAFE point (runtime, after the video init
// raised clk_sys), then the R13-style 1MB marker verify + address-wrap
// check through the non-cacheable CS1 alias.  Runs on core0 before the
// render loop starts, all in SRAM-executing functions.
// ---------------------------------------------------------------------------

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

  // The full Robson init (ID probe -> flash retime -> QPI -> M1 setup).
  const size_t sz = zx_psram_initialise(OLIMEX_RP2350PC_PSRAM_CS_PIN);
  if (sz == 0) {
    return;
  }

  // Verify through the non-cacheable CS1 alias (keeps the XIP cache
  // clean; 0x13000000 routes to CS1 like 0x11000000).
  volatile uint32_t *p = (volatile uint32_t *)ZXPSRAM_NOCACHE_BASE;
  if (!zx_psram_verify_1m(p)) {
    return;
  }

  // Address-wrap check: 0xA5A5A5A5 at offset 0, 0x5A5A5A5A at the top
  // word of the 8MB window.  If the mapped window were smaller than the
  // chip reports (aliased), the top write lands on offset 0 and the
  // 0xA5 read-back fails; if the chip ignores the upper address bits,
  // the top read-back fails.
  p[0] = 0xA5A5A5A5u;
  p[ZXPSRAM_WORDS - 1] = 0x5A5A5A5Au;
  if (p[0] != 0xA5A5A5A5u) {
    return;
  }
  if (p[ZXPSRAM_WORDS - 1] != 0x5A5A5A5Au) {
    return;
  }

  // Detected size (from the chip ID, like Robson) and the pass flag.
  zx_psram_mb = (uint8_t)(sz >> 20); // 8
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
