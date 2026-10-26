# CLAUDE.md - TiltBox

This file provides guidance to Claude Code (claude.ai/code) when working with the TiltBox project.

## Build Commands

This is an ESP-IDF project. Use the `idf.py` tool for all build operations:

```bash
idf.py build          # Build the project
idf.py flash          # Flash to connected ESP32
idf.py monitor        # Open serial monitor (Ctrl+] to exit)
idf.py flash monitor  # Flash and immediately open monitor
idf.py menuconfig     # Configure project options (stored in sdkconfig)
idf.py fullclean      # Clean all build artifacts
```

Ensure the ESP-IDF environment is sourced before running commands (`source ~/esp/esp-idf/export.sh`).

## Architecture

The root project described below is the original ESP32-C3 TiltBox. The newer
ESP32-S3 Voice PCB is separate: `hardware/voice-a/`, `firmware/voice-a/` and
`cloud/`. Consult their READMEs. Build Voice with ESP-IDF 5.4.2 and
`bash firmware/build-local.sh voice-a`; never flash hardware as part of a
repository build or publication task. Keep credentials and local cloud data out
of Git. Preserve the original files for users of the old hardware.

TiltBox is an ESP32-C3 project driving an 8x8 WS2812 LED matrix with an ADXL345 accelerometer for tilt control. The entire application is in `main/main.c`.

**Hardware interfaces:**
- WS2812 LEDs on GPIO 10 via RMT peripheral
- ADXL345 accelerometer on I2C (SDA: GPIO 8, SCL: GPIO 9)

**Game system:** Uses a dispatch table pattern (`game_interface_t`) where each game implements `init()`, `update(dx, dy, z)`, and `draw()` functions. Games are switched by flipping the board (z-axis detection). Current games: Maze, Ball Trail, Glow Ball, Snake, Breakout, Dodge, Scroll Text, Animation, Tetris.

**Display:** 8x8 matrix using GRB color format. Pixels are addressed via `set_pixel_at(row, col, g, r, b)`. The `ws2812_send()` function transmits the frame buffer.
