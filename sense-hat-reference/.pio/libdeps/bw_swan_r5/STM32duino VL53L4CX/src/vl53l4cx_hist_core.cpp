
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


void VL53L4CX::VL53L4CX_f_022(
  uint8_t                         VL53L4CX_p_032,
  uint8_t                         filter_woi,
  VL53L4CX_histogram_bin_data_t    *pbins,
  int32_t                        *pa,
  int32_t                        *pb,
  int32_t                        *pc)
{


  uint8_t w = 0;
  uint8_t j = 0;

  *pa = 0;
  *pb = pbins->bin_data[VL53L4CX_p_032];
  *pc = 0;

  for (w = 0 ; w < ((filter_woi << 1) + 1) ; w++) {


    j = ((VL53L4CX_p_032 + w + pbins->VL53L4CX_p_021) -
         filter_woi) % pbins->VL53L4CX_p_021;


    if (w < filter_woi) {
      *pa += pbins->bin_data[j];
    } else if (w > filter_woi) {
      *pc += pbins->bin_data[j];
    }
  }
}


VL53L4CX_Error VL53L4CX::VL53L4CX_f_018(
  uint16_t           vcsel_width,
  uint16_t           fast_osc_frequency,
  uint32_t           total_periods_elapsed,
  uint16_t           VL53L4CX_p_004,
  VL53L4CX_range_data_t  *pdata,
  uint8_t histo_merge_nb)
{
  VL53L4CX_Error     status = VL53L4CX_ERROR_NONE;

  uint32_t    pll_period_us       = 0;
  uint32_t    periods_elapsed     = 0;
  uint32_t    count_rate_total    = 0;

  pdata->width                  = vcsel_width;
  pdata->fast_osc_frequency     = fast_osc_frequency;
  pdata->total_periods_elapsed  = total_periods_elapsed;
  pdata->VL53L4CX_p_004 = VL53L4CX_p_004;



  if (pdata->fast_osc_frequency == 0) {
    status = VL53L4CX_ERROR_DIVISION_BY_ZERO;
  }

  if (pdata->total_periods_elapsed == 0) {
    status = VL53L4CX_ERROR_DIVISION_BY_ZERO;
  }

  if (status == VL53L4CX_ERROR_NONE) {



    pll_period_us =
      VL53L4CX_calc_pll_period_us(pdata->fast_osc_frequency);



    periods_elapsed      = pdata->total_periods_elapsed + 1;



    pdata->peak_duration_us    = VL53L4CX_duration_maths(
                                   pll_period_us,
                                   (uint32_t)pdata->width,
                                   VL53L4CX_RANGING_WINDOW_VCSEL_PERIODS,
                                   periods_elapsed);

    pdata->woi_duration_us     = VL53L4CX_duration_maths(
                                   pll_period_us,
                                   ((uint32_t)pdata->VL53L4CX_p_029) << 4,
                                   VL53L4CX_RANGING_WINDOW_VCSEL_PERIODS,
                                   periods_elapsed);



    pdata->peak_signal_count_rate_mcps = VL53L4CX_rate_maths(
                                           (int32_t)pdata->VL53L4CX_p_010,
                                           pdata->peak_duration_us);

    pdata->avg_signal_count_rate_mcps = VL53L4CX_rate_maths(
                                          (int32_t)pdata->VL53L4CX_p_010,
                                          pdata->woi_duration_us);

    pdata->ambient_count_rate_mcps    = VL53L4CX_rate_maths(
                                          (int32_t)pdata->VL53L4CX_p_016,
                                          pdata->woi_duration_us);



    count_rate_total =
      (uint32_t)pdata->peak_signal_count_rate_mcps +
      (uint32_t)pdata->ambient_count_rate_mcps;

    if (histo_merge_nb > 1) {
      count_rate_total /= histo_merge_nb;
    }

    pdata->total_rate_per_spad_mcps   =
      VL53L4CX_rate_per_spad_maths(
        0x06,
        count_rate_total,
        pdata->VL53L4CX_p_004,
        0xFFFF);



    pdata->VL53L4CX_p_009   =
      VL53L4CX_events_per_spad_maths(
        pdata->VL53L4CX_p_010,
        pdata->VL53L4CX_p_004,
        pdata->peak_duration_us);
  }

  return status;
}


void VL53L4CX::VL53L4CX_f_019(
  uint16_t             gain_factor,
  int16_t              range_offset_mm,
  VL53L4CX_range_data_t *pdata)
{

  pdata->min_range_mm =
    (int16_t)VL53L4CX_range_maths(
      pdata->fast_osc_frequency,
      pdata->VL53L4CX_p_026,
      pdata->zero_distance_phase,
      0,
      (int32_t)gain_factor,
      (int32_t)range_offset_mm);

  pdata->median_range_mm =
    (int16_t)VL53L4CX_range_maths(
      pdata->fast_osc_frequency,
      pdata->VL53L4CX_p_011,
      pdata->zero_distance_phase,
      0,
      (int32_t)gain_factor,
      (int32_t)range_offset_mm);

  pdata->max_range_mm =
    (int16_t)VL53L4CX_range_maths(
      pdata->fast_osc_frequency,
      pdata->VL53L4CX_p_027,
      pdata->zero_distance_phase,
      0,
      (int32_t)gain_factor,
      (int32_t)range_offset_mm);
}


void  VL53L4CX::VL53L4CX_f_029(
  VL53L4CX_histogram_bin_data_t   *pdata,
  int32_t                        ambient_estimate_counts_per_bin)
{


  uint8_t i = 0;

  for (i = 0 ; i <  pdata->VL53L4CX_p_021 ; i++)
    pdata->bin_data[i] = pdata->bin_data[i] -
                         ambient_estimate_counts_per_bin;
}


void  VL53L4CX::VL53L4CX_f_005(
  VL53L4CX_histogram_bin_data_t   *pxtalk,
  VL53L4CX_histogram_bin_data_t   *pbins,
  VL53L4CX_histogram_bin_data_t   *pxtalk_realigned)
{


  uint8_t i          = 0;
  uint8_t min_bins   = 0;
  int8_t  bin_offset = 0;
  int8_t  bin_access = 0;

  memcpy(
    pxtalk_realigned,
    pbins,
    sizeof(VL53L4CX_histogram_bin_data_t));

  for (i = 0 ; i < pxtalk_realigned->VL53L4CX_p_020 ; i++) {
    pxtalk_realigned->bin_data[i] = 0;
  }



  bin_offset =  VL53L4CX_f_030(
                  pbins,
                  pxtalk);



  if (pxtalk->VL53L4CX_p_021 < pbins->VL53L4CX_p_021) {
    min_bins = pxtalk->VL53L4CX_p_021;
  } else {
    min_bins = pbins->VL53L4CX_p_021;
  }


  for (i = 0 ; i <  min_bins ; i++) {



    if (bin_offset >= 0)
      bin_access = ((int8_t)i + (int8_t)bin_offset)
                   % (int8_t)pbins->VL53L4CX_p_021;
    else
      bin_access = ((int8_t)pbins->VL53L4CX_p_021 +
                    ((int8_t)i + (int8_t)bin_offset))
                   % (int8_t)pbins->VL53L4CX_p_021;


    if (pbins->bin_data[(uint8_t)bin_access] >
        pxtalk->bin_data[i]) {

      pbins->bin_data[(uint8_t)bin_access] =
        pbins->bin_data[(uint8_t)bin_access]
        - pxtalk->bin_data[i];

    } else {
      pbins->bin_data[(uint8_t)bin_access] = 0;
    }




    pxtalk_realigned->bin_data[(uint8_t)bin_access] =
      pxtalk->bin_data[i];



  }
}


int8_t  VL53L4CX::VL53L4CX_f_030(
  VL53L4CX_histogram_bin_data_t   *pdata1,
  VL53L4CX_histogram_bin_data_t   *pdata2)
{


  int32_t  phase_delta      = 0;
  int8_t   bin_offset       = 0;
  uint32_t period           = 0;
  uint32_t remapped_phase   = 0;

  period = 2048 *
           (uint32_t)VL53L4CX_decode_vcsel_period(pdata1->VL53L4CX_p_005);

  if (period != 0)
    remapped_phase =
      (uint32_t)pdata2->zero_distance_phase % period;


  phase_delta = (int32_t)pdata1->zero_distance_phase
                - (int32_t)remapped_phase;



  if (phase_delta > 0) {
    bin_offset = (int8_t)((phase_delta + 1024) / 2048);
  } else {
    bin_offset = (int8_t)((phase_delta - 1024) / 2048);
  }

  return bin_offset;
}


VL53L4CX_Error  VL53L4CX::VL53L4CX_f_031(
  VL53L4CX_histogram_bin_data_t   *pidata,
  VL53L4CX_histogram_bin_data_t   *podata)
{


  VL53L4CX_Error status = VL53L4CX_ERROR_NONE;

  uint8_t  bin_initial_index[VL53L4CX_MAX_BIN_SEQUENCE_CODE + 1];
  uint8_t  bin_repeat_count[VL53L4CX_MAX_BIN_SEQUENCE_CODE + 1];

  uint8_t  bin_cfg        = 0;
  uint8_t  bin_seq_length = 0;
  int32_t  repeat_count   = 0;

  uint8_t  VL53L4CX_p_032       = 0;
  uint8_t  lc       = 0;
  uint8_t  i       = 0;

  memcpy(podata, pidata, sizeof(VL53L4CX_histogram_bin_data_t));


  podata->VL53L4CX_p_021 = 0;

  for (lc = 0 ; lc < VL53L4CX_MAX_BIN_SEQUENCE_LENGTH ; lc++) {
    podata->bin_seq[lc] = VL53L4CX_MAX_BIN_SEQUENCE_CODE + 1;
  }

  for (lc = 0 ; lc < podata->VL53L4CX_p_020 ; lc++) {
    podata->bin_data[lc] = 0;
  }



  for (lc = 0 ; lc <= VL53L4CX_MAX_BIN_SEQUENCE_CODE ; lc++) {
    bin_initial_index[lc] = 0x00;
    bin_repeat_count[lc]  = 0x00;
  }




  bin_seq_length = 0x00;

  for (lc = 0 ; lc < VL53L4CX_MAX_BIN_SEQUENCE_LENGTH ; lc++) {

    bin_cfg = pidata->bin_seq[lc];



    if (bin_repeat_count[bin_cfg] == 0) {
      bin_initial_index[bin_cfg]      = bin_seq_length * 4;
      podata->bin_seq[bin_seq_length] = bin_cfg;
      bin_seq_length++;
    }

    bin_repeat_count[bin_cfg]++;



    VL53L4CX_p_032 = bin_initial_index[bin_cfg];

    for (i = 0 ; i < 4 ; i++)
      podata->bin_data[VL53L4CX_p_032 + i] +=
        pidata->bin_data[lc * 4 + i];

  }



  for (lc = 0 ; lc < VL53L4CX_MAX_BIN_SEQUENCE_LENGTH ; lc++) {

    bin_cfg = podata->bin_seq[lc];

    if (bin_cfg <= VL53L4CX_MAX_BIN_SEQUENCE_CODE)
      podata->bin_rep[lc] =
        bin_repeat_count[bin_cfg];
    else {
      podata->bin_rep[lc] = 0;
    }
  }

  podata->VL53L4CX_p_021 = bin_seq_length * 4;





  for (lc = 0 ; lc <= VL53L4CX_MAX_BIN_SEQUENCE_CODE ; lc++) {

    repeat_count = (int32_t)bin_repeat_count[lc];

    if (repeat_count > 0) {

      VL53L4CX_p_032 = bin_initial_index[lc];

      for (i = 0 ; i < 4 ; i++) {
        podata->bin_data[VL53L4CX_p_032 + i] +=
          (repeat_count / 2);
        podata->bin_data[VL53L4CX_p_032 + i] /=
          repeat_count;
      }
    }
  }



  podata->number_of_ambient_bins = 0;
  if ((bin_repeat_count[7] > 0) ||
      (bin_repeat_count[15] > 0)) {
    podata->number_of_ambient_bins = 4;
  }

  return status;
}
