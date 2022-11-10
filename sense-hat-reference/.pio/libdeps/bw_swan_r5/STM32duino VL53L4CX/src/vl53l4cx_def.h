
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */

/**
 * @file vl53lx_def.h
 *
 * @brief Type definitions for VL53L4CX API.
 *
 */


#ifndef _VL53L4CX_DEF_H_
#define _VL53L4CX_DEF_H_

#include "vl53l4cx_ll_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup VL53L4CX_globaldefine_group VL53L4CX Defines
 *  @brief    VL53L4CX Defines
 *  @{
 */


/** VL53L4CX IMPLEMENTATION major version */
#define VL53L4CX_IMPLEMENTATION_VER_MAJOR       1
/** VL53L4CX IMPLEMENTATION minor version */
#define VL53L4CX_IMPLEMENTATION_VER_MINOR       2
/** VL53L4CX IMPLEMENTATION sub version */
#define VL53L4CX_IMPLEMENTATION_VER_SUB         8
/** VL53L4CX IMPLEMENTATION sub version */
#define VL53L4CX_IMPLEMENTATION_VER_REVISION  2578

/****************************************
 * PRIVATE define do not edit
 ****************************************/

/** @brief Defines the parameters of the Get Version Functions
 */
typedef struct {
  uint32_t     revision; /*!< revision number */
  uint8_t      major;    /*!< major number */
  uint8_t      minor;    /*!< minor number */
  uint8_t      build;    /*!< build number */
} VL53L4CX_Version_t;


/** @brief Defines the parameters of the Get Device Info Functions
 */
typedef struct {
  uint8_t ProductType;
  /*!< Product Type, VL53L4CX = 0xAA
   * Stands as module_type in the datasheet
   */
  uint8_t ProductRevisionMajor;
  /*!< Product revision major */
  uint8_t ProductRevisionMinor;
  /*!< Product revision minor */
} VL53L4CX_DeviceInfo_t;

/** @defgroup VL53L4CX_define_DistanceModes_group Defines Distance modes
 *  Defines all possible Distance modes for the device
 *  @{
 */
typedef uint8_t VL53L4CX_DistanceModes;

#define VL53L4CX_DISTANCEMODE_SHORT             ((VL53L4CX_DistanceModes)  1)
#define VL53L4CX_DISTANCEMODE_MEDIUM            ((VL53L4CX_DistanceModes)  2)
#define VL53L4CX_DISTANCEMODE_LONG              ((VL53L4CX_DistanceModes)  3)
/** @} VL53L4CX_define_DistanceModes_group */

/** @defgroup VL53L4CX_define_OffsetCorrectionModes_group Defines Offset Correction modes
 *  Device Offset Correction Mode
 *
 *  @brief Defines all possible offset correction modes for the device
 *  @{
 */
typedef uint8_t VL53L4CX_OffsetCorrectionModes;

#define VL53L4CX_OFFSETCORRECTIONMODE_STANDARD ((VL53L4CX_OffsetCorrectionModes)  1)
#define VL53L4CX_OFFSETCORRECTIONMODE_PERVCSEL ((VL53L4CX_OffsetCorrectionModes)  3)

/** @} VL53L4CX_define_OffsetCorrectionModes_group */

/** @brief Defines all parameters for the device
 */
typedef struct {
  VL53L4CX_DistanceModes DistanceMode;
  /*!< Defines the operating mode to be used for the next measure */
  uint32_t MeasurementTimingBudgetMicroSeconds;
  /*!< Defines the allowed total time for a single measurement */
} VL53L4CX_DeviceParameters_t;


/** @defgroup VL53L4CX_define_Smudge_Mode_group Defines smudge correction modes
 *  Defines the smudge correction modes
 *  @{
 */

typedef uint8_t VL53L4CX_SmudgeCorrectionModes;

#define VL53L4CX_SMUDGE_CORRECTION_NONE       ((VL53L4CX_SmudgeCorrectionModes)  0)
/*!< Smudge correction is disabled */
#define VL53L4CX_SMUDGE_CORRECTION_CONTINUOUS ((VL53L4CX_SmudgeCorrectionModes)  1)
/*!< Smudge correction is applied continuously across the rangings */
#define VL53L4CX_SMUDGE_CORRECTION_SINGLE     ((VL53L4CX_SmudgeCorrectionModes)  2)
/*!< Smudge correction is applied only once across the rangings */
#define VL53L4CX_SMUDGE_CORRECTION_DEBUG      ((VL53L4CX_SmudgeCorrectionModes)  3)
/*!< Smudge detection is applied continuously but Xtalk values are not
 * updated automatically within the driver
 */

/** @} VL53L4CX_define_Smudge_Correction_Mode_group */

/**
 * @struct VL53L4CX_TargetRangeData_t
 * @brief One Range measurement data for each target.
 */
typedef struct {
  int16_t RangeMaxMilliMeter;
  /*!< Tells what is the maximum detection distance of the object
   * in current setup and environment conditions (Filled when
   *  applicable)
   */

  int16_t RangeMinMilliMeter;
  /*!< Tells what is the minimum detection distance of the object
   * in current setup and environment conditions (Filled when
   *  applicable)
   */

  FixPoint1616_t SignalRateRtnMegaCps;
  /*!< Return signal rate (MCPS)\n these is a 16.16 fix point
   *  value, which is effectively a measure of target
   *  reflectance.
   */

  FixPoint1616_t AmbientRateRtnMegaCps;
  /*!< Return ambient rate (MCPS)\n these is a 16.16 fix point
   *  value, which is effectively a measure of the ambient
   *  light.
   */

  FixPoint1616_t SigmaMilliMeter;
  /*!< Return the Sigma value in millimeter */

  int16_t RangeMilliMeter;
  /*!< range distance in millimeter. This should be between
   *  RangeMinMilliMeter and RangeMaxMilliMeter
   */

  uint8_t RangeStatus;
  /*!< Range Status for the current measurement. This is device
   *  dependent. Value = 0 means value is valid.
   */

  uint8_t ExtendedRange;
  /*!< Extended range flag for the current measurement.
   *  Value = 1 means timings A&B are combined to increase the
   *  maximum distance range.
   */
} VL53L4CX_TargetRangeData_t;
/**
 * @struct  VL53L4CX_MultiRangingData_t
 * @brief   Structure for storing the set of range results
 *
 */
typedef struct {
  uint32_t TimeStamp;
  /*!< 32-bit time stamp.
   * @warning Not yet implemented
   */

  uint8_t StreamCount;
  /*!< 8-bit Stream Count. */

  uint8_t NumberOfObjectsFound;
  /*!< Indicate the number of objects found.
   * This is used to know how many ranging data should be get.
   * NumberOfObjectsFound is in the range 0 to
   * VL53L4CX_MAX_RANGE_RESULTS.
   */
  VL53L4CX_TargetRangeData_t RangeData[VL53L4CX_MAX_RANGE_RESULTS];
  /*!< Range data each target distance */
  uint8_t HasXtalkValueChanged;
  /*!< set to 1 if a new Xtalk value has been computed whilst
   * smudge correction mode enable by with
   * VL53L4CX_SmudgeCorrectionEnable() function is either
   * VL53L4CX_SMUDGE_CORRECTION_CONTINUOUS or
   * VL53L4CX_SMUDGE_CORRECTION_SINGLE.
   */
  uint16_t EffectiveSpadRtnCount;
  /*!< Return the effective SPAD count for the return signal.
   *  To obtain Real value it should be divided by 256
   */
} VL53L4CX_MultiRangingData_t;


/**
 * @struct  VL53L4CX_UserRoi_t
 * @brief   Defines User Zone(ROI) parameters
 *
 */
typedef struct {

  uint8_t   TopLeftX;   /*!< Top Left x coordinate:  0-15 range */
  uint8_t   TopLeftY;   /*!< Top Left y coordinate:  0-15 range */
  uint8_t   BotRightX;  /*!< Bot Right x coordinate: 0-15 range */
  uint8_t   BotRightY;  /*!< Bot Right y coordinate: 0-15 range */

} VL53L4CX_UserRoi_t;


/**
 * @struct VL53L4CX_CustomerNvmManaged_t
 *
 */

typedef struct {
  uint8_t   global_config__spad_enables_ref_0;
  uint8_t   global_config__spad_enables_ref_1;
  uint8_t   global_config__spad_enables_ref_2;
  uint8_t   global_config__spad_enables_ref_3;
  uint8_t   global_config__spad_enables_ref_4;
  uint8_t   global_config__spad_enables_ref_5;
  uint8_t   global_config__ref_en_start_select;
  uint8_t   ref_spad_man__num_requested_ref_spads;
  uint8_t   ref_spad_man__ref_location;
  uint32_t  algo__crosstalk_compensation_plane_offset_kcps;
  int16_t   algo__crosstalk_compensation_x_plane_gradient_kcps;
  int16_t   algo__crosstalk_compensation_y_plane_gradient_kcps;
  uint16_t  ref_spad_char__total_rate_target_mcps;
  int16_t   algo__part_to_part_range_offset_mm;
  int16_t   mm_config__inner_offset_mm;
  int16_t   mm_config__outer_offset_mm;
} VL53L4CX_CustomerNvmManaged_t;

/**
 * @struct  VL53L4CX_CalibrationData_t
 * @brief   Structure for storing the Calibration Data
 *
 */

typedef struct {

  uint32_t                             struct_version;
  VL53L4CX_CustomerNvmManaged_t          customer;
  VL53L4CX_additional_offset_cal_data_t  add_off_cal_data;
  VL53L4CX_optical_centre_t              optical_centre;
  VL53L4CX_xtalk_histogram_data_t        xtalkhisto;
  VL53L4CX_gain_calibration_data_t       gain_cal;
  VL53L4CX_cal_peak_rate_map_t           cal_peak_rate_map;
  VL53L4CX_per_vcsel_period_offset_cal_data_t per_vcsel_cal_data;
  uint32_t  algo__xtalk_cpo_HistoMerge_kcps[VL53L4CX_BIN_REC_SIZE];
} VL53L4CX_CalibrationData_t;

#define VL53L4CX_ADDITIONAL_CALIBRATION_DATA_STRUCT_VERSION  0x20
/** VL53L4CX additional Calibration Data struct version final struct version
 * is given by adding it to  VL53L4CX_LL_CALIBRATION_DATA_STRUCT_VERSION
 */

#define VL53L4CX_CALIBRATION_DATA_STRUCT_VERSION \
    (VL53L4CX_LL_CALIBRATION_DATA_STRUCT_VERSION + \
    VL53L4CX_ADDITIONAL_CALIBRATION_DATA_STRUCT_VERSION)
/* VL53L4CX Calibration Data struct version */

/**
 * @struct  VL53L4CX_AdditionalData_t
 * @brief   Structure for storing the Additional Data
 *
 */
typedef VL53L4CX_additional_data_t VL53L4CX_AdditionalData_t;


/** @defgroup VL53L4CX_define_RangeStatus_group Defines the Range Status
 *  @{
 */
#define  VL53L4CX_RANGESTATUS_RANGE_VALID       0
/*!<The Range is valid. */
#define  VL53L4CX_RANGESTATUS_SIGMA_FAIL        1
/*!<Sigma Fail. */
#define  VL53L4CX_RANGESTATUS_SIGNAL_FAIL       2
/*!<Signal fail. */
#define  VL53L4CX_RANGESTATUS_RANGE_VALID_MIN_RANGE_CLIPPED 3
/*!<Target is below minimum detection threshold. */
#define  VL53L4CX_RANGESTATUS_OUTOFBOUNDS_FAIL      4
/*!<Phase out of valid limits -  different to a wrap exit. */
#define  VL53L4CX_RANGESTATUS_HARDWARE_FAIL     5
/*!<Hardware fail. */
#define  VL53L4CX_RANGESTATUS_RANGE_VALID_NO_WRAP_CHECK_FAIL  6
/*!<The Range is valid but the wraparound check has not been done. */
#define VL53L4CX_RANGESTATUS_WRAP_TARGET_FAIL     7
/*!<Wrapped target - no matching phase in other VCSEL period timing. */
#define VL53L4CX_RANGESTATUS_PROCESSING_FAIL      8
/*!<Internal algo underflow or overflow in lite ranging. */
#define VL53L4CX_RANGESTATUS_XTALK_SIGNAL_FAIL      9
/*!<Specific to lite ranging. */
#define VL53L4CX_RANGESTATUS_SYNCRONISATION_INT     10
/*!<1st interrupt when starting ranging in back to back mode. Ignore data. */
#define VL53L4CX_RANGESTATUS_RANGE_VALID_MERGED_PULSE   11
/*!<All Range ok but object is result of multiple pulses merging together.
 * Used by RQL for merged pulse detection
 */
#define VL53L4CX_RANGESTATUS_TARGET_PRESENT_LACK_OF_SIGNAL  12
/*!<Used  by RQL  as different to phase fail. */
#define VL53L4CX_RANGESTATUS_MIN_RANGE_FAIL     13
/*!<Unexpected error in SPAD Array.*/
#define VL53L4CX_RANGESTATUS_RANGE_INVALID      14
/*!<lld returned valid range but negative value ! */
#define  VL53L4CX_RANGESTATUS_NONE        255
/*!<No Update. */

/** @} VL53L4CX_define_RangeStatus_group */


/** @brief  Contains the Internal data of the Bare Driver
 */

typedef struct {
  VL53L4CX_LLDriverData_t   LLData;
  /*!< Low Level Driver data structure */

  VL53L4CX_LLDriverResults_t llresults;
  /*!< Low Level Driver data structure */

  VL53L4CX_DeviceParameters_t CurrentParameters;
  /*!< Current Device Parameter */

} VL53L4CX_DevData_t;


/* MACRO Definitions */
/** @defgroup VL53L4CX_define_GeneralMacro_group General Macro Defines
 *  General Macro Defines
 *  @{
 */

/* Defines */
#define VL53L4CX_SETPARAMETERFIELD(Dev, field, value) \
  (VL53L4CXDevDataSet(Dev, CurrentParameters.field, value))

#define VL53L4CX_GETPARAMETERFIELD(Dev, field, variable) \
  (variable = VL53L4CXDevDataGet(Dev, CurrentParameters).field)

#define VL53L4CX_SETARRAYPARAMETERFIELD(Dev, field, index, value) \
  (VL53L4CXDevDataSet(Dev, CurrentParameters.field[index], value))

#define VL53L4CX_GETARRAYPARAMETERFIELD(Dev, field, index, variable) \
  (variable = VL53L4CXDevDataGet(Dev, CurrentParameters).field[index])

#define VL53L4CX_SETDEVICESPECIFICPARAMETER(Dev, field, value) \
  (VL53L4CXDevDataSet(Dev, DeviceSpecificParameters.field, value))

#define VL53L4CX_GETDEVICESPECIFICPARAMETER(Dev, field) \
  (VL53L4CXDevDataGet(Dev, DeviceSpecificParameters).field)


#define VL53L4CX_FIXPOINT1616TOFIXPOINT44(Value) \
  (uint16_t)((Value>>12)&0xFFFF)
#define VL53L4CX_FIXPOINT44TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<12)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT72(Value) \
  (uint16_t)((Value>>14)&0xFFFF)
#define VL53L4CX_FIXPOINT72TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<14)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT97(Value) \
  (uint16_t)((Value>>9)&0xFFFF)
#define VL53L4CX_FIXPOINT97TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<9)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT88(Value) \
  (uint16_t)((Value>>8)&0xFFFF)
#define VL53L4CX_FIXPOINT88TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<8)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT412(Value) \
  (uint16_t)((Value>>4)&0xFFFF)
#define VL53L4CX_FIXPOINT412TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<4)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT313(Value) \
  (uint16_t)((Value>>3)&0xFFFF)
#define VL53L4CX_FIXPOINT313TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<3)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT08(Value) \
  (uint8_t)((Value>>8)&0x00FF)
#define VL53L4CX_FIXPOINT08TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<8)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT53(Value) \
  (uint8_t)((Value>>13)&0x00FF)
#define VL53L4CX_FIXPOINT53TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<13)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT102(Value) \
  (uint16_t)((Value>>14)&0x0FFF)
#define VL53L4CX_FIXPOINT102TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<14)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT142(Value) \
  (uint16_t)((Value>>14)&0xFFFF)
#define VL53L4CX_FIXPOINT142TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<14)

#define VL53L4CX_FIXPOINT1616TOFIXPOINT160(Value) \
  (uint16_t)((Value>>16)&0xFFFF)
#define VL53L4CX_FIXPOINT160TOFIXPOINT1616(Value) \
  (FixPoint1616_t)((uint32_t)Value<<16)

#define VL53L4CX_MAKEUINT16(lsb, msb) (uint16_t)((((uint16_t)msb)<<8) + \
    (uint16_t)lsb)

#ifndef SUPPRESS_UNUSED_WARNING
#define SUPPRESS_UNUSED_WARNING(x) ((void) (x))
#endif

/** @} VL53L4CX_define_GeneralMacro_group */

/** @} VL53L4CX_globaldefine_group */



#ifdef __cplusplus
}
#endif


#endif /* _VL53L4CX_DEF_H_ */
