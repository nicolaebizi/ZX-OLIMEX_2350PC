/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 fruit-bat
 *
 * Olimex RP2350-PC (RP2350PC Rev.A) - 80-pin RP2350B board header.
 *
 * First-start feature set (phase 1): HDMI (HSTX TMDS) + microSD (SPI1,
 * scattered pins) + USB keyboard via the on-board FE1.1s 4-port hub.
 *
 * Explicitly NOT wired in this phase (code stays, targets do not compile
 * or drive them):
 *   - VGA / PS/2 keyboard / NES pads  -> ZX_HAS_VGA/PS2/NES = 0
 *   - ES8311 microphone path (ASDOUT GP28 -> MCU) and the PAM8003
 *     speaker amp -> later phases; the RC-filtered PWM audio path on
 *     GP26/27 stays untouched (single-source rule for the summed net)
 *   - 8MB APS6404 PSRAM (CS on QMI_CS1n) -> pin noted, never driven
 *
 * Verified hardware map (auditor, 2026-11):
 *   HDMI TMDS (HSTX function on GPIO12-19, DVI-Sock order, 270R series):
 *     D0 = GP12/13, CK = GP14/15, D2 = GP16/17, D1 = GP18/19
 *   microSD (SPI1):
 *     CLK = GP10, CMD/MOSI = GP11, CS = GP9, MISO/DAT0 = GP24, CD# = GP41
 *     (4.7k on-board pull-ups)
 *   USB: dedicated RP2350B USB pins -> FE1.1s 4-port hub (no MCU GPIOs;
 *     programming cable auto-disables the hub = bootloader access works)
 *   ES8311 codec (phase 2, playback first):
 *     I2C0 SDA = GP32, SCL = GP33 (addr 0x18; lines shared with the
 *     unused HDMI DDC - fine, the codec is the only device addressed)
 *     PWR_EN# = GP22 -> codec LDO CE (polarity probed at init: HIGH
 *     first, LOW fallback; the working level is logged over UART)
 *     I2S (codec = slave): LRCK = GP29, BCLK = GP30, DSDIN = GP31
 *     (PIO1 master, data out).  ASDOUT = GP28 (codec ADC out) unused.
 *     MCLK-free (BCLK-driven) clocking -> GP23 (CLOCK_GPOUT1 MCLK)
 *     deliberately not driven.
 *   PSRAM: APS6404 CS = GP8 (QMI_CS1n), LATER PHASE
 *   User LED: GP25 (high = on)
 *
 * NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO SHOULD ONLY CONSIST
 * OF PREPROCESSOR DIRECTIVES.
 */

#ifndef _BOARDS_OLIMEX_RP2350PC_H
#define _BOARDS_OLIMEX_RP2350PC_H

// For board detection
#define OLIMEX_RP2350PC

// ---------------------------------------------------------------------
// Chip: RP2350B (80-pin QFN-204 package).  PICO_RP2350A = 0 means the
// SDK compiles for the RP2350B (48 bank0 GPIOs); PICO_PLATFORM is still
// rp2350-arm-s, so no other SDK switch is needed.
// ---------------------------------------------------------------------
#define PICO_RP2350A 0

// ---------------------------------------------------------------------
// Phase-1 feature flags (informational for the source tree; the target's
// CMakeLists enforces them by not compiling the VGA/PS2/NES/audio paths)
// ---------------------------------------------------------------------
#define ZX_HAS_VGA 0
#define ZX_HAS_PS2 0
#define ZX_HAS_NES 0
// Phase 2: the ES8311 codec IS wired up (playback via the HP jack).
// Audio route: PIO1 I2S master -> ES8311 (slave, BCLK-driven MCLK-free
// mode) -> OUTP/OUTN headphone jack.  The PAM8003 speaker amp and the
// RC-filtered PWM path (GP26/27) are NOT touched in this phase (the amp
// is parked OFF: SHDN# GP35 HIGH, VOLUME GP34 static mid).
#define ZX_AUDIO_CODEC_ES8311 1
#define ZX_AUDIO_ENABLED 1

// ---------------------------------------------------------------------
// User LED
// ---------------------------------------------------------------------
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// ---------------------------------------------------------------------
// microSD on SPI1 (scattered pin set - see SDCARD_PIN_SPI0_* / the
// SDCARD_SPI_BUS=spi1 defines in the target's CMakeLists)
// ---------------------------------------------------------------------
#define OLIMEX_RP2350PC_SD_SPI    spi1
#define OLIMEX_RP2350PC_SD_CS_PIN 9
#define OLIMEX_RP2350PC_SD_SCK_PIN 10
#define OLIMEX_RP2350PC_SD_MOSI_PIN 11
#define OLIMEX_RP2350PC_SD_MISO_PIN 24
#define OLIMEX_RP2350PC_SD_CD_PIN 41

// ---------------------------------------------------------------------
// HDMI (HSTX) TMDS pairs, DVI-Sock order (informational; the libdvi
// serialiser config olimex_rp2350pc_hdmi_cfg in
// src/hdmi/custom_dvi_pin_configs.h is the compiled truth)
// ---------------------------------------------------------------------
#define OLIMEX_RP2350PC_DVI_LANES0_PIN 12 // D0+ / D0- on GP13
#define OLIMEX_RP2350PC_DVI_CLK_PIN    14 // CK+ / CK- on GP15
#define OLIMEX_RP2350PC_DVI_LANE2_PIN  16 // D2+ / D2- on GP17
#define OLIMEX_RP2350PC_DVI_LANE1_PIN  18 // D1+ / D1- on GP19

// ---------------------------------------------------------------------
// PSRAM (later phase - never driven by the phase-1 firmware)
// ---------------------------------------------------------------------
#define OLIMEX_RP2350PC_PSRAM_CS_PIN 8 // QMI_CS1n, 8MB APS6404

// ---------------------------------------------------------------------
// Flash: W25Q128JVSIQ (16 MiB)
// ---------------------------------------------------------------------
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef __ASSEMBLER__
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

#ifndef __ASSEMBLER__
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#endif

#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
