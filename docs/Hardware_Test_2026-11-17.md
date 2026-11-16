# Sensor and button hardware test — 2026-11-17

Tested on the connected RT-Spark with sensor diagnostics commit `98629b8`. The firmware had been uploaded through ST-LINK and OpenOCD reported `Verified OK`. COM4 was captured at 115200 8N1. Values below are actual recorded samples, not calibration or accuracy measurements. Tick timestamps are device uptime.

## Sensors

- ICM20608 `WHO_AM_I=0xAF`, consistent with ICM20608-G. At rest one sample was acceleration `(-0.086, 0.028, 0.962) g` and angular rate `(-1.450, 0.038, 0.389) degrees/s`. X gyro has a visible uncalibrated offset.
- During tilt, acceleration changed to `(-0.030, -0.791, 0.561) g`. During motion, recorded gyro values included `155.175 degrees/s` on X and `-141.015 degrees/s` on Z. These support a response to orientation and movement, not precision or range calibration.
- At the AP3216C address, reported ambient light changed from approximately `19 lux` to `0 lux` and returned toward baseline; proximity rose from near `0` to `1023` and returned, with `object=1` observed. The upper count may be saturation. Lux is approximate and proximity is raw counts, not distance. There is no dedicated unique chip-ID register in the reviewed AP3216C register table, so address and plausible register behavior do not uniquely establish identity.
- Across 515 IMU records in the two sensor captures, reported `dt_ms` was `500` throughout; this is a device tick interval, not an independent instrument measurement. No sensor `invalid` messages were recorded. Captures were interrupted deliberately after the test; there is a gap between them.

## Individual buttons

The first capture did **not** include button presses. Its all-zero flags were expected; it was incorrectly described as a failure before the operator corrected the record. A later dedicated capture recorded each flag asserting individually and returning to zero:

| Button | Press observed at uptime | Release observed at uptime | Other flags |
| --- | --- | --- | --- |
| LEFT | 1,328,500 ms | 1,331,500 ms | zero |
| UP | 1,332,500 ms | 1,334,500 ms | zero |
| DOWN | 1,335,500 ms | 1,338,500 ms | zero |
| RIGHT | 1,339,500 ms | 1,341,500 ms | zero |

These are sampled serial event times, not precise switch timing or debounce measurements. Visible LED behavior was not confirmed by the operator.

## Runtime and limitations

During the captured run, diagnostics reported stack high-water marks of 231 words (heartbeat), 229 words (buttons), and 546 words (sensor), with 10,320 bytes free and minimum-ever free heap. This short run does not establish stress-case stack margins or long-term reliability. Sensor calibration, fault injection, independent timing measurements, accurate light calibration, LCD/matrix/buzzer coexistence and prolonged operation remain untested. The bus reports errors but does not implement nine-clock recovery. Serial button reporting samples state every 500 ms, so short presses may be missed.

Raw logs were retained locally as ignored `.log` files and were not committed. No ML training, alarm or matrix implementation was performed as part of these tests.
