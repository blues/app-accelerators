//*****************************************************************************
// UpbeatLabs_MCP39F521.h
//
// This is a library for the Upbeat Labs Dr. Wattson Energy Monitoring Board
// --> https://www.tindie.com/products/UpbeatLabs/dr-wattson-energy-monitoring-board-2/
//
// Written by Sridhar Rajagopal for Upbeat Labs LLC.
//
// BSD 3-Clause License

// Copyright (c) 2018, Upbeat Labs
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.

// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.

// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//*****************************************************************************

#ifndef __UPBEATLABS_MCP39F521_H__
#define __UPBEATLABS_MCP39F521_H__

#include "Arduino.h"
#include "Wire.h"


/****************************************************************************/
/*    MACROS                                                                */
/****************************************************************************/

#define CALIBRATION_CURRENT 1035
#define CALIBRATION_VOLTAGE 852
#define CALIBRATION_POWER_ACTIVE 87
#define CALIBRATION_POWER_REACTIVE 0
#define LINE_FREQUENCY_REF 60000


/****************************************************************************/
/*    DATATYPES                                                             */
/****************************************************************************/

typedef struct UpbeatLabs_MCP39F521_Data {
	uint16_t systemStatus;
	uint16_t systemVersion;
	uint16_t voltageRMS;
	uint16_t lineFrequency;
	uint16_t analogInputVoltage;
	int16_t powerFactor;
	uint32_t currentRMS;
	uint32_t activePower;
	uint32_t reactivePower;
	uint32_t apparentPower;
} UpbeatLabs_MCP39F521_Data;

typedef struct UpbeatLabs_MCP39F521_FormattedData {
	uint16_t systemStatus;
	uint16_t systemVersion;
	float voltageRMS;
	float lineFrequency;
	float analogInputVoltage;
	float powerFactor;
	float currentRMS;
	float activePower;
	float reactivePower;
	float apparentPower;
} UpbeatLabs_MCP39F521_FormattedData;

typedef struct UpbeatLabs_MCP39F521_AccumData {
	uint64_t activeEnergyImport;
	uint64_t activeEnergyExport;
	uint64_t reactiveEnergyImport;
	uint64_t reactiveEnergyExport;
} UpbeatLabs_MCP39F521_AccumData;

typedef struct UpbeatLabs_MCP39F521_FormattedAccumData {
	double activeEnergyImport;
	double activeEnergyExport;
	double reactiveEnergyImport;
	double reactiveEnergyExport;
} UpbeatLabs_MCP39F521_FormattedAccumData;

typedef struct UpbeatLabs_MCP39F521_CalibrationData {
	uint16_t calibrationRegisterDelimiter;
	uint16_t gainCurrentRMS;
	uint16_t gainVoltageRMS;
	uint16_t gainActivePower;
	uint16_t gainReactivePower;
	int32_t offsetCurrentRMS;
	int32_t offsetActivePower;
	int32_t offsetReactivePower;
	int16_t dcOffsetCurrent;
	int16_t phaseCompensation;
	uint16_t apparentPowerDivisor;
} UpbeatLabs_MCP39F521_CalibrationData;

typedef struct UpbeatLabs_MCP39F521_FormattedCalibrationData {
	uint16_t calibrationRegisterDelimiter;
	uint16_t gainCurrentRMS;
	uint16_t gainVoltageRMS;
	uint16_t gainActivePower;
	uint16_t gainReactivePower;
	int32_t offsetCurrentRMS;
	int32_t offsetActivePower;
	int32_t offsetReactivePower;
	float dcOffsetCurrent;
	float phaseCompensation;
	uint16_t apparentPowerDivisor;
} UpbeatLabs_MCP39F521_FormattedCalibrationData;

typedef struct UpbeatLabs_MCP39F521_DesignConfigData {
	uint8_t rangeVoltage;
	uint8_t rangeCurrent;
	uint8_t rangePower;
	uint8_t rangeUnimplemented;
	uint32_t calibrationCurrent;
	uint16_t calibrationVoltage;
	uint32_t calibrationPowerActive;
	uint32_t calibrationPowerReactive;
	uint16_t lineFrequencyRef;
} UpbeatLabs_MCP39F521_DesignConfigData;

typedef struct UpbeatLabs_MCP39F521_EventFlagLimits {
  uint16_t voltageSagLimit;
  uint16_t voltageSurgeLimit;
  uint32_t overCurrentLimit;
  uint32_t overPowerLimit;
} UpbeatLabs_MCP39F521_EventFlagLimits;

class UpbeatLabs_MCP39F521
{
public:
  enum error_code {
    SUCCESS = 0,
    ERROR_INCORRECT_HEADER = 1,
    ERROR_CHECKSUM_FAIL = 2,
    ERROR_UNEXPECTED_RESPONSE = 3,
    ERROR_INSUFFICIENT_ARRAY_SIZE = 4,
    ERROR_CHECKSUM_MISMATCH = 5,
    ERROR_SET_VALUE_MISMATCH = 6
  };

  enum event_config {
    EVENT_OVERCUR_TST = 0,
    EVENT_OVERPOW_TST = 1,
    EVENT_VSAG_TST = 2,
    EVENT_VSUR_TST = 3,
    EVENT_OVERCUR_LA = 4,
    EVENT_OVERPOW_LA = 5,
    EVENT_VSAG_LA = 6,
    EVENT_VSUR_LA = 7,
    EVENT_VSAG_CL = 8,
    EVENT_VSUR_CL = 9,
    EVENT_OVERPOW_CL = 10,
    EVENT_OVERCUR_CL = 11,
    EVENT_MANUAL = 14,
    EVENT_VSAG_PIN = 16,
    EVENT_VSURGE_PIN = 17,
    EVENT_OVERCUR_PIN = 18,
    EVENT_OVERPOW_PIN = 19
  };

  enum system_status {
    SYSTEM_VSAG = 0,
    SYSTEM_VSURGE = 1,
    SYSTEM_OVERCUR = 2,
    SYSTEM_OVERPOW = 3,
    SYSTEM_SIGN_PA = 4,
    SYSTEM_SIGN_PR = 5,
    SYSTEM_EVENT = 10,
  };

  enum calibration_config {
    CALIBRATION_CONFIG_4A = 0,
    CALIBRATION_CONFIG_10A = 1, /* 30 ohm burden resistor x2 */
    CALIBRATION_CONFIG_15A = 2 /* 20 ohm burden resistor x2 */
  };

  UpbeatLabs_MCP39F521();

  void begin(uint8_t _addr = 0x74);

  int read(UpbeatLabs_MCP39F521_Data *output,
           UpbeatLabs_MCP39F521_AccumData *accumOutput);

  // Event control methods

  int readEventConfigRegister(uint32_t *value);

  int setEventConfigurationRegister(uint32_t value);

  int readEventFlagLimitRegisters(UpbeatLabs_MCP39F521_EventFlagLimits *output);

  int writeEventFlagLimitRegisters(UpbeatLabs_MCP39F521_EventFlagLimits *input);

  // EEPROM methods

  int bulkEraseEEPROM();
  int pageReadEEPROM(int pageNum, uint8_t *byteArray, int byteArraySize);
  int pageWriteEEPROM(int pageNum, uint8_t *byteArray, int byteArraySize);

  // Energy Accumulation methods

  int enableEnergyAccumulation(bool enable);
  int isEnergyAccumulationEnabled(bool *enabled);

  // Helper methods

  void convertRawData(UpbeatLabs_MCP39F521_Data *data,
                      UpbeatLabs_MCP39F521_FormattedData *fData);
  void convertRawAccumData(UpbeatLabs_MCP39F521_AccumData *data,
                           UpbeatLabs_MCP39F521_FormattedAccumData *fData);
  void convertRawCalibrationData(UpbeatLabs_MCP39F521_CalibrationData *data,
                       UpbeatLabs_MCP39F521_FormattedCalibrationData *fData);

  // START --- WARNING!!! WARNING!!! WARNING!!!
  // Advanced methods for calibration, etc
  // WARNING!!!! Use with extreme caution! These can render your Dr. Wattson
  // uncalibrated. Only use if you know what you are doing!

  int readCalibrationRegisters(UpbeatLabs_MCP39F521_CalibrationData *output);

  int writeGains(int gainCurrentRMS, int gainVoltageRMS,
                 int gainActivePower, int gainReactivePower);

  int readSystemConfigRegister(uint32_t *value);

  int setSystemConfigurationRegister(uint32_t value);

  int readAccumulationIntervalRegister(int *value);

  int setAccumulationIntervalRegister(int value);

  int readDesignConfigurationRegisters(UpbeatLabs_MCP39F521_DesignConfigData *output);

  int writeDesignConfigRegisters(UpbeatLabs_MCP39F521_DesignConfigData *data);

  int writePhaseCompensation(int16_t phaseCompensation);

  int readAndSetTemperature();
  int autoCalibrateGain();
  int autoCalibrateReactiveGain();
  int autoCalibrateFrequency();
  int calibratePhase(float pfExp);
  int saveToFlash();
  int factoryReset(); // This will revert the MCP39F521 to its default settings and
                      // remove any calibration data. Use with extreme caution!!!!
  int resetCalibration(calibration_config cc = CALIBRATION_CONFIG_4A );

  // END --- WARNING!!! WARNING!!! WARNING!!!


private:
  enum response_code {
    RESPONSE_ACK = 0x06,
    RESPONSE_NAK = 0x15,
    RESPONSE_CSFAIL = 0x51
  };

  enum command_code {
    COMMAND_REGISTER_READ_N_BYTES = 0x4e,
    COMMAND_REGISTER_WRITE_N_BYTES = 0x4d,
    COMMAND_SET_ADDRESS_POINTER = 0x41,
    COMMAND_SAVE_TO_FLASH = 0x53,
    COMMAND_PAGE_READ_EEPROM = 0x42,
    COMMAND_PAGE_WRITE_EEPROM = 0x50,
    COMMAND_BULK_ERASE_EEPROM = 0x4f,
    COMMAND_AUTO_CALIBRATE_GAIN = 0x5a,
    COMMAND_AUTO_CALIBRATE_REACTIVE_GAIN = 0x7a,
    COMMAND_AUTO_CALIBRATE_FREQUENCY = 0x76
  };

  int registerReadNBytes(int addressHigh, int addressLow, int numBytesToRead,
                         uint8_t *byteArray, int byteArraySize);
  int registerWriteNBytes(int addressHigh, int addressLow, int numBytes,
                          uint8_t *byteArray);
  int issueAckNackCommand(int command);
  int checkHeaderAndChecksum( int numBytesToRead, uint8_t *byteArray,
                              int byteArraySize);
  int checkHeader( int header);

  uint8_t i2c_addr;
  int _energy_accum_correction_factor;
};


#endif
