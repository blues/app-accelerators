
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */





#include <stdio.h>
#include <stdlib.h>



#include "vl53l4cx_class.h"

VL53L4CX_Error VL53L4CX::VL53L4CX_set_calib_config(
  VL53L4CX_DEV      Dev,
  uint8_t         vcsel_delay__a0,
  uint8_t         calib_1,
  uint8_t         calib_2,
  uint8_t         calib_3,
  uint8_t         calib_2__a0,
  uint8_t         spad_readout)
{


  VL53L4CX_Error status       = VL53L4CX_ERROR_NONE;
  uint8_t      comms_buffer[3];

  status = VL53L4CX_enable_powerforce(Dev);


  if (status == VL53L4CX_ERROR_NONE) {
    status = VL53L4CX_disable_firmware(Dev);
  }




  if (status == VL53L4CX_ERROR_NONE) {
    status = VL53L4CX_WrByte(
               Dev,
               VL53L4CX_RANGING_CORE__VCSEL_DELAY__A0,
               vcsel_delay__a0);
  }



  if (status == VL53L4CX_ERROR_NONE) {


    comms_buffer[0] = calib_1;
    comms_buffer[1] = calib_2;
    comms_buffer[2] = calib_3;

    status = VL53L4CX_WriteMulti(
               Dev,
               VL53L4CX_RANGING_CORE__CALIB_1,
               comms_buffer,
               3);
  }



  if (status == VL53L4CX_ERROR_NONE)
    status = VL53L4CX_WrByte(
               Dev,
               VL53L4CX_RANGING_CORE__CALIB_2__A0,
               calib_2__a0);



  if (status == VL53L4CX_ERROR_NONE)
    status = VL53L4CX_WrByte(
               Dev,
               VL53L4CX_RANGING_CORE__SPAD_READOUT,
               spad_readout);



  if (status == VL53L4CX_ERROR_NONE) {
    status = VL53L4CX_enable_firmware(Dev);
  }

  return status;
}



VL53L4CX_Error VL53L4CX::VL53L4CX_set_hist_calib_pulse_delay(
  VL53L4CX_DEV      Dev,
  uint8_t         calib_delay)
{


  VL53L4CX_Error status       = VL53L4CX_ERROR_NONE;

  status =
    VL53L4CX_set_calib_config(
      Dev,
      0x01,
      calib_delay,
      0x04,
      0x08,
      0x14,
      VL53L4CX_RANGING_CORE__SPAD_READOUT__CALIB_PULSES);

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_disable_calib_pulse_delay(
  VL53L4CX_DEV      Dev)
{


  VL53L4CX_Error status       = VL53L4CX_ERROR_NONE;

  status =
    VL53L4CX_set_calib_config(
      Dev,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      VL53L4CX_RANGING_CORE__SPAD_READOUT__STANDARD);

  return status;
}
