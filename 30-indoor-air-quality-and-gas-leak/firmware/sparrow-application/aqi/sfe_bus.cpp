// sfe_bus.cpp
//
// This is a library written for SparkFun Indoor Air Quality Sensor - ENS160 (Qwiic)
//
// SparkFun sells these boards at its website: www.sparkfun.com
//
// Do you like this library? Help support SparkFun. Buy a board!
//
// SparkFun Indoor Air Quality Sensor - ENS160 (Qwiic)    https://www.sparkfun.com/products/20844
//
// Written by Elias Santistevan @ SparkFun Electronics, October 2022
//
//Repository:
//		https://github.com/sparkfun/SparkFun_Indoor_Air_Quality_Sensor-ENS160_Arduino_Library
//
// SparkFun code, firmware, and software is released under the MIT
// License(http://opensource.org/licenses/MIT).
//
// SPDX-License-Identifier: MIT
//
//    The MIT License (MIT)
//
//    Copyright (c) 2022 SparkFun Electronics
//    Permission is hereby granted, free of charge, to any person obtaining a
//    copy of this software and associated documentation files (the "Software"),
//    to deal in the Software without restriction, including without limitation
//    the rights to use, copy, modify, merge, publish, distribute, sublicense,
//    and/or sell copies of the Software, and to permit persons to whom the
//    Software is furnished to do so, subject to the following conditions: The
//    above copyright notice and this permission notice shall be included in all
//    copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED
//    "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
//    NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
//    PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
//    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
//    ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
//    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


// The following classes specify the behavior for communicating
// over the respective data buses: Inter-Integrated Circuit (I2C)
// and Serial Peripheral Interface (SPI). 

#include "sfe_bus.h"
#include <Arduino.h>

#define kMaxTransferBuffer 32
#define SPI_READ 0x01

// What we use for transfer chunk size
const static uint16_t kChunkSize = kMaxTransferBuffer;

//////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
//


namespace sfe_ENS160 {

QwI2C::QwI2C(void) : _i2cPort{nullptr}
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// I2C init()
//
// Methods to init/setup this device. The caller can provide a Wire Port, or this class
// will use the default

bool QwI2C::init(TwoWire &wirePort, bool bInit)
{

    // if we don't have a wire port already
    if( !_i2cPort )
    {
        _i2cPort = &wirePort;

        if( bInit )
            _i2cPort->begin();
    }
		
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// I2C init()
//
// Methods to init/setup this device. The caller can provide a Wire Port, or this class
// will use the default
bool QwI2C::init()
{
		if( !_i2cPort )
			return init(Wire);
		else
			return false;
}



//////////////////////////////////////////////////////////////////////////////////////////////////
// ping()
//
// Is a device connected?
bool QwI2C::ping(uint8_t i2c_address)
{

    if( !_i2cPort )
        return false;

    _i2cPort->beginTransmission(i2c_address);
    return _i2cPort->endTransmission() == 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// writeRegisterByte()
//
// Write a byte to a register

bool QwI2C::writeRegisterByte(uint8_t i2c_address, uint8_t offset, uint8_t dataToWrite)
{

    if (!_i2cPort)
        return false;

    _i2cPort->beginTransmission(i2c_address);
    _i2cPort->write(offset);
    _i2cPort->write(dataToWrite);
    return _i2cPort->endTransmission() == 0;
}



//////////////////////////////////////////////////////////////////////////////////////////////////
// writeRegisterRegion()
//
// Write a block of data to a device.

int QwI2C::writeRegisterRegion(uint8_t i2c_address, uint8_t offset, const uint8_t *data, uint16_t length)
{

    _i2cPort->beginTransmission(i2c_address);
    _i2cPort->write(offset);
    _i2cPort->write(data, (int)length);

    return _i2cPort->endTransmission() ? -1 : 0; // -1 = error, 0 = success
}

int QwI2C::writeRegisterRegion(uint8_t i2c_address, uint8_t offset, uint8_t data, uint16_t length)
{

    _i2cPort->beginTransmission(i2c_address);
    _i2cPort->write(offset);
    _i2cPort->write(data);

    return _i2cPort->endTransmission() ? -1 : 0; // -1 = error, 0 = success
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
// readRegisterRegion()
//
// Reads a block of data from an i2c register on the devices.
//
// For large buffers, the data is chuncked over KMaxI2CBufferLength at a time
//
//
int QwI2C::readRegisterRegion(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t numBytes)
{
    uint8_t nChunk;
    uint16_t nReturned;

    if (!_i2cPort)
        return -1;

    int i;                   // counter in loop
    bool bFirstInter = true; // Flag for first iteration - used to send register

    while (numBytes > 0)
    {
        _i2cPort->beginTransmission(addr);

        if (bFirstInter)
        {
            _i2cPort->write(reg);
            bFirstInter = false;
        }

        if (_i2cPort->endTransmission() != 0)
            return -1; // error with the end transmission

        // We're chunking in data - keeping the max chunk to kMaxI2CBufferLength
        nChunk = numBytes > kChunkSize ? kChunkSize : numBytes;

        nReturned = _i2cPort->requestFrom((int)addr, (int)nChunk, (int)true);

        // No data returned, no dice
        if (nReturned == 0)
            return -1; // error

        // Copy the retrieved data chunk to the current index in the data segment
        for (i = 0; i < nReturned; i++){
            *data++ = _i2cPort->read();
				}

        // Decrement the amount of data recieved from the overall data request amount
        numBytes = numBytes - nReturned;

    } // end while

    return 0; // Success
}



//////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
//

SfeSPI::SfeSPI(void) : _spiPort{nullptr}
{
}

////////////////////////////////////////////////////////////////////////////////////////////////
// SPI init()
//
// Methods to init/setup this device. The caller can provide a SPI Port, or this class
// will use the default


bool SfeSPI::init(SPIClass &spiPort, SPISettings& ensSPISettings, uint8_t cs,  bool bInit)
{

    // if we don't have a SPI port already
    if( !_spiPort )
    {
        _spiPort = &spiPort;

        if( bInit )
            _spiPort->begin();
    }


		// SPI settings are needed for every transaction
		_sfeSPISettings = ensSPISettings; 

		// The chip select pin can vary from platform to platform and project to project
		// and so it must be given by the user. 
		if( !cs )
			return false; 
		
		_cs = cs;

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////
// SPI init()
//
// Methods to init/setup this device. The caller can provide a SPI Port, or this class
// will use the default
bool SfeSPI::init(uint8_t cs,  bool bInit)
{

		//If the transaction settings are not provided by the user they are built here.
		SPISettings spiSettings = SPISettings(3000000, MSBFIRST, SPI_MODE0); 

		//In addition of the port is not provided by the user, it defaults to SPI here. 
		return init(SPI, spiSettings, cs, bInit);

}


//////////////////////////////////////////////////////////////////////////////////////////////////
// ping()
//
// Is a device connected? The SPI ping is not relevant but is defined here to keep consistency with
// I2C class i.e. provided for the interface.
//


bool SfeSPI::ping(uint8_t i2c_address)
{
	return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// writeRegisterByte()
//
// Write a byte to a register

bool SfeSPI::writeRegisterByte(uint8_t i2c_address, uint8_t offset, uint8_t dataToWrite)
{

    if( !_spiPort )
        return false;

		// Apply settings
    _spiPort->beginTransaction(_sfeSPISettings);
		// Signal communication start
		digitalWrite(_cs, LOW);

    _spiPort->transfer(offset);
    _spiPort->transfer(dataToWrite);

		// End communcation
		digitalWrite(_cs, HIGH);
    _spiPort->endTransaction();

		return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// writeRegisterRegion()
//
// Write a block of data to a device.

int SfeSPI::writeRegisterRegion(uint8_t i2c_address, uint8_t offset, const uint8_t *data, uint16_t length)
{

		int i;

		// Apply settings
    _spiPort->beginTransaction(_sfeSPISettings);
		// Signal communication start
		digitalWrite(_cs, LOW);

		for(i = 0; i < length; i++)
		{
			// Increment Address (Device does not do this when using SPI)
			_spiPort->transfer((offset + i) << 1);
			_spiPort->transfer(*data++);
		}

		// End communication
		digitalWrite(_cs, HIGH);
    _spiPort->endTransaction();
		return 0; 
}

int SfeSPI::writeRegisterRegion(uint8_t i2c_address, uint8_t offset, uint8_t data, uint16_t length)
{

		int i;

		// Apply settings
    _spiPort->beginTransaction(_sfeSPISettings);
		// Signal communication start
		digitalWrite(_cs, LOW);

		//ENS160 expects bits [7:1] to be the address and the leading
		//bit to be a "zero" for a write.
    _spiPort->transfer(offset << 1);
		_spiPort->transfer(data);

		// End communication
		digitalWrite(_cs, HIGH);
    _spiPort->endTransaction();
		return 0; 
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// readRegisterRegion()
//
// Reads a block of data from the register on the device.
//
//
//


int SfeSPI::readRegisterRegion(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t numBytes)
{
    if (!_spiPort)
        return -1;

    int i; // counter in loop
		// Apply settings
    _spiPort->beginTransaction(_sfeSPISettings);
		// Signal communication start
		digitalWrite(_cs, LOW);

		//ENS160 expects bits [7:1] to be the address and the leading
		//bit to be a "one" for a read.
		reg = ((reg << 1) | SPI_READ);
    _spiPort->transfer(reg);

		for(i = 0; i < numBytes; i++)
		{
			*data++ = _spiPort->transfer(0x00);
		}

		// End transaction
		digitalWrite(_cs, HIGH);
    _spiPort->endTransaction();
		return 0; 

}

}
