// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat

#include "ZxSpectrumAudioI2s.h"

#if !defined(PICO_AUDIO_I2S)
#include "ZxSpectrumAudioNull.h"

void i2s_audio_init()
{
  null_audio_init();
}

uint32_t i2s_audio_freq()
{
  return null_audio_freq();
}

void __not_in_flash_func(i2s_audio_handler)(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute)
{
  null_audio_handler(vA, vB, vC, s, buzzer, mute);
}

bool __not_in_flash_func(i2s_audio_ready)()
{
  return null_audio_ready();
}

void i2s_audio_silence()
{
}

#else

#ifdef ZX_AUDIO_I2S_LRCK_FIRST
// Boards whose LRCK sits on the LOWER GPIO of the adjacent BCLK/LRCK
// pair (Olimex RP2350-PC: LRCK=GP29, BCLK=GP30) - the side-set bit roles
// are swapped in this program variant (bit0 = LRCK, bit1 = BCLK).
// PICO_AUDIO_I2S_BCLK is the side-set BASE pin (LRCK); BCLK = base + 1.
#include "audio_i2s_olimex.pio.h"
#define I2S_PIO_PROGRAM audio_i2s_olimex_program
#define I2S_PIO_PROGRAM_INIT audio_i2s_olimex_program_init
#else
#include "audio_i2s.pio.h"
#define I2S_PIO_PROGRAM audio_i2s_program
#define I2S_PIO_PROGRAM_INIT audio_i2s_program_init
#endif
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ZxSpectrumAudioVol.h"
#include "ZxSpectrumAudio16BitStereo.h"
#ifdef ZX_AUDIO_DVI_MIRROR
// Mirror every I2S stereo sample into the DVI (HDMI) audio ring as well.
// No-op unless the DVI video pipeline is running (guarded inside).
#include "ZxSpectrumAudioHdmi.h"
#endif
#ifdef ZX_AUDIO_CODEC_ES8311
// Olimex RP2350-PC phase 2: bring the on-board ES8311 up (power LDO via
// PWR_EN# GP22, I2C0 @ GP32/33 addr 0x18, BCLK-driven clocking, DAC path
// unmuted) before the I2S master starts pushing samples to DSDIN=GP31.
#include "ZxEs8311.h"
#include "hardware/pwm.h"
#endif
#ifdef ZX_ESDIAG
// E83 diag (d)(e): live PIO FIFO sample pushes + SM running flag
#include "ZxEsDiag.h"
#endif

#ifndef PICO_I2S_AUDIO_FREQ
#define PICO_I2S_AUDIO_FREQ 44100
#endif
#ifndef PICO_AUDIO_I2S_PIO
#define PICO_AUDIO_I2S_PIO pio1
#endif
#ifndef PICO_AUDIO_I2S_PIO_FUNC
#define PICO_AUDIO_I2S_PIO_FUNC GPIO_FUNC_PIO1
#endif
#ifndef PICO_AUDIO_I2S_DATA
#define PICO_AUDIO_I2S_DATA 9
#endif
#ifndef PICO_AUDIO_I2S_BCLK
#define PICO_AUDIO_I2S_BCLK 10
#endif

static void update_pio_frequency(uint32_t sample_freq, PIO audio_pio, uint pio_sm)
{
  uint32_t system_clock_frequency = clock_get_hz(clk_sys);
  assert(system_clock_frequency < 0x40000000);
  uint32_t divider = system_clock_frequency * 4 / (sample_freq * 3); // avoid arithmetic overflow
  assert(divider < 0x1000000);
  pio_sm_set_clkdiv_int_frac(audio_pio, pio_sm, divider >> 8u, divider & 0xffu);
}

static uint i2s_audio_sm = 0;

inline bool is2_audio_ready()
{
  return !pio_sm_is_tx_fifo_full(PICO_AUDIO_I2S_PIO, i2s_audio_sm);
}

static inline void is2_audio_put(uint32_t x)
{
  *(volatile uint32_t *)&PICO_AUDIO_I2S_PIO->txf[i2s_audio_sm] = x;
}

void i2s_audio_init()
{
#ifdef ZX_AUDIO_CODEC_ES8311
  // ---- PAM8003 speaker amp pins (R7): the amp is ALWAYS ON - both
  // audio routes share the summed amp net (the codec's analog out AND
  // the RC-filtered PWM GP26/27 feed the same PAM8003 input), so the
  // speakers stay live whichever driver is selected.  GP35 = amp SHDN#
  // (active LOW = ON); GP34 = amp VOLUME control (analog gain input
  // behind an on-board RC) parked ONCE at a static comfortable level =
  // PWM duty ~70% (a plain GPIO has no mid level; this PWM channel is
  // set-and-forget, never updated by the audio path).
  gpio_init(35);
  gpio_set_dir(35, GPIO_OUT);
  gpio_put(35, 0); // SHDN# LOW = PAM8003 ON (always, R7)
  {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint32_t vol_top = sys_hz / 10000;
    const uint vol_slice = pwm_gpio_to_slice_num(34);
    pwm_config vcfg = pwm_get_default_config();
    pwm_config_set_wrap(&vcfg, vol_top);
    pwm_init(vol_slice, &vcfg, true);
    pwm_set_chan_level(vol_slice, PWM_CHAN_A, (vol_top * 7 + 5) / 10); // ~70% duty
    gpio_set_function(34, GPIO_FUNC_PWM);
  }
  // Phase 2 audio: codec bring-up first (the sys clock is already at the
  // final 640x480p60 TMDS rate here, so the 400 kHz I2C baudrate divider
  // is computed for the real clock).  If the codec does not answer the
  // I2S master still runs; audio is simply inaudible, nothing stalls -
  // and the amp stays ON for the PWM-DMA route either way (R7).
#endif
  gpio_set_function(PICO_AUDIO_I2S_DATA, PICO_AUDIO_I2S_PIO_FUNC);
  gpio_set_function(PICO_AUDIO_I2S_BCLK, PICO_AUDIO_I2S_PIO_FUNC);
  gpio_set_function(PICO_AUDIO_I2S_BCLK + 1, PICO_AUDIO_I2S_PIO_FUNC);
  uint offset = pio_add_program(PICO_AUDIO_I2S_PIO, &I2S_PIO_PROGRAM);
#ifdef PICO_AUDIO_I2S_SM
  i2s_audio_sm = PICO_AUDIO_I2S_SM;
#else
  i2s_audio_sm = pio_claim_unused_sm(PICO_AUDIO_I2S_PIO, true);
#endif
  I2S_PIO_PROGRAM_INIT(PICO_AUDIO_I2S_PIO, i2s_audio_sm, offset, PICO_AUDIO_I2S_DATA, PICO_AUDIO_I2S_BCLK);
  update_pio_frequency(PICO_I2S_AUDIO_FREQ, PICO_AUDIO_I2S_PIO, i2s_audio_sm);
  pio_sm_set_enabled(PICO_AUDIO_I2S_PIO, i2s_audio_sm, true);
#ifdef ZX_ESDIAG
  zxEsDiagSmRun(true); // E83 diag (e): the I2S PIO SM is running
#endif
}

uint32_t i2s_audio_freq() {
  return PICO_I2S_AUDIO_FREQ;
}

static inline bool audio_ready() {
  return !pio_sm_is_tx_fifo_full(PICO_AUDIO_I2S_PIO, i2s_audio_sm);
}

void __not_in_flash_func(i2s_audio_handler)(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute)
{
  uint32_t ll, rr;
  audio_handler_16bit_helper(vA, vB, vC, s, buzzer, mute, ll, rr);

  if (audio_ready())
  {
    is2_audio_put((ll << 16) | rr);
#ifdef ZX_ESDIAG
    zxEsDiagSample(); // E83 diag (d): one stereo sample -> PIO FIFO
#endif
  }

#ifdef ZX_AUDIO_DVI_MIRROR
  // Feed the same sample stream into the DVI (HDMI) audio ring when the
  // HDMI/DVI video output is active (hdmi_audio_handler checks dvi_running
  // and does nothing otherwise).
  hdmi_audio_handler(vA, vB, vC, s, buzzer, mute);
#endif
}

bool __not_in_flash_func(i2s_audio_ready)()
{
  return audio_ready();
}

void i2s_audio_silence()
{
  // R8 silence-on-pause: push one centred-silence stereo frame (signed
  // 16-bit mid level = 0x0000/0x0000, exactly what the mute path of
  // audio_handler_16bit_helper produces).  After the FIFO drains the
  // PIO pull-noblock repeats this silence forever instead of the last
  // played AY sample; re-pushed every menu frame, it also outlives any
  // stale in-FIFO sample from the moment the emulation stopped.
  if (audio_ready()) is2_audio_put(0);
}

#endif
