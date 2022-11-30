/*!
 * @file Adafruit_BMP581.h
 *
 * Adafruit BMP581 temperature & barometric pressure sensor driver
 *
 * This is the documentation for Adafruit's BMP581 driver for the
 * Arduino platform.  It is designed specifically to work with the
 * Adafruit BMP581 breakout: https://www.adafruit.com/products/3966
 *
 * These sensors use I2C or SPI to communicate
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * Written by Ladyada for Adafruit Industries.
 *
 * BSD license, all text here must be included in any redistribution.
 *
 */

#ifndef __BMP581_H__
#define __BMP581_H__

#include "bmp5.h"

#include <Adafruit_I2CDevice.h>
#include <Adafruit_SPIDevice.h>

/*=========================================================================
    I2C ADDRESS/BITS
    -----------------------------------------------------------------------*/
#define BMP581_DEFAULT_ADDRESS (0x47) ///< The default I2C address
/*=========================================================================*/
#define BMP581_DEFAULT_SPIFREQ (1000000) ///< The default SPI Clock speed

/** Adafruit_BMP581 Class for both I2C and SPI usage.
 *  Wraps the Bosch library for Arduino usage
 */

class Adafruit_BMP581 {
public:
  Adafruit_BMP581();

  bool begin_I2C(uint8_t addr = BMP581_DEFAULT_ADDRESS,
                 TwoWire *theWire = &Wire);
  bool begin_SPI(uint8_t cs_pin, SPIClass *theSPI = &SPI);
  bool begin_SPI(int8_t cs_pin, int8_t sck_pin, int8_t miso_pin,
                 int8_t mosi_pin);
  uint8_t chipID(void);
  float readTemperature(void);
  float readPressure(void);
  float readAltitude(float seaLevel);

  bool setTemperatureOversampling(uint8_t os);
  bool setPressureOversampling(uint8_t os);
  bool setIIRFilterCoeff(uint8_t fs);
  bool setOutputDataRate(uint8_t odr);

  /// Perform a reading in blocking mode
  bool performReading(void);

  /// Temperature (Celsius) assigned after calling performReading()
  double temperature;
  /// Pressure (Pascals) assigned after calling performReading()
  double pressure;

private:
  Adafruit_I2CDevice *i2c_dev = NULL; ///< Pointer to I2C bus interface
  Adafruit_SPIDevice *spi_dev = NULL; ///< Pointer to SPI bus interface

  bool _init(void);

  bool _filterEnabled, _tempOSEnabled, _presOSEnabled, _ODREnabled;
  uint8_t _i2caddr;
  int32_t _sensorID;
  int8_t _cs;
  unsigned long _meas_end;

  uint8_t spixfer(uint8_t x);

  struct bmp5_dev the_sensor;
  struct bmp5_osr_odr_press_config osr_odr_press_cfg;
  struct bmp5_iir_config set_iir_cfg;
};

#endif
