// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat

#include "ZxSpectrumAudioPwmDma.h"

#if !defined(ZX_AUDIO_PWM_DMA)
#include "ZxSpectrumAudioNull.h"

void pwm_dma_audio_init()
{
}

uint32_t pwm_dma_audio_freq()
{
  return null_audio_freq();
}

void __not_in_flash_func(pwm_dma_audio_handler)(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute)
{
  null_audio_handler(vA, vB, vC, s, buzzer, mute);
}

bool __not_in_flash_func(pwm_dma_audio_ready)()
{
  return null_audio_ready();
}

void pwm_dma_audio_stop()
{
}

void pwm_dma_audio_silence()
{
}

#else

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "pico/printf.h"

#include "ZxSpectrumAudioPwmDma.h"
#include "ZxSpectrumAudioVol.h"

#ifndef ZX_PWM_DMA_AUDIO_FREQ
#define ZX_PWM_DMA_AUDIO_FREQ 44100
#endif
#ifndef ZX_PWM_DMA_L_PIN
#define ZX_PWM_DMA_L_PIN 26
#endif
#ifndef ZX_PWM_DMA_R_PIN
#define ZX_PWM_DMA_R_PIN 27
#endif
#ifndef ZX_PWM_DMA_VOL_PIN
#define ZX_PWM_DMA_VOL_PIN 34
#endif
#ifndef ZX_PWM_DMA_AMP_PIN
#define ZX_PWM_DMA_AMP_PIN 35
#endif
// Amp VOLUME control PWM frequency (static ~70% duty, set once at init;
// the R6/R7 level of the PAM8003 analog gain input).
#ifndef ZX_PWM_DMA_VOL_FREQ
#define ZX_PWM_DMA_VOL_FREQ 10000
#endif

// Stereo ring: one word per sample, low half = L duty, high half = R
// duty (the layout of the PWM CC register: ch A = bits 15:0, ch B =
// bits 31:16), so the DMA can write one word straight into CC.
// 1024 words * 4 B = 4096 B, 4096-aligned (DMA read ring requirement).
#define ZX_PWM_DMA_RING_SAMPLES 1024
#define ZX_PWM_DMA_RING_MASK (ZX_PWM_DMA_RING_SAMPLES - 1)
#define ZX_PWM_DMA_RING_SIZE_BITS 12 // log2(4096 B)
static uint32_t __attribute__((aligned(4096))) s_pwm_dma_ring[ZX_PWM_DMA_RING_SAMPLES];

// The single audio DMA channel: paced by DREQ_PWM_WRAP of the audio
// slice, one 32-bit write to the slice CC register per PWM period.
static int s_pwm_dma_channel = -1;
static volatile uint32_t s_pwm_dma_head = 0;
static uint32_t s_pwm_dma_top = 0;   // audio slice wrap value
static uint s_pwm_dma_slice = 0;     // PWM slice of GP26/GP27 (5)
static io_rw_32 *s_pwm_dma_cc = 0;   // &pwm_hw->slice[slice].cc

// Same mixing constants as the proven PIO-PWM driver
// (ZxSpectrumAudioPioPwm.cpp): v = _vol * 60, lr = (v * l) >> 14 in the
// 1024-step duty domain, then scaled to this slice's duty range.  The
// input centring is changed to (vA<<1) + vB + s - 128 so that a silent
// machine (vA = vB = vC = 0, buzzerSmoothed mid 128) maps to lr = 0 =
// the 50% duty mid-level.
static inline int32_t pwm_dma_duty(int32_t lr, int32_t half) {
  // lr is in the 0..1024 duty domain (mid 512); scale to this slice.
  const int32_t scaled = __mul_instruction(lr, (int32_t)(s_pwm_dma_top + 1)) >> 10;
  int32_t d = half + scaled;
  if (d < 0) d = 0;
  if (d > (int32_t)s_pwm_dma_top) d = s_pwm_dma_top;
  return d;
}

void pwm_dma_audio_init()
{
  // R7: the PAM8003 amp is ALWAYS ON (SHDN# LOW = enabled) - both audio
  // routes share the summed amp net, so the amp never goes down, no
  // matter which driver is selected (re-selecting this driver re-asserts
  // the same LOW; nothing else drives GP35 HIGH any more).
  gpio_init(ZX_PWM_DMA_AMP_PIN);
  gpio_set_dir(ZX_PWM_DMA_AMP_PIN, GPIO_OUT);
  gpio_put(ZX_PWM_DMA_AMP_PIN, 0);

  // L/R audio on PWM slice 5 (GP26 = ch A, GP27 = ch B): free-running,
  // period = sys_clk / 44100 (duty resolution ~5708 steps at the
  // 251.75 MHz TMDS sys clock).  NOTE: like the I2S bring-up, this
  // assumes the sys clock is already at the final 640x480p60 rate.
  gpio_set_function(ZX_PWM_DMA_L_PIN, GPIO_FUNC_PWM);
  gpio_set_function(ZX_PWM_DMA_R_PIN, GPIO_FUNC_PWM);
  gpio_set_function(ZX_PWM_DMA_VOL_PIN, GPIO_FUNC_PWM);
  const uint l_slice = pwm_gpio_to_slice_num(ZX_PWM_DMA_L_PIN);
  __unused const uint r_slice = pwm_gpio_to_slice_num(ZX_PWM_DMA_R_PIN);
  assert(l_slice == r_slice); // both pins must share one slice (GP26/27 -> PWM5)
  s_pwm_dma_slice = l_slice;
  const uint32_t sys_hz = clock_get_hz(clk_sys);
  s_pwm_dma_top = sys_hz / ZX_PWM_DMA_AUDIO_FREQ;
  if (s_pwm_dma_top < 1024) s_pwm_dma_top = 1024; // duty floor (safety)
  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_wrap(&cfg, s_pwm_dma_top);
  pwm_init(s_pwm_dma_slice, &cfg, true);
  s_pwm_dma_cc = &pwm_hw->slice[s_pwm_dma_slice].cc;
  const int32_t half = (int32_t)((s_pwm_dma_top + 1) / 2);

  // Amp VOLUME control: GP34 = PWM9 ch A, static ~70% duty (the R6
  // level; set-and-forget - the per-sample path has the software _vol
  // chain, this is the extra analog gain input of the PAM8003).
  const uint vol_slice = pwm_gpio_to_slice_num(ZX_PWM_DMA_VOL_PIN);
  const uint32_t vol_top = sys_hz / ZX_PWM_DMA_VOL_FREQ;
  pwm_config vcfg = pwm_get_default_config();
  pwm_config_set_wrap(&vcfg, vol_top);
  pwm_init(vol_slice, &vcfg, true);
  pwm_set_chan_level(vol_slice, pwm_gpio_to_channel(ZX_PWM_DMA_VOL_PIN),
                     (vol_top * 7 + 5) / 10); // ~70% duty

  // Pre-fill the ring with silence = 50% duty on both channels.
  const uint32_t silence = (uint32_t)half | ((uint32_t)half << 16);
  for (uint i = 0; i < ZX_PWM_DMA_RING_SAMPLES; ++i) s_pwm_dma_ring[i] = silence;
  s_pwm_dma_head = 0;

  // The single DMA channel: read = the ring (auto-wrapping), write =
  // the PWM5 CC register (fixed address), paced by the PWM wrap DREQ
  // (one request per PWM period = one sample per request).  No IRQ:
  // the channel runs in a huge transfer count and never needs service.
  if (s_pwm_dma_channel < 0) s_pwm_dma_channel = dma_claim_unused_channel(true);
  dma_channel_config dc = dma_channel_get_default_config(s_pwm_dma_channel);
  channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
  channel_config_set_read_increment(&dc, true);
  channel_config_set_write_increment(&dc, false);
  channel_config_set_dreq(&dc, pwm_get_dreq(s_pwm_dma_slice));
  // Ring on the READ side: wrap the read address every 4096 B.
  channel_config_set_ring(&dc, false, ZX_PWM_DMA_RING_SIZE_BITS);
  channel_config_set_enable(&dc, true);
  dma_channel_configure(
    s_pwm_dma_channel,
    &dc,
    (void *)s_pwm_dma_cc,            // write: PWM5 CC
    s_pwm_dma_ring,                  // read: the sample ring
    0xFFFFFF00u,                     // ~27 h of transfers at 44.1 kHz
    true                             // start now
  );

  printf("PWM-DMA audio: slice %u (GP%u/GP%u), fs %u Hz (top %lu), DMA ch %d (DREQ_PWM_WRAP%u, ring %u samples), vol GP%u ~70%%, amp GP%u always-on\n",
    (unsigned)s_pwm_dma_slice, ZX_PWM_DMA_L_PIN, ZX_PWM_DMA_R_PIN,
    (unsigned)ZX_PWM_DMA_AUDIO_FREQ, (unsigned long)s_pwm_dma_top,
    s_pwm_dma_channel, (unsigned)s_pwm_dma_slice,
    (unsigned)ZX_PWM_DMA_RING_SAMPLES, ZX_PWM_DMA_VOL_PIN, ZX_PWM_DMA_AMP_PIN);
}

uint32_t pwm_dma_audio_freq() {
  return ZX_PWM_DMA_AUDIO_FREQ;
}

bool __not_in_flash_func(pwm_dma_audio_ready)()
{
  // Free space in the ring: the DMA read address (wrapping) is the tail.
  const uint32_t tail =
    (dma_channel_hw_addr(s_pwm_dma_channel)->read_addr - (uintptr_t)s_pwm_dma_ring) >> 2;
  const uint32_t used = (s_pwm_dma_head - tail) & ZX_PWM_DMA_RING_MASK;
  // Keep two slots of slack: one for the transfer in flight, one so a
  // burst of two samples never overtakes the reader.
  return used <= ZX_PWM_DMA_RING_MASK - 2;
}

void __not_in_flash_func(pwm_dma_audio_handler)(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute)
{
  if (!pwm_dma_audio_ready()) return; // ring full: drop the sample

  // Same mixing constants/shape as the proven PIO-PWM driver, but
  // centred so silence = the 50% duty mid-level:
  //   l' = (vA<<1) + vB + s - 128   (vA/vB 0..255, s mid = 128)
  //   lr = (v * l') >> 14           (v = _vol*60, 1024-step duty domain)
  const int32_t l = (int32_t)(vA << 1) + (int32_t)vB + s - 128;
  const int32_t r = (int32_t)(vC << 1) + (int32_t)vB + s - 128;
  const int32_t v = mute ? 0 : (int32_t)__mul_instruction(_vol, 60);
  const int32_t half = (int32_t)((s_pwm_dma_top + 1) / 2);
  const uint32_t duty = (uint32_t)pwm_dma_duty(__mul_instruction(v, l) >> 14, half)
                      | ((uint32_t)pwm_dma_duty(__mul_instruction(v, r) >> 14, half) << 16);

  s_pwm_dma_ring[s_pwm_dma_head] = duty;
  s_pwm_dma_head = (s_pwm_dma_head + 1) & ZX_PWM_DMA_RING_MASK;
}

void pwm_dma_audio_stop()
{
  if (s_pwm_dma_channel < 0) return;
  // Abort the transfer and park both PWM channels at the silent 50% mid
  // level (the last ring word could hold any duty; the constant mid DC
  // is exactly the init-time silence).  The PWM slices keep running and
  // the amp stays ON - the I2S/codec route shares the summed net.
  dma_channel_abort(s_pwm_dma_channel);
  const int32_t half = (int32_t)((s_pwm_dma_top + 1) / 2);
  pwm_set_chan_level(s_pwm_dma_slice, PWM_CHAN_A, half);
  pwm_set_chan_level(s_pwm_dma_slice, PWM_CHAN_B, half);
}

void pwm_dma_audio_silence()
{
  if (s_pwm_dma_channel < 0) return;
  // R8 silence-on-pause: overwrite EVERY ring slot with the same centred
  // silence word as the init pre-fill (50% duty on both channels), then
  // re-sync the software head to the DMA tail: the cyclic reader now
  // re-reads only silence, so a note stuck since the emulation stopped
  // (menu / snapshot transition) dies within one ring pass, and
  // pwm_dma_audio_ready() sees an empty ring for the resumed emulation.
  const int32_t half = (int32_t)((s_pwm_dma_top + 1) / 2);
  const uint32_t silence = (uint32_t)half | ((uint32_t)half << 16);
  for (uint i = 0; i < ZX_PWM_DMA_RING_SAMPLES; ++i) s_pwm_dma_ring[i] = silence;
  const uint32_t tail =
    (dma_channel_hw_addr(s_pwm_dma_channel)->read_addr - (uintptr_t)s_pwm_dma_ring) >> 2;
  s_pwm_dma_head = tail & ZX_PWM_DMA_RING_MASK;
}

#endif
