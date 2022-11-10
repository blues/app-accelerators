# VL53L4CX
Arduino library to support the VL53L4CX Time-of-Flight high accuracy ranging sensor with multi target detection.

## API

This sensor uses I2C to communicate. An I2C instance is required to access to the sensor.
The APIs provide simple distance measure in both polling and interrupt modes.

## Examples

There are 3 examples with the VL53L4CX library.

* VL53L4CX_Sat_HelloWorld: This example code is to show how to get proximity
  values of the VL53L4CX satellite sensor in polling mode.

* VL53L4CX_Sat_HelloWorld_Interrupt: This example code is to show how to get proximity
  values of the VL53L4CX satellite sensor in interrupt mode.

    In order to use these examples you need to connect the VL53L4CX satellite sensor directly to the Nucleo board with wires as explained below:
    - pin 1 (GND) of the VL53L4CX satellite connected to GND of the Nucleo board
    - pin 2 (VDD) of the VL53L4CX satellite connected to 3V3 pin of the Nucleo board
    - pin 3 (SCL) of the VL53L4CX satellite connected to pin D15 (SCL) of the Nucleo board
    - pin 4 (SDA) of the VL53L4CX satellite connected to pin D14 (SDA) of the Nucleo board
    - pin 5 (GPIO1) of the VL53L4CX satellite connected to pin A2 of the Nucleo board
    - pin 6 (XSHUT) of the VL53L4CX satellite connected to pin A1 of the Nucleo board


* VL53L4CX_Sat_SerialGraphic: This example code is to show how to get proximity
  values of the VL53L4CX satellite sensor in polling mode.

     In order to use this example as written, you need to connect an Adafruit VL53L4CX Time of Flight Distance Sensor
     to an Adafruit ESP32 board listed below with wires either directly to the exposed pins or the STEMMA QT connector
     where available:
     - Adafruit VL53L4CX ToF Sensor ------> VIN   GND   SCL      SDA      STEMMA QT           XSHUT
     - Adafruit HUZZAH32 – ESP32 Feather    3.3v  gnd   SCL(22)  SDA(23)    n/a                ***
     - Adafruit ESP32 Feather V2 ^          3.3v  gnd   SCL(20)  SDA(22)  SCL(20)  SDA(22)     ***
     - Adafruit QT Py ESP32 Pico ^^         3.3v  gnd   SCL(33)  SDA( 4)  SCL1(19) SDA1(22)    ***
     - Adafruit QT Py ESP32-S2   ^^         3.3v  gnd   SCL( 6)  SDA( 7)  SCL1(40) SDA1(41)    ***
        *** XSHUT connected to the desired output GPIO pin, A1 used in the example.
         ^  The Feather ESP32 V2 has a NEOPIXEL_I2C_POWER pin that must be pulled HIGH 
            to enable power to the STEMMA QT port. Without it, the QT port will not work!
         ^^ ESP32 boards with secondary I2c ports require that the secondary ports must be 
            manually assigned their pins with setPins(), e.g. Wire1.setPins(SDA1, SCL1);
   
     This example looks for the ToF device on either I2c port when two ports are known to exist.
     By default, it will display valid results "graphically" through the serial terminal.
     Display of details, as displayed in example VL53L4CX_Sat_HelloWorld, is optional by 
     changing the value of a parameter.

## Documentation

You can find the source files at  
https://github.com/stm32duino/VL53L4CX

The VL53L4CX datasheet is available at  
https://www.st.com/en/imaging-and-photonics-solutions/vl53l4cx.html
