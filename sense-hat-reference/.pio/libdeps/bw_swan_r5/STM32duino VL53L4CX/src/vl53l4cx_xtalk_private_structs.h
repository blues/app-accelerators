
// SPDX-License-Identifier: BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX Protected and is dual licensed,
 either 'STMicroelectronics Proprietary license'
 or 'BSD 3-clause "New" or "Revised" License' , at your option.

 ******************************************************************************

 'STMicroelectronics Proprietary license'

 ******************************************************************************

 License terms: STMicroelectronics Proprietary in accordance with licensing
 terms at www.st.com/sla0081

 ******************************************************************************
 */






#ifndef _VL53L4CX_XTALK_PRIVATE_STRUCTS_H_
#define _VL53L4CX_XTALK_PRIVATE_STRUCTS_H_

#include "vl53l4cx_types.h"
#include "vl53l4cx_hist_structs.h"


#ifdef __cplusplus
extern "C"
{
#endif

#define VL53L4CX_D_012  4



typedef struct {




  uint32_t VL53L4CX_p_061[VL53L4CX_D_012];


  int16_t  VL53L4CX_p_059;

  int16_t  VL53L4CX_p_060;


  VL53L4CX_histogram_bin_data_t VL53L4CX_p_056;

  VL53L4CX_histogram_bin_data_t VL53L4CX_p_057;



  uint32_t VL53L4CX_p_058;


  uint32_t VL53L4CX_p_062[VL53L4CX_XTALK_HISTO_BINS];


} VL53L4CX_xtalk_algo_data_t;

#ifdef __cplusplus
}
#endif

#endif

