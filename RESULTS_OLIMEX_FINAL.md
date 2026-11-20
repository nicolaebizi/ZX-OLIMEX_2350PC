# RESULTS — Olimex RP2350PC bring-up (FINAL, faza 1 → R11)

**Commituri-cheie:**
- `ef8778b` — R7: sunetul PWM-DMA (primul AUDIO funcțional)
- `184edd2` — R9: a 3-a variantă audio (HDMI monitor TMDS data-island)
- `048e3d6` — R11: **STAREA FINALĂ**
**Data:** 2026-11-20
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

## Starea finală (la `048e3d6`, R11 — DECIZIA UTILIZATORULUI, 2026-11-20)

**FUNCȚIONAL:**
- ✅ **HDMI video** — stabil pe 11 mașini testate
- ✅ **USB** — hub + tastatură funcționale
- ✅ **microSD** — funcțional
- ✅ **CF SuperSpec** — funcțional
- ✅ **AUDIO — PWM-DMA** — difuzoarele ✓ (confirmat de utilizator)
- ✅ **AUDIO — HDMI monitor** ✓ (TMDS data-island, R9)

**PARCAT: ES8311 codec (decizia utilizatorului):**
- misterul: digital = perfect (P01/A1/B1 + registre exacte ca la starea sănătoasă R5),
  dar analog = doar pop
- starea hardware s-a schimbat **între sesiuni** (R5 vs R10 cu cod identic → PFF/A0)
- depanare viitoare cu **multimetru/osciloscop** în sesiune dedicată
- suspect: starea fizică / alimentare-LDO / cabluri-DDC — NU un bug de cod

## Lecția-cheie (în RETRO_KNOWLEDGE/LESSONS.md)

Audio IRQ-driven (handler per-sample 44100/s) ratează timing-ul DMA al PicoDVI →
imagine neagră. Sunetul care merge = PWM-DMA direct în amplificator, cu ZERO
IRQ-uri audio. RP2350B are mapping de pini diferit: GPOUT1/GP23 ucide video DVI.

## Ne-verify / open

- ES8311 analog: cauză pop neidentificată (necesită osciloscop)
- Hardware testing long-run: doar confirmarea inițială de sunet/video de la utilizator

## Rundele R8–R11 (continuare, ES8311 HP out)

| Rundă | Ce s-a testat | Rezultat |
|---|---|---|
| R8 (`f5197aa`) | ring I2S umplut cu silență la pauză/meniu | fix nota infinită |
| R9 (`184edd2`) | a 3-a variantă audio: HDMI monitor (TMDS data-island) | P01/A1 + registre = exact (starea sănătoasă de referință) |
| R10 (`0a50370`) | init ES8311 doar DAC + restructure diag/menu | **hardware: PFF + A0 + E83C = tot FF + „Audio out [125]" corupt** |
| R11 (`048e3d6`) | `git revert 0a50370` + ONLY scoase cele 4 scrieri ADC (R0E/R14/R17/R1C); probe/I2C/SDPIN/menu = cod R9 neatins | build OK; test hardware: **digital perfect (P01/A1/B1 + registre exacte ca la R5), analog = doar pop → ES8311 PARCAT** (decizia utilizatorului) |

### Analiza bug-urilor R10 (din `git show 0a50370`, dif față de R9)

- **(a) Probe/I2C „mort":** dif R10 NU modifică nimic din calea probe: `i2c_init`,
  `gpio_set_function`/pull-up GP32/33, bucla de polaritate GP22 și `es_read(R00)`
  sunt octet-cu-octet identice cu R9; scrierile eliminate sunt TOATE după probe, iar
  readback-ul extins rulează după init. Deci PFF/A0/FF NU poate proveni din dif —
  e exact starea implicită de reset a `zxEsDiagState` (pwr=FF, ack=0, rb=FF×12),
  adică o sesiune în care probe-ul nu a primit ACK niciodată (stare de
  alimentare/bus la momentul testului), nu o schimbare sursă. Verificat și binar:
  elf-ul flash-uit R10diag conține string-ul E83C cu 12 sloturi → nu era binar vechi.
- **(b) „Audio out [125]":** R10 nu renumerotează niciun index de driver și nu
  atinge sanitise-ul (`ZxSpectrumAudioDriver.cpp`, `ZxSpectrumAudioDriverEnum.h`,
  `ZxSpectrumSettings.cpp` = neatinse din R7/R8; clamp-ul din `sanitise()` e
  intact). Rândul citește `getAudioDriver()->name` — un pointer la nume corupt +
  linia diag în starea implicită de reset = semnătura unui firmware
  parțial-initializat/repornit la momentul pozei, nu un bug de index/sanitise.
- **Lecție de proces:** cea mai mică dif posibilă pe baza R9 sănătoase (doar
  eliminarea celor 4 scrieri ADC) izolează variabila testată; restructurările
  mari de comentarii/diag în același commit fac cauza imposibil de atribuit.
