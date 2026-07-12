# TumoVGM Bridge

Native Tumoflip client for custom `t-vgm-fw` firmware on the official Flipper
Zero Video Game Module.

The app distinguishes custom compatible firmware, incompatible protocol major,
stock VGM firmware, and missing hardware. It owns USART only while open and
restores the Expansion service on every exit path.

The vendored codec in `protocol/` is synchronized with TumoVGM protocol v1.2.
Compatible firmware exposes live ICM-42688-P acceleration, angular velocity,
temperature, orientation, and debounced gesture events. Streaming is
credit-bounded and session scoped, so Stop/Back returns the module to its idle
dashboard and releases the sensor bus.

The physical binding is 230400 baud, 8N1, on the standard VGM Expansion UART.
