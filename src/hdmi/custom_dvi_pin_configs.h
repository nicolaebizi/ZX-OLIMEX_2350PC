// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#ifndef _CUSTOM_DVI_PIN_CONFIGS_H
#define _CUSTOM_DVI_PIN_CONFIGS_H

// This file defines the TMDS pair layouts on a handful of boards I have been
// developing on. It's not a particularly important file -- just saves some
// copy + paste.

#include "dvi_serialiser.h"

#ifndef DVI_DEFAULT_SERIAL_CONFIG
#define DVI_DEFAULT_SERIAL_CONFIG pico_sock_cfg
#endif

#ifndef DVI_DEFAULT_PIO_INST
#define DVI_DEFAULT_PIO_INST pio0
#endif

// ----------------------------------------------------------------------------
// Picomputer flip board by Bobricius
//
// This board is wired the same as the Adafruit sock
static const struct dvi_serialiser_cfg picomputer_flip_cfg = {
        .pio = DVI_DEFAULT_PIO_INST,
        .sm_tmds = {0, 1, 2},
        .pins_tmds = {12, 18, 16},
        .pins_clk = 14,
        .invert_diffpairs = false
};

// ----------------------------------------------------------------------------
//  Waveshare - RP2040 - PiZero
//
static const struct dvi_serialiser_cfg waveshare_rp2040_pizero_hdmi_cfg = {
	.pio = DVI_DEFAULT_PIO_INST,
	.sm_tmds = {0, 1, 2},
	.pins_tmds = {26, 24, 22},
	.pins_clk = 28,
	.invert_diffpairs = false
};

// ----------------------------------------------------------------------------
//  Waveshare - RP2350 - PiZero
//
//  Probably want to run this on a pio on its own,
//  or at least with other high numbered gpio access
//
static const struct dvi_serialiser_cfg waveshare_rp2350_pizero_hdmi_cfg = {
	.pio = DVI_DEFAULT_PIO_INST,
	.sm_tmds = {0, 1, 2},
	.pins_tmds = {36, 34, 32},
	.pins_clk = 38,
	.invert_diffpairs = false
};

static const struct dvi_serialiser_cfg murmulator2_hdmi_cfg = {
	.pio = DVI_DEFAULT_PIO_INST,
	.sm_tmds = {0, 1, 2},
	.pins_tmds = {14, 16, 18},
	.pins_clk = 12,
	.invert_diffpairs = true
};

static const struct dvi_serialiser_cfg SpotPear_hdmi_cfg = {
	.pio = DVI_DEFAULT_PIO_INST,
	.sm_tmds = {0, 1, 2},
	.pins_tmds = {10, 12, 14},
	.pins_clk = 8,
	.invert_diffpairs = true
};

// ----------------------------------------------------------------------------
// Olimex RP2350-PC (RP2350B, 80-pin).  HSTX pins GPIO12-19 wired straight
// through 270R series resistors to the HDMI socket, DVI-Sock pin order
// (pico-examples dvi_out_hstx_encoder lane_to_output_bit {0,6,4}):
//   D0 = GP12/13, CK = GP14/15, D2 = GP16/17, D1 = GP18/19
// No level shifters, no inversion.
static const struct dvi_serialiser_cfg olimex_rp2350pc_hdmi_cfg = {
	.pio = DVI_DEFAULT_PIO_INST,
	.sm_tmds = {0, 1, 2},
	.pins_tmds = {12, 18, 16}, // D0, D1, D2 (even pins of each pair)
	.pins_clk = 14,
	.invert_diffpairs = false
};

#endif
