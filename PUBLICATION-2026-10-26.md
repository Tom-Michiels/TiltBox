# TiltBox Voice repository publication — 26 October 2026

This adds the existing Voice prototype to the original TiltBox repository without
replacing the ESP32-C3 project. The source of the publication is the local Voice
development project and its released manufacturing files.

## Versions and provenance

- Electronics A.3 / enclosure A.4 / manufacturing release A.5 (September 2026).
- Voice B firmware source and prebuilt images: 9 October 2026, ESP-IDF 5.4.2.
- PCB, schematics, mechanical STL/DXF, Gerbers, BOM and CPL were compared against
  their local source/release copies. No copper, placement or enclosure geometry
  was changed for this publication.
- Prebuilt images match their original `firmware/voice-a/bin/SHA256SUMS.txt`.
  The fresh build performed for publication is not substituted for those images.

## Checks performed for publication

| Check | Result |
| --- | --- |
| Fresh ESP32-S3 firmware build outside iCloud | Passed, ESP-IDF 5.4.2; no board flashed |
| LED mapping, current limiter, audio and CRC host tests | Passed: 64 pixels, 816 frames, three WAV headers, four CRC cases |
| IMU/ADC/sleep source and mocked-hardware tests | Passed |
| Embedded MicroPython host tests and host runner build | Passed |
| KiCad DRC with schematic parity | Zero violations, zero unconnected items, zero parity differences |
| KiCad ERC | Zero violations |
| Three printable STL meshes | Watertight positive-volume meshes |
| Three DXF files | Parsed and audited without errors |
| Gerber ZIP integrity | 13 entries, CRC check passed |
| Assembly files | 33 BOM rows; 136 CPL component rows |
| Cloud Worker packaging | Wrangler dry-run passed; nothing deployed |
| Cloud development dependencies | Updated within existing version range; npm audit reported zero known vulnerabilities |
| Publication hygiene | No real cloud secrets, local databases, recordings, node_modules or temporary builds staged |

The older low-power host test expected the gyro to remain disabled while awake.
It was updated to check Voice B's existing 104 Hz / ±2000 dps setting, while still
checking that sleep turns it off. Target firmware source was not changed.

The build emits an existing warning that ESP-IDF 5.4.2 does not recognise
`ESP_CONSOLE_USB_SERIAL_JTAG_RX_BUF_SIZE` in `sdkconfig.defaults`. The firmware
also explicitly configures the USB receive buffer in its application code.
The successful build is not a new physical USB validation.

## Scope

These are file, build and host checks, not new hardware measurements or a paid
voice-service integration test. See [hardware status](hardware/voice-a/README.md)
for the earlier physical observations and outstanding electrical/fit checks.

Cloud account identifiers were replaced with placeholders. Configuration examples
are examples only; each builder must provision their own account and secrets.
The original local project, its archives and any unrelated local TiltBox changes
were left untouched.
