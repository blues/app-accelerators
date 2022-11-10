
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


void VL53L4CX::VL53L4CX_f_003(
  VL53L4CX_hist_gen3_algo_private_data_t   *palgo)
{


  uint8_t  lb                 = 0;

  palgo->VL53L4CX_p_020              = VL53L4CX_HISTOGRAM_BUFFER_SIZE;
  palgo->VL53L4CX_p_019                = 0;
  palgo->VL53L4CX_p_021           = 0;
  palgo->VL53L4CX_p_039         = 0;
  palgo->VL53L4CX_p_028   = 0;
  palgo->VL53L4CX_p_031 = 0;

  for (lb = palgo->VL53L4CX_p_019; lb < palgo->VL53L4CX_p_020; lb++) {
    palgo->VL53L4CX_p_040[lb]      = 0;
    palgo->VL53L4CX_p_041[lb] = 0;
    palgo->VL53L4CX_p_042[lb]     = 0;
    palgo->VL53L4CX_p_043[lb]      = 0;
    palgo->VL53L4CX_p_018[lb]     = 0;
  }

  palgo->VL53L4CX_p_044 = 0;
  palgo->VL53L4CX_p_045               = VL53L4CX_D_001;
  palgo->VL53L4CX_p_046             = 0;



  VL53L4CX_init_histogram_bin_data_struct(
    0,
    VL53L4CX_HISTOGRAM_BUFFER_SIZE,
    &(palgo->VL53L4CX_p_006));
  VL53L4CX_init_histogram_bin_data_struct(
    0,
    VL53L4CX_HISTOGRAM_BUFFER_SIZE,
    &(palgo->VL53L4CX_p_047));
  VL53L4CX_init_histogram_bin_data_struct(
    0,
    VL53L4CX_HISTOGRAM_BUFFER_SIZE,
    &(palgo->VL53L4CX_p_048));
  VL53L4CX_init_histogram_bin_data_struct(
    0,
    VL53L4CX_HISTOGRAM_BUFFER_SIZE,
    &(palgo->VL53L4CX_p_049));
  VL53L4CX_init_histogram_bin_data_struct(
    0,
    VL53L4CX_HISTOGRAM_BUFFER_SIZE,
    &(palgo->VL53L4CX_p_050));
}



VL53L4CX_Error VL53L4CX::VL53L4CX_f_006(
  uint16_t                          ambient_threshold_events_scaler,
  int32_t                           ambient_threshold_sigma,
  int32_t                           min_ambient_threshold_events,
  uint8_t                           algo__crosstalk_compensation_enable,
  VL53L4CX_histogram_bin_data_t           *pbins,
  VL53L4CX_histogram_bin_data_t           *pxtalk,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{



  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;
  uint8_t  lb            = 0;
  uint8_t  VL53L4CX_p_001            = 0;
  int64_t  tmp          = 0;
  int32_t  amb_events   = 0;
  int32_t  VL53L4CX_p_018       = 0;
  int32_t  samples      = 0;

  palgo->VL53L4CX_p_020            = pbins->VL53L4CX_p_020;
  palgo->VL53L4CX_p_019              = pbins->VL53L4CX_p_019;
  palgo->VL53L4CX_p_021         = pbins->VL53L4CX_p_021;
  palgo->VL53L4CX_p_028 = pbins->VL53L4CX_p_028;



  palgo->VL53L4CX_p_030 =
    VL53L4CX_decode_vcsel_period(pbins->VL53L4CX_p_005);



  tmp  = (int64_t)pbins->VL53L4CX_p_028;
  tmp *= (int64_t)ambient_threshold_events_scaler;
  tmp += 2048;
  tmp = do_division_s(tmp, 4096);
  amb_events = (int32_t)tmp;



  for (lb = 0; lb < pbins->VL53L4CX_p_021; lb++) {

    VL53L4CX_p_001 = lb >> 2;
    samples = (int32_t)pbins->bin_rep[VL53L4CX_p_001];

    if (samples > 0) {

      if (lb < pxtalk->VL53L4CX_p_021 &&
          algo__crosstalk_compensation_enable > 0)
        VL53L4CX_p_018 = samples * (amb_events +
                                    pxtalk->bin_data[lb]);
      else {
        VL53L4CX_p_018 = samples *  amb_events;
      }

      VL53L4CX_p_018  = VL53L4CX_isqrt(VL53L4CX_p_018);

      VL53L4CX_p_018 += (samples / 2);
      VL53L4CX_p_018 /= samples;
      VL53L4CX_p_018 *= ambient_threshold_sigma;
      VL53L4CX_p_018 += 8;
      VL53L4CX_p_018 /= 16;
      VL53L4CX_p_018 += amb_events;

      if (VL53L4CX_p_018 < min_ambient_threshold_events) {
        VL53L4CX_p_018 = min_ambient_threshold_events;
      }

      palgo->VL53L4CX_p_052[lb]             = VL53L4CX_p_018;
      palgo->VL53L4CX_p_031 = VL53L4CX_p_018;
    }



  }



  palgo->VL53L4CX_p_039 = 0;

  for (lb = pbins->VL53L4CX_p_019; lb < pbins->VL53L4CX_p_021; lb++) {

    if (pbins->bin_data[lb] > palgo->VL53L4CX_p_052[lb]) {
      palgo->VL53L4CX_p_040[lb]      = 1;
      palgo->VL53L4CX_p_041[lb] = 1;
      palgo->VL53L4CX_p_039++;
    } else {
      palgo->VL53L4CX_p_040[lb]      = 0;
      palgo->VL53L4CX_p_041[lb] = 0;
    }
  }

  return status;
}




VL53L4CX_Error VL53L4CX::VL53L4CX_f_007(
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{



  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  uint8_t  i            = 0;
  uint8_t  j            = 0;
  uint8_t  found        = 0;

  palgo->VL53L4CX_p_044 = 0;

  for (i = 0; i < palgo->VL53L4CX_p_030; i++) {

    j = (i + 1) % palgo->VL53L4CX_p_030;



    if (i < palgo->VL53L4CX_p_021 && j < palgo->VL53L4CX_p_021) {
      if (palgo->VL53L4CX_p_041[i] == 0 &&
          palgo->VL53L4CX_p_041[j] == 1 &&
          found == 0) {
        palgo->VL53L4CX_p_044 = i;
        found = 1;
      }
    }
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_008(
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{



  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;
  uint8_t  i            = 0;
  uint8_t  j            = 0;
  uint8_t  lb            = 0;

  for (lb = palgo->VL53L4CX_p_044;
       lb < (palgo->VL53L4CX_p_044 +
             palgo->VL53L4CX_p_030);
       lb++) {



    i =  lb      % palgo->VL53L4CX_p_030;
    j = (lb + 1) % palgo->VL53L4CX_p_030;



    if (i < palgo->VL53L4CX_p_021 && j < palgo->VL53L4CX_p_021) {

      if (palgo->VL53L4CX_p_041[i] == 0 &&
          palgo->VL53L4CX_p_041[j] == 1) {
        palgo->VL53L4CX_p_046++;
      }

      if (palgo->VL53L4CX_p_046 > palgo->VL53L4CX_p_045) {
        palgo->VL53L4CX_p_046 = palgo->VL53L4CX_p_045;
      }

      if (palgo->VL53L4CX_p_041[i] > 0) {
        palgo->VL53L4CX_p_042[i] = palgo->VL53L4CX_p_046;
      } else {
        palgo->VL53L4CX_p_042[i] = 0;
      }
    }

  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_009(
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{



  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  uint8_t  i            = 0;
  uint8_t  j            = 0;
  uint8_t  blb            = 0;
  uint8_t  pulse_no     = 0;

  uint8_t  max_filter_half_width = 0;

  VL53L4CX_hist_pulse_data_t *pdata;

  max_filter_half_width = palgo->VL53L4CX_p_030 - 1;
  max_filter_half_width = max_filter_half_width >> 1;

  for (blb = palgo->VL53L4CX_p_044;
       blb < (palgo->VL53L4CX_p_044 +
              palgo->VL53L4CX_p_030);
       blb++) {



    i =  blb      % palgo->VL53L4CX_p_030;
    j = (blb + 1) % palgo->VL53L4CX_p_030;



    if (i < palgo->VL53L4CX_p_021 &&
        j < palgo->VL53L4CX_p_021) {



      if (palgo->VL53L4CX_p_042[i] == 0 &&
          palgo->VL53L4CX_p_042[j] > 0) {

        pulse_no = palgo->VL53L4CX_p_042[j] - 1;

        if (pulse_no < palgo->VL53L4CX_p_045) {
          pdata = &(palgo->VL53L4CX_p_003[pulse_no]);
          pdata->VL53L4CX_p_012 = blb;
          pdata->VL53L4CX_p_019    = blb + 1;
          pdata->VL53L4CX_p_023   = 0xFF;
          pdata->VL53L4CX_p_024     = 0;
          pdata->VL53L4CX_p_013   = 0;
        }
      }



      if (palgo->VL53L4CX_p_042[i] > 0
          && palgo->VL53L4CX_p_042[j] == 0) {

        pulse_no = palgo->VL53L4CX_p_042[i] - 1;

        if (pulse_no < palgo->VL53L4CX_p_045) {
          pdata = &(palgo->VL53L4CX_p_003[pulse_no]);

          pdata->VL53L4CX_p_024    = blb;
          pdata->VL53L4CX_p_013  = blb + 1;

          pdata->VL53L4CX_p_025 =
            (pdata->VL53L4CX_p_024 + 1) -
            pdata->VL53L4CX_p_019;
          pdata->VL53L4CX_p_051 =
            (pdata->VL53L4CX_p_013 + 1) -
            pdata->VL53L4CX_p_012;

          if (pdata->VL53L4CX_p_051 >
              max_filter_half_width)
            pdata->VL53L4CX_p_051 =
              max_filter_half_width;
        }
      }
    }
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_016(
  VL53L4CX_HistTargetOrder                target_order,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{



  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  VL53L4CX_hist_pulse_data_t  tmp;
  VL53L4CX_hist_pulse_data_t *ptmp = &tmp;
  VL53L4CX_hist_pulse_data_t *p0;
  VL53L4CX_hist_pulse_data_t *p1;

  uint8_t i       = 0;
  uint8_t swapped = 1;

  if (!(palgo->VL53L4CX_p_046 > 1)) {
    goto ENDFUNC;
  }

  while (swapped > 0) {

    swapped = 0;

    for (i = 1; i < palgo->VL53L4CX_p_046; i++) {

      p0 = &(palgo->VL53L4CX_p_003[i - 1]);
      p1 = &(palgo->VL53L4CX_p_003[i]);



      if (target_order
          == VL53L4CX_HIST_TARGET_ORDER__STRONGEST_FIRST) {

        if (p0->VL53L4CX_p_010 <
            p1->VL53L4CX_p_010) {



          memcpy(ptmp,
                 p1, sizeof(VL53L4CX_hist_pulse_data_t));
          memcpy(p1,
                 p0, sizeof(VL53L4CX_hist_pulse_data_t));
          memcpy(p0,
                 ptmp, sizeof(VL53L4CX_hist_pulse_data_t));

          swapped = 1;
        }

      } else {

        if (p0->VL53L4CX_p_011 > p1->VL53L4CX_p_011) {



          memcpy(ptmp,
                 p1, sizeof(VL53L4CX_hist_pulse_data_t));
          memcpy(p1,
                 p0,   sizeof(VL53L4CX_hist_pulse_data_t));
          memcpy(p0,
                 ptmp, sizeof(VL53L4CX_hist_pulse_data_t));

          swapped = 1;
        }

      }
    }
  }

ENDFUNC:
  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_010(
  uint8_t                                pulse_no,
  VL53L4CX_histogram_bin_data_t           *pbins,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{


  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  uint8_t  i            = 0;
  uint8_t  lb            = 0;

  VL53L4CX_hist_pulse_data_t *pdata = &(palgo->VL53L4CX_p_003[pulse_no]);

  pdata->VL53L4CX_p_017  = 0;
  pdata->VL53L4CX_p_016 = 0;

  for (lb = pdata->VL53L4CX_p_012; lb <= pdata->VL53L4CX_p_013; lb++) {
    i =  lb % palgo->VL53L4CX_p_030;
    pdata->VL53L4CX_p_017  += pbins->bin_data[i];
    pdata->VL53L4CX_p_016 += palgo->VL53L4CX_p_028;
  }



  pdata->VL53L4CX_p_010 =
    pdata->VL53L4CX_p_017 - pdata->VL53L4CX_p_016;

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_015(
  uint8_t                                pulse_no,
  uint8_t                                clip_events,
  VL53L4CX_histogram_bin_data_t           *pbins,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo)
{


  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  uint8_t   i            = 0;
  int16_t   VL53L4CX_p_012 = 0;
  int16_t   VL53L4CX_p_013   = 0;
  int16_t   window_width = 0;
  uint32_t  tmp_phase    = 0;

  VL53L4CX_hist_pulse_data_t *pdata = &(palgo->VL53L4CX_p_003[pulse_no]);

  if (pdata->VL53L4CX_p_023 == 0xFF) {
    pdata->VL53L4CX_p_023 = 1;
  }

  i = pdata->VL53L4CX_p_023 % palgo->VL53L4CX_p_030;

  VL53L4CX_p_012  = (int16_t)i;
  VL53L4CX_p_012 += (int16_t)pdata->VL53L4CX_p_012;
  VL53L4CX_p_012 -= (int16_t)pdata->VL53L4CX_p_023;

  VL53L4CX_p_013    = (int16_t)i;
  VL53L4CX_p_013   += (int16_t)pdata->VL53L4CX_p_013;
  VL53L4CX_p_013   -= (int16_t)pdata->VL53L4CX_p_023;


  window_width = VL53L4CX_p_013 - VL53L4CX_p_012;
  if (window_width > 3) {
    window_width = 3;
  }

  status =
    VL53L4CX_f_020(
      VL53L4CX_p_012,
      VL53L4CX_p_012 + window_width,
      palgo->VL53L4CX_p_030,
      clip_events,
      pbins,
      &(pdata->VL53L4CX_p_026));


  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_f_020(
        VL53L4CX_p_013 - window_width,
        VL53L4CX_p_013,
        palgo->VL53L4CX_p_030,
        clip_events,
        pbins,
        &(pdata->VL53L4CX_p_027));


  if (pdata->VL53L4CX_p_026 > pdata->VL53L4CX_p_027) {
    tmp_phase        = pdata->VL53L4CX_p_026;
    pdata->VL53L4CX_p_026 = pdata->VL53L4CX_p_027;
    pdata->VL53L4CX_p_027 = tmp_phase;
  }


  if (pdata->VL53L4CX_p_011 < pdata->VL53L4CX_p_026) {
    pdata->VL53L4CX_p_026 = pdata->VL53L4CX_p_011;
  }


  if (pdata->VL53L4CX_p_011 > pdata->VL53L4CX_p_027) {
    pdata->VL53L4CX_p_027 = pdata->VL53L4CX_p_011;
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_020(
  int16_t                            VL53L4CX_p_019,
  int16_t                            VL53L4CX_p_024,
  uint8_t                            VL53L4CX_p_030,
  uint8_t                            clip_events,
  VL53L4CX_histogram_bin_data_t       *pbins,
  uint32_t                          *pphase)
{


  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  int16_t  i            = 0;
  int16_t  lb            = 0;

  int64_t VL53L4CX_p_018        = 0;
  int64_t event_sum     = 0;
  int64_t weighted_sum  = 0;

  *pphase = VL53L4CX_MAX_ALLOWED_PHASE;

  if (VL53L4CX_p_030 != 0)
    for (lb = VL53L4CX_p_019; lb <= VL53L4CX_p_024; lb++) {

      if (lb < 0) {
        i = lb + (int16_t)VL53L4CX_p_030;
      } else {
        i = lb % (int16_t)VL53L4CX_p_030;
      }

      if ((i >= 0) && (i < VL53L4CX_HISTOGRAM_BUFFER_SIZE)) {
        VL53L4CX_p_018 =
          (int64_t)pbins->bin_data[i] -
          (int64_t)pbins->VL53L4CX_p_028;

        if (clip_events > 0 && VL53L4CX_p_018 < 0) {
          VL53L4CX_p_018 = 0;
        }
        event_sum += VL53L4CX_p_018;
        weighted_sum +=
          (VL53L4CX_p_018 * (1024 + (2048 * (int64_t)lb)));
      }
    }

  if (event_sum  > 0) {
    weighted_sum += do_division_s(event_sum, 2);
    weighted_sum = do_division_s(weighted_sum, event_sum);
    if (weighted_sum < 0) {
      weighted_sum = 0;
    }
    *pphase = (uint32_t)weighted_sum;
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_011(
  uint8_t                                pulse_no,
  VL53L4CX_histogram_bin_data_t           *pbins,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo,
  int32_t                                pad_value,
  VL53L4CX_histogram_bin_data_t           *ppulse)
{



  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  uint8_t  i            = 0;
  uint8_t  lb            = 0;

  VL53L4CX_hist_pulse_data_t *pdata = &(palgo->VL53L4CX_p_003[pulse_no]);

  memcpy(ppulse, pbins, sizeof(VL53L4CX_histogram_bin_data_t));



  for (lb = palgo->VL53L4CX_p_044;
       lb < (palgo->VL53L4CX_p_044 +
             palgo->VL53L4CX_p_030);
       lb++) {

    if (lb < pdata->VL53L4CX_p_012 || lb > pdata->VL53L4CX_p_013) {
      i =  lb % palgo->VL53L4CX_p_030;
      if (i < ppulse->VL53L4CX_p_021) {
        ppulse->bin_data[i] = pad_value;
      }
    }
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_014(
  uint8_t                       bin,
  uint8_t                       sigma_estimator__sigma_ref_mm,
  uint8_t                       VL53L4CX_p_030,
  uint8_t                       VL53L4CX_p_051,
  uint8_t                       crosstalk_compensation_enable,
  VL53L4CX_histogram_bin_data_t  *phist_data_ap,
  VL53L4CX_histogram_bin_data_t  *phist_data_zp,
  VL53L4CX_histogram_bin_data_t  *pxtalk_hist,
  uint16_t                     *psigma_est)
{


  VL53L4CX_Error status      = VL53L4CX_ERROR_NONE;
  VL53L4CX_Error func_status = VL53L4CX_ERROR_NONE;

  uint8_t  i    = 0;
  int32_t  VL53L4CX_p_007    = 0;
  int32_t  VL53L4CX_p_032    = 0;
  int32_t  VL53L4CX_p_001    = 0;
  int32_t  a_zp = 0;
  int32_t  c_zp = 0;
  int32_t  ax   = 0;
  int32_t  bx   = 0;
  int32_t  cx   = 0;


  if (VL53L4CX_p_030 == 0) {
    *psigma_est = 0xFFFF;
    return VL53L4CX_ERROR_DIVISION_BY_ZERO;
  }
  i = bin % VL53L4CX_p_030;



  VL53L4CX_f_022(
    i,
    VL53L4CX_p_051,
    phist_data_zp,
    &a_zp,
    &VL53L4CX_p_032,
    &c_zp);



  VL53L4CX_f_022(
    i,
    VL53L4CX_p_051,
    phist_data_ap,
    &VL53L4CX_p_007,
    &VL53L4CX_p_032,
    &VL53L4CX_p_001);

  if (crosstalk_compensation_enable > 0)
    VL53L4CX_f_022(
      i,
      VL53L4CX_p_051,
      pxtalk_hist,
      &ax,
      &bx,
      &cx);







  func_status =
    VL53L4CX_f_023(
      sigma_estimator__sigma_ref_mm,
      (uint32_t)VL53L4CX_p_007,
      (uint32_t)VL53L4CX_p_032,
      (uint32_t)VL53L4CX_p_001,
      (uint32_t)a_zp,
      (uint32_t)c_zp,
      (uint32_t)bx,
      (uint32_t)ax,
      (uint32_t)cx,
      (uint32_t)phist_data_ap->VL53L4CX_p_028,
      phist_data_ap->VL53L4CX_p_015,
      psigma_est);




  if (func_status == VL53L4CX_ERROR_DIVISION_BY_ZERO) {
    *psigma_est = 0xFFFF;
  }


  return status;
}


void VL53L4CX::VL53L4CX_f_017(
  uint8_t                      range_id,
  uint8_t                      valid_phase_low,
  uint8_t                      valid_phase_high,
  uint16_t                     sigma_thres,
  VL53L4CX_histogram_bin_data_t *pbins,
  VL53L4CX_hist_pulse_data_t    *ppulse,
  VL53L4CX_range_data_t         *pdata)
{

  uint16_t  lower_phase_limit = 0;
  uint16_t  upper_phase_limit = 0;



  pdata->range_id              = range_id;
  pdata->time_stamp            = 0;

  pdata->VL53L4CX_p_012          = ppulse->VL53L4CX_p_012;
  pdata->VL53L4CX_p_019             = ppulse->VL53L4CX_p_019;
  pdata->VL53L4CX_p_023            = ppulse->VL53L4CX_p_023;
  pdata->VL53L4CX_p_024              = ppulse->VL53L4CX_p_024;
  pdata->VL53L4CX_p_013            = ppulse->VL53L4CX_p_013;
  pdata->VL53L4CX_p_025             = ppulse->VL53L4CX_p_025;



  pdata->VL53L4CX_p_029  =
    (ppulse->VL53L4CX_p_013 + 1) - ppulse->VL53L4CX_p_012;



  pdata->zero_distance_phase   = pbins->zero_distance_phase;
  pdata->VL53L4CX_p_002              = ppulse->VL53L4CX_p_002;
  pdata->VL53L4CX_p_026             = (uint16_t)ppulse->VL53L4CX_p_026;
  pdata->VL53L4CX_p_011          = (uint16_t)ppulse->VL53L4CX_p_011;
  pdata->VL53L4CX_p_027             = (uint16_t)ppulse->VL53L4CX_p_027;
  pdata->VL53L4CX_p_017  = (uint32_t)ppulse->VL53L4CX_p_017;
  pdata->VL53L4CX_p_010   = ppulse->VL53L4CX_p_010;
  pdata->VL53L4CX_p_016 = (uint32_t)ppulse->VL53L4CX_p_016;
  pdata->total_periods_elapsed = pbins->total_periods_elapsed;



  pdata->range_status = VL53L4CX_DEVICEERROR_RANGECOMPLETE_NO_WRAP_CHECK;


  if (sigma_thres > 0 &&
      (uint32_t)ppulse->VL53L4CX_p_002 > ((uint32_t)sigma_thres << 5)) {
    pdata->range_status = VL53L4CX_DEVICEERROR_SIGMATHRESHOLDCHECK;
  }



  lower_phase_limit  = (uint8_t)valid_phase_low << 8;
  if (lower_phase_limit < pdata->zero_distance_phase)
    lower_phase_limit =
      pdata->zero_distance_phase -
      lower_phase_limit;
  else {
    lower_phase_limit  = 0;
  }

  upper_phase_limit  = (uint8_t)valid_phase_high << 8;
  upper_phase_limit += pbins->zero_distance_phase;

  if (pdata->VL53L4CX_p_011 < lower_phase_limit ||
      pdata->VL53L4CX_p_011 > upper_phase_limit) {
    pdata->range_status = VL53L4CX_DEVICEERROR_RANGEPHASECHECK;
  }

}
