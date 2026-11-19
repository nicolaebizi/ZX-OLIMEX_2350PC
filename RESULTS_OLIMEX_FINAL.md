# RESULTS — Olimex RP2350PC bring-up (FINAL, faza 1 → R7)

**Commit de lucru:** `ef8778b` — R7: audio PWM-DMA direct în amp (fără codec) — difuzoarele = ruta sigură
**Data:** 2026-11-19
**Hardware:** Olimex RP2350PC (RP2350B, 80-pin, DVI + USB + microSD + ES8311 codec + PAM8003 amp)

---

## Bring-up story

### Faza 1 — recrearea exactă
Arborele Olimex recreat EXACT (`6b48cce`) din fructul funcțional pico-zxspectrum, cu
mecanismul `ZX_AUDIO_NULL_ONLY` (audio dezactivat, video DVI curat) — inclusiv partea
ratată la recreare, completată ulterior în `ZxSpectrumAudioPwm.h` (`93b0151`).

### Fazele 2–9 — reconstrucția audio IRQ-driven (LESAȚE!)
Reconstrucția audio pe rutele clasice (ES8311/I2S, handler per-sample la 44100/s cu
IRQ-uri) a KILLED imaginea DVI de fiecare dată: **imagine neagră, indiferent de
driver**. Cauza: handler-ul audio per-sample ratează timing-ul DMA al PicoDVI.
Video-ul mergea doar cu audio-ul complet NULL — asta a fost perioada de recovery.

### Recovery — arborele curat
Faza 2 a fost reluată pe arborele curat din faza 1, cu rundele R1–R7, câte o singură
variabilă pe rundă, fiecare cu build dedicat (`_build_olimexR*`) + hosttest.

## Rundele R1–R7

| Rundă | Ce s-a testat | Rezultat |
|---|---|---|
| R1 (`8f1acde`) | audio ES8311 + I2S PIO1 (faza 2 pe arborele curat) | video OK, fără sunet |
| R2 (`969c299`) | dispatch audio forțat I2S + sanitise clamp — settings.bin vechi selecta Null | sunet: doar pop |
| R3 (`a730014`) | init ES8311 din esp-bsp + readback diag | registri OK (digital perfect), analog tot pop |
| R4 (`ec9c29d` + revert `9409428`) | MCLK pe GP23 GPOUT1 (vendor default) + readback | **video MORT** — GPOUT1/GP23 ucide randarea DVI pe RP2350B, fără erori |
| R5 (`ad60460`) | revert MCLK + readback clocking complet R01/R02/R11/R12/R13 | video revenit OK |
| R6 (`6b0a2d1`) | amp PAM8003 pornit (SHDN# GP35 LOW) | ruta difuzoarelor pregătită |
| R7 (`ef8778b`) | audio PWM-DMA direct în amp — GP26/27, DREQ_PWM_WRAP5, ring DMA, ZERO IRQ-uri audio | **SUNET CONFIRMAT de utilizator** ✅ |

## Starea finală (la `ef8778b`)

- ✅ **HDMI/DVI** — video stabil pe Olimex RP2350PC
- ✅ **USB** — funcțional
- ✅ **microSD** — funcțional
- ✅ **SUNET** — PWM-DMA direct în amplificator (difuzoare), confirmat de utilizator
- 🔒 **ES8311 codec = mister PARCAT** — partea digitală = perfectă (init + readback
  OK din esp-bsp), partea analogică = doar pop. Rămâne de investigat cu **osciloscop**
  (cine excită efectiv etajul analog). NU se reia până nu există scope.
- 🔮 **GP28 = line-in** — viitor
- 🔮 **PSRAM** — viitor

## Lecția-cheie (în RETRO_KNOWLEDGE/LESSONS.md)

Audio IRQ-driven (handler per-sample 44100/s) ratează timing-ul DMA al PicoDVI →
imagine neagră. Sunetul care merge = PWM-DMA direct în amplificator, cu ZERO
IRQ-uri audio. RP2350B are mapping de pini diferit: GPOUT1/GP23 ucide video DVI.

## Ne-verify / open

- ES8311 analog: cauză pop neidentificată (necesită osciloscop)
- Hardware testing long-run: doar confirmarea inițială de sunet/video de la utilizator
