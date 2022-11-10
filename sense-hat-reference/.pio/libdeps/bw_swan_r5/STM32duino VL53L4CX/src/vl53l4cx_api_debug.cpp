
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */




#include "vl53l4cx_class.h"


VL53L4CX_Error VL53L4CX::VL53L4CX_decode_calibration_data_buffer(
  uint16_t                   buf_size,
  uint8_t                   *pbuffer,
  VL53L4CX_calibration_data_t *pdata)
{
  VL53L4CX_Error  status = VL53L4CX_ERROR_NONE;

  if (sizeof(VL53L4CX_calibration_data_t) > buf_size) {
    return VL53L4CX_ERROR_COMMS_BUFFER_TOO_SMALL;
  }

  memcpy(pdata, pbuffer, sizeof(VL53L4CX_calibration_data_t));

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_get_nvm_debug_data(
  VL53L4CX_DEV                          Dev,
  VL53L4CX_decoded_nvm_data_t          *pdata)
{


  VL53L4CX_Error  status = VL53L4CX_ERROR_NONE;

  status = VL53L4CX_read_nvm(Dev, 0, pdata);

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_get_histogram_debug_data(
  VL53L4CX_DEV                          Dev,
  VL53L4CX_histogram_bin_data_t        *pdata)
{


  VL53L4CX_Error  status = VL53L4CX_ERROR_NONE;

  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  memcpy(
    pdata,
    &(pdev->hist_data),
    sizeof(VL53L4CX_histogram_bin_data_t));

  return status;
}




VL53L4CX_Error VL53L4CX::VL53L4CX_get_additional_data(
  VL53L4CX_DEV                       Dev,
  VL53L4CX_additional_data_t        *pdata)
{


  VL53L4CX_Error  status = VL53L4CX_ERROR_NONE;

  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  pdata->preset_mode             = pdev->preset_mode;
  pdata->zone_preset             = pdev->zone_preset;
  pdata->measurement_mode        = pdev->measurement_mode;
  pdata->offset_calibration_mode = pdev->offset_calibration_mode;
  pdata->offset_correction_mode  = pdev->offset_correction_mode;
  pdata->dmax_mode               = pdev->dmax_mode;

  pdata->phasecal_config_timeout_us  = pdev->phasecal_config_timeout_us;
  pdata->mm_config_timeout_us        = pdev->mm_config_timeout_us;
  pdata->range_config_timeout_us     = pdev->range_config_timeout_us;
  pdata->inter_measurement_period_ms = pdev->inter_measurement_period_ms;
  pdata->dss_config__target_total_rate_mcps =
    pdev->dss_config__target_total_rate_mcps;



  status =
    VL53L4CX_get_histogram_debug_data(
      Dev,
      &(pdata->VL53L4CX_p_006));

  return status;
}




VL53L4CX_Error VL53L4CX::VL53L4CX_get_xtalk_debug_data(
  VL53L4CX_DEV                          Dev,
  VL53L4CX_xtalk_debug_data_t          *pdata)
{


  VL53L4CX_Error  status = VL53L4CX_ERROR_NONE;

  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  memcpy(
    &(pdata->customer),
    &(pdev->customer),
    sizeof(VL53L4CX_customer_nvm_managed_t));

  memcpy(
    &(pdata->xtalk_cfg),
    &(pdev->xtalk_cfg),
    sizeof(VL53L4CX_xtalk_config_t));

  memcpy(
    &(pdata->hist_data),
    &(pdev->hist_data),
    sizeof(VL53L4CX_histogram_bin_data_t));

  memcpy(
    &(pdata->xtalk_shapes),
    &(pdev->xtalk_shapes),
    sizeof(VL53L4CX_xtalk_histogram_data_t));

  memcpy(
    &(pdata->xtalk_results),
    &(pdev->xtalk_results),
    sizeof(VL53L4CX_xtalk_range_results_t));

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_get_offset_debug_data(
  VL53L4CX_DEV                          Dev,
  VL53L4CX_offset_debug_data_t         *pdata)
{


  VL53L4CX_Error  status = VL53L4CX_ERROR_NONE;

  VL53L4CX_LLDriverData_t *pdev = VL53L4CXDevStructGetLLDriverHandle(Dev);

  memcpy(
    &(pdata->customer),
    &(pdev->customer),
    sizeof(VL53L4CX_customer_nvm_managed_t));

  memcpy(
    &(pdata->fmt_dmax_cal),
    &(pdev->fmt_dmax_cal),
    sizeof(VL53L4CX_dmax_calibration_data_t));

  memcpy(
    &(pdata->cust_dmax_cal),
    &(pdev->cust_dmax_cal),
    sizeof(VL53L4CX_dmax_calibration_data_t));

  memcpy(
    &(pdata->add_off_cal_data),
    &(pdev->add_off_cal_data),
    sizeof(VL53L4CX_additional_offset_cal_data_t));

  memcpy(
    &(pdata->offset_results),
    &(pdev->offset_results),
    sizeof(VL53L4CX_offset_range_results_t));

  return status;
}
