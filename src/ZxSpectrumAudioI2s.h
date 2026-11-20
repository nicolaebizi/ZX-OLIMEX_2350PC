// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#pragma once

#include <pico/stdlib.h>

void i2s_audio_init();

uint32_t i2s_audio_freq();

void i2s_audio_handler(uint32_t vA, uint32_t vB, uint32_t vC, int32_t s, uint32_t buzzer, bool mute);

bool i2s_audio_ready();

// R8 silence-on-pause: the emulation is not stepping (menu shown /
// snapshot transition), so the I2S PIO state machine drains its FIFO and
// then pull-noblocks forever, repeating the LAST pushed sample - a stuck
// note.  Push one centred-silence stereo frame (0x0000/0x0000, the
// signed-16-bit mid level): the PIO then repeats SILENCE until the
// emulation resumes.  Cheap enough to call every menu frame at 60 Hz.
void i2s_audio_silence();
