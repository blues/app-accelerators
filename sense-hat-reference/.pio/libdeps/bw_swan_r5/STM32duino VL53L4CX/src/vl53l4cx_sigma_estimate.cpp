
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




#include "vl53l4cx_class.h"


VL53L4CX_Error VL53L4CX::VL53L4CX_f_023(
  uint8_t  sigma_estimator__sigma_ref_mm,
  uint32_t VL53L4CX_p_007,
  uint32_t VL53L4CX_p_032,
  uint32_t VL53L4CX_p_001,
  uint32_t a_zp,
  uint32_t c_zp,
  uint32_t bx,
  uint32_t ax_zp,
  uint32_t cx_zp,
  uint32_t VL53L4CX_p_028,
  uint16_t fast_osc_frequency,
  uint16_t *psigma_est)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_DIVISION_BY_ZERO;
  uint32_t sigma_int  = VL53L4CX_D_002;

  uint32_t pll_period_mm  = 0;

  uint64_t tmp0        = 0;
  uint64_t tmp1        = 0;
  uint64_t b_minus_amb = 0;
  uint64_t VL53L4CX_p_055   = 0;

  *psigma_est  = VL53L4CX_D_002;



  if (fast_osc_frequency != 0) {



    pll_period_mm = VL53L4CX_calc_pll_period_mm(fast_osc_frequency);



    if (VL53L4CX_p_028 > VL53L4CX_p_032)
      b_minus_amb = (uint64_t)VL53L4CX_p_028 -
                    (uint64_t)VL53L4CX_p_032;
    else
      b_minus_amb = (uint64_t)VL53L4CX_p_032 -
                    (uint64_t)VL53L4CX_p_028;



    if (VL53L4CX_p_007 > VL53L4CX_p_001)
      VL53L4CX_p_055 = (uint64_t)VL53L4CX_p_007 -
                       (uint64_t)VL53L4CX_p_001;
    else
      VL53L4CX_p_055 = (uint64_t)VL53L4CX_p_001 -
                       (uint64_t)VL53L4CX_p_007;



    if (b_minus_amb != 0) {




      tmp0 = (uint64_t)VL53L4CX_p_032 + (uint64_t)bx +
             (uint64_t)VL53L4CX_p_028;
      if (tmp0 > VL53L4CX_D_003) {
        tmp0 = VL53L4CX_D_003;
      }



      tmp1 = (uint64_t)VL53L4CX_p_055 * (uint64_t)VL53L4CX_p_055;
      tmp1 = tmp1 << 8;


      if (tmp1 > VL53L4CX_D_004) {
        tmp1 = VL53L4CX_D_004;
      }


      tmp1 = do_division_u(tmp1, b_minus_amb);
      tmp1 = do_division_u(tmp1, b_minus_amb);


      if (tmp1 > (uint64_t)VL53L4CX_D_005) {
        tmp1 = (uint64_t)VL53L4CX_D_005;
      }


      tmp0 = tmp1 * tmp0;


      tmp1 = (uint64_t)c_zp + (uint64_t)cx_zp +
             (uint64_t)a_zp + (uint64_t)ax_zp;


      if (tmp1 > (uint64_t)VL53L4CX_D_003) {
        tmp1 = (uint64_t)VL53L4CX_D_003;
      }

      tmp1 = tmp1 << 8;


      tmp0 = tmp1 + tmp0;
      if (tmp0 > (uint64_t)VL53L4CX_D_006) {
        tmp0 = (uint64_t)VL53L4CX_D_006;
      }






      if (tmp0 > (uint64_t)VL53L4CX_D_007) {
        tmp0 = do_division_u(tmp0, b_minus_amb);
        tmp0 = tmp0 * pll_period_mm;
      } else {
        tmp0 = tmp0 * pll_period_mm;
        tmp0 = do_division_u(tmp0, b_minus_amb);
      }


      if (tmp0 > (uint64_t)VL53L4CX_D_006) {
        tmp0 = (uint64_t)VL53L4CX_D_006;
      }



      if (tmp0 > (uint64_t)VL53L4CX_D_007) {
        tmp0 = do_division_u(tmp0, b_minus_amb);
        tmp0 = do_division_u(tmp0, 4);
        tmp0 = tmp0 * pll_period_mm;
      } else {
        tmp0 = tmp0 * pll_period_mm;
        tmp0 = do_division_u(tmp0, b_minus_amb);
        tmp0 = do_division_u(tmp0, 4);
      }


      if (tmp0 > (uint64_t)VL53L4CX_D_006) {
        tmp0 = (uint64_t)VL53L4CX_D_006;
      }


      tmp0 = tmp0 >> 2;


      if (tmp0 > (uint64_t)VL53L4CX_D_007) {
        tmp0 = (uint64_t)VL53L4CX_D_007;
      }


      tmp1 = (uint64_t)sigma_estimator__sigma_ref_mm << 7;
      tmp1 = tmp1 * tmp1;
      tmp0 = tmp0 + tmp1;


      if (tmp0 > (uint64_t)VL53L4CX_D_007) {
        tmp0 = (uint64_t)VL53L4CX_D_007;
      }


      sigma_int = VL53L4CX_isqrt((uint32_t)tmp0);

      *psigma_est = (uint16_t)sigma_int;

      status = VL53L4CX_ERROR_NONE;
    }

  }

  return status;
}
