# ZX-OLIMEX_2350PC — ZX Spectrum & clone românești pe Olimex RP2350PC

Portul proiectului **pico-zxspectrum** (fruit-bat) pe placa **OLIMEX RP2350PC**.

## CE ESTE

Un firmware ZX Spectrum care rulează pe placa OLIMEX RP2350PC (RP2350B, 16MB flash, 8MB PSRAM, DVI/HDMI, hub USB 4×port, microSD) și emulează **11 mașini**:

| # | Mașină |
| - | ------ |
| 1 | ZX Spectrum 48K |
| 2 | ZX Spectrum 128K |
| 3 | TURBO SPECTRUM |
| 4 | HC2000 |
| 5 | CoBra 1 |
| 6 | ZX+3 (Amstrad +3, ROM-uri incluse) |
| 7 | TIM S Plus |
| 8 | CUB-Z |
| 9 | HC88 |
| E | +3s (Amstrad +3 by Cristian Secară) |
| S | SuperSpec (Roelof Koning) |

Toate clonele românești funcționale: HC2000, CoBra 1, TIM S Plus, CUB-Z, HC88, +3s.

## HARDWARE FOLOSIT (confirmat pe placă!)

* **Video**: HDMI prin HSTX (PicoDVI, 640×480×60Hz). Ordinea pinilor DVI-Sock: GP12–GP19.
* **Tastatură**: USB prin hub-ul **FE1.1s** al plăcii (portul USB-C de sus).
* **microSD**: SPI1 — CS=**GP9**, SCK=**GP10**, MOSI=**GP11**, MISO=**GP24**.
* **Audio — difuzoare**: PWM pe **GP26/GP27** prin amplificatorul **PAM8003**; volum pe **GP34**; SHDN pe **GP35**.
* **Audio — HDMI monitor**: TMDS data-island (audio prin HDMI).
* **CF card (SuperSpec)**: imaginile `.CF` din `zxspectrum/cf/*.CF`; din meniul **D → CF-SUPERSPECS (E) → S** se încarcă imaginea CF! (experimentele Roelof Koning).

## INSTALARE

1. Țineți apăsat **BOOT** și conectați USB-C (programarea) — apare drive-ul **RP2350**.
2. Copiați (drag-drop) fișierul **UF2** din folderul [`firmware/`](firmware/) (`ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz.uf2`).
3. Placa se resetează singură. Alimentați din portul **POWER**, conectați **monitor HDMI** și **tastatură USB**.

## MENIUL

Apăsați **F1** (sau tasta meniu) pentru meniul on-screen:

* **0–9 / E = mașinile** (scurtături, tabelul de mai sus).
* **D = meniul discuri/ROM-uri** (mount imagini EDSK/TAP/CF de pe microSD).
* **R = ROM custom** de încărcat de pe card.
* **Audio = 3 variante** (selectabile din meniu):
  1. **PWM** — difuzoarele plăcii (GP26/27);
  2. **I2S** — experimental (ES8311 codec);
  3. **HDMI** — audio prin monitorul HDMI.

## COMPILARE

Necesar: **pico-sdk 2.3.1**, **PicoDVI** (inclus în arbore), PicoDVI configurat `DVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg`, toolchain arm-none-eabi.

```bash
# din rădăcina proiectului (git submodules: PicoDVI, pico-zxspectrum, pico-hid-host, ...)
cd <radacina-proiect>
mkdir -p _build_olimexR17 && cd _build_olimexR17
cmake -DPICO_BOARD=olimex_rp2350pc \
      -DPICO_SDK_PATH=$HOME/pico-sdk \
      -DZX_PLUS3_ROMS=ON \
      -DZX_CUBZ_DVI=ON \
      -DDVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg ..
make -j8
```

Artifacts: `bin/vgahdmi/ZX-OLIMEX_RP2350PC_HDMI_640x480x60Hz.uf2`.

**Notă**: `ZX_PLUS3_ROMS=ON` este **obligatoriu** pentru mașinile +3 / +3s (ROM-urile Amstrad +3, inclusiv varianta localizată +3s de Cristian Secară).

## STARE CUNOSCUTĂ / PARCAT

* **PSRAM 8MB** (APS6404): inițializarea QMI omoară boot-ul — **studiul este dedicat pentru viitor**; firmware-ul curent rulează fără PSRAM (revert la starea funcțională R12).
* **ES8311 codec**: partea digitală (I2C/I2S) confirmată ✓; partea **analogică = mister** — de verificat cu multimetrul.
* **PS/2 și NES**: **dezactivate** — placa nu are conectorii corespunzători.

## STRUCTURA PROIECTULUI

```
├── pico-zxspectrum/      # nucleul emulării (fruit-bat) — mașinile + meniul
├── pico-dvi-menu/        # meniul on-screen DVI
├── PicoDVI/              # biblioteca DVI/HDMI (Wren6991)
├── pico-hid-host/        # tastatură USB
├── boards/               # definiția plăcii olimex_rp2350pc
├── zxspectrum/           # ROM-uri, disk-uri, imagini CF (SD card layout)
├── firmware/             # UF2 funcțional gata de flash
       └── _build_olimexR17*/    # directoarele de build (nu se publică integral)
```

## CREDITE

* **fruit-bat** — proiectul de bază pico-zxspectrum.
* **Paul Robson** — rp2350-modules, referința pentru PSRAM/codec.
* **Autorii clonelor românești** (creditați în [CLONE-ROMANESTI](https://github.com/nicolaebizi/CLONE-ROMANESTI), README + release-urile v0.5–v0.11):
  * **Sorin Cismas** (WWW.COBRASOV.ORG) — TURBO SPECTRUM (ROM turbo, porturi F5/CE/SM);
  * **CoBra 1 (Brașov)** — scheme, ROM-uri și documentație de pe cobrasov.com (ElectroNNix);
  * **TIM S Plus** — „TIM-S Plus – șapte calculatoare într-unul singur" (1990, Pănescu, Dumitru) + dump-ul EPROM-ului original;
  * **CUB-Z și HC88** — „Manual tehnic CUB-Z" (1989, ICE Felix); reconstrucția Fuse „BMC2026" pentru TIM-S Plus și CUB-Z;
  * **Y03GHM** — discheta CUB-Z;
  * **Bogdan Marcu (@bmc2026)** — dischetele TIM-S PLUS / HC-88 (bmc.net2000@gmail.com);
  * **HC2000** — creditat în CLONE-ROMANESTI ca produs **ICE Felix**; autorul individual nu este nominalizat acolo — [de completat de utilizator, dacă e cunoscut].
* **ROELOF KONING** — ROM-ul ZX48 SuperSpec (supr190712, bank 224 = superDOS) + documentația structurii FS de pe CF.
* **Cristian Secară** — +3s (Amstrad +3 localizat).
* **Utilizatorul** — validarea pe hardware (VGA M1/M2/M2-AUTO + TFT M1), DESIGN-ul meniului final (D → CF-SUPERSPECS → S: CF IMAGINE) și decizia de retragere (CUB-Z pe VGA pe M2-AUTO dacă HDMI nu ține).
