# TiltBox Voice hardware

Publication snapshot: **26 October 2026**. Electronics **A.3**, enclosure **A.4**,
manufacturing package **A.5**. Firmware **Voice B** runs on this same board.

## Files to use

| Purpose | Files |
| --- | --- |
| 3D print | [body](mechanical/TiltBox-Voice-A-body.stl), [grid](mechanical/TiltBox-Voice-A-grid.stl), [button](mechanical/TiltBox-Voice-A-button.stl) |
| Laser-cut 3 mm walnut | [walnut DXF](mechanical/TiltBox-Voice-A-walnut-3mm.dxf) |
| Laser-cut 3 mm dark grey smoked PMMA | [PMMA DXF](mechanical/TiltBox-Voice-A-smoked-plexi-3mm.dxf) |
| Combined laser layout | [wood and PMMA DXF](mechanical/TiltBox-Voice-A-wood-and-plexi.dxf) |
| Hand-cut diffuser / filter | [SVG template](mechanical/diffuser-manual-cut.svg) |
| Editable electronics | [KiCad project](pcb/TiltBox-Voice-A.kicad_pro), [PCB](pcb/TiltBox-Voice-A.kicad_pcb), [schematic](pcb/TiltBox-Voice-A.kicad_sch) |
| JLCPCB fabrication | [Gerber ZIP](jlcpcb/TiltBox-Voice-A-Gerbers.zip) |
| JLCPCB assembly | [BOM](jlcpcb/BOM.csv), [CPL](jlcpcb/CPL.csv) |
| Assembly / ordering guides (Dutch) | [Assembly](jlcpcb/Assemblage.pdf), [ordering and checks](jlcpcb/Bestellen-en-controle.pdf) |
| Schematic PDF | [Schema.pdf](reports/Schema.pdf) |
| Firmware | [Source and instructions](../../firmware/voice-a/README.md) |

The combined DXF is an alternative layout, not an additional set of parts.
Files in `mechanical/reference/` are original inputs for the generation scripts,
not the Voice parts to print. Units are millimetres. Measure actual sheet
thickness and calibrate laser kerf before cutting the final parts.

## Design

- 78 × 78 mm, 1.6 mm, four-layer PCB, 64 WS2812B-V5 RGB LEDs.
- ESP32-S3-WROOM-1U-N16R8; attach the external antenna before radio tests.
- LSM6DS3TR-C IMU and ICS-43434 bottom-port I2S microphone.
- USB-C programming and power, BQ25185 power-path charger, edge sleep/wake button.
- R20/R21 are 1 MΩ each. Firmware configures the IMU interrupt idle-high.
- No separate per-LED C1–C64 capacitors; other supply decoupling remains.

## Enclosure and battery

Walnut and PMMA are **3 mm** thick. Print the upper grid in opaque black material
to absorb stray light. The paper/dark translucent filter stack is **0.2 mm total**,
90 × 90 mm including the rim, with the eight mounting holes from the SVG. It has
no cut-outs over the LED cells. **Never laser-cut PVC.**

Use eight ISO 7380-1 **M3×14** screws above and eight **M3×6** screws at the sides.
The vertical insert pilot holes in the printed body are **4 mm**; match the actual
heat-set inserts to the material and check a test coupon before assembly.

Battery: protected **1S LiPo, 3.7 V nominal / 4.2 V charging**, with **JST-PH 2-pin,
2.0 mm pitch (PHR-2)** cable connector. PCB J2 **pin 1 is VBAT+**, **pin 2 is GND−**.
Identical-looking battery plugs can have reversed polarity: verify before connecting.
The charger is configured for approximately 300 mA; the cell must permit this
charge current, and the original assembly specification calls for ≥2 A discharge.
TH1 must be thermally coupled to the cell, with suitable electrical insulation.

The enclosure CAD retains the original nominal **45 × 26 × 5 mm** pack envelope.
A MakerFocus 1000 mAh pack was used in subsequent functional battery tests; that
does **not** establish its fit in the closed enclosure or all of its electrical
ratings. Do not assume arbitrary 1000 mAh packs fit these STL files.

## Validation and limitations

The included manufacturing files are the existing prototype release, not a new
PCB layout. Archived KiCad release checks report zero DRC/ERC violations and no
unconnected items; their dates remain visible in the JSON reports. Fresh DRC
(including schematic parity) and ERC checks on 26 October also returned zero
violations; see `reports/*publication-2026-10-26.json`. These checks
do not establish electrical safety or eliminate the need for fabrication review.

On the first assembled board (2 October 2026), the 64 LEDs, IMU/self-test,
microphone data, PSRAM test, passive Wi-Fi scan, USB-C in both orientations,
tilt directions and button sleep/wake were checked. On 3 October, the owner
confirmed operation on battery, sleep/wake and a cool battery. Seven USB ADC
readings over about 78 seconds ranged from 3.958 to 3.966 V; these are firmware
readings, not calibrated multimeter measurements. USB reconnection after sleep
needed an off/on cycle during that session; the cause was not established.

The 9 October firmware notes report target interpreter/IMU/USB tests and local
mock-cloud tests. Earlier hardware results must not be treated as a full
regression test of every behaviour in this newer firmware.

Still outstanding: measured charging and sleep currents, charger termination and
thermal protection, supply behaviour under peak load, calibrated ADC accuracy,
closed-enclosure fit/RF/thermal tests and full paid voice-service integration.
Before ordering, reconfirm JLCPCB stack-up, impedance assumptions, component
availability and assembly orientation. This is a prototype, not certified hardware.

## Mechanical source

`tools/build_enclosure.py`, `build_wood.py`, `build_accessories.py`, `design.py`
and `mechanical_spec.py` preserve the source used for the generated geometry.
They require Python with numpy, scipy, trimesh, manifold3d, shapely, triangle,
matplotlib and ezdxf. Run enclosure, then wood, then accessories in a disposable
copy: these scripts overwrite generated mechanical
outputs. The released STL/DXF files above are the manufacturing snapshot.

The routed KiCad project is authoritative for the PCB; these mechanical helper
scripts are not a PCB routing/release pipeline.
