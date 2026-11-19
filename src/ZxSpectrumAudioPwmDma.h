// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat

#pragma once

#include <pico/stdlib.h>

// R7 (Olimex RP2350-PC): ZERO-IRQ DMA-driven PWM speaker audio, ported
// from the proven phase-9 driver of the olimex_rp2350pc tree.
//
// The per-sample audio handler only writes one packed stereo word into
// a ring buffer in SRAM; a single DMA channel paced by the PWM wrap
// DREQ (one request per PWM period = one sample at 44.1 kHz) drains the
// ring into the PWM CC register of the audio slice.  No audio IRQ of
// any kind runs (no per-sample handler IRQ, no DMA completion IRQ):
// the audio IRQ count per second is 0.
//
// Hardware net (Olimex RP2350-PC, R7):
//   GP26 = PWM5 ch A  = L audio (RC-filtered into the PAM8003 amp)
//   GP27 = PWM5 ch B  = R audio
//   GP34 = PWM9 ch A  = amp VOLUME control duty (static ~70%, set at init)
//   GP35 = amp SHDN#  (LOW = amp on) - the amp is ALWAYS ON in R7: both
//           audio routes share the summed amp net (the codec keeps the
//           HP jack + the speakers, the PWM-DMA drives the speakers).
//
// Silence is centred at 50% duty (mid-supply DC into the amp input).
// The boot default is this driver (the sure route); the ES8311/I2S codec
// stays selectable live from the menu (the codec is the electrical
// mystery, kept for the HP jack).

void pwm_dma_audio_init();

uint32_t pwm_dma_audio_freq();

void pwm_dma_audio_handler(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute);

bool pwm_dma_audio_ready();

// Park the PWM-DMA route when another driver is selected live (menu ->
// I2S/ES8311): abort the DMA transfer and leave both PWM channels at the
// silent 50% mid level.  The amp stays ON (shared net).
void pwm_dma_audio_stop();
