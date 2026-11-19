// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <pico/stdlib.h>

// Default to hardware PWM
// TODO This is messy - force a choice in makefile
// ZX_AUDIO_NULL_ONLY: targets with no physical audio path at all (e.g.
// Olimex RP2350-PC phase 1, ES8311 codec in a later phase) opt out of the
// auto-selection below; every audio driver then compiles down to the null
// driver.
#if !defined(PICO_PIO_PWM_AUDIO) && !defined(PICO_AUDIO_I2S) && !defined(PICO_HDMI_AUDIO) && !defined(ZX_AUDIO_NULL_ONLY)
#define PICO_PWM_AUDIO
#endif

void pwm_audio_init();

uint32_t pwm_audio_freq();

void pwm_audio_handler(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute);

bool pwm_audio_ready();
