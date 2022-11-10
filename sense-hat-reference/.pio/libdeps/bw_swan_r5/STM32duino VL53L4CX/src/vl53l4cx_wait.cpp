
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */





#include "vl53l4cx_class.h"


VL53L4CX_Error VL53L4CX::VL53L4CX_wait_for_boot_completion(
  VL53L4CX_DEV     Dev)
{



  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint8_t      fw_ready  = 0;

  if (pdev->wait_method == VL53L4CX_WAIT_METHOD_BLOCKING) {



    status =
      VL53L4CX_poll_for_boot_completion(
        Dev,
        VL53L4CX_BOOT_COMPLETION_POLLING_TIMEOUT_MS);

  } else {



    fw_ready = 0;
    while (fw_ready == 0x00 && status == VL53L4CX_ERROR_NONE) {
      status = VL53L4CX_is_boot_complete(
                 Dev,
                 &fw_ready);

      if (status == VL53L4CX_ERROR_NONE) {
        status = VL53L4CX_WaitMs(
                   Dev,
                   VL53L4CX_POLLING_DELAY_MS);
      }
    }
  }

  return status;

}


VL53L4CX_Error VL53L4CX::VL53L4CX_wait_for_range_completion(
  VL53L4CX_DEV     Dev)
{



  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint8_t      data_ready  = 0;

  if (pdev->wait_method == VL53L4CX_WAIT_METHOD_BLOCKING) {



    status =
      VL53L4CX_poll_for_range_completion(
        Dev,
        VL53L4CX_RANGE_COMPLETION_POLLING_TIMEOUT_MS);

  } else {



    data_ready = 0;
    while (data_ready == 0x00 && status == VL53L4CX_ERROR_NONE) {
      status = VL53L4CX_is_new_data_ready(
                 Dev,
                 &data_ready);

      if (status == VL53L4CX_ERROR_NONE) {
        status = VL53L4CX_WaitMs(
                   Dev,
                   VL53L4CX_POLLING_DELAY_MS);
      }
    }
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_wait_for_test_completion(
  VL53L4CX_DEV     Dev)
{



  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint8_t      data_ready  = 0;

  if (pdev->wait_method == VL53L4CX_WAIT_METHOD_BLOCKING) {



    status =
      VL53L4CX_poll_for_range_completion(
        Dev,
        VL53L4CX_TEST_COMPLETION_POLLING_TIMEOUT_MS);

  } else {



    data_ready = 0;
    while (data_ready == 0x00 && status == VL53L4CX_ERROR_NONE) {
      status = VL53L4CX_is_new_data_ready(
                 Dev,
                 &data_ready);

      if (status == VL53L4CX_ERROR_NONE) {
        status = VL53L4CX_WaitMs(
                   Dev,
                   VL53L4CX_POLLING_DELAY_MS);
      }
    }
  }

  return status;
}




VL53L4CX_Error VL53L4CX::VL53L4CX_is_boot_complete(
  VL53L4CX_DEV     Dev,
  uint8_t       *pready)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  uint8_t  firmware__system_status = 0;

  status =
    VL53L4CX_RdByte(
      Dev,
      VL53L4CX_FIRMWARE__SYSTEM_STATUS,
      &firmware__system_status);



  if ((firmware__system_status & 0x01) == 0x01) {
    *pready = 0x01;
    VL53L4CX_init_ll_driver_state(
      Dev,
      VL53L4CX_DEVICESTATE_SW_STANDBY);
  } else {
    *pready = 0x00;
    VL53L4CX_init_ll_driver_state(
      Dev,
      VL53L4CX_DEVICESTATE_FW_COLDBOOT);
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_is_firmware_ready(
  VL53L4CX_DEV     Dev,
  uint8_t       *pready)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  status = VL53L4CX_is_firmware_ready_silicon(
             Dev,
             pready);

  pdev->fw_ready = *pready;

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_is_new_data_ready(
  VL53L4CX_DEV     Dev,
  uint8_t       *pready)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint8_t  gpio__mux_active_high_hv = 0;
  uint8_t  gpio__tio_hv_status      = 0;
  uint8_t  interrupt_ready          = 0;

  gpio__mux_active_high_hv =
    pdev->stat_cfg.gpio_hv_mux__ctrl &
    VL53L4CX_DEVICEINTERRUPTLEVEL_ACTIVE_MASK;

  if (gpio__mux_active_high_hv == VL53L4CX_DEVICEINTERRUPTLEVEL_ACTIVE_HIGH) {
    interrupt_ready = 0x01;
  } else {
    interrupt_ready = 0x00;
  }



  status = VL53L4CX_RdByte(
             Dev,
             VL53L4CX_GPIO__TIO_HV_STATUS,
             &gpio__tio_hv_status);



  if ((gpio__tio_hv_status & 0x01) == interrupt_ready) {
    *pready = 0x01;
  } else {
    *pready = 0x00;
  }

  return status;
}




VL53L4CX_Error VL53L4CX::VL53L4CX_poll_for_boot_completion(
  VL53L4CX_DEV    Dev,
  uint32_t      timeout_ms)
{


  VL53L4CX_Error status       = VL53L4CX_ERROR_NONE;

  status = VL53L4CX_WaitUs(
             Dev,
             VL53L4CX_FIRMWARE_BOOT_TIME_US);

  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_WaitValueMaskEx(
        Dev,
        timeout_ms,
        VL53L4CX_FIRMWARE__SYSTEM_STATUS,
        0x01,
        0x01,
        VL53L4CX_POLLING_DELAY_MS);

  if (status == VL53L4CX_ERROR_NONE) {
    VL53L4CX_init_ll_driver_state(Dev, VL53L4CX_DEVICESTATE_SW_STANDBY);
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_poll_for_firmware_ready(
  VL53L4CX_DEV    Dev,
  uint32_t      timeout_ms)
{


  VL53L4CX_Error status          = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint32_t     start_time_ms   = 0;
  uint32_t     current_time_ms = 0;
  int32_t      poll_delay_ms   = VL53L4CX_POLLING_DELAY_MS;
  uint8_t      fw_ready        = 0;



  VL53L4CX_GetTickCount(&start_time_ms);
  pdev->fw_ready_poll_duration_ms = 0;



  while ((status == VL53L4CX_ERROR_NONE) &&
         (pdev->fw_ready_poll_duration_ms < timeout_ms) &&
         (fw_ready == 0)) {

    status = VL53L4CX_is_firmware_ready(
               Dev,
               &fw_ready);

    if (status == VL53L4CX_ERROR_NONE &&
        fw_ready == 0 &&
        poll_delay_ms > 0) {
      status = VL53L4CX_WaitMs(
                 Dev,
                 poll_delay_ms);
    }


    VL53L4CX_GetTickCount(&current_time_ms);
    pdev->fw_ready_poll_duration_ms =
      current_time_ms - start_time_ms;
  }

  if (fw_ready == 0 && status == VL53L4CX_ERROR_NONE) {
    status = VL53L4CX_ERROR_TIME_OUT;
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_poll_for_range_completion(
  VL53L4CX_DEV     Dev,
  uint32_t       timeout_ms)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  uint8_t  gpio__mux_active_high_hv = 0;
  uint8_t  interrupt_ready          = 0;

  gpio__mux_active_high_hv =
    pdev->stat_cfg.gpio_hv_mux__ctrl &
    VL53L4CX_DEVICEINTERRUPTLEVEL_ACTIVE_MASK;

  if (gpio__mux_active_high_hv == VL53L4CX_DEVICEINTERRUPTLEVEL_ACTIVE_HIGH) {
    interrupt_ready = 0x01;
  } else {
    interrupt_ready = 0x00;
  }

  status =
    VL53L4CX_WaitValueMaskEx(
      Dev,
      timeout_ms,
      VL53L4CX_GPIO__TIO_HV_STATUS,
      interrupt_ready,
      0x01,
      VL53L4CX_POLLING_DELAY_MS);

  return status;
}
