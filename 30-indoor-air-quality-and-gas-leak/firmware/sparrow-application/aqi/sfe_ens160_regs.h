// sfe_ens160_regs.h
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


// The following defines the bits and registers of the ENS160.
#define SFE_ENS160_PART_ID    0x00

typedef struct
{
	uint8_t part_id_msb : 8; // Value = 0x01
	uint8_t part_id_lsb : 8; // Value = 0x60
}	sfe_ens160_part_id_t;

#define SFE_ENS160_OP_MODE    0x10
// Possible Operating Mode defines:
#define SFE_ENS160_DEEP_SLEEP 0x00
#define SFE_ENS160_IDLE       0x01
#define SFE_ENS160_STANDARD   0x02
#define SFE_ENS160_RESET      0xF0
typedef struct
{
	uint8_t op_mode : 8;
}	sfe_ens160_op_mode_t;

#define SFE_ENS160_CONFIG          0x11
typedef struct
{
	uint8_t reserved_three :  1;
	uint8_t int_pol        :  1;
	uint8_t int_cfg        :  1;
	uint8_t reserved_two   :  1;
	uint8_t int_gpr        :  1;
	uint8_t reserved_one   :  1;
	uint8_t int_dat        :  1;
	uint8_t int_en         :  1;
}	sfe_ens160_config_t;

#define SFE_ENS160_COMMAND							0x12
// All commands must be issued when device is idle. 
#define SFE_ENS160_COMMAND_NOP				  0x00
// Get Firwmware App Version - version is placed in General Purpose Read Registers as follows:
// GPR_READ04 - Version (Major)
// GPR_READ05 - Version (Minor)
// GPR_READ06 - Version (Release)
#define SFE_ENS160_COMMAND_GET_APPVER   0x0E
// Clear General Purpose Read Register
#define SFE_ENS160_COMMAND_CLRGPR       0xCC
typedef struct
{
	uint8_t command : 8;	
}	sfe_ens160_command_t;

#define SFE_ENS160_TEMP_IN         0x13
// Temperature compensation can be given to the sensor for more accurate
// readings. 
// Temperature should be given in Kelvin in the following format:
// Value: Temperature in Kelvin * 64 
// Converting Celsius to Kelvin = Temp + 273.15
// Ergo: (Temp + 273.15) * 64
typedef struct
{
	uint8_t temp_in_msb : 8;	//0x14 -- Integer Part
	uint8_t temp_in_lsb : 8;	//0x13 -- Fractions Part
}	sfe_ens160_temp_in_t;

#define SFE_ENS160_RH_IN           0x15
// Relative Humidity compensation can be given to the sensor for more accurate
// readings. 
// RH should be given in %rH * 512
typedef struct
{
	uint8_t rh_in_msb	: 8; //0x16 -- Integer Part
	uint8_t rh_in_lsb	: 8; //0x15 -- Fraction Part
}	sfe_ens160_rh_in_t;

#define SFE_ENS160_DEVICE_STATUS   0x20
typedef struct
{
	uint8_t stat_as       :  1;
	uint8_t stat_er       :  1;
	uint8_t reserved_two  :  1;
	uint8_t reserved_one  :  1;
	uint8_t validity_flag :  2;
	uint8_t new_dat				:  1;
	uint8_t new_gpr       :  1;
}	sfe_ens160_device_status_t;

#define SFE_ENS160_DATA_AQI        0x21
typedef struct
{
	uint8_t reserved_one :  6;
	uint8_t aqi_uba      :  2;
}	sfe_ens160_data_aqi_t;

#define SFE_ENS160_DATA_TVOC       0x22
// TVOC Data - shares register with ethanol data
typedef struct
{
	uint8_t tvoc_msb : 8;
	uint8_t tvoc_lsb : 8;
}	sfe_ens160_data_tvoc_t;

#define SFE_ENS160_DATA_ETOH       0x22
// Ethanol Data - shares register with TVOC data
typedef struct
{
	uint8_t etoh_msb : 8;
	uint8_t etoh_lsb : 8;
}	sfe_ens160_data_etho_t;

#define SFE_ENS160_DATA_ECO2       0x24
// CO2 Data
typedef struct
{
	uint8_t eco2_msb : 8;
	uint8_t eco2_lsb : 8;
}	sfe_ens160_data_eco2_t;

#define SFE_ENS160_DATA_T          0x30
// Reports the temperature data given to TEMP_IN in the following manner:
// Temperature in Kelvin: Register Value / 64
// Converting Kelvin to Celsius =  Temperature in Kelvin - 273.15
// Ergo: (Register Value / 64) - 273.15
typedef struct
{
	uint8_t data_t_msb : 8; // Fractions -- 0x31
	uint8_t data_t_lsb : 8; // Integer -- 0x30
}	sfe_ens160_data_t_t;

#define SFE_ENS160_DATA_RH         0x32
// Reports the Relative Humidity compensation given to RH_IN in the following manner
// RH = Register Value / 512
typedef struct
{
	uint8_t data_rh_msb : 8; // Fractions -- 0x33
	uint8_t data_rh_lsb : 8; // Integer -- 0x32
}	sfe_ens160_data_rh_t;

#define SFE_ENS160_DATA_MISR       0x38
#define POLY 0x1D
// Gives calculated checksum of "DATA_" registers
typedef struct
{
	uint8_t data_misr : 8;
}	sfe_ens160_data_misr_t;

// General Purpose Write registers
#define SFE_ENS160_GPR_WRITE0      0x40
#define SFE_ENS160_GPR_WRITE1      0x41
#define SFE_ENS160_GPR_WRITE2      0x42
#define SFE_ENS160_GPR_WRITE3      0x43
#define SFE_ENS160_GPR_WRITE4      0x44
#define SFE_ENS160_GPR_WRITE5      0x45
#define SFE_ENS160_GPR_WRITE6      0x46
#define SFE_ENS160_GPR_WRITE7      0x47

// General Purpose Read registers
#define SFE_ENS160_GPR_READ0       0x48
#define SFE_ENS160_GPR_READ1       0x49
#define SFE_ENS160_GPR_READ2       0x4A
#define SFE_ENS160_GPR_READ3       0x4B
#define SFE_ENS160_GPR_READ4       0x4C
#define SFE_ENS160_GPR_READ5       0x4D
#define SFE_ENS160_GPR_READ6       0x4E
#define SFE_ENS160_GPR_READ7       0x4F

