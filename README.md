# TiltBox

![TiltBox](pictures/IMG_2051.jpeg)

A handheld retro game console with tilt controls, featuring 15 games on an 8x8 LED matrix. Designed at [Maakleerplek](https://maakleerplek.be).

## Bill of Materials

| Component | Quantity | Description | Link |
|-----------|----------|-------------|------|
| ESP32-C3 | 1 | Microcontroller (any ESP32-C3 dev board) | [Amazon](https://www.amazon.de/diymore-ESP32-C3-Development-Supports-Bluetooth/dp/B0DS23ML1L/ref=sr_1_1_sspa?th=1) |
| WS2812 8x8 LED Matrix | 1 | 64 RGB LEDs in 8x8 grid | [Amazon](https://www.amazon.de/CJMCU-8x8-Development-Compatible-Arduino-Raspberry/dp/B0DDPWTH83/ref=sxin_15_pa_sp_search_thematic_sspa) |
| ADXL345 | 1 | 3-axis accelerometer module (I2C) | [Amazon](https://www.amazon.de/APKLVSR-ADXL345-Digital-Accelerating-Transmission/dp/B0CRVL4S54/ref=sr_1_2_sspa) |
| USB Charger Board | 1 | LiPo charging module with protection | [Amazon](https://www.amazon.de/-/en/dp/B09C34TK4F?th=1) |
| LiPo Battery | 1 | 3.7V lithium polymer battery | [Amazon](https://www.amazon.de/-/en/Seamuing-Lithium-Protective-Insulation-Development/dp/B087LTZW61/ref=sr_1_12) |
| Threaded Inserts | 4 | M3x5x7 heat-set inserts for enclosure | [Amazon](https://www.amazon.de/Threaded-RX-M3x5-7-Lead-Free-Bushings-Insertion/dp/B08BCRZZS3/ref=sr_1_1_sspa) |
| M3 Screws | 4 | M3x6mm hex socket button head | [Amazon](https://www.amazon.de/dp/B07ZPTX8B2?ref=ppx_yo2ov_dt_b_fed_asin_title) |
| M3 Screws | 4 | M3x12mm hex socket button head | [Amazon](https://www.amazon.de/dp/B07ZPT7Y9R?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) |
| Walnut Wood | 1 | 3.2mm x 115x368mm sheet | [Gravierbedarf](https://www.gravierbedarf.de/epages/63201300.sf/en_GB/?ObjectPath=/Shops/63201300/Products/NUSS) |
| Black PLA | 1 | 3D printer filament for enclosure | |
| Jumper wires | 7 | For connections | |

## Wiring

### WS2812 LED Matrix

| WS2812 Pin | ESP32-C3 Pin |
|------------|--------------|
| VCC | 5V |
| GND | GND |
| DIN | GPIO 10 |

### ADXL345 Accelerometer (I2C)

| ADXL345 Pin | ESP32-C3 Pin |
|-------------|--------------|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 8 |
| SCL | GPIO 9 |

## Build Instructions

### 1. 3D Printing

Print the enclosure parts in black PLA:

- [tilt_box.stl](hardware/tilt_box.stl) - Main enclosure body
- [tilt box grid.stl](hardware/tilt%20box%20grid.stl) - Grid overlay

![CAD Model](pictures/cad.png)

[Open in Onshape](https://cad.onshape.com/documents/1ea29db64e579952e6381490/w/44e133dfefb2de18973d13df/e/b5270f9dee790fa11f86224a?renderMode=0&uiState=69ad3dda5e52657494bd1281)

### 2. Laser Cutting

Cut the following from the [wood and plexi.dxf](hardware/wood%20and%20plexi.dxf) file:

- **Walnut wood** (3.2mm thickness): Front frame
- **Clear acrylic/plexi**: Screen cover

### 3. Soldering

Solder the components according to the wiring diagram:

![Wiring Diagram](pictures/diagram.png)

![Assembly](pictures/IMG_2046.jpeg)

### 4. Screen Assembly

The LED matrix needs a diffuser layer to blend the individual LEDs into a smooth display:

- **Diffuser**: Use a white plastic bag or similar translucent material between the LED matrix and the plexi cover
- **Optional darkening**: Adding a dark PVC film (e.g. available at Lukas Creative) provides additional darkness to the screen, improving contrast

### 5. Final Assembly

1. Insert the M3x5x7 threaded inserts into the 3D printed enclosure using a soldering iron
2. Mount all electronics inside the enclosure
3. Stack the layers: LED matrix -> diffuser -> (optional dark film) -> plexi -> walnut frame
4. Secure with M3x6mm screws (front) and M3x12mm screws (back)

## Games

Switch between games by flipping the board upside down.

### 1. Maze
Navigate a ball through a randomly generated maze. Tilt to move the yellow ball to the red goal. A rainbow animation plays when you win.

### 2. Ball Trail
A simple ball that leaves a fading trail as it moves. Tilt to control direction.

### 3. Glow Ball
A glowing ball with a smooth gradient effect that follows your tilt movements.

### 4. Snake
Classic snake game. Tilt to change direction and eat the red food to grow. Don't hit yourself!

### 5. Breakout
Break the colored bricks at the top with a bouncing ball. Tilt left/right to move the paddle.

### 6. Dodge
Avoid falling obstacles. Tilt left/right to move your player at the bottom. Score increases as obstacles pass.

### 7. Scroll Text
Displays scrolling text: "github.com/Tom-Michiels/TiltBox"

### 8. Animation
Cycles through animated patterns:
- Spiral rainbow
- Wave
- Rainbow gradient

### 9. Tetris
Classic falling block game. Tilt left/right to move pieces, tilt forward to rotate, tilt backward for fast drop.

### 10. Pong
1-player Pong against an AI opponent. Tilt left/right to move your paddle. Score increases with each volley. 3 misses and the game resets.

### 11. Balance
Keep a ball centered against random wind forces. The target zone shrinks over time, making it harder to stay alive. Pure tilt precision challenge.

### 12. Catch
Catch falling green items for points while avoiding red ones. Tilt left/right to move your basket. 3 lives — lose one for each red item caught.

### 13. Target Practice
Tilt a cursor to hit randomly appearing targets as fast as possible. 30-second timer. Tests raw tilt accuracy and speed.

### 14. Sokoban
Push-block puzzle game. Tilt to move your character and push blocks onto target positions. 8 levels of increasing difficulty.

### 15. Marble Race
Steer a marble along a scrolling track. Tilt left/right to stay on the path as speed increases. 3 lives — go off-track and you lose one.

## Building and Flashing

Requires ESP-IDF v4.3+.

```bash
# Source ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Build
idf.py build

# Flash
idf.py flash

# Monitor serial output
idf.py monitor
```

## Controls

- **Tilt**: Move/control in all games
- **Flip board**: Switch to next game
- **Keep level during startup**: Calibrates the accelerometer

## Display Format

The WS2812 LEDs use GRB color format. Pixels are addressed row by row, 0-63.
