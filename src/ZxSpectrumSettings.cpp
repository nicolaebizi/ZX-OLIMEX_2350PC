// SPDX-License-Identifier: MIT
// Copyright (c) 2026 fruit-bat
#include "ZxSpectrumSettings.h"
#include "ZxSpectrumAudioDriver.h"
#include "ZxSpectrumVideoDriver.h"

ZxSpectrumSettings::ZxSpectrumSettings()
{
}

ZxSpectrumSettings::~ZxSpectrumSettings()
{
}

inline static bool is_audio_driver_valid(uint32_t i) {
  return i < (uint32_t)ZX_SPECTRUM_AUDIO_DRIVER_COUNT;
}

void ZxSpectrumSettings::sanitise(ZxSpectrumSettingValues *values) {

    // Volume
    if (values->volume > 0x100) values->volume = 0x100;

    // Joystick
    switch(values->joystickMode) {
      case ZxSpectrumJoystickModeKempston:
      case ZxSpectrumJoystickModeSinclairLR:
      case ZxSpectrumJoystickModeSinclairRL:
        break;
      default: 
        values->joystickMode = ZxSpectrumJoystickModeKempston;
        break;
    }

    // Mouse
    switch(values->mouseMode) {
      case ZxSpectrumMouseModeKempstonMouse:
      case ZxSpectrumMouseModeJoystick:
        break;
      default: 
        values->mouseMode = ZxSpectrumMouseModeKempstonMouse;
        break;
    } 

    // Mouse Joystick
    switch(values->mouseJoystickMode) {
      case ZxSpectrumJoystickModeKempston:
      case ZxSpectrumJoystickModeSinclairLR:
      case ZxSpectrumJoystickModeSinclairRL:
        break;
      default: 
        values->mouseJoystickMode = ZxSpectrumJoystickModeKempston;
        break;
    }

    // Video output
    if (values->videoDriverDefault >= ZX_SPECTRUM_VIDEO_DRIVER_COUNT || !isZxSpectrumVideoDriverInstalled((zx_spectrum_video_driver_enum_t)values->videoDriverDefault)) {
      values->videoDriverDefault = (uint8_t)videoDriverIndex();
    }

    // Audio output
    // NOTE: the array is indexed by VIDEO driver, so the loop bound is
    // the video driver count (the old AUDIO-count bound read past the
    // 3-entry video driver table on R7's 6 audio entries).
    for(uint8_t i = 0; i < ZX_SPECTRUM_VIDEO_DRIVER_COUNT; ++i) {
#ifdef ZX_AUDIO_PWM_DMA_DEFAULT
      // R7 (Olimex RP2350-PC): the PWM-DMA direct-amp route is the boot
      // default.  R2 fix pattern, retargeted: on this target the null
      // driver (0) is not a wired output but IS in range, so a stale SD
      // settings.bin storing 0 would re-select the null driver at every
      // boot; out-of-range entries land here too.  Clamp both to the
      // PWM-DMA index (the sure speakers route).  Stale R2-R6 bins
      // storing the OLD i2s index (3) also land on the NEW pwm_dma
      // index (3) through the enum renumbering.
      if (values->audioDriverDefault[i] == zx_spectrum_audio_driver_null_index ||
          values->audioDriverDefault[i] >= ZX_SPECTRUM_AUDIO_DRIVER_COUNT) {
        values->audioDriverDefault[i] = zx_spectrum_audio_driver_pwm_dma_index;
      }
#else
      if (values->audioDriverDefault[i] >= ZX_SPECTRUM_AUDIO_DRIVER_COUNT) {
        zx_spectrum_video_driver_t* videoDriver = getZxSpectrumVideoDriver((zx_spectrum_video_driver_enum_t)i);
        values->audioDriverDefault[i] = videoDriver->audio_default != NULL
          ? (uint8_t)videoDriver->audio_default()
          : 0;
      }
#endif
    }
}

bool ZxSpectrumSettings::onSave(ZxSpectrumSettingValues *values)
{
    return false;
}

bool ZxSpectrumSettings::onLoad(ZxSpectrumSettingValues *values)
{
    return false;
}

bool ZxSpectrumSettings::save(ZxSpectrumSettingValues *values)
{
    sanitise(values);
    return onSave(values);
}

bool ZxSpectrumSettings::load(ZxSpectrumSettingValues *values)
{
    defaults(values);
    bool r = onLoad(values);
    if (!r ) r = onLoad(values);
    sanitise(values);
    return r;
}

void ZxSpectrumSettings::defaults(ZxSpectrumSettingValues *values) {
  values->volume = 0x100;
  values->joystickMode = ZxSpectrumJoystickModeKempston;
  values->mouseMode = ZxSpectrumMouseModeKempstonMouse;
  values->videoDriverDefault = (uint8_t)videoDriverIndex();
  for(uint32_t i = 0; i < (uint32_t)ZX_SPECTRUM_VIDEO_DRIVER_COUNT; ++i) {
    zx_spectrum_video_driver_t* videoDriver = getZxSpectrumVideoDriver((zx_spectrum_video_driver_enum_t)i);
    values->audioDriverDefault[i] = videoDriver->audio_default != NULL
       ? (uint8_t)videoDriver->audio_default()
       : 0;
  }
}
