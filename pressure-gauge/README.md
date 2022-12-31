Swan Pressure Gauge w/ OLED
===========================

Bill of Materials
-----------------

- [Adafruit FeatherWing OLED (128x32)](https://www.adafruit.com/product/2900)
- [Adafruit Stacking Feather Headers](https://www.adafruit.com/product/2830)
- [Analog Pressure Gauge](https://www.amazon.com/Pressure-Transducer-Sender-Connector-Stainless/dp/B07TLJNSV8)
- [Blues Wireless Notecarrier-F](https://shop.blues.io/products/notecarrier-f)
  - [Molex 213353 Antenna](https://www.molex.com/molex/products/part-detail/antennas/2133530100)
- [Blues Wireless Swan](https://shop.blues.io/products/swan)
  - [STLINK-V3MINI](https://shop.blues.io/products/stlink-v3mini)
- [Male-to-Male Jumper Wires](https://www.amazon.com/Solderless-Multicolored-Electronic-Breadboard-Protoboard/dp/B09FP517VM)
- [TRRS Terminal Block Breakout](https://www.amazon.com/Glarks-Balanced-Terminal-Converter-Connectors/dp/B07KF8SRC3)
- [Micro USB Cable (2x)](https://www.amazon.com/Rankie-Micro-Charging-Braided-3-Pack/dp/B01JPDTZXK)
- [5000 mAh LiPo Battery](https://shop.blues.io/collections/accessories/products/5-000-mah-lipo-battery)

Firmware
--------

- [GitHub: Pressure Gauge](https://github.com/zfields/PressureGauge)
  - [Arduino Library: Adafruit GFX](https://www.arduino.cc/reference/en/libraries/adafruit-gfx-library/)
  - [Arduino Library: Adafruit SSD1306](https://www.arduino.cc/reference/en/libraries/adafruit-ssd1306/)

Build Instructions
------------------

1. Flash firmware on Swan
2. Install stacking headers on the Swan
3. Attach Swan to Notecarrier-F
4. Attach OLED to Swan
5. Screw pressure sensor wires into TRRS terminal block
6. Screw jumper wires into corresponding TRRS terminal block
7. Plug jumper wires into Notecarrier-F

**Wiring Table:**

| Notecarrier-F |  TRRS  | Pressure Sensor |
|:-------------:|:------:|:---------------:|
|      A5       |  Tip   |   Green Wire    |
|      --       | Ring 1 |       --        |
|     F_3V3     | Ring 2 |    Red Wire     |
|      GND      | Sleeve |   Black Wire    |
