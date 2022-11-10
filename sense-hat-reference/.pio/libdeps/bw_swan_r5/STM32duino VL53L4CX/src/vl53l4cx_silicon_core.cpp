
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */





#include "vl53l4cx_class.h"


VL53L4CX_Error VL53L4CX::VL53L4CX_is_firmware_ready_silicon(
  VL53L4CX_DEV     Dev,
  uint8_t       *pready)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint8_t  comms_buffer[5];

  status = VL53L4CX_ReadMulti(
             Dev,
             VL53L4CX_INTERRUPT_MANAGER__ENABLES,
             comms_buffer,
             5);

  if (status != VL53L4CX_ERROR_NONE) {
    goto ENDFUNC;
  }

  pdev->dbg_results.interrupt_manager__enables =
    comms_buffer[0];
  pdev->dbg_results.interrupt_manager__clear =
    comms_buffer[1];
  pdev->dbg_results.interrupt_manager__status =
    comms_buffer[2];
  pdev->dbg_results.mcu_to_host_bank__wr_access_en =
    comms_buffer[3];
  pdev->dbg_results.power_management__go1_reset_status =
    comms_buffer[4];

  if ((pdev->sys_ctrl.power_management__go1_power_force & 0x01)
      == 0x01) {

    if (((pdev->dbg_results.interrupt_manager__enables &
          0x1F) == 0x1F) &&
        ((pdev->dbg_results.interrupt_manager__clear
          & 0x1F) == 0x1F)) {
      *pready = 0x01;
    } else {
      *pready = 0x00;
    }

  } else {


    if ((pdev->dbg_results.power_management__go1_reset_status
         & 0x01) == 0x00) {
      *pready = 0x01;
    } else {
      *pready = 0x00;
    }
  }


ENDFUNC:
  return status;
}
