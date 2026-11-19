// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
//
// ES8311 register bring-up for the Olimex RP2350-PC (phase 2, playback).
//
// Reference (R3): the vendor-proven init sequence, ported from Espressif's
// esp-bsp es8311.c verbatim (saved unmodified in
// docs/audio/es8311_esp-bsp_reference.c, fetched 2026-11-19 from
// https://raw.githubusercontent.com/espressif/esp-bsp/master/components/es8311/es8311.c).
// Order: reset -> clock tree -> serial port -> analog power (0x0D/0x0E) ->
// DAC power + HP drive (0x12/0x13) -> mic defaults (0x14/0x17, from the
// vendor's es8311_microphone_config, written explicitly so the E83C
// readback diag compares against known-written values) -> EQ bypasses
// (0x1C/0x37) -> volume/unmute (0x32/0x31).
//
// Clocking decision (R4, changed from the R3 BCLK-driven experiment):
//   The R3 diag hardware was fully healthy per the user's photo (S44100,
//   readback R13=0x10 R14=0x1A ok) yet the headphone jack stayed silent -
//   so the R3 BCLK-driven codec clocking (R01 bit7 = 1) is the suspect.
//   R4 switches to the esp-bsp DEFAULT mode: mclk_from_mclk_pin = true,
//   i.e. the codec takes its internal MCLK from the MCLK pin (R01 bit7
//   = 0, es8311_clock_config() lines 242-256 of the vendored reference),
//   and we drive that pin: GP23 = CLOCK_GPOUT1 on the RP2350 (SDK
//   clock_gpio_init mapping: GP15/GP23 -> GPOUT1), source clk_sys.
//   fs = 44100 -> ideal MCLK = 256 x fs = 11.2896 MHz; sys = 251.75 MHz
//   (the 640x480p60 TMDS rate) -> integer GPOUT divider 22 -> MCLK =
//   11.4432 MHz (+1.4%).  The ES8311 divides MCLK internally with the
//   coefficient-table entry {11289600, 44100, pre_div 1, pre_multi 0
//   (x1), adc_div 1, dac_div 1, fs_mode 0, lrck 0x00/0xFF, bclk_div 4}
//   and tolerates that small MCLK/fs deviation in practice.  Kept
//   44.1 kHz + /22 as the tightest integer fit (48 kHz alternatives are
//   worse: 12.288 MHz = sys/20.48 not integer; sys/20 = 12.5875 MHz =
//   +2.4%).  The I2S PIO stays the BCLK/LRCK MASTER, unchanged.

#include "ZxEs8311.h"

#ifdef ZX_AUDIO_CODEC_ES8311

#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "pico/time.h"
#include "pico/printf.h"
#ifdef ZX_ESDIAG
#include "ZxEsDiag.h" // E83 diag (a)(b)(c): probe + reg-write evidence
#endif

// ---- Board wiring (auditor-verified, Olimex RP2350-PC Rev.A) ----
#define ZX_ES8311_I2C      i2c0
#define ZX_ES8311_SDA_PIN  32
#define ZX_ES8311_SCL_PIN  33
#define ZX_ES8311_ADDR     0x18  // 7-bit I2C address (AD0 pulled low)
#define ZX_ES8311_PWR_PIN  22    // PWR_EN# -> codec LDO CE (polarity probed)
#define ZX_ES8311_PWR_LDO_RAMP_MS 20
// R4: MCLK pin for the esp-bsp-default MCLK-driven codec mode.  GP23 =
// CLOCK_GPOUT1 on the RP2350 (clock_gpio_init's GP15/GP23 -> GPOUT1
// mapping).  Divider computed from clk_sys at init (22 at 251.75 MHz).
#define ZX_ES8311_MCLK_PIN       23
#define ZX_ES8311_MCLK_FS_RATIO  256u      // ideal MCLK = 256 x fs
#define ZX_ES8311_MCLK_FS        44100u    // fs of the I2S PIO (PICO_I2S_AUDIO_FREQ)

// Default hardware volume (R3): 100% = reg32 0xFF = the 0 dB top of the
// 0..255 DAC gain range (esp-bsp es8311_voice_volume_set formula: reg32 =
// percent*256/100 - 1).  The HP path is the fault under test, so the
// codec starts at full DAC gain; the menu volume trims from there.
#ifndef ZX_ES8311_DEFAULT_VOLUME_PERCENT
#define ZX_ES8311_DEFAULT_VOLUME_PERCENT 100
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
#define ES8311_SYSTEM_REG11   0x11  // system "VSEL" (datasheet default 0x7C, never written by us - readback sanity)
#define ES8311_SYSTEM_REG12   0x12  // DAC power
#define ES8311_SYSTEM_REG13   0x13  // output (HP drive) enable
#define ES8311_SYSTEM_REG14   0x14  // analog input select + mic PGA enable
#define ES8311_ADC_REG16      0x16  // ADC gain scale
#define ES8311_ADC_REG17      0x17  // ADC gain (vendor default 0xC8)
#define ES8311_ADC_REG1C      0x1C  // ADC EQ + DC-offset cancel
#define ES8311_DAC_REG31      0x31  // DAC mute control
#define ES8311_DAC_REG32      0x32  // DAC volume (0 = min, 255 = max)
#define ES8311_DAC_REG37      0x37  // DAC EQ + fade

static bool _es8311_present = false;
static int _es8311_pwr_level = -1; // -1 unknown, 0/1 = the working polarity

static inline bool es_write(uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = { reg, val };
  const bool ok = i2c_write_blocking(ZX_ES8311_I2C, ZX_ES8311_ADDR, buf, 2, false) == 2;
#ifdef ZX_ESDIAG
  zxEsDiagWr(reg, ok); // E83 diag (c): write count + first-failure register
#endif
  return ok;
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

// Belt-and-braces menu note support (ungated): 0xFF = neither GP22
// polarity ever got an I2C ACK at 0x18 (power-domain question), else the
// working PWR_EN# polarity (0/1).  Lets the menu show "E83: PWR?" without
// any diag build.
uint8_t zxEs8311PwrProbe() {
  return (_es8311_pwr_level >= 0) ? (uint8_t)_es8311_pwr_level : 0xFF;
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
#ifdef ZX_ESDIAG
    // E83 diag (a): the GP22 polarity whose probe got an I2C ACK
    if (_es8311_present) zxEsDiagPwr((uint8_t)pwr_levels[i]);
#endif
    if (_es8311_present) _es8311_pwr_level = pwr_levels[i];
  }
#ifdef ZX_ESDIAG
  zxEsDiagAck(_es8311_present); // E83 diag (b): final I2C ACK state at 0x18
  if (!_es8311_present) zxEsDiagPwrNone(); // E83 diag (a): neither ACKed
#endif
  if (!_es8311_present) {
    printf("ES8311: no codec answered at 0x%02X on I2C0 (GP22 probed HIGH and LOW)\n",
           ZX_ES8311_ADDR);
    return false;
  }
  printf("ES8311: found at 0x%02X (PWR_EN# GP22 driven %s)\n",
         ZX_ES8311_ADDR, _es8311_pwr_level ? "HIGH" : "LOW");

  // ---- MCLK for the codec (R4, esp-bsp-default MCLK-driven mode) ----
  // CLOCK_GPOUT1 on GP23, source clk_sys, integer divider:
  //   fs = 44100 -> ideal MCLK = 256 x fs = 11.2896 MHz;
  //   sys = 251.75 MHz (640x480p60 TMDS) -> div = sys/11289600 = 22
  //   -> MCLK = 11.4432 MHz (+1.4%), tolerated by the codec's internal
  //   dividers (see the clocking-decision comment at the top).
  // Started BEFORE the codec reset/power-on sequence so the chip's POR
  // always sees a running MCLK (the esp-bsp MCLK-driven pattern).
  {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    uint32_t mclk_div = sys_hz / (ZX_ES8311_MCLK_FS_RATIO * ZX_ES8311_MCLK_FS);
    if (mclk_div < 1) mclk_div = 1; // GPOUT divider is at least 1
    clock_gpio_init_int_frac(ZX_ES8311_MCLK_PIN,
                             CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS,
                             mclk_div, 0);
    printf("ES8311: MCLK GP23 GPOUT1 sys/%lu = %lu Hz (ideal %u, fs %u)\n",
           (unsigned long)mclk_div,
           (unsigned long)(sys_hz / mclk_div),
           (unsigned)(ZX_ES8311_MCLK_FS_RATIO * ZX_ES8311_MCLK_FS),
           (unsigned)ZX_ES8311_MCLK_FS);
  }

  // ---- Reset (Espressif reference sequence) ----
  es_write(ES8311_RESET_REG00, 0x1F);
  sleep_ms(20);
  es_write(ES8311_RESET_REG00, 0x00);
  es_write(ES8311_RESET_REG00, 0x80); // power-on command

  // ---- Clock tree: MCLK-driven (esp-bsp default, mclk_from_mclk_pin) ----
  // R01: enable all clocks; bit7 = 0 -> internal MCLK from the MCLK pin
  // (the vendor's es8311_clock_config() writes plain 0x3F in that mode);
  // no MCLK/SCLK inversions.
  es_write(ES8311_CLKM1_REG01, 0x3F);

  // Coefficients for fs = 44100, MCLK = 256fs (coefficient table entry
  // {11289600, 44100}: pre_div=1, pre_multi=0 (x1), adc_div=1, dac_div=1,
  // fs_mode=0 (single speed), lrck_h=0x00, lrck_l=0xff, bclk_div=4).
  uint8_t r02 = 0;
  es_read(ES8311_CLKM2_REG02, &r02);
  r02 = (r02 & 0x07) | ((1 - 1) << 5) | (0 << 3); // pre-div 1, x1 pre-mult
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

  // ---- Analog power: DAC path on (playback phase), per esp-bsp verbatim ----
  es_write(ES8311_SYSTEM_REG0D, 0x01); // power up analog circuitry - NOT default
  es_write(ES8311_SYSTEM_REG0E, 0x02); // enable analog PGA, enable ADC modulator (esp-bsp value)
  es_write(ES8311_SYSTEM_REG12, 0x00); // power-up DAC - NOT default
  es_write(ES8311_SYSTEM_REG13, 0x10); // enable output to HP drive - NOT default
  // Mic-path defaults from the vendor's es8311_microphone_config(), written
  // unconditionally like esp-bsp board bring-up does, so the E83C readback
  // diag (R14/R17) always compares against known-written values.
  es_write(ES8311_SYSTEM_REG14, 0x1A); // enable analog MIC, max PGA gain
  es_write(ES8311_ADC_REG17, 0xC8);    // ADC gain (vendor value)
  es_write(ES8311_ADC_REG1C, 0x6A);    // ADC EQ bypass, digital DC cancel
  es_write(ES8311_DAC_REG37, 0x08);    // bypass DAC equalizer - NOT default

  // ---- Volume (modest default) + DAC unmute ----
  const int percent = ZX_ES8311_DEFAULT_VOLUME_PERCENT;
  const int vol = (percent <= 0) ? 0 : (percent * 256 / 100) - 1;
  es_write(ES8311_DAC_REG32, (uint8_t)vol);
  printf("ES8311: DAC volume %d%% (reg32=0x%02X)\n", percent, (unsigned)vol);
  uint8_t r31 = 0;
  es_read(ES8311_DAC_REG31, &r31);
  es_write(ES8311_DAC_REG31, r31 & ~0x60); // clear the two DAC mute bits

#ifdef ZX_ESDIAG
  // E83 diag (f): readback of the key registers, via the proven
  // repeated-START es_read.  Row order (R4, 6 values): R13/R14/R32/R17
  // (DAC/HP + ADC path, unchanged from R3) + R01/R11 (R4 clocking
  // evidence).  Expected: R13=0x10, R14=0x1A, R32=0xFF at 100% volume,
  // R17=0xC8, R01=0x3F (bit7 = 0 -> MCLK taken from the MCLK pin, all
  // codec clocks on; a 0xBF/0xFF here = the codec fell back to another
  // clock source or the write was dropped) and R11=0x7C (datasheet
  // default of this never-written register - a wrong R11 means the
  // read path or the chip state is off).  A readback of 0xFF itself
  // means the read transaction failed.
  {
    static const uint8_t rbRegs[6] = {
      ES8311_SYSTEM_REG13, ES8311_SYSTEM_REG14,
      ES8311_DAC_REG32, ES8311_ADC_REG17,
      ES8311_CLKM1_REG01, ES8311_SYSTEM_REG11
    };
    for (int i = 0; i < 6; ++i) {
      uint8_t v = 0;
      if (!es_read(rbRegs[i], &v)) v = 0xFF;
      zxEsDiagRb((uint8_t)i, v);
    }
  }
#endif

  printf("ES8311: playback path up: 44100Hz 16-bit I2S slave, MCLK-driven (GP23 GPOUT1), HP out\n");
  return true;
}

#else // !ZX_AUDIO_CODEC_ES8311

bool zxEs8311Init() { return false; }
bool zxEs8311Present() { return false; }
uint8_t zxEs8311PwrProbe() { return 0xFF; }

#endif
