# SPDX-License-Identifier: MIT
# Copyright (c) 2026 fruit-bat
########################################################################
# ZX-MURMULATOR2 SpeccyP - dual VGA / HDMI with boot-time auto-detect
#
# Single RP2350 (pico2) target: at boot the shared GPIO12-19 block is
# probed (VideoAutoDetect) and exactly one video pipeline is started:
#   - HDMI/DVI sink detected  -> libdvi DVI 640x480p60 (TMDS on 12-19)
#   - VGA monitor assumed     -> pico_scanvideo_dpi 640x480x60 (RGB 222
#                                + VSYNC on the same GPIO12-19)
# Audio is always the I2S DAC (GPIO9 data, 10 BCLK, 11 LRCLK); when the
# HDMI sink was detected the same sample stream is additionally mirrored
# into the DVI audio ring (ZX_AUDIO_DVI_MIRROR).
########################################################################

# The menu library normally comes in three mutually-exclusive flavours
# (pico_dvi_menu / pico_scanvideo_menu / pico_vga_menu) which all share the
# same pico_menu_common sources, so linking two of them would duplicate
# every common symbol. For this target we build a combined INTERFACE
# library with the common sources compiled once plus both renderers.
set(PICO_DVI_MENU_DIR ${PICO_FOLDER}/pico-dvi-menu)
set(pico_menu_common
	${PICO_DVI_MENU_DIR}/src/PicoRectXYWH.cpp
	${PICO_DVI_MENU_DIR}/src/PicoRectXYXY.cpp
	${PICO_DVI_MENU_DIR}/src/PicoCharScreen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoClippedScreen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWinHidKeyboard.cpp
	${PICO_DVI_MENU_DIR}/src/PicoPen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWin.cpp
	${PICO_DVI_MENU_DIR}/src/PicoDisplay.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWinBlock.cpp
	${PICO_DVI_MENU_DIR}/src/PicoSelect.cpp
	${PICO_DVI_MENU_DIR}/src/PicoOption.cpp
	${PICO_DVI_MENU_DIR}/src/PicoOptionText.cpp
	${PICO_DVI_MENU_DIR}/src/PicoQuickKey.cpp
	${PICO_DVI_MENU_DIR}/src/PicoQuickKeyAscii.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWiz.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWizUtils.cpp
	${PICO_DVI_MENU_DIR}/src/PicoWizExplorer.cpp
	${PICO_DVI_MENU_DIR}/src/PicoTextField.cpp
	${PICO_DVI_MENU_DIR}/src/PicoExplorer.cpp
	${PICO_DVI_MENU_DIR}/src/PicoStaticScreen.cpp
	${PICO_DVI_MENU_DIR}/src/PicoSlider.cpp
)

add_library(pico_dual_menu INTERFACE)

target_sources(pico_dual_menu INTERFACE
	${pico_menu_common}
	${PICO_DVI_MENU_DIR}/src/PicoCharRenderer.cpp       # DVI/HDMI menu renderer
	${PICO_DVI_MENU_DIR}/src/PicoCharRendererScanvideo.cpp # scanvideo menu renderer
)

target_include_directories(pico_dual_menu INTERFACE
	${PICO_DVI_MENU_DIR}/src
)

set(zxspectrum_vgahdmi_auto_src
  main_auto.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../VideoAutoDetect.cpp
)

add_executable(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz
  ${zxspectrum_common_src}
  ${zxspectrum_rgb_src}
  ${zxspectrum_scanvideo_src}
  ${zxspectrum_dvi_src}
  ${zxspectrum_vgahdmi_auto_src}
)

target_link_libraries(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz
  ${zxspectrum_common_libs}
  pico_emu_utils
  libdvi
  pico_scanvideo_dpi
  pico_dual_menu
  tinyusb_host
  tinyusb_board
  hardware_pio
  hardware_pwm
)

zxspectrum_generate_pio_headers(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz)

pico_enable_stdio_usb(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz 0)
pico_enable_stdio_uart(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz 1)

set(zxspectrum_vgahdmi_auto_defines
  # ---- MURMULATOR2 board (same as the existing M2 HDMI/VGA targets) ----
  ZX_PLATFORM="MURMULATOR2"
  DVI_CUSTOM_CONFIG
  DVI_DEFAULT_SERIAL_CONFIG=murmulator2_hdmi_cfg
  PICO_CORE1_STACK_SIZE=0x400
  DVI_VERTICAL_REPEAT=2
  DVI_N_TMDS_BUFFERS=3
  DVI_1BPP_BIT_REVERSE=1
  # Tell FATFS we want to find files
  FF_USE_FIND=1
  # SD card on SPI0
  SDCARD_PIN_SPI0_CS=5
  SDCARD_PIN_SPI0_SCK=6
  SDCARD_PIN_SPI0_MOSI=7
  SDCARD_PIN_SPI0_MISO=4
  # Alternate AY volume map, tape input, PS/2 keyboard
  AY8912_ALT_VOL_MAP
  EAR_PIN=22
  USE_PS2_KBD
  PS2KBD_CLK2_DAT
  # Dual NES joypads
  NESPAD_DATA_PIN_BASE=26
  NESPAD_CLK_PIN_BASE=20
  NESPAD_PIO=pio1
  NESPAD_IRQ=PIO1_IRQ_0
  NESPAD_ENABLE
  # ---- I2S DAC, always on (LRCLK = BCLK + 1 = GPIO11) ----
  PICO_AUDIO_I2S=1
  PICO_AUDIO_I2S_PIO=pio1
  PICO_AUDIO_I2S_PIO_FUNC=GPIO_FUNC_PIO1
  PICO_AUDIO_I2S_DATA=9
  PICO_AUDIO_I2S_BCLK=10
  # ---- DVI/HDMI 640x480p60 pipeline ----
  DVI_TIMING=dvi_timing_640x480p_60hz
  # Menu screen resolution
  PCS_COLS=80
  PCS_ROWS=30
  # Display resolution
  DISPLAY_WIDTH_PIXELS=640
  DISPLAY_HEIGHT_PIXELS=480
  ZX_SPECTRUM_INT_SRC=SyncToDisplay
  PICO_PWM_AUDIO_FREQ=42000
  # ---- VGA 640x480x60 pipeline (shared GPIO12-19, RGB 222 + VSYNC) ----
  VGA_MODE=vga_mode_640x240_60
  VGA_ENC_RGB_222
  VGA_VSYNC
  PICO_SCANVIDEO_COLOR_PIN_COUNT=6u
  PICO_SCANVIDEO_DPI_PIXEL_RCOUNT=2u
  PICO_SCANVIDEO_DPI_PIXEL_GCOUNT=2u
  PICO_SCANVIDEO_DPI_PIXEL_BCOUNT=3u
  PICO_SCANVIDEO_DPI_PIXEL_RSHIFT=4u
  PICO_SCANVIDEO_DPI_PIXEL_GSHIFT=2u
  PICO_SCANVIDEO_DPI_PIXEL_BSHIFT=0u
  PICO_SCANVIDEO_COLOR_PIN_BASE=12u
  PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=700
  # ---- Core voltage 1.30V ----
  VREG_VSEL=VREG_VOLTAGE_1_30
  # ---- Dual audio: I2S always + DVI audio ring when HDMI is detected ----
  PICO_HDMI_AUDIO
  ZX_AUDIO_DVI_MIRROR
  # ---- Boot-time VGA/HDMI auto-detect ----
  ZX_VIDEO_AUTO_DETECT
  # ---- CUB-Z renders on the VGA scanvideo path only (heap retreat):
  # the DVI cubz encoder + its ~6.9 KB statics are compiled out of this
  # dual-pipeline target; with a CUB-Z selected over HDMI it falls back
  # to the generic DVI path (no picture - use the VGA display for the
  # CUB-Z).  M1-HDMI and the other DVI targets keep ZX_CUBZ_DVI=ON. ----
  ZX_CUBZ_DVI=0
)

target_compile_definitions(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz PRIVATE
  ${zxspectrum_vgahdmi_auto_defines}
)

# SDK >= 2.x board headers (pico2) contain pico_board_cmake_set(...) /
# pico_board_cmake_set_default(...) marker macros which pico.h defines as
# no-ops. libdvi's tmds_encode.S includes pico/config.h directly (bypassing
# pico.h), so when assembling for rp2350 the markers leak into the
# assembler. Define the markers empty for this target only.
target_compile_options(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz PRIVATE
  "-Dpico_board_cmake_set(x,y)="
  "-Dpico_board_cmake_set_default(x,y)="
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(ZX-MURMULATOR2_VGA_HDMI_AUTO_640x480x60Hz)

########################################################################
# Olimex RP2350-PC (RP2350B, 80-pin) - HDMI build
#
# Phase 1: HDMI (HSTX TMDS on GPIO12-19, DVI-Sock order) + microSD (SPI1,
# scattered pins) + USB keyboard/mouse via the on-board FE1.1s 4-port hub.
# NO audio anything: every audio driver compiles to the null stub
# (ZX_AUDIO_ENABLED=0 + ZX_AUDIO_NULL_ONLY).
#
# Still hard-disabled (code stays in the tree, this target does not
# compile or wire it):
#   - VGA / scanvideo pipeline      (no zxspectrum_rgb332/scanvideo src,
#                                    no VGA_MODE define)
#   - PS/2 keyboard, NES pads       (no USE_PS2_KBD / NESPAD_ENABLE)
#   - PWM/I2S/TMDS audio, PAM8003 speaker amp, codec mic path
#
# main.cpp is shared verbatim with the HDMI family (src/hdmi/main.cpp):
# pure DVI pipeline, menu + all machines + SuperSpec CF, LED on GP25.
########################################################################

set(zxspectrum_olimex_rp2350pc_src
  ${CMAKE_CURRENT_LIST_DIR}/../hdmi/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../ZxEs8311.cpp # ES8311 codec bring-up (real body, phase 4 fix)
  ${zxspectrum_dvi_src}
)

add_executable(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz
  ${zxspectrum_common_src}
  ${zxspectrum_olimex_rp2350pc_src}
)

target_link_libraries(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz
  ${zxspectrum_common_libs}
  # zxspectrum_hdmi_libs (defined in src/hdmi, not visible in sibling
  # subdirectories) expanded here:
  libdvi
  tinyusb_host
  tinyusb_board
  pico_emu_utils
  pico_dvi_menu
  # Phase-2 audio: the ES8311 codec bring-up (ZxEs8311.cpp) talks I2C0
  # (SDA=GP32/SCL=GP33) and PIO1 for the I2S master.
  hardware_i2c
)

# The common sources include the PIO header stubs (pwm.pio.h, audio_i2s.pio.h,
# ...) unconditionally even on targets whose audio/PIO paths are disabled;
# the header generation is inert without the corresponding PICO_AUDIO_*
# defines.
zxspectrum_generate_pio_headers(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz)

# The Olimex audio pins are LRCK=GP29 (LOWER gpio) / BCLK=GP30 / DSDIN
# data=GP31, so the audio_i2s.pio side-set roles need swapping: generate
# the olimex variant header for this target only (shared by no other
# target; inert unless ZX_AUDIO_I2S_LRCK_FIRST selects it).
pico_generate_pio_header(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz
  ${CMAKE_CURRENT_LIST_DIR}/../audio_i2s_olimex.pio
)

pico_enable_stdio_usb(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz 0)
pico_enable_stdio_uart(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz 1)

set(zxspectrum_olimex_rp2350pc_defines
  ZX_PLATFORM="OLIMEX_RP2350PC"
  # ---- DVI/HDMI 640x480p60 pipeline, Olimex lane order ----
  DVI_CUSTOM_CONFIG
  DVI_DEFAULT_SERIAL_CONFIG=olimex_rp2350pc_hdmi_cfg
  PICO_CORE1_STACK_SIZE=0x400
  DVI_VERTICAL_REPEAT=2
  DVI_N_TMDS_BUFFERS=3
  DVI_1BPP_BIT_REVERSE=1
  DVI_TIMING=dvi_timing_640x480p_60hz
  # Menu screen resolution
  PCS_COLS=80
  PCS_ROWS=30
  # Display resolution
  DISPLAY_WIDTH_PIXELS=640
  DISPLAY_HEIGHT_PIXELS=480
  # Default interrupt source (same as the other 640x480 HDMI targets)
  ZX_SPECTRUM_INT_SRC=SyncToCpu
  # (nominal PWM audio frequency; the audio path is the null stub on this
  # target - this define only feeds the idle PWM driver stub)
  PICO_PWM_AUDIO_FREQ=42000
  # Tell FATFS we want to find files
  FF_USE_FIND=1
  # ---- microSD on SPI1 (scattered pins): CLK=GP10, CMD=GP11, CS=GP9,
  # DAT0=GP24.  The macro names say SPI0 but only pin the sdcard driver
  # uses; SDCARD_SPI_BUS selects the actual peripheral (GP9/10/11/24 all
  # belong to SPI1 on the RP2350B pin-out). ----
  SDCARD_SPI_BUS=spi1
  SDCARD_PIN_SPI0_CS=9
  SDCARD_PIN_SPI0_SCK=10
  SDCARD_PIN_SPI0_MOSI=11
  SDCARD_PIN_SPI0_MISO=24
  # ---- Core voltage 1.30V for the 251.75MHz TMDS bit clock (same
  # clock/voltage pairing as the proven 640x480 DVI targets) ----
  VREG_VSEL=VREG_VOLTAGE_1_30
  # ---- Feature flags (board header sets the same values) ----
  ZX_HAS_VGA=0
  ZX_HAS_PS2=0
  ZX_HAS_NES=0
  # ---- Phase-2 audio: the ES8311 codec (ZxEs8311.cpp) talks I2C0
  # (SDA=GP32/SCL=GP33) and PIO1 for the I2S master.  PIO1 I2S master,
  # LRCK=GP29 is the side-set base / LOWER gpio of the pair, BCLK=GP30,
  # data out = GP31 -> audio_i2s_olimex.pio program variant
  # (ZX_AUDIO_I2S_LRCK_FIRST).  The boot init sweep brings the codec up
  # (zxEs8311Init(), PWR_EN# GP22 auto-probe + I2C0 GP32/33 addr 0x18,
  # repeated-START es_read).  R7: the boot default is the PWM-DMA route
  # (ZxDviRenderLoopAudioDefault -> pwm_dma index), the codec/I2S entry
  # stays selectable live from the menu.  Speaker amp (SHDN# GP35) =
  # ALWAYS on (R6/R7, both routes share the amp net); VOLUME GP34
  # static ~70%. ----
  PICO_AUDIO_I2S=1
  PICO_AUDIO_I2S_PIO=pio1
  PICO_AUDIO_I2S_PIO_FUNC=GPIO_FUNC_PIO1
  PICO_AUDIO_I2S_DATA=31
  PICO_AUDIO_I2S_BCLK=29
  ZX_AUDIO_I2S_LRCK_FIRST
  PICO_I2S_AUDIO_FREQ=44100
  # ES8311 codec bring-up compiled for real: defining it on the command
  # line guarantees ZxEs8311.cpp sees it regardless of include order
  # (same value as the board header: ZX_AUDIO_CODEC_ES8311=1).
  ZX_AUDIO_CODEC_ES8311=1
  # Menu: the codec entry keeps its spelled-out role (now "experimental").
  ZX_AUDIO_ES8311_ONLY
  # ---- R7 audio: the PWM-DMA direct-amp route is the boot DEFAULT (the
  # sure speakers path; the codec's DAC is the electrical mystery, the
  # scope comes later).  GP26 = PWM5 ch A (L), GP27 = PWM5 ch B (R), one
  # DMA channel drains a 1024-sample packed-CC ring on DREQ_PWM_WRAP5 -
  # ZERO audio IRQs.  GP34 = amp VOLUME static ~70% duty, GP35 = amp
  # SHDN# LOW = the amp is ALWAYS on (both routes share the summed amp
  # net: the codec keeps the HP jack + the speakers via the same net).
  # The I2S/ES8311 entry stays selectable live from the menu (the boot
  # init sweep still brings the codec up; stale SD settings clamping is
  # keyed on ZX_AUDIO_PWM_DMA_DEFAULT). ----
  ZX_AUDIO_PWM_DMA=1
  ZX_AUDIO_PWM_DMA_DEFAULT
  # (board header sets the same audio flags: ZX_AUDIO_CODEC_ES8311=1,
  # ZX_AUDIO_ENABLED=1)
  ZX_AUDIO_ENABLED=1
)

target_compile_definitions(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz PRIVATE
  ${zxspectrum_olimex_rp2350pc_defines}
)

# Same assembler marker fix as the M2-AUTO target above (libdvi's
# tmds_encode.S includes pico/config.h directly when assembling for rp2350).
target_compile_options(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz PRIVATE
  "-Dpico_board_cmake_set(x,y)="
  "-Dpico_board_cmake_set_default(x,y)="
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz)
