# TumoModule Runtime

TumoModule Runtime loads bounded declarative `.tmod` packages from:

```text
/ext/apps_data/tumomodule_runtime/modules
```

Version 0.1.0 trusts only two built-in adapters:

- `bme280_i2c`: bounded forced-mode temperature sample at I2C address `0x76`
  or `0x77`; the sensor returns to sleep after the measurement.
- `tumovgm_uart`: bounded TumoVGM protocol `HELLO` and `IMU_INFO` request at
  230400 baud.

The manifest cannot request OTG power or arbitrary pin operations. Unknown adapters,
API mismatches, oversized files, unsafe addresses, and unsafe baud values are blocked
before hardware ownership. Native `.fal` loading is intentionally out of scope until
there is a stronger trust and capability boundary.
