/*!
 * @file Adafruit_BMP581.cpp
 *
 * @mainpage Adafruit BMP581 temperature & barometric pressure sensor driver
 *
 * @section intro_sec Introduction
 *
 * This is the documentation for Adafruit's BMP581 driver for the
 * Arduino platform.  It is designed specifically to work with the
 * Adafruit BMP388 breakout: https://www.adafruit.com/products/3966
 *
 * These sensors use I2C or SPI to communicate
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * @section author Author
 *
 * Written by Ladyada for Adafruit Industries.
 *
 * @section license License
 *
 * BSD license, all text here must be included in any redistribution.
 *
 */

#include "Adafruit_BMP581.h"
#include "Arduino.h"

// #define BMP581_DEBUG

Adafruit_I2CDevice *g_i2c_dev = NULL; ///< Global I2C interface pointer
Adafruit_SPIDevice *g_spi_dev = NULL; ///< Global SPI interface pointer

// Our hardware interface functions
static int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len,
                        void *intf_ptr);
static int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len,
                       void *intf_ptr);
static int8_t spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len,
                       void *intf_ptr);
static int8_t spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len,
                        void *intf_ptr);
static void delay_usec(uint32_t us, void *intf_ptr);
static int8_t validate_trimming_param(struct bmp5_dev *dev);
static int8_t cal_crc(uint8_t seed, uint8_t data);

/***************************************************************************
 PUBLIC FUNCTIONS
 ***************************************************************************/

/**************************************************************************/
/*!
    @brief  Instantiates sensor
*/
/**************************************************************************/
Adafruit_BMP581::Adafruit_BMP581(void) {
  _meas_end = 0;
  _filterEnabled = _tempOSEnabled = _presOSEnabled = false;
}

/**************************************************************************/
/*!
    @brief Initializes the sensor

    Hardware ss initialized, verifies it is in the I2C or SPI bus, then reads
    calibration data in preparation for sensor reads.

    @param  addr Optional parameter for the I2C address of BMP3. Default is 0x77
    @param  theWire Optional parameter for the I2C device we will use. Default
   is "Wire"
    @return True on sensor initialization success. False on failure.
*/
/**************************************************************************/
bool Adafruit_BMP581::begin_I2C(uint8_t addr, TwoWire *theWire) {
  if (i2c_dev)
    delete i2c_dev;
  if (spi_dev)
    delete spi_dev;
  spi_dev = NULL;

  g_i2c_dev = i2c_dev = new Adafruit_I2CDevice(addr, theWire);

  // verify i2c address was found
  if (!i2c_dev->begin()) {
    return false;
  }

  the_sensor.chip_id = addr;
  the_sensor.intf = BMP5_I2C_INTF;
  the_sensor.read = &i2c_read;
  the_sensor.write = &i2c_write;
  the_sensor.intf_ptr = g_i2c_dev;

  return _init();
}

/*!
 *    @brief  Sets up the hardware and initializes hardware SPI
 *    @param  cs_pin The arduino pin # connected to chip select
 *    @param  theSPI The SPI object to be used for SPI connections.
 *    @return True if initialization was successful, otherwise false.
 */
bool Adafruit_BMP581::begin_SPI(uint8_t cs_pin, SPIClass *theSPI) {
  if (i2c_dev)
    delete i2c_dev;
  if (spi_dev)
    delete spi_dev;
  i2c_dev = NULL;

  g_spi_dev = spi_dev =
      new Adafruit_SPIDevice(cs_pin,
                             BMP581_DEFAULT_SPIFREQ, // frequency
                             SPI_BITORDER_MSBFIRST,  // bit order
                             SPI_MODE0,              // data mode
                             theSPI);

  if (!spi_dev->begin()) {
    return false;
  }

  the_sensor.chip_id = cs_pin;
  the_sensor.intf = BMP5_SPI_INTF;
  the_sensor.read = &spi_read;
  the_sensor.write = &spi_write;
  the_sensor.intf_ptr = g_spi_dev;

  return _init();
}

/*!
 *    @brief  Sets up the hardware and initializes software SPI
 *    @param  cs_pin The arduino pin # connected to chip select
 *    @param  sck_pin The arduino pin # connected to SPI clock
 *    @param  miso_pin The arduino pin # connected to SPI MISO
 *    @param  mosi_pin The arduino pin # connected to SPI MOSI
 *    @return True if initialization was successful, otherwise false.
 */
bool Adafruit_BMP581::begin_SPI(int8_t cs_pin, int8_t sck_pin, int8_t miso_pin,
                                int8_t mosi_pin) {
  if (i2c_dev)
    delete i2c_dev;
  if (spi_dev)
    delete spi_dev;
  i2c_dev = NULL;

  g_spi_dev = spi_dev =
      new Adafruit_SPIDevice(cs_pin, sck_pin, miso_pin, mosi_pin,
                             BMP581_DEFAULT_SPIFREQ, // frequency
                             SPI_BITORDER_MSBFIRST,  // bit order
                             SPI_MODE0);             // data mode

  if (!spi_dev->begin()) {
    return false;
  }

  the_sensor.chip_id = cs_pin;
  the_sensor.intf = BMP5_SPI_INTF;
  the_sensor.read = &spi_read;
  the_sensor.write = &spi_write;
  the_sensor.intf_ptr = g_spi_dev;

  return _init();
}

bool Adafruit_BMP581::_init(void) {
  g_i2c_dev = i2c_dev;
  g_spi_dev = spi_dev;
  the_sensor.delay_us = delay_usec;
  int8_t rslt = BMP5_OK;

  /* Reset the sensor */
  rslt = bmp5_soft_reset(&the_sensor);
#ifdef BMP581_DEBUG
  Serial.print("Reset result: ");
  Serial.println(rslt);
#endif
  if (rslt != BMP5_OK)
    return false;

  rslt = bmp5_init(&the_sensor);
#ifdef BMP581_DEBUG
  Serial.print("Init result: ");
  Serial.println(rslt);
#endif

  setTemperatureOversampling(BMP5_OVERSAMPLING_1X);
  setPressureOversampling(BMP5_OVERSAMPLING_1X);
  setIIRFilterCoeff(BMP5_IIR_FILTER_BYPASS);
  setOutputDataRate(BMP5_ODR_25_HZ);

  // don't do anything till we request a reading
  rslt = bmp5_set_power_mode(BMP5_POWERMODE_FORCED, &the_sensor);

  if (rslt == BMP5_OK) {
    return true;
  } else {
    return false;
  }
}

/**************************************************************************/
/*!
    @brief Performs a reading and returns the ambient temperature.
    @return Temperature in degrees Centigrade
*/
/**************************************************************************/
float Adafruit_BMP581::readTemperature(void) {
  performReading();
  return temperature;
}

/**************************************************************************/
/*!
    @brief Reads the chip identifier
    @return BMP581_CHIP_ID
*/
/**************************************************************************/
uint8_t Adafruit_BMP581::chipID(void) { return the_sensor.chip_id; }

/**************************************************************************/
/*!
    @brief Performs a reading and returns the barometric pressure.
    @return Barometic pressure in Pascals
*/
/**************************************************************************/
float Adafruit_BMP581::readPressure(void) {
  performReading();
  return pressure;
}

/**************************************************************************/
/*!
    @brief Calculates the altitude (in meters).

    Reads the current atmostpheric pressure (in hPa) from the sensor and
   calculates via the provided sea-level pressure (in hPa).

    @param  seaLevel      Sea-level pressure in hPa
    @return Altitude in meters
*/
/**************************************************************************/
float Adafruit_BMP581::readAltitude(float seaLevel) {
  // Equation taken from BMP180 datasheet (page 16):
  // http://www.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf

  // Note that using the equation from wikipedia can give bad results
  // at high altitude. See this thread for more information:
  // http://forums.adafruit.com/viewtopic.php?f=22&t=58064

  float atmospheric = readPressure() / 100.0F;
  // return 44330.0 * (1.0 - pow(atmospheric / seaLevel, 0.1903));
  return 44330.0 * abs(pow(atmospheric / seaLevel, 0.1903) - 1.0);
}

/**************************************************************************/
/*!
    @brief Performs a full reading of all sensors in the BMP581.

    Assigns the internal Adafruit_BMP581#temperature & Adafruit_BMP581#pressure
   member variables

    @return True on success, False on failure
*/
/**************************************************************************/
bool Adafruit_BMP581::performReading(void) {
  g_i2c_dev = i2c_dev;
  g_spi_dev = spi_dev;
  int8_t rslt;
  /* Used to select the settings user needs to change */
  uint16_t settings_sel = 0;
  /* Variable used to select the sensor component */
  uint8_t sensor_comp = 0;

  osr_odr_press_cfg.press_en = BMP5_ENABLE;

  /* Set the desired sensor configuration */
#ifdef BMP581_DEBUG
  Serial.println("Setting sensor settings");
#endif
  rslt = bmp5_set_osr_odr_press_config(&osr_odr_press_cfg, &the_sensor);

  if (rslt != BMP5_OK)
    return false;

#ifdef BMP581_DEBUG
  Serial.println(F("Setting power mode"));
#endif
  /* Set the power mode */
  rslt = bmp5_set_power_mode(BMP5_POWERMODE_FORCED, &the_sensor);

  if (rslt != BMP5_OK)
    return false;

  /* Variable used to store the compensated data */
  struct bmp5_sensor_data data;

  /* Temperature and Pressure data are read and stored in the bmp5_sensor_data instance
   */
#ifdef BMP581_DEBUG
  Serial.println(F("Getting sensor data"));
#endif
  rslt = bmp5_get_sensor_data(&data, &osr_odr_press_cfg, &the_sensor);
  if (rslt != BMP5_OK)
    return false;

  /*
#ifdef BMP581_DEBUG
  Serial.println(F("Analyzing sensor data"));
#endif
  rslt = analyze_sensor_data(&data);
  if (rslt != BMP5_OK)
    return false;
    */

  /* Save the temperature and pressure data */
  temperature = data.temperature;
  pressure = data.pressure;

  return true;
}

/**************************************************************************/
/*!
    @brief  Setter for Temperature oversampling
    @param  oversample Oversampling setting, can be BMP5_OVERSAMPLING_1X,
   BMP5_OVERSAMPLING_2X, BMP5_OVERSAMPLING_4X, BMP5_OVERSAMPLING_8X,
   BMP5_OVERSAMPLING_16X, BMP5_OVERSAMPLING_32X
    @return True on success, False on failure
*/
/**************************************************************************/

bool Adafruit_BMP581::setTemperatureOversampling(uint8_t oversample) {
  if (oversample > BMP5_OVERSAMPLING_32X)
    return false;

  osr_odr_press_cfg.osr_t = oversample;

  if (oversample == BMP5_OVERSAMPLING_1X)
    _tempOSEnabled = false;
  else
    _tempOSEnabled = true;

  return true;
}

/**************************************************************************/
/*!
    @brief  Setter for Pressure oversampling
    @param  oversample Oversampling setting, can be BMP5_OVERSAMPLING_1X,
   BMP5_OVERSAMPLING_2X, BMP5_OVERSAMPLING_4X, BMP5_OVERSAMPLING_8X,
   BMP5_OVERSAMPLING_16X, BMP5_OVERSAMPLING_32X
    @return True on success, False on failure
*/
/**************************************************************************/
bool Adafruit_BMP581::setPressureOversampling(uint8_t oversample) {
  if (oversample > BMP5_OVERSAMPLING_32X)
    return false;

  osr_odr_press_cfg.osr_p = oversample;

  if (oversample == BMP5_OVERSAMPLING_1X)
    _presOSEnabled = false;
  else
    _presOSEnabled = true;

  return true;
}

/**************************************************************************/
/*!
    @brief  Setter for IIR filter coefficient
    @param filtercoeff Coefficient of the filter (in samples). Can be
   BMP5_IIR_FILTER_BYPASS (no filtering), BMP5_IIR_FILTER_COEFF_1,
   BMP5_IIR_FILTER_COEFF_3, BMP5_IIR_FILTER_COEFF_7, BMP5_IIR_FILTER_COEFF_15,
   BMP5_IIR_FILTER_COEFF_31, BMP5_IIR_FILTER_COEFF_63, BMP5_IIR_FILTER_COEFF_127
    @return True on success, False on failure

*/
/**************************************************************************/
bool Adafruit_BMP581::setIIRFilterCoeff(uint8_t filtercoeff) {
  if (filtercoeff > BMP5_IIR_FILTER_COEFF_127)
    return false;

  set_iir_cfg.set_iir_p = filtercoeff;
  set_iir_cfg.set_iir_t = filtercoeff;

  if (filtercoeff == BMP5_IIR_FILTER_BYPASS)
    _filterEnabled = false;
  else
    _filterEnabled = true;

  return true;
}

/**************************************************************************/
/*!
    @brief  Setter for output data rate (ODR)
    @param odr Sample rate in Hz. Can be BMP5_ODR_240_HZ, BMP5_ODR_218_5_HZ,
   BMP5_ODR_199_1_HZ, BMP5_ODR_179_2_HZ, BMP5_ODR_160_HZ, BMP5_ODR_149_3_HZ,
   BMP5_ODR_140_HZ, BMP5_ODR_129_8_HZ, BMP5_ODR_120_HZ, BMP5_ODR_110_1_HZ,
   BMP5_ODR_100_2_HZ, BMP5_ODR_89_6_HZ, BMP5_ODR_80_HZ, BMP5_ODR_70_HZ,
   BMP5_ODR_60_HZ, BMP5_ODR_50_HZ, BMP5_ODR_45_HZ, BMP5_ODR_40_HZ,
   BMP5_ODR_35_HZ, BMP5_ODR_30_HZ, BMP5_ODR_25_HZ, BMP5_ODR_20_HZ,
   BMP5_ODR_15_HZ, BMP5_ODR_10_HZ, BMP5_ODR_05_HZ, BMP5_ODR_04_HZ,
   BMP5_ODR_03_HZ, BMP5_ODR_02_HZ, BMP5_ODR_01_HZ, BMP5_ODR_0_5_HZ,
   BMP5_ODR_0_250_HZ, or BMP5_ODR_0_125_HZ
    @return True on success, False on failure

*/
/**************************************************************************/
bool Adafruit_BMP581::setOutputDataRate(uint8_t odr) {
  if (odr > BMP5_ODR_0_125_HZ)
    return false;

  osr_odr_press_cfg.odr = odr;

  _ODREnabled = true;

  return true;
}

/**************************************************************************/
/*!
    @brief  Reads 8 bit values over I2C
*/
/**************************************************************************/
int8_t i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len,
                void *intf_ptr) {
  // Serial.print("I2C read address 0x"); Serial.print(reg_addr, HEX);
  // Serial.print(" len "); Serial.println(len, HEX);

  if (!g_i2c_dev->write_then_read(&reg_addr, 1, reg_data, len))
    return 1;

  return 0;
}

/**************************************************************************/
/*!
    @brief  Writes 8 bit values over I2C
*/
/**************************************************************************/
int8_t i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len,
                 void *intf_ptr) {
  // Serial.print("I2C write address 0x"); Serial.print(reg_addr, HEX);
  // Serial.print(" len "); Serial.println(len, HEX);

  if (!g_i2c_dev->write((uint8_t *)reg_data, len, true, &reg_addr, 1))
    return 1;

  return 0;
}

/**************************************************************************/
/*!
    @brief  Reads 8 bit values over SPI
*/
/**************************************************************************/
static int8_t spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len,
                       void *intf_ptr) {
  g_spi_dev->write_then_read(&reg_addr, 1, reg_data, len, 0xFF);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Writes 8 bit values over SPI
*/
/**************************************************************************/
static int8_t spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len,
                        void *intf_ptr) {
  g_spi_dev->write((uint8_t *)reg_data, len, &reg_addr, 1);

  return 0;
}

static void delay_usec(uint32_t us, void *intf_ptr) { delayMicroseconds(us); }

static int8_t validate_trimming_param(struct bmp5_dev *dev) {
  int8_t rslt;
  uint8_t crc = 0xFF;
  uint8_t stored_crc;
  uint8_t trim_param[21];
  uint8_t i;

  rslt = bmp5_get_regs(BMP5_REG_DRIVE_CONFIG, trim_param, 21, dev);
  if (rslt == BMP5_OK) {
    for (i = 0; i < 21; i++) {
      crc = (uint8_t)cal_crc(crc, trim_param[i]);
    }

    crc = (crc ^ 0xFF);
    rslt = bmp5_get_regs(0x30, &stored_crc, 1, dev);
    if (stored_crc != crc) {
      rslt = -1;
    }
  }

  return rslt;
}

/*
 * @brief function to calculate CRC for the trimming parameters
 * */
static int8_t cal_crc(uint8_t seed, uint8_t data) {
  int8_t poly = 0x1D;
  int8_t var2;
  uint8_t i;

  for (i = 0; i < 8; i++) {
    if ((seed & 0x80) ^ (data & 0x80)) {
      var2 = 1;
    } else {
      var2 = 0;
    }

    seed = (seed & 0x7F) << 1;
    data = (data & 0x7F) << 1;
    seed = seed ^ (uint8_t)(poly * var2);
  }

  return (int8_t)seed;
}