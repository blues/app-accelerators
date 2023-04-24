//*****************************************************************************
// UpbeatLabs_MCP39F521.cpp
//
// This is a library for the Upbeat Labs Dr. Wattson Energy Monitoring Board
// --> https://www.tindie.com/products/UpbeatLabs/dr-wattson-energy-monitoring-board-2/
//
//
// Written by Sridhar Rajagopal for Upbeat Labs LLC.
//
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

#include <string.h>
#include "UpbeatLabs_MCP39F521.h"
#include <stdint.h>
#include <math.h>

// Constructor

UpbeatLabs_MCP39F521::UpbeatLabs_MCP39F521(void) {

}

// Call begin to start using the module

// There is a bug in current MCP39F511/521 where energy accumulation
// values are off if the energy accumulation interval is
// anything but 2. This applies the workaround for that problem.
// To be removed for chips that have the issue fixed.

void UpbeatLabs_MCP39F521::begin(uint8_t _addr)
{
  i2c_addr = _addr;

  Wire.begin();

  int retVal = SUCCESS;
  bool enabled = false;
  retVal = isEnergyAccumulationEnabled(&enabled);

  if (retVal == SUCCESS && enabled) {
    // First, note the accumulation interval. If it is anything
    // other than the default (2), note the correction
    // factor that has to be applied to the energy
    // accumulation.
    int accumIntervalReg;

    retVal = readAccumulationIntervalRegister(&accumIntervalReg);

    _energy_accum_correction_factor = (accumIntervalReg - 2);
  }

}


// Get energy related data from the module
//
// Called after module "begin" has been called, it is used to get
// all the energy related data. Energy accumulator data is
// presented in a separate struct, as you need to enable the
// accumulator first before those fields are filled out.
// You can skip the accummulator data by passing in a NULL
// value (similarly, you can skip the regular data as well)
//
// Parameters:
// UpbeatLabs_MCP39F521_Data (output) - Metering data
// UpbeatLabs_MCP39F521_AccumData (output) - accumulator data for energy
//
// Notes:
// On Arduino cannot read more than 32 bytes on I2C
// Let's just stick to that limit!
// Splitting out activeEnergyImport, activeEnergyExport and
// reactiveEnergyImport, reactiveEnergyExport into two calls
// as the total is 32+3 = 35 bytes otherwise.

int UpbeatLabs_MCP39F521::read(UpbeatLabs_MCP39F521_Data *output,
                               UpbeatLabs_MCP39F521_AccumData *accumOutput)
{
  //uint8_t aucWriteDataBuf[8];
  uint8_t aucReadDataBuf[35];
  //int i;
  int retval = SUCCESS;

  if (output) {

    retval = registerReadNBytes(0x00, 0x02, 28, aucReadDataBuf, 35);

    if (retval != SUCCESS) {
      return retval;
    } else {

      /* System status */
      output->systemStatus = ((aucReadDataBuf[3] << 8) | aucReadDataBuf[2]);
      output->systemVersion = ((aucReadDataBuf[5] << 8) | aucReadDataBuf[4]);
      output->voltageRMS = ((aucReadDataBuf[7] << 8) | aucReadDataBuf[6]);
      output->lineFrequency = ((aucReadDataBuf[9] << 8) | aucReadDataBuf[8]);
      output->analogInputVoltage = ((aucReadDataBuf[11] << 8) | aucReadDataBuf[10]);
      output->powerFactor = (((signed char)aucReadDataBuf[13] << 8) +
                             (unsigned char)aucReadDataBuf[12]);

      output->currentRMS =  ((uint32_t)(aucReadDataBuf[17]) << 24 |
                             (uint32_t)(aucReadDataBuf[16]) << 16 |
                             (uint32_t)(aucReadDataBuf[15]) << 8 |
                             aucReadDataBuf[14]);
      output->activePower =  ((uint32_t)(aucReadDataBuf[21]) << 24 |
                              (uint32_t)(aucReadDataBuf[20]) << 16 |
                              (uint32_t)(aucReadDataBuf[19]) << 8 |
                              aucReadDataBuf[18]);
      output->reactivePower =  ((uint32_t)(aucReadDataBuf[25]) << 24 |
                                (uint32_t)(aucReadDataBuf[24]) << 16 |
                                (uint32_t)(aucReadDataBuf[23]) << 8 |
                                aucReadDataBuf[22]);
      output->apparentPower =  ((uint32_t)(aucReadDataBuf[29]) << 24 |
                                (uint32_t)(aucReadDataBuf[28]) << 16 |
                                (uint32_t)(aucReadDataBuf[27]) << 8 |
                                aucReadDataBuf[26]);
    }

    delay(100);

  }

  if (accumOutput) {

    retval = registerReadNBytes(0x00, 0x1e, 16, aucReadDataBuf, 19);

    if (retval != SUCCESS) {
      return retval;
    } else {

      if (_energy_accum_correction_factor == -1)  {
        accumOutput->activeEnergyImport =  (((uint64_t)aucReadDataBuf[9]) << 56 |
                                            ((uint64_t)aucReadDataBuf[8]) << 48 |
                                            ((uint64_t)aucReadDataBuf[7]) << 40 |
                                            ((uint64_t)aucReadDataBuf[6]) << 32 |
                                            (uint64_t)(aucReadDataBuf[5]) << 24 |
                                            (uint64_t)(aucReadDataBuf[4]) << 16 |
                                            (uint64_t)(aucReadDataBuf[3]) << 8 |
                                            (uint64_t)aucReadDataBuf[2]) /
          2;
        accumOutput->activeEnergyExport =  (((uint64_t)aucReadDataBuf[17]) << 56 |
                                            ((uint64_t)aucReadDataBuf[16]) << 48 |
                                            ((uint64_t)aucReadDataBuf[15]) << 40 |
                                            ((uint64_t)aucReadDataBuf[14]) << 32 |
                                            (uint64_t)(aucReadDataBuf[13]) << 24 |
                                            (uint64_t)(aucReadDataBuf[12]) << 16 |
                                            (uint64_t)(aucReadDataBuf[11]) << 8 |
                                            (uint64_t)aucReadDataBuf[10]) /
          2;
      } else {
        accumOutput->activeEnergyImport =  (((uint64_t)aucReadDataBuf[9]) << 56 |
                                            ((uint64_t)aucReadDataBuf[8]) << 48 |
                                            ((uint64_t)aucReadDataBuf[7]) << 40 |
                                            ((uint64_t)aucReadDataBuf[6]) << 32 |
                                            (uint64_t)(aucReadDataBuf[5]) << 24 |
                                            (uint64_t)(aucReadDataBuf[4]) << 16 |
                                            (uint64_t)(aucReadDataBuf[3]) << 8 |
                                            (uint64_t)aucReadDataBuf[2]) *
          ( 1 << _energy_accum_correction_factor);
        accumOutput->activeEnergyExport =  (((uint64_t)aucReadDataBuf[17]) << 56 |
                                            ((uint64_t)aucReadDataBuf[16]) << 48 |
                                            ((uint64_t)aucReadDataBuf[15]) << 40 |
                                            ((uint64_t)aucReadDataBuf[14]) << 32 |
                                            (uint64_t)(aucReadDataBuf[13]) << 24 |
                                            (uint64_t)(aucReadDataBuf[12]) << 16 |
                                            (uint64_t)(aucReadDataBuf[11]) << 8 |
                                            (uint64_t)aucReadDataBuf[10]) *
          (1 << _energy_accum_correction_factor);
      }

    }

    delay(100);

    retval = registerReadNBytes(0x00, 0x2e, 16, aucReadDataBuf, 19);

    if (retval != SUCCESS) {
      return retval;
    } else {

      if (_energy_accum_correction_factor == -1)  {
        accumOutput->reactiveEnergyImport =  (((uint64_t)aucReadDataBuf[9]) << 56 |
                                              ((uint64_t)aucReadDataBuf[8]) << 48 |
                                              ((uint64_t)aucReadDataBuf[7]) << 40 |
                                              ((uint64_t)aucReadDataBuf[6]) << 32 |
                                              (uint64_t)(aucReadDataBuf[5]) << 24 |
                                              (uint64_t)(aucReadDataBuf[4]) << 16 |
                                              (uint64_t)(aucReadDataBuf[3]) << 8 |
                                              (uint64_t)aucReadDataBuf[2]) /
          2;
        accumOutput->reactiveEnergyExport =  (((uint64_t)aucReadDataBuf[17]) << 56 |
                                              ((uint64_t)aucReadDataBuf[16]) << 48 |
                                              ((uint64_t)aucReadDataBuf[15]) << 40 |
                                              ((uint64_t)aucReadDataBuf[14]) << 32 |
                                              (uint64_t)(aucReadDataBuf[13]) << 24 |
                                              (uint64_t)(aucReadDataBuf[12]) << 16 |
                                              (uint64_t)(aucReadDataBuf[11]) << 8 |
                                              (uint64_t)aucReadDataBuf[10]) /
          2;

      } else {
        accumOutput->reactiveEnergyImport =  (((uint64_t)aucReadDataBuf[9]) << 56 |
                                              ((uint64_t)aucReadDataBuf[8]) << 48 |
                                              ((uint64_t)aucReadDataBuf[7]) << 40 |
                                              ((uint64_t)aucReadDataBuf[6]) << 32 |
                                              (uint64_t)(aucReadDataBuf[5]) << 24 |
                                              (uint64_t)(aucReadDataBuf[4]) << 16 |
                                              (uint64_t)(aucReadDataBuf[3]) << 8 |
                                              (uint64_t)aucReadDataBuf[2]) *
          (1 << _energy_accum_correction_factor);
        accumOutput->reactiveEnergyExport =  (((uint64_t)aucReadDataBuf[17]) << 56 |
                                              ((uint64_t)aucReadDataBuf[16]) << 48 |
                                              ((uint64_t)aucReadDataBuf[15]) << 40 |
                                              ((uint64_t)aucReadDataBuf[14]) << 32 |
                                              (uint64_t)(aucReadDataBuf[13]) << 24 |
                                              (uint64_t)(aucReadDataBuf[12]) << 16 |
                                              (uint64_t)(aucReadDataBuf[11]) << 8 |
                                              (uint64_t)aucReadDataBuf[10]) *
          (1 << _energy_accum_correction_factor);
      }

    }
  }

  return SUCCESS;
}

// Reads the event configuration and returns the 32-bit bit map.
// Use the event_config enum to set bits of interest, without
// worrying about the bit position and structure of the
// event config register
//
// For example, bitSet(eventConfigRegisterValue, EVENT_VSAG_PIN)
// to turn on the event notification for VSAG events

int UpbeatLabs_MCP39F521::readEventConfigRegister(uint32_t *value)
{
  int retVal = 0;
  uint8_t readArray[7];
  //int readValue;

  retVal = registerReadNBytes(0x00, 0x7e, 4, readArray, 7);
  if (retVal != SUCCESS) {
    return retVal;
  } else {
    *value = ((uint32_t)(readArray[5]) << 24 | (uint32_t)(readArray[4]) << 16 |
              (uint32_t)(readArray[3] << 8) | readArray[2]);
  }

  return SUCCESS;
}

// Set the event configuration register to the appropriate value
//
// First, read the existing register value. Then, set (or clear) appropriate
// bits using the event_config enum for assitance. Lastly, set the
// new value back in the register.
//
// For example, bitSet(eventConfigRegisterValue, EVENT_VSAG_PIN)
// to turn on the event notification for VSAG events

int UpbeatLabs_MCP39F521::setEventConfigurationRegister(uint32_t value)
{
  int retVal = 0;
  uint8_t byteArray[4];
  uint8_t readArray[7];
  uint32_t readArray_32[7];
  uint32_t readValue;
  byteArray[0] = value & 0xFF;
  byteArray[1] = (value >> 8) & 0xFF;
  byteArray[2] = (value >> 16) & 0xFF;
  byteArray[3] = (value >> 24) & 0xFF;

  retVal = registerReadNBytes(0x00, 0x7e, 4, readArray, 7);

  for(auto i=0;i<7;i++){
    readArray_32[i]=readArray[i];
  }

  if (retVal != SUCCESS) {
    return retVal;
  } else {
    readValue = ((readArray_32[5] << 24) | (readArray_32[4] << 16) |
                 (readArray_32[3] << 8) | readArray_32[2]);
  }

  retVal = registerWriteNBytes(0x00, 0x7e, 4, byteArray);
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = registerReadNBytes(0x00, 0x7e, 4, readArray, 7);
  readValue = ((readArray_32[5] << 24) | (readArray_32[4] << 16) |
               (readArray_32[3] << 8) | readArray_32[2]);

  if (readValue != value) {
    return ERROR_SET_VALUE_MISMATCH;
  }

  return SUCCESS;
}

// Read the event flag limits that have been set for the various events.
// For example, the voltage sag limit sets the voltage value below which
// the VSAG event is triggered
//
// See UpbeatLabs_MCP39F521_EventFlagLimits for more information about
// various limits

int UpbeatLabs_MCP39F521::readEventFlagLimitRegisters(UpbeatLabs_MCP39F521_EventFlagLimits *output)
{
  //uint8_t aucWriteDataBuf[8];
  uint8_t aucReadDataBuf[15];
  //int i;
  int retval = 0;

  retval = registerReadNBytes(0x00, 0xA0, 12, aucReadDataBuf, 15);

  if (retval != SUCCESS) {
    return retval;
  } else {

    /* System status */
    output->voltageSagLimit = ((aucReadDataBuf[3] << 8) | aucReadDataBuf[2]);
    output->voltageSurgeLimit = ((aucReadDataBuf[5] << 8) | aucReadDataBuf[4]);
    output->overCurrentLimit = ((uint32_t)(aucReadDataBuf[9]) << 24 |
                                (uint32_t)(aucReadDataBuf[8]) << 16 |
                                (uint32_t)(aucReadDataBuf[7] << 8) |
                                aucReadDataBuf[6]);
    output->overPowerLimit = ((uint32_t)(aucReadDataBuf[13]) << 24 |
                              (uint32_t)(aucReadDataBuf[12]) << 16 |
                              (uint32_t)(aucReadDataBuf[11] << 8) |
                              aucReadDataBuf[10]);
  }

  return SUCCESS;

}

// Write the event flag limits for the various events.
// For example, the voltage sag limit sets the voltage value below which
// the VSAG event is triggered
//
// See UpbeatLabs_MCP39F521_EventFlagLimits for more information about
// various limits

int UpbeatLabs_MCP39F521::writeEventFlagLimitRegisters(UpbeatLabs_MCP39F521_EventFlagLimits *input)
{
  int retVal = 0;
  UpbeatLabs_MCP39F521_EventFlagLimits eventFlagLimitsData;

  // Convert data structure to byte stream
  uint8_t byteArray[12];

  // voltage sag limit
  byteArray[0] = input->voltageSagLimit & 0xFF;
  byteArray[1] = (input->voltageSagLimit >> 8) & 0xFF;

  // voltage surge limit
  byteArray[2] = input->voltageSurgeLimit & 0xFF;
  byteArray[3] = (input->voltageSurgeLimit >> 8) & 0xFF;

  // over current
  byteArray[4] = input->overCurrentLimit & 0xFF;
  byteArray[5] = (input->overCurrentLimit >> 8) & 0xFF;
  byteArray[6] = (input->overCurrentLimit >> 16) & 0xFF;
  byteArray[7] = (input->overCurrentLimit >> 24) & 0xFF;

  // over power
  byteArray[8] = input->overPowerLimit & 0xFF;
  byteArray[9] = (input->overPowerLimit >> 8) & 0xFF;
  byteArray[10] = (input->overPowerLimit >> 16) & 0xFF;
  byteArray[11] = (input->overPowerLimit >> 24) & 0xFF;

  // write register
  retVal = registerWriteNBytes(0x00, 0xA0, 12, byteArray);

  if (retVal != SUCCESS) {
    return retVal;
  }

  // Read the values to verify write
  retVal = readEventFlagLimitRegisters(&eventFlagLimitsData);
  if (retVal != SUCCESS) {
    return retVal;
  }

  // Verify read values with input values
  if (eventFlagLimitsData.voltageSagLimit != input->voltageSagLimit ||
      eventFlagLimitsData.voltageSurgeLimit != input->voltageSurgeLimit ||
      eventFlagLimitsData.overCurrentLimit != input->overCurrentLimit ||
      eventFlagLimitsData.overPowerLimit != input->overPowerLimit) {
    return ERROR_SET_VALUE_MISMATCH;
  }

  return SUCCESS;
}


// EEPROM methods --->


// Bulk erase all pages of the EEPROM memory

int UpbeatLabs_MCP39F521::bulkEraseEEPROM()
{
  return issueAckNackCommand(COMMAND_BULK_ERASE_EEPROM);
}

// Read a single page of memory from the EEPROM. Returns 16 bytes of data
// Byte array passed in has to be at least 16 bytes, as a result.
// Page numbers go from 0 to 31

int UpbeatLabs_MCP39F521::pageReadEEPROM(int pageNum, uint8_t *byteArray,
                                         int byteArraySize)
{
  uint8_t aucWriteDataBuf[5];
  uint8_t aucReadDataBuf[19];
  int i;
  uint32_t checksumTotal = 0;

  if (byteArraySize < 16) {
    return ERROR_INSUFFICIENT_ARRAY_SIZE;
  }

  aucWriteDataBuf[0] = 0xa5; // Header
  aucWriteDataBuf[1] = 0x05; // Num bytes
  aucWriteDataBuf[2] = COMMAND_PAGE_READ_EEPROM; // Command - set address pointer
  aucWriteDataBuf[3] = pageNum;
  aucWriteDataBuf[4] = 0; // Checksum - computed below
  for(i=0; i<4;i++) {
    checksumTotal += aucWriteDataBuf[i];
  }
  aucWriteDataBuf[4] = checksumTotal % 256;

  Wire.beginTransmission(i2c_addr);
  for(i=0; i< 5; i++) {
    Wire.write(aucWriteDataBuf[i]);
  }
  Wire.endTransmission();

  delay(100);

  //
  // Read the specified length of data - ACK, Num Bytes, EEPROM Page Data, Checksum
  // -> 1 + 1 + 16 + 1 = 19 bytes of data
  //

  Wire.requestFrom(i2c_addr,  (uint8_t)19);
  int requestDataLength = Wire.available();
  if (requestDataLength==19) {
    aucReadDataBuf[0] = Wire.read();
    aucReadDataBuf[1] = Wire.read();
    for (i = 2; i < 18 ; i++) {
      uint8_t data = Wire.read();
      aucReadDataBuf[i] = data;
      byteArray[i-2] = data;
    }
    aucReadDataBuf[i] = Wire.read(); // Read final data, the checksum

    // Check header and checksum
    return checkHeaderAndChecksum(16, aucReadDataBuf, 19);

  } else {
    // Unexpected. Handle error
    return ERROR_UNEXPECTED_RESPONSE;
  }

  return 1;
}

// Write a single page of memory to the EEPROM. Writes 16 bytes of data
// Byte array passed in has to be at least 16 bytes, as a result.
// Page numbers go from 0 to 31

int UpbeatLabs_MCP39F521::pageWriteEEPROM(int pageNum, uint8_t *byteArray,
                                          int byteArraySize)
{
  uint8_t aucWriteDataBuf[21];
  uint8_t aucReadDataBuf[1];
  int i;
  uint32_t checksumTotal = 0;

  if (byteArraySize != 16) {
    return ERROR_INSUFFICIENT_ARRAY_SIZE;
  }

  aucWriteDataBuf[0] = 0xa5; // Header
  aucWriteDataBuf[1] = 21; // Num bytes in frame
  aucWriteDataBuf[2] = COMMAND_PAGE_WRITE_EEPROM; // Command - set address pointer
  aucWriteDataBuf[3] = pageNum; // Page Number

  // Data here ...
  for(i=4; i<4+16; i++) {
    aucWriteDataBuf[i] = byteArray[i-4];
  }
  // i should have been incremented so is the last element here
  aucWriteDataBuf[i] = 0; // Checksum - computed below
  for(i=0; i<20;i++) {
    checksumTotal += aucWriteDataBuf[i];
  }

  // i should have been incremented so is the last element here
  aucWriteDataBuf[i] = checksumTotal % 256;

  Wire.beginTransmission(i2c_addr);
  for(i=0; i< 21; i++) {
    Wire.write(aucWriteDataBuf[i]);
  }
  Wire.endTransmission();

  delay(100);

  Wire.requestFrom(i2c_addr, (uint8_t)1);
  aucReadDataBuf[0] = Wire.read();

  //int error = SUCCESS;
  uint8_t header = aucReadDataBuf[0];

  return checkHeader(header);

}
// <--- EEPROM methods



// Energy Accumulation methods --->

// This method is used to turn on/off energy accumulation.
// When it is turned on, the data read from the module
// in UpbeatLabs_MCP39F521_AccumData represents the
// accumulated energy data over the no load threshold
// (defaults to 1w). Therefore any energy over 1w
// gets accumulated over time.

// There is a bug in current MCP39F511/521 where energy accumulation
// values are off if the energy accumulation interval is
// anything but 2. This applies the workaround for that problem.
// To be removed for chips that have the issue fixed.

int UpbeatLabs_MCP39F521::enableEnergyAccumulation(bool enable)
{
  int retVal;
  uint8_t byteArray[2];

  // First, note the accumulation interval. If it is anything
  // other than the default (2), note the correction
  // factor that has to be applied to the energy
  // accumulation.
  int accumIntervalReg;

  retVal = readAccumulationIntervalRegister(&accumIntervalReg);

  _energy_accum_correction_factor = (accumIntervalReg - 2);

  byteArray[0] = enable;
  byteArray[1] = 0;

  // write register
  retVal = registerWriteNBytes(0x00, 0xDC, 2, byteArray);

  if (retVal != SUCCESS) {
    return retVal;
  }
  return SUCCESS;

}

int UpbeatLabs_MCP39F521::isEnergyAccumulationEnabled(bool *enabled)
{
  int retVal;
  uint8_t readArray[5];
  //int readValue;

  retVal = registerReadNBytes(0x00, 0xDC, 2, readArray, 5);

  if (retVal != SUCCESS) {
    return retVal;
  } else {
    *enabled = readArray[2];
  }

  return SUCCESS;

}

// <--- Energy Accumulation methods


// Helper methods --->

// The data returned by the MCP39F521 is all integer. If you want the results in more human-readable format,
// use this method to convert from UpbeatLabs_MCP39F521_Data to UpbeatLabs_MCP39F521_FormattedData.

void UpbeatLabs_MCP39F521::convertRawData(UpbeatLabs_MCP39F521_Data *data,
                                          UpbeatLabs_MCP39F521_FormattedData *fData)
{
  fData->voltageRMS = data->voltageRMS/10.0f;
  fData->currentRMS = data->currentRMS/10000.0f;
  fData->lineFrequency = data->lineFrequency/1000.0f;
  // Analog Input Voltage represents ADC output for 10 bit ADC
  // By trial, it's been found that it has a ref voltage of 3.3v
  // So the register value/1023 * 3.3v will give the analog input voltage in volts.
  // analogInputVoltage = RegData/1023.0 * 3.3;
  // Do this on the application side?
  fData->analogInputVoltage = data->analogInputVoltage/1023.0f*3.3;

  float f;
  unsigned char ch;

  f = ((data->powerFactor & 0x8000)>>15) * -1.0;

  for(ch=14; ch > 3; ch--)
    f += ((data->powerFactor & (1 << ch)) >> ch) * 1.0 / (1 << (15 - ch));

  fData->powerFactor = f;

  fData->activePower = data->activePower/100.0f;
  fData->reactivePower = data->reactivePower/100.0f;
  fData->apparentPower = data->apparentPower/100.0f;
}

// The data returned by the MCP39F521 is all integer. If you want the results in more human-readable format,
// use this method to convert from UpbeatLabs_MCP39F521_AccumData to UpbeatLabs_MCP39F521_FormattedAccumData.

void UpbeatLabs_MCP39F521::convertRawAccumData(UpbeatLabs_MCP39F521_AccumData *data,
                                               UpbeatLabs_MCP39F521_FormattedAccumData *fData)
{
  fData->activeEnergyImport = data->activeEnergyImport/1000.0f;
  fData->activeEnergyExport = data->activeEnergyExport/1000.0f;
  fData->reactiveEnergyImport = data->reactiveEnergyImport/1000.0f;
  fData->reactiveEnergyExport = data->reactiveEnergyExport/1000.0f;
}

// The data returned by the MCP39F521 is all integer. If you want the results in more human-readable format,
// use this method to convert from UpbeatLabs_MCP39F521_CalibrationData to UpbeatLabs_MCP39F521_FormattedCalibrationData.

void UpbeatLabs_MCP39F521::convertRawCalibrationData(UpbeatLabs_MCP39F521_CalibrationData *data,
                                                     UpbeatLabs_MCP39F521_FormattedCalibrationData *fData)
{
  fData->calibrationRegisterDelimiter = data->calibrationRegisterDelimiter;
  fData->gainCurrentRMS = data->gainCurrentRMS;
  fData->gainVoltageRMS = data->gainVoltageRMS;
  fData->gainActivePower = data->gainActivePower;
  fData->gainReactivePower = data->gainReactivePower;
  fData->offsetCurrentRMS = data->offsetCurrentRMS;
  fData->offsetActivePower = data->offsetActivePower;
  fData->offsetReactivePower = data->offsetReactivePower;

  float f;
  unsigned char ch;

  f = ((data->dcOffsetCurrent & 0x8000)>>15) * -1.0;
  for(ch=14; ch > 3; ch--)
    f += ((data->dcOffsetCurrent & (1 << ch)) >> ch) * 1.0 / (1 << (15 - ch));
  fData->dcOffsetCurrent = f;

  f = ((data->phaseCompensation & 0x8000)>>15) * -1.0;
  for(ch=14; ch > 3; ch--)
    f += ((data->phaseCompensation & (1 << ch)) >> ch) * 1.0 / (1 << (15 - ch));
  fData->phaseCompensation = f;

  fData->apparentPowerDivisor = data->apparentPowerDivisor;
}


// <--- Helper methods


// START --- WARNING!!! WARNING!!! WARNING!!!
// Advanced methods for calibration, etc
// WARNING!!!! Use with extreme caution! These can render your Dr. Wattson uncalibrated
// Only use if you know what you are doing!



// Read the contents of the calibration registers. Data is returned formatted in
// UpbeatLabs_MCP39F521_CalibrationData. See struct for more details.

int UpbeatLabs_MCP39F521::readCalibrationRegisters(UpbeatLabs_MCP39F521_CalibrationData *output)
{
  //uint8_t aucWriteDataBuf[8];
  uint8_t aucReadDataBuf[35];
  //int i;
  int retval = 0;

  retval = registerReadNBytes(0x00, 0x5e, 28, aucReadDataBuf, 35);

  if (retval != SUCCESS) {
    return retval;
  } else {

    /* System status */
    output->calibrationRegisterDelimiter = ((aucReadDataBuf[3] << 8) | aucReadDataBuf[2]);
    output->gainCurrentRMS = ((aucReadDataBuf[5] << 8) | aucReadDataBuf[4]);
    output->gainVoltageRMS = ((aucReadDataBuf[7] << 8) | aucReadDataBuf[6]);
    output->gainActivePower = ((aucReadDataBuf[9] << 8) | aucReadDataBuf[8]);
    output->gainReactivePower = ((aucReadDataBuf[11] << 8) | aucReadDataBuf[10]);
    output->offsetCurrentRMS = (((int32_t)aucReadDataBuf[15] << 24) +
                                ((uint32_t)aucReadDataBuf[14] << 16) +
                                ((uint32_t)aucReadDataBuf[13] << 8) +
                                (uint32_t)aucReadDataBuf[12] );
    output->offsetActivePower = (((int32_t)aucReadDataBuf[19] << 24) +
                                 ((uint32_t)aucReadDataBuf[18] << 16) +
                                 ((uint32_t)aucReadDataBuf[17] << 8) +
                                 (uint32_t)aucReadDataBuf[16] );
    output->offsetReactivePower = (((int32_t)aucReadDataBuf[23] << 24) +
                                   ((uint32_t)aucReadDataBuf[22] << 16) +
                                   ((uint32_t)aucReadDataBuf[21] << 8) +
                                   (uint32_t)aucReadDataBuf[20] );
    output->dcOffsetCurrent = (((signed char)aucReadDataBuf[25] << 8) +
                               (unsigned char)aucReadDataBuf[24]);
    output->phaseCompensation = (((signed char)aucReadDataBuf[27] << 8) +
                                 (unsigned char)aucReadDataBuf[26]);
    output->apparentPowerDivisor = ((aucReadDataBuf[29] << 8) |
                                    aucReadDataBuf[28]);

  }

  return SUCCESS;

}

// This method writes the current, voltage, active power and reactive power gains directly to the
// MCP39F521 registers. Use this if you know what the appropriate gains are to be for your
// particular metering range and design. Typically, these are not to be changed unless you
// are performing your own calibration, and even so, it is better to use the
// auto-calibration methods instead. This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::writeGains(int gainCurrentRMS, int gainVoltageRMS,
                                     int gainActivePower, int gainReactivePower)
{
  int retVal;
  uint8_t byteArray[8];
  //UpbeatLabs_MCP39F521_CalibrationData data;


  byteArray[0] = gainCurrentRMS & 0xFF;
  byteArray[1] = (gainCurrentRMS >> 8) & 0xFF;

  byteArray[2] = gainVoltageRMS & 0xFF;
  byteArray[3] = (gainVoltageRMS >> 8) & 0xFF;

  byteArray[4] = gainActivePower & 0xFF;
  byteArray[5] = (gainActivePower >> 8) & 0xFF;

  byteArray[6] = gainReactivePower & 0xFF;
  byteArray[7] = (gainReactivePower >> 8) & 0xFF;

  // write register
  retVal = registerWriteNBytes(0x00, 0x60, 8, byteArray);

  if (retVal != SUCCESS) {
    return retVal;
  }
  return SUCCESS;

}

// Read the system config register, which is a 32-bit bit map.
// The system config register is used to set setting like
// PGA gains for current and voltage channels, etc
// You will typically not be changing these unless you are
// performing a calibration.

int UpbeatLabs_MCP39F521::readSystemConfigRegister(uint32_t *value)
{
  int retVal = 0;
  uint8_t readArray[7];
  //int readValue;

  retVal = registerReadNBytes(0x00, 0x7a, 4, readArray, 7);
  if (retVal != SUCCESS) {
    return retVal;
  } else {
    *value = ((uint32_t)(readArray[5]) << 24 | (uint32_t)(readArray[4]) << 16 |
              (uint32_t)(readArray[3]) << 8 | readArray[2]);
  }

  return SUCCESS;
}

// Set the system config register, which is a 32-bit bit map.
// The system config register is used to set setting like
// PGA gains for current and voltage channels, etc
// You will typically not be changing these unless you are
// performing a calibration. Do not use unless you know what
// you are doing! This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::setSystemConfigurationRegister(uint32_t value)
{
  int retVal = 0;
  uint8_t byteArray[4];
  uint8_t readArray[7];
  uint32_t readValue;
  byteArray[0] = value & 0xFF;
  byteArray[1] = (value >> 8) & 0xFF;
  byteArray[2] = (value >> 16) & 0xFF;
  byteArray[3] = (value >> 24) & 0xFF;

  retVal = registerWriteNBytes(0x00, 0x7a, 4, byteArray);
  if (retVal != SUCCESS) {
    return retVal;
  }

  delay(500);

  retVal = registerReadNBytes(0x00, 0x7a, 4, readArray, 7);
  readValue = ((uint32_t)(readArray[5]) << 24 | (uint32_t)(readArray[4]) << 16 |
               (uint32_t)(readArray[3]) << 8 | readArray[2]);

  if (readValue != value) {
    return ERROR_SET_VALUE_MISMATCH;
  }

  return SUCCESS;
}

// Read the accumlation interval register, which represents N in 2^N
// number of line cycles to be used for a single computation.
// You will not be modifying this unless you are performing a
// calibration.

int UpbeatLabs_MCP39F521::readAccumulationIntervalRegister(int *value)
{
  int retVal = 0;
  uint8_t readArray[5];
  //int readValue;

  retVal = registerReadNBytes(0x00, 0x9e, 2, readArray, 5);
  if (retVal != SUCCESS) {
    return retVal;
  } else {
    *value = ((readArray[3] << 8) | readArray[2]);
  }

  return SUCCESS;
}

// Set the accumlation interval register, which represents N in 2^N
// number of line cycles to be used for a single computation.
// You will not be modifying this unless you are performing a
// calibration. Use with caution!!

int UpbeatLabs_MCP39F521::setAccumulationIntervalRegister(int value)
{
  int retVal = 0;
  uint8_t byteArray[2];
  uint8_t readArray[5];
  int readValue;
  byteArray[0] = value & 0xFF;
  byteArray[1] = (value >> 8) & 0xFF;

  retVal = registerReadNBytes(0x00, 0x9e, 2, readArray, 5);
  if (retVal != SUCCESS) {
    return retVal;
  } else {
    readValue = ((readArray[3] << 8) | readArray[2]);
  }

  retVal = registerWriteNBytes(0x00, 0x9e, 2, byteArray);
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = registerReadNBytes(0x00, 0x9e, 2, readArray, 5);
  readValue = ((readArray[3] << 8) | readArray[2]);

  if (readValue != value) {
    return ERROR_SET_VALUE_MISMATCH;
  }

  return SUCCESS;
}

// Read the design config registers into the UpbeatLabs_MCP39F521_DesignConfigData struct. See
// struct for more details. These are used to set the appropriate calibration values for
// calibrating your module.

int UpbeatLabs_MCP39F521::readDesignConfigurationRegisters(UpbeatLabs_MCP39F521_DesignConfigData *output)
{
  //uint8_t aucWriteDataBuf[8];
  uint8_t aucReadDataBuf[35];
  //int i;
  int retval = 0;

  retval = registerReadNBytes(0x00, 0x82, 20, aucReadDataBuf, 35);

  if (retval != SUCCESS) {
    return retval;
  } else {

    /* System status */
    output->rangeVoltage = aucReadDataBuf[2];
    output->rangeCurrent = aucReadDataBuf[3];
    output->rangePower = aucReadDataBuf[4];
    output->rangeUnimplemented = aucReadDataBuf[5];

    uint32_t aucReadDataBuf_32[35];

    for(auto i=0;i<35;i++){
      aucReadDataBuf_32[i]=aucReadDataBuf[i];
    }

    output->calibrationCurrent =  ((aucReadDataBuf_32[9] << 24) |
                                   (aucReadDataBuf_32[8] << 16) |
                                   (aucReadDataBuf_32[7] << 8) |
                                   aucReadDataBuf_32[6]);

    output->calibrationVoltage = ((aucReadDataBuf_32[11] << 8) | aucReadDataBuf_32[10]);
    output->calibrationPowerActive =  ((aucReadDataBuf_32[15] << 24) |
                                       (aucReadDataBuf_32[14] << 16) |
                                       (aucReadDataBuf_32[13] << 8) |
                                       aucReadDataBuf_32[12]);
    output->calibrationPowerReactive =  ((aucReadDataBuf_32[19] << 24) |
                                         (aucReadDataBuf_32[18] << 16) |
                                         (aucReadDataBuf_32[17] << 8) |
                                         aucReadDataBuf_32[16]);
    output->lineFrequencyRef = ((aucReadDataBuf_32[21] << 8) | aucReadDataBuf_32[20]);

  }

  return SUCCESS;

}


// Write the design config registers. See UpbeatLabs_MCP39F521_DesignConfigData
// struct for more details. These are used to set the appropriate calibration values for
// calibrating your module. Use this method only if you know what you are doing!
// This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::writeDesignConfigRegisters(UpbeatLabs_MCP39F521_DesignConfigData *data)
{
  int retVal = 0;
  UpbeatLabs_MCP39F521_DesignConfigData designConfigData;

  // Convert data structure to byte stream
  uint8_t byteArray[20];

  // range
  byteArray[0] = data->rangeVoltage;
  byteArray[1] = data->rangeCurrent;
  byteArray[2] = data->rangePower;
  byteArray[3] = data->rangeUnimplemented;

  // calibration current
  byteArray[4] = data->calibrationCurrent & 0xFF;
  byteArray[5] = (data->calibrationCurrent >> 8) & 0xFF;
  byteArray[6] = (data->calibrationCurrent >> 16) & 0xFF;
  byteArray[7] = (data->calibrationCurrent >> 24) & 0xFF;

  // calibration voltage
  byteArray[8] = data->calibrationVoltage & 0xFF;
  byteArray[9] = (data->calibrationVoltage >> 8) & 0xFF;

  // calibration power active
  byteArray[10] = data->calibrationPowerActive & 0xFF;
  byteArray[11] = (data->calibrationPowerActive >> 8) & 0xFF;
  byteArray[12] = (data->calibrationPowerActive >> 16) & 0xFF;
  byteArray[13] = (data->calibrationPowerActive >> 24) & 0xFF;

  // calibration power reactive
  byteArray[14] = data->calibrationPowerReactive & 0xFF;
  byteArray[15] = (data->calibrationPowerReactive >> 8) & 0xFF;
  byteArray[16] = (data->calibrationPowerReactive >> 16) & 0xFF;
  byteArray[17] = (data->calibrationPowerReactive >> 24) & 0xFF;

  // line frequency ref
  byteArray[18] = data->lineFrequencyRef & 0xFF;
  byteArray[19] = (data->lineFrequencyRef >> 8) & 0xFF;


  // write register
  retVal = registerWriteNBytes(0x00, 0x82, 20, byteArray);

  if (retVal != SUCCESS) {
    return retVal;
  }

  delay(500);

  // Read the values to verify write
  retVal = readDesignConfigurationRegisters(&designConfigData);
  if (retVal != SUCCESS) {
    return retVal;
  }

  // Verify read values with input values
  if (designConfigData.rangeVoltage != data->rangeVoltage ||
      designConfigData.rangeCurrent != data->rangeCurrent ||
      designConfigData.rangePower != data->rangePower ||
      designConfigData.calibrationCurrent != data->calibrationCurrent ||
      designConfigData.calibrationVoltage != data->calibrationVoltage ||
      designConfigData.calibrationPowerActive != data->calibrationPowerActive ||
      designConfigData.calibrationPowerReactive != data->calibrationPowerReactive ||
      designConfigData.lineFrequencyRef != data->lineFrequencyRef) {
    return ERROR_SET_VALUE_MISMATCH;
  }

  return SUCCESS;
}

// Write the phase compensation register. This will not be required unless
// you are manually writing calibration values yourself. Use with caution!

int UpbeatLabs_MCP39F521::writePhaseCompensation(int16_t phaseCompensation)
{
  int retVal;
  uint8_t byteArray[2];

  // Calibrating phase
  byteArray[0] = (uint8_t)phaseCompensation;
  byteArray[1] = 0;

  retVal = registerWriteNBytes(0x00, 0x76, 2, byteArray);

  if (retVal != SUCCESS) {
    return retVal;
  }
  return SUCCESS;
}

// Read and set the ambient reference temperature when
// calibrating. This is used during calibration as one
// of the steps. Use with caution!

int UpbeatLabs_MCP39F521::readAndSetTemperature()
{
  int retVal = 0;
  //int16_t phaseComp, phaseCompNew;
  uint8_t byteArray[5];

  retVal = registerReadNBytes(0x00, 0x0a, 2, byteArray, 5);

  if (retVal != SUCCESS) {
    return retVal;
  } else {

    uint8_t bytesWrite[2];
    bytesWrite[0] = byteArray[2];
    bytesWrite[1] = byteArray[3];

    return registerWriteNBytes(0x00, 0xcc, 2, bytesWrite);
  }
  return SUCCESS;

}

// Invoke the "autoCalibrate Gain" command. Prior to this,
// other requisite steps need to be taken like setting
// the design config registers with the appropriate
// calibration values. Use only if you know what you are
// doing! This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::autoCalibrateGain()
{
  return issueAckNackCommand(COMMAND_AUTO_CALIBRATE_GAIN);
}

// Invoke the "autoCalibrate Reactive Gain" command. Prior to this,
// other requisite steps need to be taken like setting
// the design config registers with the appropriate
// calibration values, and auto calibrating gain.
// Use only if you know what you are doing!
// This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::autoCalibrateReactiveGain()
{
  return issueAckNackCommand(COMMAND_AUTO_CALIBRATE_REACTIVE_GAIN);
}

// Invoke the "autoCalibrate Line Frequency" command. Prior to this,
// other requisite calibration steps need to be taken like setting
// the appropriate design config registers.
// Use only if you know what you are doing!
// This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::autoCalibrateFrequency()
{
  return issueAckNackCommand(COMMAND_AUTO_CALIBRATE_FREQUENCY);
}

// This method is used to calibrate the phase compensation
// when calibrating the module, as one of the steps during
// system calibration. Use only if you know what you are doing!
// This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::calibratePhase(float pfExp)
{
  int16_t powerFactor;
  int8_t phaseComp, phaseCompNew;
  uint8_t byteArray[5];
  // Reading PF
  int retval = 0;
  retval = registerReadNBytes(0x00, 0x0c, 2, byteArray, 5);

  if (retval != SUCCESS) {
    return retval;
  }
  powerFactor = (((signed char)byteArray[3] << 8) + (unsigned char)byteArray[2]);

  float pfMeasured = powerFactor/32768.0;

  float angleMeasured = acos(pfMeasured);
  float angleExp = acos(pfExp);
  float angleMeasuredDeg = angleMeasured * 180.0/3.14159;
  float angleExpDeg = angleExp * 180.0/3.14159;

  float phi = (angleMeasuredDeg - angleExpDeg) * 40.0;

  // Reading Phase Comp register
  retval = registerReadNBytes(0x00, 0x76, 2, byteArray, 5);

  if (retval != SUCCESS) {
    return retval;
  }
  phaseComp = (signed char)byteArray[2];

  phaseCompNew = phaseComp + phi;

  // Calibrating phase
  uint8_t bytes[2];
  bytes[0] = (uint8_t)phaseCompNew;
  bytes[1] = 0;

  retval = registerWriteNBytes(0x00, 0x76, 2, bytes);

  if (retval != SUCCESS) {
    return retval;
  }

  return SUCCESS;

}

// This method saves the contents of all calibration
// and configuration registers to flash. Use with caution!

int UpbeatLabs_MCP39F521::saveToFlash()
{
  return issueAckNackCommand(COMMAND_SAVE_TO_FLASH);
}


// This method will reset the MCP39F21 to its factory
// settings. This also means that any and all
// calibrations that were done as part of the
// Dr. Wattson setup will be lost.
// There is really no reason to use this method -
// this is provided for the sake of completeness
// of the firmware for MCP39F521.
// This is one big gun to shoot yourself, be warned!

int UpbeatLabs_MCP39F521::factoryReset()
{
  int retVal = 0;
  uint8_t byteArray[2];
  byteArray[0] = 0xa5;
  byteArray[1] = 0xa5;

  retVal = registerWriteNBytes(0x00, 0x5e, 2, byteArray);
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = saveToFlash();
  if (retVal != SUCCESS) {
    return retVal;
  }

  return SUCCESS;
}


typedef struct UpbeatLabs_MCP39F521_CalibrationConfig {
  uint32_t systemConfig;
  int accumInt;
  UpbeatLabs_MCP39F521_DesignConfigData designConfigData;
  uint32_t gainCurrentRMS;
  uint32_t gainVoltageRMS;
  uint32_t gainActivePower;
  uint32_t gainReactivePower;
  int16_t phaseCompensation;
} UpbeatLabs_MCP39F521_CalibrationConfig;


UpbeatLabs_MCP39F521_CalibrationConfig calibConfig[] = {     // Tamaño de array puesto para que no de error
  {268435456, 5, {18, 15, 22, 0, 8456, 1203, 9920, 1101, 60000}, 40386, 57724, 50987, 45458, 208 },
  {268435456, 5, {18, 13, 20, 27, 20000, 1200, 24000, 20785, 60000}, 33247, 57917, 42529, 38181, 0 },
  {268435456, 5, {18, 13, 20, 27, 17100, 1218, 20800, 5470, 60000}, 50090, 57394, 63413, 57227, 58 },
};

// This method will reset the calibration values to Dr. Wattson

int UpbeatLabs_MCP39F521::resetCalibration(calibration_config cc)
{
  int retVal = 0;

  retVal = setSystemConfigurationRegister(calibConfig[cc].systemConfig); // Channel 1 Gain 4, Channel 0 Gain 1
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = setAccumulationIntervalRegister(calibConfig[cc].accumInt); // Accumulation interval 5
  if (retVal != SUCCESS) {
    return retVal;
  }

  // We need to apply correction factor where accumulation interval is not 2;
  if (calibConfig[cc].accumInt > 2) {
    _energy_accum_correction_factor = (calibConfig[cc].accumInt - 2);
  }

  UpbeatLabs_MCP39F521_DesignConfigData data;

  data.calibrationVoltage = calibConfig[cc].designConfigData.calibrationVoltage;
  data.calibrationCurrent = calibConfig[cc].designConfigData.calibrationCurrent;
  data.calibrationPowerActive = calibConfig[cc].designConfigData.calibrationPowerActive;
  data.calibrationPowerReactive = calibConfig[cc].designConfigData.calibrationPowerReactive;
  data.lineFrequencyRef = calibConfig[cc].designConfigData.lineFrequencyRef;
  data.rangeVoltage = calibConfig[cc].designConfigData.rangeVoltage;
  data.rangeCurrent = calibConfig[cc].designConfigData.rangeCurrent;
  data.rangePower = calibConfig[cc].designConfigData.rangePower;
  data.rangeUnimplemented = calibConfig[cc].designConfigData.rangeUnimplemented;

  retVal = writeDesignConfigRegisters(&data);
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = writeGains(calibConfig[cc].gainCurrentRMS, calibConfig[cc].gainVoltageRMS,
                      calibConfig[cc].gainActivePower, calibConfig[cc].gainReactivePower);
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = writePhaseCompensation(calibConfig[cc].phaseCompensation);
  if (retVal != SUCCESS) {
    return retVal;
  }

  retVal = saveToFlash();
  if (retVal != SUCCESS) {
    return retVal;
  }

  return SUCCESS;
}


// END --- WARNING!!! WARNING!!! WARNING!!!


// Private methods ---

// Read the contents of the registers starting with the starting address, up to the number of bytes specified. 

int UpbeatLabs_MCP39F521::registerReadNBytes(int addressHigh, int addressLow,
                                             int numBytesToRead, uint8_t *byteArray,
                                             int byteArraySize)
{

  uint8_t aucWriteDataBuf[8];
  int i;
  uint32_t checksumTotal = 0;

  if (byteArraySize < numBytesToRead + 3) {
    return ERROR_INSUFFICIENT_ARRAY_SIZE;
  }

  aucWriteDataBuf[0] = 0xa5; // Header
  aucWriteDataBuf[1] = 0x08; // Num bytes
  aucWriteDataBuf[2] = COMMAND_SET_ADDRESS_POINTER; // Command - set address pointer
  aucWriteDataBuf[3] = addressHigh;
  aucWriteDataBuf[4] = addressLow;
  aucWriteDataBuf[5] = COMMAND_REGISTER_READ_N_BYTES; // Command - read register, N bytes
  aucWriteDataBuf[6] = numBytesToRead;
  aucWriteDataBuf[7] = 0; // Checksum - computed below
  for(i=0; i<7;i++) {
    checksumTotal += aucWriteDataBuf[i];
  }
  aucWriteDataBuf[7] = checksumTotal % 256;

  Wire.beginTransmission(i2c_addr);
  for(i=0; i< 8; i++) {
    Wire.write(aucWriteDataBuf[i]);
  }
  Wire.endTransmission();

  delay(100);

  //
  // Read the specified length of data - numBytesToRead + 3 bytes
  //

  Wire.requestFrom(i2c_addr, (uint8_t)(numBytesToRead + 3));
  int requestDataLength = Wire.available();
  if (requestDataLength==(numBytesToRead + 3)) {
    for (i = 0; i < numBytesToRead + 3 ; i++) {
      byteArray[i] = Wire.read();
    }

    // Check header and checksum
    return checkHeaderAndChecksum(numBytesToRead, byteArray, byteArraySize);

  } else {
    // Unexpected. Handle error
    return ERROR_UNEXPECTED_RESPONSE;
  }

  return SUCCESS;
}

// Write to the registers, starting from the starting address the number of bytes specified in the byteArray

int UpbeatLabs_MCP39F521::registerWriteNBytes(int addressHigh, int addressLow,
                                              int numBytes, uint8_t *byteArray)
{
  uint8_t aucWriteDataBuf[35];
  uint8_t aucReadDataBuf[1];
  int i;
  uint32_t checksumTotal = 0;

  aucWriteDataBuf[0] = 0xa5; // Header
  aucWriteDataBuf[1] = numBytes + 8; // Num bytes in frame
  aucWriteDataBuf[2] = COMMAND_SET_ADDRESS_POINTER; // Command - set address pointer
  aucWriteDataBuf[3] = addressHigh; // Address high
  aucWriteDataBuf[4] = addressLow; // Address low - design config registers
  aucWriteDataBuf[5] = COMMAND_REGISTER_WRITE_N_BYTES; // Command - write register, N bytes
  aucWriteDataBuf[6] = numBytes; // Num bytes of data
  // Data here ...
  for(i=7; i<7+numBytes; i++) {
    aucWriteDataBuf[i] = byteArray[i-7];
  }
  // i should have been incremented so is the last element here
  aucWriteDataBuf[i] = 0; // Checksum - computed below
  for(i=0; i<numBytes+7;i++) {
    checksumTotal += aucWriteDataBuf[i];
  }

  // i should have been incremented so is the last element here
  aucWriteDataBuf[i] = checksumTotal % 256;

  Wire.beginTransmission(i2c_addr);
  for(i=0; i< (numBytes+8); i++) {
    Wire.write(aucWriteDataBuf[i]);
  }
  Wire.endTransmission();

  delay(100);

  Wire.requestFrom(i2c_addr, (uint8_t)1);
  aucReadDataBuf[0] = Wire.read();

  //int error = SUCCESS;
  uint8_t header = aucReadDataBuf[0];

  return checkHeader(header);

}

// Some commands are issued and just return an ACK (or NAK)
// This method factors out those types of commands
// and takes in as argument the specified command to issue.

int UpbeatLabs_MCP39F521::issueAckNackCommand(int command)
{
  uint8_t aucWriteDataBuf[4];
  uint8_t aucReadDataBuf[1];
  int i;
  //uint32_t checksumTotal = 0;

  aucWriteDataBuf[0] = 0xa5; // Header
  aucWriteDataBuf[1] = 0x04; // Num bytes
  aucWriteDataBuf[2] = command; // Command
  aucWriteDataBuf[3] = (0xa5 + 0x04 + command) % 256; // checksum

  Wire.beginTransmission(i2c_addr);
  for(i=0; i< 4; i++) {
    Wire.write(aucWriteDataBuf[i]);
  }
  Wire.endTransmission();

  delay(100);

  //
  // Read the ack
  //

  Wire.requestFrom(i2c_addr,  (uint8_t)1);
  int requestDataLength = Wire.available();
  if (requestDataLength==1) {
    aucReadDataBuf[0] = Wire.read();

    //int error = 0;
    uint8_t header = aucReadDataBuf[0];

    return checkHeader(header);

  } else {
    return ERROR_UNEXPECTED_RESPONSE;
  }

  return SUCCESS;
}

// Convenience method to check the header and the checksum for the returned data. If all is good,
// this method should return SUCCESS

int UpbeatLabs_MCP39F521::checkHeaderAndChecksum( int numBytesToRead,
                                                  uint8_t *byteArray,
                                                  int byteArraySize)
{
  int i;
  uint16_t checksumTotal = 0;

  uint8_t header = byteArray[0];
  //uint8_t dataLen = byteArray[1];
  uint8_t checksum = byteArray[numBytesToRead + 3 - 1];

  for (i = 0; i < numBytesToRead + 3 - 1; i++) {
    checksumTotal += byteArray[i];
  }
  uint8_t calculatedChecksum = checksumTotal % 256;
  int error = SUCCESS;

  error = checkHeader(header);

  if (calculatedChecksum != checksum) {
    error = ERROR_CHECKSUM_MISMATCH;
  }

  return error;
}

// Convenience method to check the header of the response.
// If all is good, this will return SUCCESS

int UpbeatLabs_MCP39F521::checkHeader( int header)
{
  int error = SUCCESS;
  if (header != RESPONSE_ACK) {
    error = ERROR_INCORRECT_HEADER;
    if (header == RESPONSE_CSFAIL) {
      error = ERROR_CHECKSUM_FAIL;
    }
  }
  return error;
}
