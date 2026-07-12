# TumoScope

TumoScope is a bounded three-channel digital logic analyzer for Flipper Zero.
It samples GPIO `PC0`, `PC1`, and `PC3` through TIM16-triggered DMA, keeps a
25-percent pre-trigger ring, renders a local waveform, and exports standard VCD
plus a compact text report under `/ext/apps_data/tumoscope/captures`.

## Profiles

- Raw: PC0, PC1, PC3.
- UART 8N1: RX on PC0 at 9,600 to 115,200 baud.
- I2C: SDA on PC0, SCL on PC1.
- SPI mode 0: MOSI on PC0, MISO on PC1, clock on PC3.
- 1-Wire: data on PC0.

Capture rates are 100 kHz, 250 kHz, 500 kHz, and 1 MHz. Capture depth is
bounded to 1,024, 2,048, 4,096, or 8,192 samples. Edge- and level-triggered
captures keep 25 percent pre-trigger data; automatic captures begin immediately.

## Electrical boundary

Inputs are digital-only and must remain within Flipper Zero GPIO voltage limits.
TumoScope is not an analog oscilloscope and does not provide input protection or
5 V tolerance. The app restores all three pins to analog mode on completion,
cancel, error, and exit.
