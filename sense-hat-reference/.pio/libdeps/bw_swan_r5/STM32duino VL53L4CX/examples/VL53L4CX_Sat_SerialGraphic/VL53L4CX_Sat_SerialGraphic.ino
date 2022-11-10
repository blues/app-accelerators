/**
 ******************************************************************************
 * @file    VL53L4CX_Sat_SerialGraphic.ino
 * @author  SRA/CVanzant
 * @version V1.0.0
 * @date    26 June 2022
 * @brief   Arduino test application for the STMicrolectronics VL53L4CX
 *          proximity sensor satellite based on FlightSense.
 *          This application makes use of C++ classes obtained from the C
 *          components' drivers.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2022 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************
 */
/*
 * This sketch, as written, assumes use with the Adafruit VL53L4CX Time of Flight Distance Sensor
 * connected to any of the following boards:
 *   > Adafruit VL53L4CX ToF Sensor ------> VIN   GND   SCL      SDA      STEMMA QT           XSHUT
 *   > Adafruit HUZZAH32 – ESP32 Feather    3.3v  gnd   SCL(22)  SDA(23)    n/a                ***
 *   > Adafruit ESP32 Feather V2 ^          3.3v  gnd   SCL(20)  SDA(22)  SCL(20)  SDA(22)     ***
 *   > Adafruit QT Py ESP32 Pico #          3.3v  gnd   SCL(33)  SDA( 4)  SCL1(19) SDA1(22)    ***
 *   > Adafruit QT Py ESP32-S2   #          3.3v  gnd   SCL( 6)  SDA( 7)  SCL1(40) SDA1(41)    ***
 *      *** XSHUT connected to the desired output GPIO pin, A1 used in the sketch
 *       ^  The Feather ESP32 V2 has a NEOPIXEL_I2C_POWER pin that must be pulled HIGH 
 *          to enable power to the STEMMA QT port. Without it, the QT port will not work!
 *       #  ESP32 boards with secondary I2c ports require that the secondary ports must be 
 *          manually assigned their pins with setPins(), e.g. Wire1.setPins(SDA1, SCL1);
 * 
 * This sketch looks for the ToF device on either I2c port when two ports are known to exist. 
 */
/* Includes ------------------------------------------------------------------*/
#include <Arduino.h>
#include <Wire.h>
#include <vl53l4cx_class.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#define SerialPort Serial

#define XSHUT_PIN A1

// Display valid results "graphically" when true instead of details
bool showGraphicly = true;

// Components.
TwoWire *DEV_I2C = &Wire;

// The following object assumes a zero parameter constructor exists.
VL53L4CX sensor_vl53l4cx_sat;

/* VL53L4CX_RangeStatusCode --------------------------------------------------*/
String VL53L4CX_RangeStatusCode(uint8_t status)
{
  switch (status)
  {
    case VL53L4CX_RANGESTATUS_RANGE_VALID:
      return "VL53L4CX_RANGESTATUS_RANGE_VALID";
    case VL53L4CX_RANGESTATUS_SIGMA_FAIL:
      return "VL53L4CX_RANGESTATUS_SIGMA_FAIL";
    case VL53L4CX_RANGESTATUS_RANGE_VALID_MIN_RANGE_CLIPPED:
      return "VL53L4CX_RANGESTATUS_RANGE_VALID_MIN_RANGE_CLIPPED";
    case VL53L4CX_RANGESTATUS_OUTOFBOUNDS_FAIL:
      return "VL53L4CX_RANGESTATUS_OUTOFBOUNDS_FAIL";
    case VL53L4CX_RANGESTATUS_HARDWARE_FAIL:
      return "VL53L4CX_RANGESTATUS_HARDWARE_FAIL";
    case VL53L4CX_RANGESTATUS_RANGE_VALID_NO_WRAP_CHECK_FAIL:
      return "VL53L4CX_RANGESTATUS_RANGE_VALID_NO_WRAP_CHECK_FAIL";
    case VL53L4CX_RANGESTATUS_WRAP_TARGET_FAIL:
      return "VL53L4CX_RANGESTATUS_WRAP_TARGET_FAIL";
    case VL53L4CX_RANGESTATUS_PROCESSING_FAIL:
      return "VL53L4CX_RANGESTATUS_PROCESSING_FAIL";
    case VL53L4CX_RANGESTATUS_XTALK_SIGNAL_FAIL:
      return "VL53L4CX_RANGESTATUS_XTALK_SIGNAL_FAIL";
    case VL53L4CX_RANGESTATUS_SYNCRONISATION_INT:
      return "VL53L4CX_RANGESTATUS_SYNCRONISATION_INT";
    case VL53L4CX_RANGESTATUS_RANGE_VALID_MERGED_PULSE:
      return "VL53L4CX_RANGESTATUS_RANGE_VALID_MERGED_PULSE";
    case VL53L4CX_RANGESTATUS_TARGET_PRESENT_LACK_OF_SIGNAL:
      return "VL53L4CX_RANGESTATUS_TARGET_PRESENT_LACK_OF_SIGNAL";
    case VL53L4CX_RANGESTATUS_MIN_RANGE_FAIL:
      return "VL53L4CX_RANGESTATUS_MIN_RANGE_FAIL";
    case VL53L4CX_RANGESTATUS_RANGE_INVALID:
      return "VL53L4CX_RANGESTATUS_RANGE_INVALID";
    case VL53L4CX_RANGESTATUS_NONE:
      return "VL53L4CX_RANGESTATUS_NONE";
    default:
      return ("UNKNOWN STATUS: " + String(status));
  }
}

/* I2CDeviceAvailable --------------------------------------------------------*/
bool I2CDeviceAvailable(uint8_t address, TwoWire **wire)
{
  byte error = 1;
  bool available = false;

  // Check if device is available at the expected address
  Wire.begin();
  Wire.beginTransmission(address);
  error = Wire.endTransmission();

  if (error == 0) {
    *wire = &Wire;
    available = true;

    Serial.print("  I2c Device Found at Address 0x");
    Serial.print(address, HEX); Serial.println(" on Wire");
  }
  Wire.end();

#if defined(ARDUINO_ADAFRUIT_KB2040_RP2040)     || \
    defined(ARDUINO_ADAFRUIT_ITSYBITSY_RP2040)  || \
    defined(ARDUINO_ADAFRUIT_QTPY_RP2040)       || \
    defined(ARDUINO_ADAFRUIT_FEATHER_RP2040)    || \
    defined(ARDUINO_ADAFRUIT_QTPY_ESP32S2)      || \
    defined(ARDUINO_ADAFRUIT_QTPY_ESP32_PICO)
  if (error != 0)
  {
    Wire1.begin();
    Wire1.beginTransmission(address);
    byte error1 = Wire1.endTransmission();
    if (error1 == 0)
    {
      *wire = &Wire1;
      available = true;

      Serial.print("  I2c Device Found at Address 0x");
      Serial.print(address, HEX); Serial.println(" on Wire1");
    }
    Wire1.end();
  }
#endif
  return available;
}

/* Setup ---------------------------------------------------------------------*/
void setup()
{
  VL53L4CX_Error error = VL53L4CX_ERROR_NONE;

  // delay to view any serial setup output before any loop output is displayed
  delay(10000);

#if defined(LED_BUILTIN)
  // Led.
  pinMode(LED_BUILTIN, OUTPUT);
#endif

  // Initialize serial for output.
  SerialPort.begin(115200);
  SerialPort.println("Starting...");

#if defined(ARDUINO_ADAFRUIT_QTPY_ESP32S2)         || \
    defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_NOPSRAM) || \
    defined(ARDUINO_ADAFRUIT_QTPY_ESP32_PICO) 
    // ESP32 is kinda odd in that secondary ports must be manually
    // assigned their pins with setPins()!
  Wire1.setPins(SDA1, SCL1);
#endif

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32_V2)
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
#endif

  // Determine the ToF device's address and Twowire device to which it is connected.
  // The default address must be shifted right 1 bit to match the expected physical
  // I2c address.
  if (I2CDeviceAvailable(VL53L4CX_DEFAULT_DEVICE_ADDRESS >> 1, &DEV_I2C)) {
    DEV_I2C->begin();
    sensor_vl53l4cx_sat.setI2cDevice(DEV_I2C);
    sensor_vl53l4cx_sat.setXShutPin(XSHUT_PIN);
  }
  else
  {
    Serial.println("Failure Initializing I2c Port or No I2c device found");
    while (true)
      delay(10);
  }

  // Configure VL53L4CX satellite component.
  sensor_vl53l4cx_sat.begin();

  // Switch off VL53L4CX satellite component.
  sensor_vl53l4cx_sat.VL53L4CX_Off();

  //Initialize VL53L4CX satellite component.
  error = sensor_vl53l4cx_sat.InitSensor(VL53L4CX_DEFAULT_DEVICE_ADDRESS);

  if (error != VL53L4CX_ERROR_NONE) {
    Serial.print("Error Initializing Sensor: ");
    Serial.println(error);
    while (true)
      delay(10);
  }

  // Start Measurements
  sensor_vl53l4cx_sat.VL53L4CX_StartMeasurement();
}

/* Loop ---------------------------------------------------------------------*/
void loop()
{
  VL53L4CX_MultiRangingData_t MultiRangingData;
  VL53L4CX_MultiRangingData_t *pMultiRangingData = &MultiRangingData;
  uint8_t NewDataReady = 0;
  int no_of_object_found = 0, j;
  char report[64];
  int status;

  do {
    status = sensor_vl53l4cx_sat.VL53L4CX_GetMeasurementDataReady(&NewDataReady);
  } while (!NewDataReady);

#if defined(LED_BUILTIN)
  //Led on
  digitalWrite(LED_BUILTIN, HIGH);
#endif

  if ((!status) && (NewDataReady != 0)) {
    status = sensor_vl53l4cx_sat.VL53L4CX_GetMultiRangingData(pMultiRangingData);
    no_of_object_found = pMultiRangingData->NumberOfObjectsFound;

    if (showGraphicly) {
      for (j = 0; j < no_of_object_found; j++) {
        if (pMultiRangingData->RangeData[j].RangeStatus == VL53L4CX_RANGESTATUS_RANGE_VALID ||
            pMultiRangingData->RangeData[j].RangeStatus == VL53L4CX_RANGESTATUS_RANGE_VALID_MERGED_PULSE) {
          int16_t mm = pMultiRangingData->RangeData[j].RangeMilliMeter;
          for (int k=0; k<mm/10; k++)
            Serial.print(" ");
          Serial.println(mm);
        }
      }
    }
    else {
      snprintf(report, sizeof(report), "VL53L4CX Satellite: Count=%d, #Objs=%1d ", pMultiRangingData->StreamCount, no_of_object_found);
      SerialPort.print(report);
      for (j = 0; j < no_of_object_found; j++) {
        SerialPort.print("\r\n                               ");
        SerialPort.print("status=");
        SerialPort.print(pMultiRangingData->RangeData[j].RangeStatus);
        SerialPort.print(", D=");
        SerialPort.print(pMultiRangingData->RangeData[j].RangeMilliMeter);
        SerialPort.print("mm");
        SerialPort.print(", Signal=");
        SerialPort.print((float)pMultiRangingData->RangeData[j].SignalRateRtnMegaCps / 65536.0);
        SerialPort.print(" Mcps, Ambient=");
        SerialPort.print((float)pMultiRangingData->RangeData[j].AmbientRateRtnMegaCps / 65536.0);
        SerialPort.print(" Mcps, ");
        SerialPort.print(VL53L4CX_RangeStatusCode(pMultiRangingData->RangeData[j].RangeStatus));
      }
      SerialPort.println();
    }
    if (status == 0) {
      status = sensor_vl53l4cx_sat.VL53L4CX_ClearInterruptAndStartMeasurement();
    }
  }

#if defined(LED_BUILTIN)
  digitalWrite(LED_BUILTIN, LOW);
#endif
}
