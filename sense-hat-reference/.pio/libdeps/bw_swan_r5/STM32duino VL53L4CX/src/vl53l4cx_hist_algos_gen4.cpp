
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


VL53L4CX_Error VL53L4CX::VL53L4CX_f_025(
  VL53L4CX_dmax_calibration_data_t         *pdmax_cal,
  VL53L4CX_hist_gen3_dmax_config_t         *pdmax_cfg,
  VL53L4CX_hist_post_process_config_t      *ppost_cfg,
  VL53L4CX_histogram_bin_data_t            *pbins_input,
  VL53L4CX_histogram_bin_data_t            *pxtalk,
  VL53L4CX_hist_gen3_algo_private_data_t   *palgo3,
  VL53L4CX_hist_gen4_algo_filtered_data_t  *pfiltered,
  VL53L4CX_hist_gen3_dmax_private_data_t   *pdmax_algo,
  VL53L4CX_range_results_t                 *presults,
  uint8_t                                histo_merge_nb)
{


  VL53L4CX_Error  status  = VL53L4CX_ERROR_NONE;

  VL53L4CX_hist_pulse_data_t     *ppulse_data;
  VL53L4CX_range_data_t          *prange_data;

  uint8_t                       p = 0;
  VL53L4CX_histogram_bin_data_t *pB = &(palgo3->VL53L4CX_p_006);

  VL53L4CX_f_003(palgo3);



  memcpy(
    &(palgo3->VL53L4CX_p_006),
    pbins_input,
    sizeof(VL53L4CX_histogram_bin_data_t));



  presults->cfg_device_state = pbins_input->cfg_device_state;
  presults->rd_device_state  = pbins_input->rd_device_state;
  presults->zone_id          = pbins_input->zone_id;
  presults->stream_count     = pbins_input->result__stream_count;
  presults->wrap_dmax_mm     = 0;
  presults->max_results      = VL53L4CX_MAX_RANGE_RESULTS;
  presults->active_results   = 0;

  for (p = 0; p < VL53L4CX_MAX_AMBIENT_DMAX_VALUES; p++) {
    presults->VL53L4CX_p_022[p] = 0;
  }



  VL53L4CX_hist_calc_zero_distance_phase(&(palgo3->VL53L4CX_p_006));



  VL53L4CX_hist_estimate_ambient_from_thresholded_bins(
    (int32_t)ppost_cfg->ambient_thresh_sigma0,
    &(palgo3->VL53L4CX_p_006));

  VL53L4CX_hist_estimate_ambient_from_ambient_bins(
    &(palgo3->VL53L4CX_p_006));


  VL53L4CX_hist_remove_ambient_bins(&(palgo3->VL53L4CX_p_006));


  if (ppost_cfg->algo__crosstalk_compensation_enable > 0)
    VL53L4CX_f_005(
      pxtalk,
      &(palgo3->VL53L4CX_p_006),
      &(palgo3->VL53L4CX_p_047));


  pdmax_cfg->ambient_thresh_sigma =
    ppost_cfg->ambient_thresh_sigma1;

  for (p = 0; p < VL53L4CX_MAX_AMBIENT_DMAX_VALUES; p++) {
    if (status == VL53L4CX_ERROR_NONE) {
      status =
        VL53L4CX_f_001(
          pdmax_cfg->target_reflectance_for_dmax_calc[p],
          pdmax_cal,
          pdmax_cfg,
          &(palgo3->VL53L4CX_p_006),
          pdmax_algo,
          &(presults->VL53L4CX_p_022[p]));
    }
  }





  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_f_006(
        ppost_cfg->ambient_thresh_events_scaler,
        (int32_t)pdmax_cfg->ambient_thresh_sigma,
        (int32_t)ppost_cfg->min_ambient_thresh_events,
        ppost_cfg->algo__crosstalk_compensation_enable,
        &(palgo3->VL53L4CX_p_006),
        &(palgo3->VL53L4CX_p_047),
        palgo3);





  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_f_007(palgo3);



  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_f_008(palgo3);



  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_f_009(palgo3);



  for (p = 0; p < palgo3->VL53L4CX_p_046; p++) {

    ppulse_data = &(palgo3->VL53L4CX_p_003[p]);



    if (status == VL53L4CX_ERROR_NONE)
      status =
        VL53L4CX_f_010(
          p,
          &(palgo3->VL53L4CX_p_006),
          palgo3);



    if (status == VL53L4CX_ERROR_NONE)
      status =
        VL53L4CX_f_011(
          p,
          &(palgo3->VL53L4CX_p_006),
          palgo3,
          pB->VL53L4CX_p_028,
          &(palgo3->VL53L4CX_p_048));



    if (status == VL53L4CX_ERROR_NONE) {
      status =
        VL53L4CX_f_011(
          p,
          &(palgo3->VL53L4CX_p_006),
          palgo3,
          0,
          &(palgo3->VL53L4CX_p_049));
    }



    if (status == VL53L4CX_ERROR_NONE) {
      status =
        VL53L4CX_f_011(
          p,
          &(palgo3->VL53L4CX_p_047),
          palgo3,
          0,
          &(palgo3->VL53L4CX_p_050));
    }



    if (status == VL53L4CX_ERROR_NONE)
      status =
        VL53L4CX_f_026(
          p,
          &(palgo3->VL53L4CX_p_048),
          palgo3,
          pfiltered);



    if (status == VL53L4CX_ERROR_NONE)
      status =
        VL53L4CX_f_027(
          p,
          ppost_cfg->noise_threshold,
          pfiltered,
          palgo3);

    if (status == VL53L4CX_ERROR_NONE)
      status =
        VL53L4CX_f_014(
          ppulse_data->VL53L4CX_p_023,
          ppost_cfg->sigma_estimator__sigma_ref_mm,
          palgo3->VL53L4CX_p_030,
          ppulse_data->VL53L4CX_p_051,
          ppost_cfg->algo__crosstalk_compensation_enable,
          &(palgo3->VL53L4CX_p_048),
          &(palgo3->VL53L4CX_p_049),
          &(palgo3->VL53L4CX_p_050),
          &(ppulse_data->VL53L4CX_p_002));



    if (status == VL53L4CX_ERROR_NONE)
      status =
        VL53L4CX_f_015(
          p,
          1,
          &(palgo3->VL53L4CX_p_006),
          palgo3);

  }



  if (status == VL53L4CX_ERROR_NONE)
    status =
      VL53L4CX_f_016(
        ppost_cfg->hist_target_order,
        palgo3);



  for (p = 0; p < palgo3->VL53L4CX_p_046; p++) {

    ppulse_data = &(palgo3->VL53L4CX_p_003[p]);


    if (!(presults->active_results < presults->max_results)) {
      continue;
    }




    if (ppulse_data->VL53L4CX_p_010 >
        ppost_cfg->signal_total_events_limit &&
        ppulse_data->VL53L4CX_p_023 < 0xFF) {

      prange_data =
        &(presults->VL53L4CX_p_003[presults->active_results]);

      if (status == VL53L4CX_ERROR_NONE)
        VL53L4CX_f_017(
          presults->active_results,
          ppost_cfg->valid_phase_low,
          ppost_cfg->valid_phase_high,
          ppost_cfg->sigma_thresh,
          &(palgo3->VL53L4CX_p_006),
          ppulse_data,
          prange_data);

      if (status == VL53L4CX_ERROR_NONE)
        status =
          VL53L4CX_f_018(
            pB->vcsel_width,
            pB->VL53L4CX_p_015,
            pB->total_periods_elapsed,
            pB->result__dss_actual_effective_spads,
            prange_data,
            histo_merge_nb);

      if (status == VL53L4CX_ERROR_NONE)
        VL53L4CX_f_019(
          ppost_cfg->gain_factor,
          ppost_cfg->range_offset_mm,
          prange_data);

      presults->active_results++;
    }

  }

  return status;
}



VL53L4CX_Error VL53L4CX::VL53L4CX_f_026(
  uint8_t                                pulse_no,
  VL53L4CX_histogram_bin_data_t           *ppulse,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo3,
  VL53L4CX_hist_gen4_algo_filtered_data_t *pfiltered)
{




  VL53L4CX_Error  status       = VL53L4CX_ERROR_NONE;

  VL53L4CX_hist_pulse_data_t *pdata = &(palgo3->VL53L4CX_p_003[pulse_no]);

  uint8_t  lb     = 0;
  uint8_t  i     = 0;
  int32_t  suma  = 0;
  int32_t  sumb  = 0;
  int32_t  sumc  = 0;

  pfiltered->VL53L4CX_p_020    = palgo3->VL53L4CX_p_020;
  pfiltered->VL53L4CX_p_019      = palgo3->VL53L4CX_p_019;
  pfiltered->VL53L4CX_p_021 = palgo3->VL53L4CX_p_021;



  for (lb = pdata->VL53L4CX_p_012; lb <= pdata->VL53L4CX_p_013; lb++) {

    i =  lb  % palgo3->VL53L4CX_p_030;


    VL53L4CX_f_022(
      i,
      pdata->VL53L4CX_p_051,
      ppulse,
      &suma,
      &sumb,
      &sumc);


    pfiltered->VL53L4CX_p_007[i] = suma;
    pfiltered->VL53L4CX_p_032[i] = sumb;
    pfiltered->VL53L4CX_p_001[i] = sumc;



    pfiltered->VL53L4CX_p_053[i] =
      (suma + sumb) -
      (sumc + palgo3->VL53L4CX_p_028);



    pfiltered->VL53L4CX_p_054[i] =
      (sumb + sumc) -
      (suma + palgo3->VL53L4CX_p_028);
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_027(
  uint8_t                                pulse_no,
  uint16_t                               noise_threshold,
  VL53L4CX_hist_gen4_algo_filtered_data_t *pfiltered,
  VL53L4CX_hist_gen3_algo_private_data_t  *palgo3)
{



  VL53L4CX_Error  status       = VL53L4CX_ERROR_NONE;
  VL53L4CX_Error  func_status  = VL53L4CX_ERROR_NONE;

  VL53L4CX_hist_pulse_data_t *pdata = &(palgo3->VL53L4CX_p_003[pulse_no]);

  uint8_t  lb            = 0;
  uint8_t  i            = 0;
  uint8_t  j            = 0;

  SUPPRESS_UNUSED_WARNING(noise_threshold);

  for (lb = pdata->VL53L4CX_p_012; lb < pdata->VL53L4CX_p_013; lb++) {

    i =  lb    % palgo3->VL53L4CX_p_030;
    j = (lb + 1) % palgo3->VL53L4CX_p_030;

    if (i < palgo3->VL53L4CX_p_021 &&
        j < palgo3->VL53L4CX_p_021) {

      if (pfiltered->VL53L4CX_p_053[i] == 0 &&
          pfiltered->VL53L4CX_p_054[i] == 0)

      {
        pfiltered->VL53L4CX_p_040[i] = 0;
      }

      else if (pfiltered->VL53L4CX_p_053[i] >= 0 &&
               pfiltered->VL53L4CX_p_054[i] >= 0) {
        pfiltered->VL53L4CX_p_040[i] = 1;
      }

      else if (pfiltered->VL53L4CX_p_053[i] <  0 &&
               pfiltered->VL53L4CX_p_054[i] >= 0 &&
               pfiltered->VL53L4CX_p_053[j] >= 0 &&
               pfiltered->VL53L4CX_p_054[j] <  0) {
        pfiltered->VL53L4CX_p_040[i] = 1;
      }

      else {
        pfiltered->VL53L4CX_p_040[i] = 0;
      }


      if (pfiltered->VL53L4CX_p_040[i] > 0) {

        pdata->VL53L4CX_p_023 = lb;

        func_status =
          VL53L4CX_f_028(
            lb,
            pfiltered->VL53L4CX_p_007[i],
            pfiltered->VL53L4CX_p_032[i],
            pfiltered->VL53L4CX_p_001[i],
            0,
            0,
            0,
            palgo3->VL53L4CX_p_028,
            palgo3->VL53L4CX_p_030,
            &(pdata->VL53L4CX_p_011));

        if (func_status ==
            VL53L4CX_ERROR_DIVISION_BY_ZERO) {
          pfiltered->VL53L4CX_p_040[i] = 0;
        }

      }
    }
  }

  return status;
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_028(
  uint8_t   bin,
  int32_t   VL53L4CX_p_007,
  int32_t   VL53L4CX_p_032,
  int32_t   VL53L4CX_p_001,
  int32_t   ax,
  int32_t   bx,
  int32_t   cx,
  int32_t   VL53L4CX_p_028,
  uint8_t   VL53L4CX_p_030,
  uint32_t *pmean_phase)
{


  VL53L4CX_Error  status = VL53L4CX_ERROR_DIVISION_BY_ZERO;

  int64_t  mean_phase  = VL53L4CX_MAX_ALLOWED_PHASE;
  int32_t  mean_phase32;
  int64_t  VL53L4CX_p_055   = 0;
  int64_t  half_b_minus_amb = 0;


  VL53L4CX_p_055    =     4096 * ((int64_t)VL53L4CX_p_001 -
                                  (int64_t)cx - (int64_t)VL53L4CX_p_007 - (int64_t)ax);
  half_b_minus_amb  = 4096 * ((int64_t)VL53L4CX_p_032 -
                              (int64_t)bx - (int64_t)VL53L4CX_p_028);

  if (half_b_minus_amb != 0) {

    mean_phase = (4096 * VL53L4CX_p_055) + half_b_minus_amb;
    mean_phase = do_division_s(mean_phase, (half_b_minus_amb * 2));

    mean_phase  +=  2048;
    mean_phase  += (4096 * (int64_t)bin);


    mean_phase  = do_division_s((mean_phase + 1), 2);


    if (mean_phase  < 0) {
      mean_phase = 0;
    }
    if (mean_phase > VL53L4CX_MAX_ALLOWED_PHASE) {
      mean_phase = VL53L4CX_MAX_ALLOWED_PHASE;
    }


    mean_phase32 = (int32_t)mean_phase;
    mean_phase32 = mean_phase32 %
                   ((int32_t)VL53L4CX_p_030 * 2048);
    mean_phase = mean_phase32;

    status = VL53L4CX_ERROR_NONE;

  }

  *pmean_phase = (uint32_t)mean_phase;

  return status;
}
