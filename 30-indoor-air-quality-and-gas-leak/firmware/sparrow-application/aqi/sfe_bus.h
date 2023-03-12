// sfe_bus.h
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
// and Serial Peripheral Interface (SPI). For ease of implementation
// an abstract interface (QwIDeviceBus) is used. 

#pragma once

#ifdef ARDUINO
#include <Wire.h>
#include <SPI.h>
#endif

#include <stdint.h>

namespace sfe_ENS160 {
// The following abstract class is used an interface for upstream implementation.
class QwIDeviceBus 
{
	public: 

		virtual bool ping(uint8_t address) = 0;

		virtual	bool writeRegisterByte(uint8_t address, uint8_t offset, uint8_t data) = 0;

		virtual int writeRegisterRegion(uint8_t address, uint8_t offset, const uint8_t* data, uint16_t length) = 0;
		// MDM: this makes no sense. Should use writeRegisterByte since it's not possible to pass more than 1 byte of data with this signature
		//virtual int writeRegisterRegion(uint8_t address, uint8_t offset, uint8_t data, uint16_t length) = 0;

		virtual int readRegisterRegion(uint8_t addr, uint8_t reg, uint8_t* data, uint16_t numBytes) = 0;

};

#ifdef ARDUINO
// The QwI2C device defines behavior for I2C implementation based around the TwoWire class (Wire).
// This is Arduino specific. 
class QwI2C : public QwIDeviceBus
{
	public: 

		QwI2C(void);

		bool init();

		bool init(TwoWire& wirePort, bool bInit=false);

		bool ping(uint8_t address);

		bool writeRegisterByte(uint8_t address, uint8_t offset, uint8_t data);

		int writeRegisterRegion(uint8_t address, uint8_t offset, const uint8_t* data, uint16_t length);

		int writeRegisterRegion(uint8_t address, uint8_t offset, uint8_t data, uint16_t length);

		int readRegisterRegion(uint8_t addr, uint8_t reg, uint8_t* data, uint16_t numBytes);

	private: 

    TwoWire* _i2cPort;
};
#endif

#ifdef ARDUINO
// The SfeSPI class defines behavior for SPI implementation based around the SPIClass class (SPI).
// This is Arduino specific. 
// Paramaters like "address" are kept although irrelevant to SPI due to the use of the abstract class
// as interface, QwIDeviceBus.
class SfeSPI : public QwIDeviceBus
{
	public:

		SfeSPI(void);

		bool init(uint8_t cs, bool bInit=false);

		bool init(SPIClass& spiPort, SPISettings& ensSPISettings, uint8_t cs,  bool bInit=false);

		bool ping(uint8_t address);

		bool writeRegisterByte(uint8_t address, uint8_t offset, uint8_t data);

		int writeRegisterRegion(uint8_t address, uint8_t offset, const uint8_t* data, uint16_t length);

		int writeRegisterRegion(uint8_t address, uint8_t offset, uint8_t data, uint16_t length);

		int readRegisterRegion(uint8_t addr, uint8_t reg, uint8_t* data, uint16_t numBytes);

	private:

		SPIClass* _spiPort; 
		// Settings are used for every transaction.
		SPISettings _sfeSPISettings;
		uint8_t _cs; 
};
#endif


}
