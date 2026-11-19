// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// ES8311 register bring-up for the Olimex RP2350-PC (phase 2, playback).
//
// Reference: Everest ES8311 datasheet register map + the vendor-maintained
// init sequence in Espressif's esp-bsp components/es8311/es8311.c (the
// structure here follows it verbatim: reset -> clock tree -> serial port
// -> analog power -> DAC path -> volume/unmute).
//
// Clocking decision (documented for the hardware bring-up):
//   The codec is an I2S SLAVE.  Our PIO master emits BCLK = 32 x fs
//   (1.4112 MHz at 44100 Hz, 16-bit stereo) on GP30 and LRCK = fs on
//   GP29; playback data goes to the codec's DSDIN/SDAI on GP31.
//   ES8311 supports operating without MCLK: R01 bit7 selects the SCLK
//   pin as the source of the internal clock tree.  With 16-bit stereo
//   the internal MCLK equals SCLK exactly (fs*16*2 = 1.4112 MHz), and
//   the pre-multiplier (x8) raises it to the 256fs-equivalent the DAC
//   needs.  This is the robust option: no GP23 GPOUT1 MCLK, no divider
//   rounding, and MCLK/BCLK are inherently phase-locked.
//   (Documented fallback if MCLK is ever wanted: CLOCK_GPOUT1 on GP23 at
//   12.288 MHz = 256fs for 48 kHz, int.frac divider 20 + 124/256 =
//   0.002% error; then R01 bit7 = 0 and the coefficient entry for
//   mclk=12288000 applies.)

#include "ZxEs8311.h"

#ifdef ZX_AUDIO_CODEC_ES8311

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"
#include "pico/printf.h"

// ---- Board wiring (auditor-verified, Olimex RP2350-PC Rev.A) ----
#define ZX_ES8311_I2C      i2c0
#define ZX_ES8311_SDA_PIN  32
#define ZX_ES8311_SCL_PIN  33
#define ZX_ES8311_ADDR     0x18  // 7-bit I2C address (AD0 pulled low)
#define ZX_ES8311_PWR_PIN  22    // PWR_EN# -> codec LDO CE (polarity probed)
#define ZX_ES8311_PWR_LDO_RAMP_MS 20

// Modest default hardware volume: 50% of the 0..255 DAC gain range.
#ifndef ZX_ES8311_DEFAULT_VOLUME_PERCENT
#define ZX_ES8311_DEFAULT_VOLUME_PERCENT 50
#endif

// ---- ES8311 register map (datasheet) ----
#define ES8311_RESET_REG00    0x00
#define ES8311_CLKM1_REG01    0x01  // MCLK/SCLK inv+div, clock enables
#define ES8311_CLKM2_REG02    0x02  // pre-div/pre-mult of internal MCLK
#define ES8311_CLKM3_REG03    0x03  // fs mode + ADC OSR
#define ES8311_CLKM4_REG04    0x04  // DAC OSR
#define ES8311_CLKM5_REG05    0x05  // ADC/DAC clock dividers
#define ES8311_CLKM6_REG06    0x06  // SCLK inv + SCLK divider
#define ES8311_CLKM7_REG07    0x07  // LRCK divider MSBs
#define ES8311_CLKM8_REG08    0x08  // LRCK divider LSBs
#define ES8311_SDPIN_REG09    0x09  // serial data port in (DSDIN)
#define ES8311_SDPOUT_REG0A   0x0A  // serial data port out (ASDOUT)
#define ES8311_SYSTEM_REG0D   0x0D  // analog power
#define ES8311_SYSTEM_REG0E   0x0E  // ADC PGA + modulator power
#define ES8311_SYSTEM_REG12   0x12  // DAC power
#define ES8311_SYSTEM_REG13   0x13  // output (HP drive) enable
#define ES8311_ADC_REG1C      0x1C  // ADC EQ + DC-offset cancel
#define ES8311_DAC_REG31      0x31  // DAC mute control
#define ES8311_DAC_REG32      0x32  // DAC volume (0 = min, 255 = max)
#define ES8311_DAC_REG37      0x37  // DAC EQ + fade

static bool _es8311_present = false;
static int _es8311_pwr_level = -1; // -1 unknown, 0/1 = the working polarity

static inline bool es_write(uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = { reg, val };
  return i2c_write_blocking(ZX_ES8311_I2C, ZX_ES8311_ADDR, buf, 2, false) == 2;
}

static inline bool es_read(uint8_t reg, uint8_t *val) {
  // This SDK has no i2c_write_read_blocking: set the register pointer
  // with a repeated START (nostop) then read one byte.
  if (i2c_write_blocking(ZX_ES8311_I2C, ZX_ES8311_ADDR, &reg, 1, true) != 1)
    return false;
  return i2c_read_blocking(ZX_ES8311_I2C, ZX_ES8311_ADDR, val, 1, false) == 1;
}

// Presence probe: read R00 and expect a NACK-free transaction.
static bool es8311_probe() {
  uint8_t v = 0;
  return es_read(ES8311_RESET_REG00, &v);
}

bool zxEs8311Present() {
  return _es8311_present;
}

bool zxEs8311Init() {
  _es8311_present = false;

  // ---- Power: PWR_EN# -> codec LDO CE ----
  gpio_init(ZX_ES8311_PWR_PIN);
  gpio_set_dir(ZX_ES8311_PWR_PIN, GPIO_OUT);
  gpio_put(ZX_ES8311_PWR_PIN, 0);

  // ---- I2C0 @ 400 kHz, SDA=GP32 / SCL=GP33 (shared with the unused
  // HDMI DDC connector; open-drain I2C mode, on-board HDMI pull-ups +
  // internal pull-ups keep both lines high) ----
  i2c_init(ZX_ES8311_I2C, 400000);
  gpio_set_function(ZX_ES8311_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(ZX_ES8311_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(ZX_ES8311_SDA_PIN);
  gpio_pull_up(ZX_ES8311_SCL_PIN);

  // ---- PWR_EN# polarity: HIGH first, LOW as fallback ----
  static const int pwr_levels[2] = { 1, 0 };
  for (int i = 0; i < 2 && !_es8311_present; ++i) {
    gpio_put(ZX_ES8311_PWR_PIN, pwr_levels[i]);
    sleep_ms(ZX_ES8311_PWR_LDO_RAMP_MS); // LDO ramp + codec power-on reset
    _es8311_present = es8311_probe();
    if (_es8311_present) _es8311_pwr_level = pwr_levels[i];
  }
  if (!_es8311_present) {
    printf("ES8311: no codec answered at 0x%02X on I2C0 (GP22 probed HIGH and LOW)\n",
           ZX_ES8311_ADDR);
    return false;
  }
  printf("ES8311: found at 0x%02X (PWR_EN# GP22 driven %s)\n",
         ZX_ES8311_ADDR, _es8311_pwr_level ? "HIGH" : "LOW");

  // ---- Reset (Espressif reference sequence) ----
  es_write(ES8311_RESET_REG00, 0x1F);
  sleep_ms(20);
  es_write(ES8311_RESET_REG00, 0x00);
  es_write(ES8311_RESET_REG00, 0x80); // power-on command

  // ---- Clock tree: BCLK-driven (MCLK-free) mode ----
  // R01: enable all clocks; bit7 = take the internal MCLK from the SCLK
  // pin; no MCLK/SCLK inversions.  Internal MCLK = fs*16*2 = 1411200 Hz
  // (= SCLK at 16-bit stereo); x8 pre-multiplier -> 256fs-equivalent.
  es_write(ES8311_CLKM1_REG01, 0x3F | 0x80);

  // Coefficients for fs = 44100, internal MCLK = 1411200 Hz (coefficient
  // table entry: pre_div=1, pre_multi=3 (x8), adc_div=1, dac_div=1,
  // fs_mode=0 (single speed), lrck_h=0x00, lrck_l=0xff, bclk_div=4).
  uint8_t r02 = 0;
  es_read(ES8311_CLKM2_REG02, &r02);
  r02 = (r02 & 0x07) | ((1 - 1) << 5) | (3 << 3); // pre-div 1, x8 pre-mult
  es_write(ES8311_CLKM2_REG02, r02);
  es_write(ES8311_CLKM3_REG03, (0 << 6) | 0x10);  // single speed, ADC OSR 32
  es_write(ES8311_CLKM4_REG04, 0x10);             // DAC OSR 32
  es_write(ES8311_CLKM5_REG05, ((1 - 1) << 4) | (1 - 1)); // adc/dac div = 1
  uint8_t r06 = 0;
  es_read(ES8311_CLKM6_REG06, &r06);
  r06 = (r06 & 0xE0) | (4 - 1); // no SCLK inversion, SCLK divider field
  es_write(ES8311_CLKM6_REG06, r06);
  uint8_t r07 = 0;
  es_read(ES8311_CLKM7_REG07, &r07);
  r07 = (r07 & 0xC0) | 0x00;    // LRCK divider MSBs
  es_write(ES8311_CLKM7_REG07, r07);
  es_write(ES8311_CLKM8_REG08, 0xFF); // LRCK divider LSBs

  // ---- Serial port: slave mode, standard I2S, 16 bits in and out ----
  uint8_t r00 = 0;
  es_read(ES8311_RESET_REG00, &r00);
  es_write(ES8311_RESET_REG00, r00 & 0xBF); // bit6 = 0: slave serial port
  es_write(ES8311_SDPIN_REG09, 0x0C);  // in: I2S format, 16-bit (bits[3:2]=11)
  es_write(ES8311_SDPOUT_REG0A, 0x0C); // out: I2S format, 16-bit

  // ---- Analog power: DAC path on (playback phase), ADC stays off ----
  es_write(ES8311_SYSTEM_REG0D, 0x01); // power up analog circuitry
  es_write(ES8311_SYSTEM_REG0E, 0x00); // ADC PGA/modulator stay off (mic later)
  es_write(ES8311_SYSTEM_REG12, 0x00); // power up DAC
  es_write(ES8311_SYSTEM_REG13, 0x10); // enable output to HP drive
  es_write(ES8311_ADC_REG1C, 0x6A);    // ADC EQ bypass, digital DC cancel
  es_write(ES8311_DAC_REG37, 0x08);    // bypass DAC equalizer

  // ---- Volume (modest default) + DAC unmute ----
  const int percent = ZX_ES8311_DEFAULT_VOLUME_PERCENT;
  const int vol = (percent <= 0) ? 0 : (percent * 256 / 100) - 1;
  es_write(ES8311_DAC_REG32, (uint8_t)vol);
  printf("ES8311: DAC volume %d%% (reg32=0x%02X)\n", percent, (unsigned)vol);
  uint8_t r31 = 0;
  es_read(ES8311_DAC_REG31, &r31);
  es_write(ES8311_DAC_REG31, r31 & ~0x60); // clear the two DAC mute bits

  printf("ES8311: playback path up: 44100Hz 16-bit I2S slave, BCLK-driven, HP out\n");
  return true;
}

#else // !ZX_AUDIO_CODEC_ES8311

bool zxEs8311Init() { return false; }
bool zxEs8311Present() { return false; }

#endif
