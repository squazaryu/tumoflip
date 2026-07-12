# TumoScope

TumoScope is a bounded three-channel digital logic analyzer for Flipper Zero.
It samples GPIO `PC0`, `PC1`, and `PC3` through TIM16-triggered DMA, keeps a
25-percent pre-trigger ring, renders a local waveform, and exports standard VCD
plus a compact text report under `/ext/apps_data/tumoscope/captures`.

The default `Demo Edge` mode is synthetic and never touches GPIO, so the full
waveform, frequency display, decoder, and export flow can be checked while a
Module One is attached. `Demo UART` emits known rate-safe frames and `Demo I2C`
emits known address/data frames. Live modes remain explicitly labeled and use
the physical pins documented below.

## Profiles

- Live Raw: PC0, PC1, PC3.
- Demo Edge: 5 kHz on PC0, 3.125 kHz on PC1, and 2.5 kHz on PC3.
- Demo UART: synthetic UART 8N1 on PC0 with a rate-safe baud profile.
- Demo I2C: synthetic SDA on PC0 and SCL on PC1.
- UART 8N1: RX on PC0 at 9,600 to 115,200 baud.
- I2C: SDA on PC0, SCL on PC1.
- SPI mode 0: MOSI on PC0, MISO on PC1, clock on PC3.
- 1-Wire: data on PC0.

Capture rates are 100 kHz, 250 kHz, 500 kHz, and 1 MHz. Capture depth is
bounded to 1,024, 2,048, 4,096, or 8,192 samples. Edge- and level-triggered
captures keep 25 percent pre-trigger data; automatic captures begin immediately.
The waveform header reports measured frequency per channel or clearly shows
`No transitions / static` when a live capture contains no edges.

## Electrical boundary

Inputs are digital-only and must remain within Flipper Zero GPIO voltage limits.
TumoScope is not an analog oscilloscope and does not provide input protection or
5 V tolerance. The app restores all three pins to analog mode on completion,
cancel, error, and exit.
