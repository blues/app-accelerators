
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */




#include "vl53l4cx_class.h"


VL53L4CX_Error VL53L4CX::VL53L4CX_ipp_hist_process_data(
  VL53L4CX_DEV                         Dev,
  VL53L4CX_dmax_calibration_data_t    *pdmax_cal,
  VL53L4CX_hist_gen3_dmax_config_t    *pdmax_cfg,
  VL53L4CX_hist_post_process_config_t *ppost_cfg,
  VL53L4CX_histogram_bin_data_t       *pbins,
  VL53L4CX_xtalk_histogram_data_t     *pxtalk,
  uint8_t                           *pArea1,
  uint8_t                           *pArea2,
  uint8_t                           *phisto_merge_nb,
  VL53L4CX_range_results_t            *presults)
{



  VL53L4CX_Error status         = VL53L4CX_ERROR_NONE;

  SUPPRESS_UNUSED_WARNING(Dev);

  status =
    VL53L4CX_hist_process_data(
      pdmax_cal,
      pdmax_cfg,
      ppost_cfg,
      pbins,
      pxtalk,
      pArea1,
      pArea2,
      presults,
      phisto_merge_nb);

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_ipp_hist_ambient_dmax(
  VL53L4CX_DEV                         Dev,
  uint16_t                           target_reflectance,
  VL53L4CX_dmax_calibration_data_t    *pdmax_cal,
  VL53L4CX_hist_gen3_dmax_config_t    *pdmax_cfg,
  VL53L4CX_histogram_bin_data_t       *pbins,
  int16_t                           *pambient_dmax_mm)
{



  VL53L4CX_Error status         = VL53L4CX_ERROR_NONE;

  SUPPRESS_UNUSED_WARNING(Dev);

  status =
    VL53L4CX_hist_ambient_dmax(
      target_reflectance,
      pdmax_cal,
      pdmax_cfg,
      pbins,
      pambient_dmax_mm);

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_ipp_xtalk_calibration_process_data(
  VL53L4CX_DEV                          Dev,
  VL53L4CX_xtalk_range_results_t       *pxtalk_ranges,
  VL53L4CX_xtalk_histogram_data_t      *pxtalk_shape,
  VL53L4CX_xtalk_calibration_results_t *pxtalk_cal)
{



  VL53L4CX_Error status         = VL53L4CX_ERROR_NONE;

  SUPPRESS_UNUSED_WARNING(Dev);

  status =
    VL53L4CX_xtalk_calibration_process_data(
      pxtalk_ranges,
      pxtalk_shape,
      pxtalk_cal);

  return status;
}
