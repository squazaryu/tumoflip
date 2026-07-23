# Tumo UART Field Console

Tumo UART Field Console is the Cockpit-owned UART discovery and terminal
workflow for 3.3 V TTL devices.

It is deliberately installed under
`/ext/apps_data/module_one_cockpit/modules/` and launched from Module One
Cockpit. It is not exposed as a second loose UART application.

## Safety

- Connect ground first.
- Use 3.3 V TTL only.
- Do not connect RS-232 voltage levels directly.
- Detection releases TX and only observes RX.
- Transmission is possible only after the user opens the console.

Session logs are stored in `/ext/apps_data/tumo_uart_console/`.

## Upstream

The auto-baud, verifier, terminal, watch, script, logging, and loopback engines
are adapted from Hermes 1.2. See `UPSTREAM.md`, `UPSTREAM_README.md`, and
`LICENSE`.
