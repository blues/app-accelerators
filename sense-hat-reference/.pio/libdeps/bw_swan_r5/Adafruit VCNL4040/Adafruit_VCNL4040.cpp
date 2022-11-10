
/*!
 *  @file Adafruit_VCNL4040.cpp
 *
 *  @mainpage Adafruit VCNL4040 proximity and ambient light sensor library
 *
 *  @section intro_sec Introduction
 *
 * 	I2C Driver for the VCNL4040 proximity and ambient light sensor library
 *
 * 	This is a library for the Adafruit VCNL4040 breakout:
 * 	https://www.adafruit.com/product/4161
 *
 * 	Adafruit invests time and resources providing this open source code,
 *  please support Adafruit and open-source hardware by purchasing products from
 * 	Adafruit!
 *
 *  @section dependencies Dependencies
 *
 *  This library depends on the Adafruit BusIO library
 *
 *  @section author Author
 *
 *  Bryan Siepert for Adafruit Industries
 *
 * 	@section license License
 *
 * 	BSD (see license.txt)
 *
 * 	@section  HISTORY
 *
 *     v1.0 - First release
 */

#include "Arduino.h"
#include <Wire.h>

#include "Adafruit_VCNL4040.h"

/*!
 *    @brief  Instantiates a new VCNL4040 class
 */
Adafruit_VCNL4040::Adafruit_VCNL4040(void) {}

/*!
 *    @brief  Sets up the hardware and initializes I2C
 *    @param  i2c_address
 *            The I2C address to be used.
 *    @param  wire
 *            The Wire object to be used for I2C connections.
 *    @return True if initialization was successful, otherwise false.
 */
boolean Adafruit_VCNL4040::begin(uint8_t i2c_address, TwoWire *wire) {
  i2c_dev = new Adafruit_I2CDevice(i2c_address, wire);

  if (!i2c_dev->begin()) {
    return false;
  }

  return _init();
}

boolean Adafruit_VCNL4040::_init(void) {
  Adafruit_BusIO_Register chip_id =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_DEVICE_ID, 2);

  // make sure we're talking to the right chip
  if (chip_id.read() != 0x0186) {
    return false;
  }

  ALS_CONFIG = new Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_CONFIG, 2);
  PS_CONFIG_12 = new Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_CONF1_L, 2);
  PS_MS = new Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_MS_H, 2);

  enableProximity(true);
  enableWhiteLight(true);
  enableAmbientLight(true);
  setProximityHighResolution(true);

  return true;
}
/**************** Sensor Data Getters *************************************/
/**************************************************************************/
/*!
    @brief Gets the current proximity sensor value.
    @return The current proximity measurement in units
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getProximity(void) {
  Adafruit_BusIO_Register proximity =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_DATA, 2);
  delay(10);
  return (int16_t)proximity.read();
}

/**************************************************************************/
/*!
    @brief Gets the current ambient light sensor value.
    @return The current ambient light measurement in units
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getAmbientLight(void) {
  Adafruit_BusIO_Register ambient_light =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_DATA, 2);
  return (int16_t)ambient_light.read();
}
/**************************************************************************/
/*!
    @brief Gets the current white light value.
    @return The current white light measurement in units
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getWhiteLight(void) {
  Adafruit_BusIO_Register white_light =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_WHITE_DATA, 2);
  delay(10);

  // scale the light depending on the value of the integration time
  // see page 8 of the VCNL4040 application note:
  // https://www.vishay.com/docs/84307/designingvcnl4040.pdf
  return (white_light.read() * (0.1 / (1 << getAmbientIntegrationTime())));
}

/**************************************************************************/
/*!
    @brief Gets the current ambient light sensor in Lux.
    @return The current ambient light measurement in Lux
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getLux(void) {
  Adafruit_BusIO_Register ambient_light =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_DATA, 2);
  // scale the lux depending on the value of the integration time
  // see page 8 of the VCNL4040 application note:
  // https://www.vishay.com/docs/84307/designingvcnl4040.pdf
  return (ambient_light.read() * (0.1 / (1 << getAmbientIntegrationTime())));
}
/**************** Sensor Enable Functions   *******************************/

/**************************************************************************/
/*!
    @brief Enables or disables proximity measurements.
    @param  enable
            Set to true to enable proximity measurements,
            set to false to disable.
*/
void Adafruit_VCNL4040::enableProximity(bool enable) {
  Adafruit_BusIO_RegisterBits ps_disable =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 1, 0);
  ps_disable.write(!enable);
}
/**************************************************************************/
/*!
    @brief Enables ambient light measurements
    @param  enable
            Set to true to enable ambient light measurements,
            set to false to disable.
*/
void Adafruit_VCNL4040::enableAmbientLight(bool enable) {
  Adafruit_BusIO_RegisterBits als_disable =
      Adafruit_BusIO_RegisterBits(ALS_CONFIG, 1, 0);
  als_disable.write(!enable);
}
/**************************************************************************/
/*!
    @brief Enables white light measurements
    @param  enable
            Set to true to enable white light measurements,
            set to false to disable.
*/
void Adafruit_VCNL4040::enableWhiteLight(bool enable) {
  Adafruit_BusIO_RegisterBits white_disable =
      Adafruit_BusIO_RegisterBits(PS_MS, 1, 15);
  white_disable.write(!enable);
}

/*************************** Interrupt Functions  *********************** */

/**************************************************************************/
/*!
    @brief Gets and clears the interrupt status register.
    @return The current value of the interrupt status register.
            Indivitual interrupt types can be checked by anding the returned
   byte with the members of `VCNL4040_InterruptType`:`VCNL4040_PROXIMITY_AWAY`,
            `VCNL4040_PROXIMITY_CLOSE`, `PROXIMITY_LOW`, or `PROXIMITY_HIGH`
*/
/**************************************************************************/
uint8_t Adafruit_VCNL4040::getInterruptStatus(void) {
  Adafruit_BusIO_Register interrupt_status_register =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_INT_FLAG, 2);

  Adafruit_BusIO_RegisterBits interrupt_status =
      Adafruit_BusIO_RegisterBits(&interrupt_status_register, 8, 8);
  return (uint16_t)interrupt_status.read();
}

/********************* Ambient Light Interrupt Functions **************** */

/**************************************************************************/
/*!
    @brief Enables or disables ambient light based interrupts.
    @param  enable
            Set to true to enable or false to disable ambient light interrupts
*/
/**************************************************************************/
void Adafruit_VCNL4040::enableAmbientLightInterrupts(bool enable) {
  Adafruit_BusIO_RegisterBits als_interrupt_enable =
      Adafruit_BusIO_RegisterBits(ALS_CONFIG, 1, 1);
  als_interrupt_enable.write(enable);
}

/**************************************************************************/
/*!
    @brief Gets the current ambient light high threshold.
    @return The current ambient light high threshold
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getAmbientLightHighThreshold(void) {
  Adafruit_BusIO_Register als_high_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_THDH, 2);
  return (uint16_t)als_high_threshold.read();
}
/**************************************************************************/
/*!
    @brief Sets the ambient light high threshold.
    @param  high_threshold
            The high threshold to set
*/
/**************************************************************************/
void Adafruit_VCNL4040::setAmbientLightHighThreshold(uint16_t high_threshold) {
  Adafruit_BusIO_Register als_high_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_THDH, 2);
  als_high_threshold.write(high_threshold);
}
/**************************************************************************/
/*!
    @brief Gets the ambient light low threshold.
    @return the current ambient light low threshold
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getAmbientLightLowThreshold(void) {
  Adafruit_BusIO_Register als_low_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_THDL, 2);
  return (uint16_t)als_low_threshold.read();
}

/**************************************************************************/
/*!
    @brief Sets the ambient light low threshold.
    @param  low_threshold
            The low threshold to set
*/
/**************************************************************************/
void Adafruit_VCNL4040::setAmbientLightLowThreshold(uint16_t low_threshold) {
  Adafruit_BusIO_Register als_low_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_ALS_THDL, 2);
  als_low_threshold.write(low_threshold);
}

/********************* Proximity Interrupt Functions **************** */

/**************************************************************************/
/*!
    @brief Disables or  enables proximity interrupts under a given condition.
    @param  interrupt_condition
            The condition under which to raise an interrupt. Must be a
   `VCNL4040_ProximityType`. Use `VCNL4040_PROXIMITY_INT_DISABLE` to disable
   proximity interrupts.
*/
/**************************************************************************/
void Adafruit_VCNL4040::enableProximityInterrupts(
    VCNL4040_ProximityType interrupt_condition) {
  Adafruit_BusIO_RegisterBits proximity_int_config =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 2, 8);

  proximity_int_config.write(interrupt_condition);
}

/**************************************************************************/
/*!
    @brief Gets the proximity low threshold.
    @returns  The current low threshold
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getProximityLowThreshold(void) {
  Adafruit_BusIO_Register proximity_low_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_THDL, 2);

  return (uint16_t)proximity_low_threshold.read();
}
/**************************************************************************/
/*!
    @brief Sets the proximity low threshold.
    @param  low_threshold
            The low threshold to set
*/
/**************************************************************************/
void Adafruit_VCNL4040::setProximityLowThreshold(uint16_t low_threshold) {
  Adafruit_BusIO_Register proximity_low_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_THDL, 2);

  proximity_low_threshold.write(low_threshold);
}
/**************************************************************************/
/*!
    @brief Gets the proximity high threshold.
    @returns  The current high threshold
*/
/**************************************************************************/
uint16_t Adafruit_VCNL4040::getProximityHighThreshold(void) {
  Adafruit_BusIO_Register proximity_high_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_THDH, 2);

  return (uint16_t)proximity_high_threshold.read();
}
/**************************************************************************/
/*!
    @brief Sets the proximity high threshold.
    @param  high_threshold
            The high threshold to set
*/
/**************************************************************************/
void Adafruit_VCNL4040::setProximityHighThreshold(uint16_t high_threshold) {
  Adafruit_BusIO_Register proximity_high_threshold =
      Adafruit_BusIO_Register(i2c_dev, VCNL4040_PS_THDH, 2);

  proximity_high_threshold.write(high_threshold);
}

/******************** Tuning Functions ********************************** */

/**************************************************************************/
/*!
    @brief Gets the integration time for proximity sensing measurements.
    @returns The integration time being used for proximity measurements.
*/
VCNL4040_ProximityIntegration
Adafruit_VCNL4040::getProximityIntegrationTime(void) {
  Adafruit_BusIO_RegisterBits proximity_int_config =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 3, 1);
  delay(50);
  return (VCNL4040_ProximityIntegration)proximity_int_config.read();
}
/**************************************************************************/
/*!
    @brief Sets the integration time for proximity sensing measurements.
    @param  integration_time
            The integration time to use for proximity measurements. Must be a
            `VCNL4040_ProximityIntegration`.
*/
void Adafruit_VCNL4040::setProximityIntegrationTime(
    VCNL4040_ProximityIntegration integration_time) {
  Adafruit_BusIO_RegisterBits proximity_int_config =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 3, 1);
  delay(50);
  proximity_int_config.write(integration_time);
}

/**************************************************************************/
/*!
    @brief Gets the integration time for ambient light sensing measurements.
    @returns The integration time being used for ambient light measurements.
*/
VCNL4040_AmbientIntegration Adafruit_VCNL4040::getAmbientIntegrationTime(void) {
  Adafruit_BusIO_RegisterBits ambient_int_config =
      Adafruit_BusIO_RegisterBits(ALS_CONFIG, 2, 6);
  delay(50);
  return (VCNL4040_AmbientIntegration)ambient_int_config.read();
}

/**************************************************************************/
/*!
    @brief Sets the integration time for ambient light sensing measurements.
    @param  integration_time
            The integration time to use for ambient light measurements. Must be
   a `VCNL4040_AmbientIntegration`.
*/
void Adafruit_VCNL4040::setAmbientIntegrationTime(
    VCNL4040_AmbientIntegration integration_time) {
  Adafruit_BusIO_RegisterBits ambient_int_config =
      Adafruit_BusIO_RegisterBits(ALS_CONFIG, 2, 6);

  // delay according to the integration time to let the reading at the old IT
  // clear out
  uint8_t old_it_raw = ambient_int_config.read();
  uint16_t old_it_ms = ((8 << ambient_int_config.read()) * 10);
  uint16_t new_it_ms = ((8 << integration_time) * 10);

  ambient_int_config.write(integration_time);
  delay((old_it_ms + new_it_ms + 1));
}

/**************************************************************************/
/*!
    @brief Gets the current for the LED used for proximity measurements.
    @returns The LED current value being used for proximity measurements.
*/
VCNL4040_LEDCurrent Adafruit_VCNL4040::getProximityLEDCurrent(void) {
  Adafruit_BusIO_RegisterBits led_current_config =
      Adafruit_BusIO_RegisterBits(PS_MS, 2, 8);
  return (VCNL4040_LEDCurrent)led_current_config.read();
} /**************************************************************************/
/*!
    @brief Sets the current for the LED used for proximity measurements.
    @param  led_current
            The current value to be used for proximity measurements. Must be a
            `VCNL4040_LEDCurrent`.
*/
void Adafruit_VCNL4040::setProximityLEDCurrent(
    VCNL4040_LEDCurrent led_current) {
  Adafruit_BusIO_RegisterBits led_current_config =
      Adafruit_BusIO_RegisterBits(PS_MS, 2, 8);
  led_current_config.write(led_current);
}

/**************************************************************************/
/*!
    @brief Sets the duty cycle for the LED used for proximity measurements.
    @returns The duty cycle value being used for proximity measurements.
*/
VCNL4040_LEDDutyCycle Adafruit_VCNL4040::getProximityLEDDutyCycle(void) {
  Adafruit_BusIO_RegisterBits led_duty_cycle_config =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 2, 6);
  return (VCNL4040_LEDDutyCycle)led_duty_cycle_config.read();
}
/**************************************************************************/
/*!
    @brief Sets the duty cycle for the LED used for proximity measurements.
    @param  duty_cycle
            The duty cycle value to be used for proximity measurements. Must be
   a `VCNL4040_LEDDutyCycle`.
*/
void Adafruit_VCNL4040::setProximityLEDDutyCycle(
    VCNL4040_LEDDutyCycle duty_cycle) {
  Adafruit_BusIO_RegisterBits led_duty_cycle_config =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 2, 6);
  led_duty_cycle_config.write(duty_cycle);
}

/**************************************************************************/
/*!
    @brief Gets the resolution of proximity measurements
    @return The current proximity measurement resolution
            If true, proximity measurements are 16-bit,
            If false, proximity measurements are 12-bit,
*/
bool Adafruit_VCNL4040::getProximityHighResolution(void) {
  Adafruit_BusIO_RegisterBits ps_hd =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 1, 11);
  return (bool)ps_hd.read();
}
/**************************************************************************/
/*!
    @brief Sets the resolution of proximity measurements
    @param  high_resolution
            Set to true to take 16-bit measurements for proximity,
            set to faluse to use 12-bit measurements.
*/
void Adafruit_VCNL4040::setProximityHighResolution(bool high_resolution) {
  Adafruit_BusIO_RegisterBits ps_hd =
      Adafruit_BusIO_RegisterBits(PS_CONFIG_12, 1, 11);
  ps_hd.write(high_resolution);
}
