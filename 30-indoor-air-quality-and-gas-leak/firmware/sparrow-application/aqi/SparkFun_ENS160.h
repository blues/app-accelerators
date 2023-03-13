// SparkFun_ENS160.h
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


// The following classes act as the interface between Arduino and the 
// Air Quality Sensor source files. 

#pragma once
#include "sfe_ens160.h"
#include "sfe_bus.h"
#include <Wire.h>
#include <SPI.h>

class SparkFun_ENS160 : public QwDevENS160
{

	public: 

		SparkFun_ENS160() {};

    ///////////////////////////////////////////////////////////////////////
    // begin()
    //
    //
    // This method follows the standard startup pattern in SparkFun Arduino
    // libraries.
    //
    //  Parameter   Description
    //  ---------   ----------------------------
    //  wirePort    optional. The Wire port. If not provided, the default port is used
    //  address     optional. I2C Address. If not provided, the default address is used.
    //  retval      true on success, false on startup failure
    //
    // This method is overridden, implementing two versions.
    //
    // Version 1:
    // User skips passing in an I2C object which then defaults to Wire.
		bool begin(uint8_t deviceAddress = ENS160_ADDRESS_HIGH)
		{
        // Setup  I2C object and pass into the superclass
        setCommunicationBus(_i2cBus, deviceAddress);

				// Initialize the I2C buss class i.e. setup default Wire port
        _i2cBus.init();

        // Initialize the system - return results
        return this->QwDevENS160::init();
		}

		//Version 2:
    // User passes in an I2C object and an address (optional).
    bool begin(TwoWire &wirePort, uint8_t deviceAddress = ENS160_ADDRESS_HIGH)
    {
        // Setup  I2C object and pass into the superclass
        setCommunicationBus(_i2cBus, deviceAddress);

				// Give the I2C port provided by the user to the I2C bus class.
        _i2cBus.init(wirePort, true);

        // Initialize the system - return results
        return this->QwDevENS160::init();
    }

	private: 

		//I2C bus class
		sfe_ENS160::QwI2C _i2cBus; 

};
	
class SparkFun_ENS160_SPI : public QwDevENS160
{
		public:

		SparkFun_ENS160_SPI() {};

    ///////////////////////////////////////////////////////////////////////
    // begin()
    //
    // This method is called to initialize the ISM330DHCX library and connect to
    // the ISM330DHCX device. This method must be called before calling any other method
    // that interacts with the device.
    //
    // This method follows the standard startup pattern in SparkFun Arduino
    // libraries.
    //
    //  Parameter   Description
    //  ---------   ----------------------------
    //  spiPort     optional. The SPI port. If not provided, the default port is used
    //  SPISettings optional. SPI "transaction" settings are need for every data transfer. 
		//												Default used if not provided.
    //  Chip Select mandatory. The chip select pin ("CS") can't be guessed, so must be provided.
    //  retval      true on success, false on startup failure
    //
    // This methond is overridden, implementing two versions.
    //
    // Version 1:
    // User skips passing in an SPI object which then defaults to SPI.

    bool begin(uint8_t cs)
    {
        // Setup a SPI object and pass into the superclass
        setCommunicationBus(_spiBus);

				// Initialize the SPI bus class with the chip select pin, SPI port defaults to SPI, 
				// and SPI settings are set to class defaults.
        _spiBus.init(cs, true);

        // Initialize the system - return results
        return this->QwDevENS160::init();
    }

    bool begin(SPIClass &spiPort, SPISettings ensSettings, uint8_t cs)
    {
        // Setup a SPI object and pass into the superclass
        setCommunicationBus(_spiBus);

				// Initialize the SPI bus class with provided SPI port, SPI setttings, and chip select pin.
        _spiBus.init(spiPort, ensSettings, cs, true);

        // Initialize the system - return results
        return this->QwDevENS160::init();
    }

		private:

		// SPI bus class
		sfe_ENS160::SfeSPI _spiBus;

};
