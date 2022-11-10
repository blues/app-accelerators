
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */

#ifndef __VL53L4CX_CLASS_H
#define __VL53L4CX_CLASS_H

/* Includes ------------------------------------------------------------------*/
#include "Arduino.h"
#include "vl53l4cx_preset_setup.h"
#include "vl53l4cx_def.h"
#include "vl53l4cx_nvm_structs.h"
#include "vl53l4cx_nvm_map.h"
#include "vl53l4cx_ll_def.h"
#include "vl53l4cx_dmax_structs.h"
#include "vl53l4cx_types.h"
#include "vl53l4cx_hist_structs.h"
#include "vl53l4cx_dmax_private_structs.h"
#include "vl53l4cx_error_codes.h"
#include "vl53l4cx_hist_private_structs.h"
#include "vl53l4cx_hist_map.h"
#include "vl53l4cx_register_map.h"
#include "vl53l4cx_platform_user_config.h"
#include "vl53l4cx_platform_user_defines.h"
#include "vl53l4cx_platform_user_data.h"
#include "vl53l4cx_ll_device.h"
#include "vl53l4cx_register_map.h"
#include "vl53l4cx_register_settings.h"
#include "vl53l4cx_register_structs.h"
#include "vl53l4cx_tuning_parm_defaults.h"
#include "vl53l4cx_xtalk_private_structs.h"
#include "Wire.h"

#define VL53L4CX_DEFAULT_DEVICE_ADDRESS           0x52

#define VL53L4CX_NVM_POWER_UP_DELAY_US             50
#define VL53L4CX_NVM_READ_TRIGGER_DELAY_US          5

#define  VL53L4CX_D_002  0xFFFF


#define VL53L4CX_D_008  0xFFFF
#define VL53L4CX_D_003  0xFFFFFF
#define VL53L4CX_D_007  0xFFFFFFFF
#define VL53L4CX_D_005  0x7FFFFFFFFF
#define VL53L4CX_D_009  0xFFFFFFFFFF
#define VL53L4CX_D_010  0xFFFFFFFFFFFF
#define VL53L4CX_D_004  0xFFFFFFFFFFFFFF
#define VL53L4CX_D_006  0x7FFFFFFFFFFFFFFF
#define VL53L4CX_D_011  0xFFFFFFFFFFFFFFFF


/* Classes -------------------------------------------------------------------*/
/** Class representing a VL53L4CX sensor component
 */

class VL53L4CX {
  public:
    /** Constructor
     * @param[in] i2c device I2C to be used for communication
     * @param[in] xshut_pin shutdown pin to be used as component XSHUT
     */
    VL53L4CX(TwoWire *i2c, int xshut_pin) : dev_i2c(i2c), xshut(xshut_pin)
    {
      Dev = &MyDevice;
      memset((void *)Dev, 0x0, sizeof(VL53L4CX_Dev_t));
      MyDevice.I2cHandle = i2c;
      MyDevice.I2cDevAddr = VL53L4CX_DEFAULT_DEVICE_ADDRESS ;
    }

    /** Constructor
     * No parameters
     * Functions to set the Twowire I2c device and XShut pin
     * should be executed before begin();
     */
    VL53L4CX() : dev_i2c(), xshut()
    {
      Dev = &MyDevice;
      memset((void *)Dev, 0x0, sizeof(VL53L4CX_Dev_t));
      MyDevice.I2cDevAddr = VL53L4CX_DEFAULT_DEVICE_ADDRESS ;
    }

    /** Destructor
     */
    virtual ~VL53L4CX() {}
    /* warning: VL53L4CX class does not have a destructor.
       The warning should request to introduce a virtual destructor to make sure to delete the object */

    virtual int begin()
    {
      if (xshut >= 0) {
        pinMode(xshut, OUTPUT);
        digitalWrite(xshut, LOW);
      }
      return 0;
    }

    virtual int end()
    {
      if (xshut >= 0) {
        pinMode(xshut, INPUT);
      }
      return 0;
    }

    /*** Interface Methods ***/
    /*** High level API ***/

    /**
     * @brief       Set the Twowire I2c Device
     *
     * This function is intended to initialize for the Twowire I2c bus
     * when the constructor with no parameters is used.
     *
     * @param       Pointer to the i2c bus
     * @return      void
     */
    void setI2cDevice(TwoWire *i2c)
    {
      dev_i2c = i2c;
      MyDevice.I2cHandle = i2c;
    }

    /**
     * @brief       Set the XShut Pin
     *
     * This function is intended to initialize the XShut pin
     * when the constructor with no parameters is used.
     *
     * @param       xshut_pin
     * @return      void
     */
    void setXShutPin(int xshut_pin)
    {
      xshut = xshut_pin;
    }

    /**
     * @brief       PowerOn the sensor
     * @return      void
     */
    /* turns on the sensor */
    virtual void VL53L4CX_On(void)
    {
      if (xshut >= 0) {
        digitalWrite(xshut, HIGH);
      }
      delay(10);
    }

    /**
     * @brief       PowerOff the sensor
     * @return      void
     */
    /* turns off the sensor */
    virtual void VL53L4CX_Off(void)
    {
      if (xshut >= 0) {
        digitalWrite(xshut, LOW);
      }
      delay(10);
    }

    /**
     * @brief       Initialize the sensor with default values
     * @return      0 on Success
     */

    VL53L4CX_Error InitSensor(uint8_t address)
    {
      VL53L4CX_Error status = VL53L4CX_ERROR_NONE;
      VL53L4CX_Off();
      VL53L4CX_On();
      status = VL53L4CX_SetDeviceAddress(address);

      if (status == VL53L4CX_ERROR_NONE) {
        status = VL53L4CX_WaitDeviceBooted();
      }

      if (status == VL53L4CX_ERROR_NONE) {
        status = VL53L4CX_DataInit();
      }

      return status;
    }

    /* vl53lx_api.h functions */

    /**
     * @brief Return the VL53L4CX driver Version
     *
     * @note This function doesn't access to the device
     *
     * @param   pVersion              Rer to current driver Version
     * @return  VL53L4CX_ERROR_NONE     Success
     * @return  "Other error code"    See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetVersion(VL53L4CX_Version_t *pVersion);

    /**
     * @brief Reads the Product Revision for a for given Device
     * This function can be used to distinguish cut1.0 from cut1.1.
     *
     * @param   pProductRevisionMajor  Pointer to Product Revision Major
     * for a given Device
     * @param   pProductRevisionMinor  Pointer to Product Revision Minor
     * for a given Device
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"    See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetProductRevision(uint8_t *pProductRevisionMajor, uint8_t *pProductRevisionMinor);

    /**
     * @brief Reads the Device information for given Device
     *
     * @note This function Access to the device
     *
     * @param   pVL53L4CX_DeviceInfo  Pointer to current device info for a given
     *  Device
     * @return  VL53L4CX_ERROR_NONE   Success
     * @return  "Other error code"  See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetDeviceInfo(VL53L4CX_DeviceInfo_t *pVL53L4CX_DeviceInfo);

    /**
     * @brief Reads the Device unique identifier
     *
     * @note This function Access to the device
     *
     * @param   pUid                Pointer to current device unique ID
     * @return  VL53L4CX_ERROR_NONE   Success
     * @return  "Other error code"  See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetUID(uint64_t *pUid);


    /**
     * @brief Set new device address
     *
     * After completion the device will answer to the new address programmed.
     * This function should be called when several devices are used in parallel
     * before start programming the sensor.
     * When a single device us used, there is no need to call this function.
     *
     * @note This function Access to the device
     *
     * @param   DeviceAddress         The new Device address
     * @return  VL53L4CX_ERROR_NONE     Success
     * @return  "Other error code"    See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetDeviceAddress(uint8_t DeviceAddress);

    /**
     *
     * @brief One time device initialization
     *
     * To be called after device has been powered on and booted
     * see @a VL53L4CX_WaitDeviceBooted()
     *
     * @par Function Description
     * When not used after a fresh device "power up", it may return
     * @a #VL53L4CX_ERROR_CALIBRATION_WARNING meaning wrong calibration data
     * may have been fetched from device that can result in ranging offset error\n
     * If VL53L4CX_DataInit is called several times then the application must restore
     * calibration calling @a VL53L4CX_SetOffsetCalibrationData()
     * It implies application has gathered calibration data thanks to
     * @a VL53L4CX_GetOffsetCalibrationData() after an initial calibration stage.
     *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE     Success
     * @return  "Other error code"    See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_DataInit();

    /**
     * @brief Wait for device booted after chip enable (hardware standby)
     *
     * @return  VL53L4CX_ERROR_NONE     Success
     * @return  "Other error code"    See ::VL53L4CX_Error
     *
     */
    VL53L4CX_Error VL53L4CX_WaitDeviceBooted();

    /**
     * @brief  Set the distance mode
     * @par Function Description
     * Set the distance mode to be used for the next ranging.<br>
     * The modes Short, Medium and Long are used to optimize the ranging accuracy
     * in a specific range of distance.<br> The user select one of these modes to
     * select the distance range.
     * @note This function doesn't Access to the device
     *
     * @warning This function should be called after @a VL53L4CX_DataInit().

     * @param   DistanceMode          Distance mode to apply, valid values are:
     * @li VL53L4CX_DISTANCEMODE_SHORT
     * @li VL53L4CX_DISTANCEMODE_MEDIUM
     * @li VL53L4CX_DISTANCEMODE_LONG
     * @return  VL53L4CX_ERROR_NONE               Success
     * @return  VL53L4CX_ERROR_MODE_NOT_SUPPORTED This error occurs when DistanceMode
     *                                          is not in the supported list
     * @return  "Other error code"              See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetDistanceMode(VL53L4CX_DistanceModes DistanceMode);

    /**
     * @brief  Get the distance mode
     * @par Function Description
     * Get the distance mode used for the next ranging.
     *
     * @param   *pDistanceMode        Pointer to Distance mode
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetDistanceMode(VL53L4CX_DistanceModes *pDistanceMode);


    /**
     * @brief Set Ranging Timing Budget in microseconds
     *
     * @par Function Description
     * Defines the maximum time allowed by the user to the device to run a
     * full ranging sequence for the current mode (ranging, histogram, ASL ...)
     *
     * @param MeasurementTimingBudgetMicroSeconds  Max measurement time in
     * microseconds.
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  VL53L4CX_ERROR_INVALID_PARAMS  Error timing parameter not
     *                                       supported.
     *                                       The maximum accepted value for the
     *                                       computed timing budget is 10 seconds
     *                                       the minimum value depends on the preset
     *                                       mode selected.
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetMeasurementTimingBudgetMicroSeconds(uint32_t MeasurementTimingBudgetMicroSeconds);

    /**
     * @brief Get Ranging Timing Budget in microseconds
     *
     * @par Function Description
     * Returns the programmed the maximum time allowed by the user to the
     * device to run a full ranging sequence for the current mode
     * (ranging, histogram, ...)
     *
     * @param   pMeasurementTimingBudgetMicroSeconds   Max measurement time in
     * microseconds.
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetMeasurementTimingBudgetMicroSeconds(uint32_t *pMeasurementTimingBudgetMicroSeconds);

    /**
     * @brief Set the ROI  to be used for ranging
     *
     * @par Function Description
     * The user defined ROI is a rectangle described as per the following system
     * from the Top Left corner to the Bottom Right corner.
     * <br>Minimal ROI size is 4x4 spads
     * @image html roi_coord.png
     *
     * @param   pUserROi                 Pointer to the Structure defining the ROI
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetUserROI(VL53L4CX_UserRoi_t *pUserROi);

    /**
     * @brief Get the ROI managed by the Device
     *
     * @par Function Description
     * Get the ROI managed by the Device
     *
     * @param   pUserROi                 Pointer to the Structure defining the ROI
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetUserROI(VL53L4CX_UserRoi_t *pUserROi);

    /**
     * @brief Start device measurement
     *
     * @details Started measurement will depend on distance parameter set through
     * @a VL53L4CX_SetDistanceMode()
     *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE                  Success
     * @return  VL53L4CX_ERROR_TIME_OUT    Time out on start measurement
     * @return  VL53L4CX_ERROR_INVALID_PARAMS This error might occur in timed mode
     * when inter measurement period is smaller or too close to the timing budget.
     * In such case measurements are not started and user must correct the timings
     * passed to @a VL53L4CX_SetMeasurementTimingBudgetMicroSeconds() and
     * @a VL53L4CX_SetInterMeasurementPeriodMilliSeconds() functions.
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_StartMeasurement();

    /**
     * @brief Stop device measurement
     *
     * @details Will set the device in standby mode at end of current measurement\n
     *          Not necessary in single mode as device shall return automatically
     *          in standby mode at end of measurement.
     *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE    Success
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_StopMeasurement();

    /**
     * @brief Clear the Interrupt flag and start new measurement
     * *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE    Success
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_ClearInterruptAndStartMeasurement();

    /**
     * @brief Return Measurement Data Ready
     *
     * @par Function Description
     * This function indicate that a measurement data is ready.
     * This function is used for non-blocking capture.
     *
     * @note This function Access to the device
     *
     * @param   pMeasurementDataReady  Pointer to Measurement Data Ready.
     * 0 = data not ready, 1 = data ready
     * @return  VL53L4CX_ERROR_NONE      Success
     * @return  "Other error code"     See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetMeasurementDataReady(uint8_t *pMeasurementDataReady);

    /**
     * @brief Wait for measurement data ready.
     * Blocking function.
     * Note that the timeout is given by:
     * VL53L4CX_RANGE_COMPLETION_POLLING_TIMEOUT_MS defined in def.h
     *
     *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  VL53L4CX_ERROR_TIME_OUT In case of timeout
     */
    VL53L4CX_Error VL53L4CX_WaitMeasurementDataReady();


    /**
     * @brief Retrieve all measurements from device with the current setup
     *
     * @par Function Description
     * Get data from last successful Ranging measurement
     *
     * @warning USER must call @a VL53L4CX_ClearInterruptAndStartMeasurement() prior
     * to call again this function
     *
     * @note This function Access to the device
     *
     * @note The first valid value returned by this function will have a range
     * status equal to VL53L4CX_RANGESTATUS_RANGE_VALID_NO_WRAP_CHECK which means that
     * the data is valid but no wrap around check have been done. User should take
     * care about that.
     *
     * @param   pMultiRangingData        Pointer to the data structure to fill up.
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetMultiRangingData(VL53L4CX_MultiRangingData_t *pMultiRangingData);

    /**
     * @brief Get Additional Data
     *
     * @par Function Description
     * This function is used to get lld debugging data on the last histogram
     * measurement. shall be called when a new measurement is ready (interrupt or
     * positive VL53L4CX_GetMeasurementDataReady() polling) and before a call to
     * VL53L4CX_ClearInterruptAndStartMeasurement().
     *
     * @param   pAdditionalData          Pointer to Additional data
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetAdditionalData(VL53L4CX_AdditionalData_t *pAdditionalData);

    /**
     * @brief Set Tuning Parameter value for a given parameter ID
     *
     * @par Function Description
     * This function is used to improve the performance of the device. It permit to
     * change a particular value used for a timeout or a threshold or a constant
     * in an algorithm. The function will change the value of the parameter
     * identified by an unique ID.
     *
     * @note This function doesn't Access to the device
     *
     * @param   TuningParameterId            Tuning Parameter ID
     * @param   TuningParameterValue         Tuning Parameter Value
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetTuningParameter(uint16_t TuningParameterId, int32_t TuningParameterValue);

    /**
     * @brief Get Tuning Parameter value for a given parameter ID
     *
     * @par Function Description
     * This function is used to get the value of the parameter
     * identified by an unique ID.
     *
     * @note This function doesn't Access to the device
     *
     * @param   TuningParameterId            Tuning Parameter ID
     * @param   pTuningParameterValue        Pointer to Tuning Parameter Value
     * for a given TuningParameterId.
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetTuningParameter(uint16_t TuningParameterId, int32_t *pTuningParameterValue);

    /**
     * @brief Performs Reference Spad Management
     *
     * @par Function Description
     * The reference SPAD initialization procedure determines the minimum amount
     * of reference spads to be enables to achieve a target reference signal rate
     * and should be performed once during initialization.
     *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_PerformRefSpadManagement();

    /**
     * @brief Enable/Disable dynamic Xtalk compensation feature
     *
     * Enable/Disable dynamic Xtalk compensation (aka smudge correction).
     *
     * @param   Mode   Set the smudge correction mode
     * See ::VL53L4CX_SmudgeCorrectionModes
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SmudgeCorrectionEnable(VL53L4CX_SmudgeCorrectionModes Mode);


    /**
     * @brief Enable/Disable Cross talk compensation feature
     *
     * Enable/Disable Cross Talk correction.
     *
     * @param   XTalkCompensationEnable   Cross talk compensation
     *  to be set 0 = disabled or 1 = enabled.
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetXTalkCompensationEnable(uint8_t XTalkCompensationEnable);

    /**
     * @brief Get Cross talk compensation rate enable
     *
     * Get if the Cross Talk is Enabled or Disabled.
     *
     * @note This function doesn't access to the device
     *
     * @param   pXTalkCompensationEnable   Pointer to the Cross talk compensation
     *  state 0=disabled or 1 = enabled
     * @return  VL53L4CX_ERROR_NONE        Success
     * @return  "Other error code"       See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetXTalkCompensationEnable(uint8_t *pXTalkCompensationEnable);

    /**
     * @brief Perform XTalk Calibration
     *
     * @details Perform a XTalk calibration of the Device.
     * This function will launch a  measurement, if interrupts
     * are enabled an interrupt will be done.
     * This function will clear the interrupt generated automatically.
     * This function will program a new value for the XTalk compensation
     * and it will enable the cross talk before exit.
     *
     * @warning This function is a blocking function
     *
     * @note This function Access to the device
     *
     * the calibration sets appropriate
     * distance mode and thus override existing one<br>
     * The calibration uses a target which should be located at least @60cm from the
     * device. The actual location of the target shall be passed
     * through the bare driver tuning parameters table
     *
     * @return  VL53L4CX_ERROR_NONE    Success
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_PerformXTalkCalibration();


    /**
     * @brief Define the mode to be used for the offset correction
     *
     * Define the mode to be used for the offset correction.
     *
     * @param   OffsetCorrectionMode      Offset Correction Mode valid values are:
     * @li                                VL53L4CX_OFFSETCORRECTIONMODE_STANDARD
     * @li                                VL53L4CX_OFFSETCORRECTIONMODE_PERVCSEL
     *
     * @return  VL53L4CX_ERROR_NONE         Success
     * @return  "Other error code"        See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetOffsetCorrectionMode(VL53L4CX_OffsetCorrectionModes OffsetCorrectionMode);


    /**
     * @brief Perform Offset simple Calibration
     *
     * @details Perform a very simple offset calibration of the Device.
     * This function will launch few ranging measurements and computes offset
     * calibration. The preset mode and the distance mode MUST be set by the
     * application before to call this function.
     *
     * @warning This function is a blocking function
     *
     * @note This function Access to the device
     *
     * @param   CalDistanceMilliMeter     Calibration distance value used for the
     * offset compensation.
     *
     * @return  VL53L4CX_ERROR_NONE
     * @return  VL53L4CX_ERROR_OFFSET_CAL_NO_SAMPLE_FAIL the calibration failed by
     * lack of valid measurements
     * @return  VL53L4CX_WARNING_OFFSET_CAL_SIGMA_TOO_HIGH means that the target
     * distance combined to the number of loops performed in the calibration lead to
     * an internal overflow. Try to reduce the distance of the target (140 mm)
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_PerformOffsetSimpleCalibration(int32_t CalDistanceMilliMeter);

    /**
     * @brief Perform Offset simple Calibration with a "zero distance" target
     *
     * @details Perform a simple offset calibration of the Device.
     * This function will launch few ranging measurements and computes offset
     * calibration. The preset mode and the distance mode MUST be set by the
     * application before to call this function.
     * A target must be place very close to the device.
     * Ideally the target shall be touching the coverglass.
     *
     * @warning This function is a blocking function
     *
     * @note This function Access to the device
     *
     * @return  VL53L4CX_ERROR_NONE
     * @return  VL53L4CX_ERROR_OFFSET_CAL_NO_SAMPLE_FAIL the calibration failed by
     * lack of valid measurements
     * @return  VL53L4CX_WARNING_OFFSET_CAL_SIGMA_TOO_HIGH means that the target
     * distance is too large, try to put the target closer to the device
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_PerformOffsetZeroDistanceCalibration();


    /**
     * @brief Perform Offset per Vcsel Calibration. i.e. per distance mode
     *
     * @details Perform offset calibration of the Device depending on the
     * three distance mode settings: short, medium and long.
     * This function will launch few ranging measurements and computes offset
     * calibration in each of the three distance modes.
     * The preset mode MUST be set by the application before to call this function.
     *
     * @warning This function is a blocking function
     *
     * @note This function Access to the device
     *
     * @param   CalDistanceMilliMeter     Distance of the target used for the
     * offset compensation calibration.
     *
     * @return  VL53L4CX_ERROR_NONE
     * @return  VL53L4CX_ERROR_OFFSET_CAL_NO_SAMPLE_FAIL the calibration failed by
     * lack of valid measurements
     * @return  VL53L4CX_WARNING_OFFSET_CAL_SIGMA_TOO_HIGH means that the target
     * distance combined to the number of loops performed in the calibration lead to
     * an internal overflow. Try to reduce the distance of the target (140 mm)
     * @return  "Other error code"   See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_PerformOffsetPerVcselCalibration(int32_t CalDistanceMilliMeter);


    /**
     * @brief Sets the Calibration Data.
     *
     * @par Function Description
     * This function set all the Calibration Data issued from the functions
     * @a VL53L4CX_PerformRefSpadManagement(), @a VL53L4CX_PerformXTalkCalibration,
     * @a VL53L4CX_PerformOffsetCalibration()
     *
     * @note This function doesn't Accesses the device
     *
     * @param   *pCalibrationData            Pointer to Calibration data to be set.
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  VL53L4CX_ERROR_INVALID_PARAMS  pCalibrationData points to an older
     * version of the inner structure. Need for support to convert its content.
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_SetCalibrationData(VL53L4CX_CalibrationData_t *pCalibrationData);

    /**
     * @brief Gets the Calibration Data.
     *
     * @par Function Description
     * This function get all the Calibration Data issued from the functions
     * @a VL53L4CX_PerformRefSpadManagement(), @a VL53L4CX_PerformXTalkCalibration,
     * @a VL53L4CX_PerformOffsetCalibration()
     *
     * @note This function doesn't Accesses the device
     *
     * @param   *pCalibrationData            pointer where to store Calibration
     *  data.
     * @return  VL53L4CX_ERROR_NONE            Success
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetCalibrationData(VL53L4CX_CalibrationData_t  *pCalibrationData);


    /**
     * @brief Gets the optical center.
     *
     * @par Function Description
     * This function get the optical center issued from the nvm set at FTM stage
     * expressed in the same coordinate system as the ROI are
     *
     * @note This function doesn't Accesses the device
     *
     * @param   pOpticalCenterX              pointer to the X position of center
     * in 16.16 fix point
     * @param   pOpticalCenterY              pointer to the Y position of center
     * in 16.16 fix point
     * @return  VL53L1_ERROR_NONE            Success
     * @return  "Other error code"           See ::VL53L4CX_Error
     */
    VL53L4CX_Error VL53L4CX_GetOpticalCenter(FixPoint1616_t *pOpticalCenterX,
                                             FixPoint1616_t *pOpticalCenterY);


  protected:

    /* vl53lx_api_core.h functions */

    VL53L4CX_Error VL53L4CX_get_version(
      VL53L4CX_DEV            Dev,
      VL53L4CX_ll_version_t  *pversion);





    VL53L4CX_Error VL53L4CX_data_init(
      VL53L4CX_DEV         Dev,
      uint8_t            read_p2p_data);




    VL53L4CX_Error VL53L4CX_read_p2p_data(
      VL53L4CX_DEV      Dev);




    VL53L4CX_Error VL53L4CX_set_part_to_part_data(
      VL53L4CX_DEV                            Dev,
      VL53L4CX_calibration_data_t            *pcal_data);




    VL53L4CX_Error VL53L4CX_get_part_to_part_data(
      VL53L4CX_DEV                            Dev,
      VL53L4CX_calibration_data_t            *pcal_data);




    VL53L4CX_Error VL53L4CX_get_tuning_debug_data(
      VL53L4CX_DEV                            Dev,
      VL53L4CX_tuning_parameters_t            *ptun_data);




    VL53L4CX_Error VL53L4CX_set_inter_measurement_period_ms(
      VL53L4CX_DEV          Dev,
      uint32_t            inter_measurement_period_ms);




    VL53L4CX_Error VL53L4CX_get_inter_measurement_period_ms(
      VL53L4CX_DEV          Dev,
      uint32_t           *pinter_measurement_period_ms);




    VL53L4CX_Error VL53L4CX_set_timeouts_us(
      VL53L4CX_DEV          Dev,
      uint32_t            phasecal_config_timeout_us,
      uint32_t            mm_config_timeout_us,
      uint32_t            range_config_timeout_us);




    VL53L4CX_Error VL53L4CX_get_timeouts_us(
      VL53L4CX_DEV          Dev,
      uint32_t           *pphasecal_config_timeout_us,
      uint32_t           *pmm_config_timeout_us,
      uint32_t           *prange_config_timeout_us);




    VL53L4CX_Error VL53L4CX_set_user_zone(
      VL53L4CX_DEV          Dev,
      VL53L4CX_user_zone_t *puser_zone);




    VL53L4CX_Error VL53L4CX_get_user_zone(
      VL53L4CX_DEV          Dev,
      VL53L4CX_user_zone_t *puser_zone);




    VL53L4CX_Error VL53L4CX_get_mode_mitigation_roi(
      VL53L4CX_DEV          Dev,
      VL53L4CX_user_zone_t *pmm_roi);

    VL53L4CX_Error VL53L4CX_init_zone_config_histogram_bins(VL53L4CX_zone_config_t *pdata);
    VL53L4CX_Error VL53L4CX_set_zone_config(VL53L4CX_DEV Dev, VL53L4CX_zone_config_t *pzone_cfg);
    VL53L4CX_Error VL53L4CX_get_zone_config(VL53L4CX_DEV Dev, VL53L4CX_zone_config_t *pzone_cfg);



    VL53L4CX_Error VL53L4CX_set_preset_mode(
      VL53L4CX_DEV                   Dev,
      VL53L4CX_DevicePresetModes     device_preset_mode,
      uint16_t                     dss_config__target_total_rate_mcps,
      uint32_t                     phasecal_config_timeout_us,
      uint32_t                     mm_config_timeout_us,
      uint32_t                     range_config_timeout_us,
      uint32_t                     inter_measurement_period_ms);




    VL53L4CX_Error VL53L4CX_get_preset_mode_timing_cfg(
      VL53L4CX_DEV                   Dev,
      VL53L4CX_DevicePresetModes     device_preset_mode,
      uint16_t                    *pdss_config__target_total_rate_mcps,
      uint32_t                    *pphasecal_config_timeout_us,
      uint32_t                    *pmm_config_timeout_us,
      uint32_t                    *prange_config_timeout_us);



    VL53L4CX_Error VL53L4CX_enable_xtalk_compensation(
      VL53L4CX_DEV                 Dev);



    VL53L4CX_Error VL53L4CX_disable_xtalk_compensation(
      VL53L4CX_DEV                 Dev);




    void VL53L4CX_get_xtalk_compensation_enable(
      VL53L4CX_DEV    Dev,
      uint8_t       *pcrosstalk_compensation_enable);



    VL53L4CX_Error VL53L4CX_init_and_start_range(
      VL53L4CX_DEV                      Dev,
      uint8_t                         measurement_mode,
      VL53L4CX_DeviceConfigLevel        device_config_level);




    VL53L4CX_Error VL53L4CX_stop_range(
      VL53L4CX_DEV  Dev);




    VL53L4CX_Error VL53L4CX_get_measurement_results(
      VL53L4CX_DEV                  Dev,
      VL53L4CX_DeviceResultsLevel   device_result_level);




    VL53L4CX_Error VL53L4CX_get_device_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_DeviceResultsLevel  device_result_level,
      VL53L4CX_range_results_t    *prange_results);




    VL53L4CX_Error VL53L4CX_clear_interrupt_and_enable_next_range(
      VL53L4CX_DEV       Dev,
      uint8_t          measurement_mode);




    VL53L4CX_Error VL53L4CX_get_histogram_bin_data(
      VL53L4CX_DEV                   Dev,
      VL53L4CX_histogram_bin_data_t *phist_data);




    void VL53L4CX_copy_sys_and_core_results_to_range_results(
      int32_t                           gain_factor,
      VL53L4CX_system_results_t          *psys,
      VL53L4CX_core_results_t            *pcore,
      VL53L4CX_range_results_t           *presults);



    VL53L4CX_Error VL53L4CX_set_zone_dss_config(
      VL53L4CX_DEV                      Dev,
      VL53L4CX_zone_private_dyn_cfg_t  *pzone_dyn_cfg);




    VL53L4CX_Error VL53L4CX_set_dmax_mode(
      VL53L4CX_DEV              Dev,
      VL53L4CX_DeviceDmaxMode   dmax_mode);



    VL53L4CX_Error VL53L4CX_get_dmax_mode(
      VL53L4CX_DEV               Dev,
      VL53L4CX_DeviceDmaxMode   *pdmax_mode);




    VL53L4CX_Error VL53L4CX_get_dmax_calibration_data(
      VL53L4CX_DEV                      Dev,
      VL53L4CX_DeviceDmaxMode           dmax_mode,
      VL53L4CX_dmax_calibration_data_t *pdmax_cal);




    VL53L4CX_Error VL53L4CX_set_offset_correction_mode(
      VL53L4CX_DEV                     Dev,
      VL53L4CX_OffsetCalibrationMode   offset_cor_mode);




    VL53L4CX_Error VL53L4CX_get_offset_correction_mode(
      VL53L4CX_DEV                    Dev,
      VL53L4CX_OffsetCorrectionMode  *poffset_cor_mode);




    VL53L4CX_Error VL53L4CX_get_tuning_parm(
      VL53L4CX_DEV                     Dev,
      VL53L4CX_TuningParms             tuning_parm_key,
      int32_t                       *ptuning_parm_value);



    VL53L4CX_Error VL53L4CX_set_tuning_parm(
      VL53L4CX_DEV                     Dev,
      VL53L4CX_TuningParms             tuning_parm_key,
      int32_t                        tuning_parm_value);



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_enable(
      VL53L4CX_DEV                     Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_disable(
      VL53L4CX_DEV                     Dev
    );




    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_apply_enable(
      VL53L4CX_DEV                          Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_apply_disable(
      VL53L4CX_DEV                          Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_single_apply_enable(
      VL53L4CX_DEV                          Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_single_apply_disable(
      VL53L4CX_DEV                          Dev
    );



    VL53L4CX_Error VL53L4CX_get_current_xtalk_settings(
      VL53L4CX_DEV                          Dev,
      VL53L4CX_xtalk_calibration_results_t *pxtalk
    );



    VL53L4CX_Error VL53L4CX_set_current_xtalk_settings(
      VL53L4CX_DEV                          Dev,
      VL53L4CX_xtalk_calibration_results_t *pxtalk
    );

    VL53L4CX_Error VL53L4CX_load_patch(VL53L4CX_DEV Dev);

    VL53L4CX_Error VL53L4CX_unload_patch(VL53L4CX_DEV Dev);


    /* vl53lx_api_calibration.h functions */

    VL53L4CX_Error VL53L4CX_run_ref_spad_char(VL53L4CX_DEV Dev,
                                              VL53L4CX_Error            *pcal_status);




    VL53L4CX_Error VL53L4CX_run_device_test(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_DeviceTestMode      device_test_mode);




    VL53L4CX_Error VL53L4CX_get_and_avg_xtalk_samples(
      VL53L4CX_DEV                    Dev,
      uint8_t                       num_of_samples,
      uint8_t                       measurement_mode,
      int16_t                       xtalk_filter_thresh_max_mm,
      int16_t                       xtalk_filter_thresh_min_mm,
      uint16_t                      xtalk_max_valid_rate_kcps,
      uint8_t                       xtalk_result_id,
      uint8_t                       xtalk_histo_id,
      VL53L4CX_xtalk_range_results_t *pxtalk_results,
      VL53L4CX_histogram_bin_data_t  *psum_histo,
      VL53L4CX_histogram_bin_data_t  *pavg_histo);





    VL53L4CX_Error   VL53L4CX_run_phasecal_average(
      VL53L4CX_DEV              Dev,
      uint8_t                 measurement_mode,
      uint8_t                 phasecal_result__vcsel_start,
      uint16_t                phasecal_num_of_samples,
      VL53L4CX_range_results_t *prange_results,
      uint16_t               *pphasecal_result__reference_phase,
      uint16_t               *pzero_distance_phase);




    void VL53L4CX_hist_xtalk_extract_data_init(
      VL53L4CX_hist_xtalk_extract_data_t   *pxtalk_data);



    VL53L4CX_Error VL53L4CX_hist_xtalk_extract_update(
      int16_t                             target_distance_mm,
      uint16_t                            target_width_oversize,
      VL53L4CX_histogram_bin_data_t        *phist_bins,
      VL53L4CX_hist_xtalk_extract_data_t   *pxtalk_data);



    VL53L4CX_Error VL53L4CX_hist_xtalk_extract_fini(
      VL53L4CX_histogram_bin_data_t        *phist_bins,
      VL53L4CX_hist_xtalk_extract_data_t   *pxtalk_data,
      VL53L4CX_xtalk_calibration_results_t *pxtalk_cal,
      VL53L4CX_xtalk_histogram_shape_t     *pxtalk_shape);




    VL53L4CX_Error   VL53L4CX_run_hist_xtalk_extraction(
      VL53L4CX_DEV                    Dev,
      int16_t                       cal_distance_mm,
      VL53L4CX_Error                 *pcal_status);


    /* vl53lx_api_debug.h functions */

    VL53L4CX_Error VL53L4CX_decode_calibration_data_buffer(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_calibration_data_t *pdata);






    VL53L4CX_Error VL53L4CX_get_nvm_debug_data(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_decoded_nvm_data_t *pdata);



    VL53L4CX_Error VL53L4CX_get_histogram_debug_data(
      VL53L4CX_DEV                   Dev,
      VL53L4CX_histogram_bin_data_t *pdata);


    VL53L4CX_Error VL53L4CX_get_additional_data(
      VL53L4CX_DEV                Dev,
      VL53L4CX_additional_data_t *pdata);


    VL53L4CX_Error VL53L4CX_get_xtalk_debug_data(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_xtalk_debug_data_t *pdata);


    VL53L4CX_Error VL53L4CX_get_offset_debug_data(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_offset_debug_data_t *pdata);


    /* vl53lx_api_preset_modes.h functions */

    VL53L4CX_Error VL53L4CX_init_refspadchar_config_struct(
      VL53L4CX_refspadchar_config_t     *pdata);




    VL53L4CX_Error VL53L4CX_init_ssc_config_struct(
      VL53L4CX_ssc_config_t     *pdata);




    VL53L4CX_Error VL53L4CX_init_xtalk_config_struct(
      VL53L4CX_customer_nvm_managed_t *pnvm,
      VL53L4CX_xtalk_config_t   *pdata);



    VL53L4CX_Error VL53L4CX_init_xtalk_extract_config_struct(
      VL53L4CX_xtalkextract_config_t   *pdata);



    VL53L4CX_Error VL53L4CX_init_offset_cal_config_struct(
      VL53L4CX_offsetcal_config_t   *pdata);



    VL53L4CX_Error VL53L4CX_init_zone_cal_config_struct(
      VL53L4CX_zonecal_config_t   *pdata);



    VL53L4CX_Error VL53L4CX_init_hist_post_process_config_struct(
      uint8_t                              xtalk_compensation_enable,
      VL53L4CX_hist_post_process_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_init_dmax_calibration_data_struct(
      VL53L4CX_dmax_calibration_data_t   *pdata);




    VL53L4CX_Error VL53L4CX_init_tuning_parm_storage_struct(
      VL53L4CX_tuning_parm_storage_t   *pdata);



    VL53L4CX_Error VL53L4CX_init_hist_gen3_dmax_config_struct(
      VL53L4CX_hist_gen3_dmax_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_preset_mode_standard_ranging(
      VL53L4CX_static_config_t     *pstatic,
      VL53L4CX_histogram_config_t  *phistogram,
      VL53L4CX_general_config_t    *pgeneral,
      VL53L4CX_timing_config_t     *ptiming,
      VL53L4CX_dynamic_config_t    *pdynamic,
      VL53L4CX_system_control_t    *psystem,
      VL53L4CX_tuning_parm_storage_t *ptuning_parms,
      VL53L4CX_zone_config_t       *pzone_cfg);




    VL53L4CX_Error VL53L4CX_preset_mode_histogram_ranging(
      VL53L4CX_hist_post_process_config_t *phistpostprocess,
      VL53L4CX_static_config_t            *pstatic,
      VL53L4CX_histogram_config_t         *phistogram,
      VL53L4CX_general_config_t           *pgeneral,
      VL53L4CX_timing_config_t            *ptiming,
      VL53L4CX_dynamic_config_t           *pdynamic,
      VL53L4CX_system_control_t           *psystem,
      VL53L4CX_tuning_parm_storage_t      *ptuning_parms,
      VL53L4CX_zone_config_t              *pzone_cfg);




    VL53L4CX_Error VL53L4CX_preset_mode_histogram_long_range(
      VL53L4CX_hist_post_process_config_t *phistpostprocess,
      VL53L4CX_static_config_t            *pstatic,
      VL53L4CX_histogram_config_t         *phistogram,
      VL53L4CX_general_config_t           *pgeneral,
      VL53L4CX_timing_config_t            *ptiming,
      VL53L4CX_dynamic_config_t           *pdynamic,
      VL53L4CX_system_control_t           *psystem,
      VL53L4CX_tuning_parm_storage_t      *ptuning_parms,
      VL53L4CX_zone_config_t              *pzone_cfg);




    VL53L4CX_Error VL53L4CX_preset_mode_histogram_medium_range(
      VL53L4CX_hist_post_process_config_t *phistpostprocess,
      VL53L4CX_static_config_t            *pstatic,
      VL53L4CX_histogram_config_t         *phistogram,
      VL53L4CX_general_config_t           *pgeneral,
      VL53L4CX_timing_config_t            *ptiming,
      VL53L4CX_dynamic_config_t           *pdynamic,
      VL53L4CX_system_control_t           *psystem,
      VL53L4CX_tuning_parm_storage_t      *ptuning_parms,
      VL53L4CX_zone_config_t              *pzone_cfg);




    VL53L4CX_Error VL53L4CX_preset_mode_histogram_short_range(
      VL53L4CX_hist_post_process_config_t *phistpostprocess,
      VL53L4CX_static_config_t            *pstatic,
      VL53L4CX_histogram_config_t         *phistogram,
      VL53L4CX_general_config_t           *pgeneral,
      VL53L4CX_timing_config_t            *ptiming,
      VL53L4CX_dynamic_config_t           *pdynamic,
      VL53L4CX_system_control_t           *psystem,
      VL53L4CX_tuning_parm_storage_t      *ptuning_parms,
      VL53L4CX_zone_config_t              *pzone_cfg);




    void VL53L4CX_copy_hist_cfg_to_static_cfg(
      VL53L4CX_histogram_config_t  *phistogram,
      VL53L4CX_static_config_t     *pstatic,
      VL53L4CX_general_config_t    *pgeneral,
      VL53L4CX_timing_config_t     *ptiming,
      VL53L4CX_dynamic_config_t    *pdynamic);



    void VL53L4CX_copy_hist_bins_to_static_cfg(
      VL53L4CX_histogram_config_t *phistogram,
      VL53L4CX_static_config_t    *pstatic,
      VL53L4CX_timing_config_t    *ptiming);

    /* vl53lx_core.h functions */

    void VL53L4CX_init_version(
      VL53L4CX_DEV         Dev);




    void VL53L4CX_init_ll_driver_state(
      VL53L4CX_DEV         Dev,
      VL53L4CX_DeviceState ll_state);




    VL53L4CX_Error VL53L4CX_update_ll_driver_rd_state(
      VL53L4CX_DEV         Dev);




    VL53L4CX_Error VL53L4CX_check_ll_driver_rd_state(
      VL53L4CX_DEV         Dev);




    VL53L4CX_Error VL53L4CX_update_ll_driver_cfg_state(
      VL53L4CX_DEV         Dev);




    void VL53L4CX_copy_rtn_good_spads_to_buffer(
      VL53L4CX_nvm_copy_data_t  *pdata,
      uint8_t                 *pbuffer);




    void VL53L4CX_init_system_results(
      VL53L4CX_system_results_t      *pdata);




    void V53L1_init_zone_results_structure(
      uint8_t                 active_zones,
      VL53L4CX_zone_results_t  *pdata);




    void V53L1_init_zone_dss_configs(
      VL53L4CX_DEV              Dev);




    void VL53L4CX_init_histogram_config_structure(
      uint8_t   even_bin0,
      uint8_t   even_bin1,
      uint8_t   even_bin2,
      uint8_t   even_bin3,
      uint8_t   even_bin4,
      uint8_t   even_bin5,
      uint8_t   odd_bin0,
      uint8_t   odd_bin1,
      uint8_t   odd_bin2,
      uint8_t   odd_bin3,
      uint8_t   odd_bin4,
      uint8_t   odd_bin5,
      VL53L4CX_histogram_config_t  *pdata);



    void VL53L4CX_init_histogram_multizone_config_structure(
      uint8_t   even_bin0,
      uint8_t   even_bin1,
      uint8_t   even_bin2,
      uint8_t   even_bin3,
      uint8_t   even_bin4,
      uint8_t   even_bin5,
      uint8_t   odd_bin0,
      uint8_t   odd_bin1,
      uint8_t   odd_bin2,
      uint8_t   odd_bin3,
      uint8_t   odd_bin4,
      uint8_t   odd_bin5,
      VL53L4CX_histogram_config_t  *pdata);




    void VL53L4CX_init_xtalk_bin_data_struct(
      uint32_t                        bin_value,
      uint16_t                        VL53L4CX_p_021,
      VL53L4CX_xtalk_histogram_shape_t *pdata);




    void VL53L4CX_i2c_encode_uint16_t(
      uint16_t    ip_value,
      uint16_t    count,
      uint8_t    *pbuffer);




    uint16_t VL53L4CX_i2c_decode_uint16_t(
      uint16_t    count,
      uint8_t    *pbuffer);




    void VL53L4CX_i2c_encode_int16_t(
      int16_t     ip_value,
      uint16_t    count,
      uint8_t    *pbuffer);




    int16_t VL53L4CX_i2c_decode_int16_t(
      uint16_t    count,
      uint8_t    *pbuffer);




    void VL53L4CX_i2c_encode_uint32_t(
      uint32_t    ip_value,
      uint16_t    count,
      uint8_t    *pbuffer);




    uint32_t VL53L4CX_i2c_decode_uint32_t(
      uint16_t    count,
      uint8_t    *pbuffer);




    uint32_t VL53L4CX_i2c_decode_with_mask(
      uint16_t    count,
      uint8_t    *pbuffer,
      uint32_t    bit_mask,
      uint32_t    down_shift,
      uint32_t    offset);




    void VL53L4CX_i2c_encode_int32_t(
      int32_t     ip_value,
      uint16_t    count,
      uint8_t    *pbuffer);




    int32_t VL53L4CX_i2c_decode_int32_t(
      uint16_t    count,
      uint8_t    *pbuffer);




    VL53L4CX_Error VL53L4CX_start_test(
      VL53L4CX_DEV     Dev,
      uint8_t        test_mode__ctrl);




    VL53L4CX_Error VL53L4CX_set_firmware_enable_register(
      VL53L4CX_DEV         Dev,
      uint8_t            value);




    VL53L4CX_Error VL53L4CX_enable_firmware(
      VL53L4CX_DEV         Dev);




    VL53L4CX_Error VL53L4CX_disable_firmware(
      VL53L4CX_DEV         Dev);




    VL53L4CX_Error VL53L4CX_set_powerforce_register(
      VL53L4CX_DEV         Dev,
      uint8_t            value);





    VL53L4CX_Error VL53L4CX_enable_powerforce(
      VL53L4CX_DEV         Dev);



    VL53L4CX_Error VL53L4CX_disable_powerforce(
      VL53L4CX_DEV         Dev);





    VL53L4CX_Error VL53L4CX_clear_interrupt(
      VL53L4CX_DEV         Dev);





    VL53L4CX_Error VL53L4CX_force_shadow_stream_count_to_zero(
      VL53L4CX_DEV         Dev);




    uint32_t VL53L4CX_calc_macro_period_us(
      uint16_t fast_osc_frequency,
      uint8_t  VL53L4CX_p_005);




    uint16_t VL53L4CX_calc_range_ignore_threshold(
      uint32_t central_rate,
      int16_t  x_gradient,
      int16_t  y_gradient,
      uint8_t  rate_mult);




    uint32_t VL53L4CX_calc_timeout_mclks(
      uint32_t  timeout_us,
      uint32_t  macro_period_us);



    uint16_t VL53L4CX_calc_encoded_timeout(
      uint32_t  timeout_us,
      uint32_t  macro_period_us);




    uint32_t VL53L4CX_calc_timeout_us(
      uint32_t  timeout_mclks,
      uint32_t  macro_period_us);



    uint32_t VL53L4CX_calc_decoded_timeout_us(
      uint16_t  timeout_encoded,
      uint32_t  macro_period_us);




    uint16_t VL53L4CX_encode_timeout(
      uint32_t timeout_mclks);




    uint32_t VL53L4CX_decode_timeout(
      uint16_t encoded_timeout);




    VL53L4CX_Error  VL53L4CX_calc_timeout_register_values(
      uint32_t                 phasecal_config_timeout_us,
      uint32_t                 mm_config_timeout_us,
      uint32_t                 range_config_timeout_us,
      uint16_t                 fast_osc_frequency,
      VL53L4CX_general_config_t *pgeneral,
      VL53L4CX_timing_config_t  *ptiming);




    uint8_t VL53L4CX_encode_vcsel_period(
      uint8_t VL53L4CX_p_030);




    uint32_t VL53L4CX_decode_unsigned_integer(
      uint8_t  *pbuffer,
      uint8_t   no_of_bytes);




    void   VL53L4CX_encode_unsigned_integer(
      uint32_t  ip_value,
      uint8_t   no_of_bytes,
      uint8_t  *pbuffer);




    VL53L4CX_Error VL53L4CX_hist_copy_and_scale_ambient_info(
      VL53L4CX_zone_hist_info_t        *pidata,
      VL53L4CX_histogram_bin_data_t    *podata);




    void  VL53L4CX_hist_get_bin_sequence_config(
      VL53L4CX_DEV                     Dev,
      VL53L4CX_histogram_bin_data_t   *pdata);




    VL53L4CX_Error  VL53L4CX_hist_phase_consistency_check(
      VL53L4CX_DEV                   Dev,
      VL53L4CX_zone_hist_info_t     *phist_prev,
      VL53L4CX_zone_objects_t       *prange_prev,
      VL53L4CX_range_results_t      *prange_curr);







    VL53L4CX_Error  VL53L4CX_hist_events_consistency_check(
      uint8_t                      event_sigma,
      uint16_t                     min_effective_spad_count,
      VL53L4CX_zone_hist_info_t     *phist_prev,
      VL53L4CX_object_data_t        *prange_prev,
      VL53L4CX_range_data_t         *prange_curr,
      int32_t                     *pevents_tolerance,
      int32_t                     *pevents_delta,
      VL53L4CX_DeviceError          *prange_status);







    VL53L4CX_Error  VL53L4CX_hist_merged_pulse_check(
      int16_t                      min_max_tolerance_mm,
      VL53L4CX_range_data_t         *pdata,
      VL53L4CX_DeviceError          *prange_status);






    VL53L4CX_Error  VL53L4CX_hist_xmonitor_consistency_check(
      VL53L4CX_DEV                   Dev,
      VL53L4CX_zone_hist_info_t     *phist_prev,
      VL53L4CX_zone_objects_t       *prange_prev,
      VL53L4CX_range_data_t         *prange_curr);






    VL53L4CX_Error  VL53L4CX_hist_wrap_dmax(
      VL53L4CX_hist_post_process_config_t *phistpostprocess,
      VL53L4CX_histogram_bin_data_t       *pcurrent,
      int16_t                           *pwrap_dmax_mm);




    void VL53L4CX_hist_combine_mm1_mm2_offsets(
      int16_t                              mm1_offset_mm,
      int16_t                              mm2_offset_mm,
      uint8_t                              encoded_mm_roi_centre,
      uint8_t                              encoded_mm_roi_size,
      uint8_t                              encoded_zone_centre,
      uint8_t                              encoded_zone_size,
      VL53L4CX_additional_offset_cal_data_t *pcal_data,
      uint8_t                             *pgood_spads,
      uint16_t                             aperture_attenuation,
      int16_t                             *prange_offset_mm);




    VL53L4CX_Error VL53L4CX_hist_xtalk_extract_calc_window(
      int16_t                             target_distance_mm,
      uint16_t                            target_width_oversize,
      VL53L4CX_histogram_bin_data_t        *phist_bins,
      VL53L4CX_hist_xtalk_extract_data_t   *pxtalk_data);




    VL53L4CX_Error VL53L4CX_hist_xtalk_extract_calc_event_sums(
      VL53L4CX_histogram_bin_data_t        *phist_bins,
      VL53L4CX_hist_xtalk_extract_data_t   *pxtalk_data);




    VL53L4CX_Error VL53L4CX_hist_xtalk_extract_calc_rate_per_spad(
      VL53L4CX_hist_xtalk_extract_data_t   *pxtalk_data);



    VL53L4CX_Error VL53L4CX_hist_xtalk_extract_calc_shape(
      VL53L4CX_hist_xtalk_extract_data_t  *pxtalk_data,
      VL53L4CX_xtalk_histogram_shape_t    *pxtalk_shape);



    VL53L4CX_Error VL53L4CX_hist_xtalk_shape_model(
      uint16_t                         events_per_bin,
      uint16_t                         pulse_centre,
      uint16_t                         pulse_width,
      VL53L4CX_xtalk_histogram_shape_t  *pxtalk_shape);




    uint16_t VL53L4CX_hist_xtalk_shape_model_interp(
      uint16_t      events_per_bin,
      uint32_t      phase_delta);




    void VL53L4CX_spad_number_to_byte_bit_index(
      uint8_t  spad_number,
      uint8_t *pbyte_index,
      uint8_t *pbit_index,
      uint8_t *pbit_mask);




    void VL53L4CX_encode_row_col(
      uint8_t  row,
      uint8_t  col,
      uint8_t *pspad_number);




    void VL53L4CX_decode_zone_size(
      uint8_t   encoded_xy_size,
      uint8_t  *pwidth,
      uint8_t  *pheight);




    void VL53L4CX_encode_zone_size(
      uint8_t  width,
      uint8_t  height,
      uint8_t *pencoded_xy_size);




    void VL53L4CX_decode_zone_limits(
      uint8_t   encoded_xy_centre,
      uint8_t   encoded_xy_size,
      int16_t  *px_ll,
      int16_t  *py_ll,
      int16_t  *px_ur,
      int16_t  *py_ur);




    uint8_t VL53L4CX_is_aperture_location(
      uint8_t   row,
      uint8_t   col);




    void VL53L4CX_calc_max_effective_spads(
      uint8_t     encoded_zone_centre,
      uint8_t     encoded_zone_size,
      uint8_t    *pgood_spads,
      uint16_t    aperture_attenuation,
      uint16_t   *pmax_effective_spads);




    void VL53L4CX_calc_mm_effective_spads(
      uint8_t     encoded_mm_roi_centre,
      uint8_t     encoded_mm_roi_size,
      uint8_t     encoded_zone_centre,
      uint8_t     encoded_zone_size,
      uint8_t    *pgood_spads,
      uint16_t    aperture_attenuation,
      uint16_t   *pmm_inner_effective_spads,
      uint16_t   *pmm_outer_effective_spads);




    void VL53L4CX_hist_copy_results_to_sys_and_core(
      VL53L4CX_histogram_bin_data_t      *pbins,
      VL53L4CX_range_results_t           *phist,
      VL53L4CX_system_results_t          *psys,
      VL53L4CX_core_results_t            *pcore);




    VL53L4CX_Error VL53L4CX_sum_histogram_data(
      VL53L4CX_histogram_bin_data_t *phist_input,
      VL53L4CX_histogram_bin_data_t *phist_output);




    VL53L4CX_Error VL53L4CX_avg_histogram_data(
      uint8_t no_of_samples,
      VL53L4CX_histogram_bin_data_t *phist_sum,
      VL53L4CX_histogram_bin_data_t *phist_avg);




    VL53L4CX_Error VL53L4CX_save_cfg_data(
      VL53L4CX_DEV  Dev);




    VL53L4CX_Error VL53L4CX_dynamic_zone_update(
      VL53L4CX_DEV  Dev,
      VL53L4CX_range_results_t *presults);




    VL53L4CX_Error VL53L4CX_update_internal_stream_counters(
      VL53L4CX_DEV  Dev,
      uint8_t     external_stream_count,
      uint8_t     *pinternal_stream_count,
      uint8_t     *pinternal_stream_count_val
    );



    VL53L4CX_Error VL53L4CX_multizone_hist_bins_update(
      VL53L4CX_DEV  Dev);



    VL53L4CX_Error VL53L4CX_set_histogram_multizone_initial_bin_config(
      VL53L4CX_zone_config_t           *pzone_cfg,
      VL53L4CX_histogram_config_t      *phist_cfg,
      VL53L4CX_histogram_config_t      *pmulti_hist
    );



    uint8_t VL53L4CX_encode_GPIO_interrupt_config(
      VL53L4CX_GPIO_interrupt_config_t  *pintconf);



    VL53L4CX_GPIO_interrupt_config_t VL53L4CX_decode_GPIO_interrupt_config(
      uint8_t   system__interrupt_config);



    VL53L4CX_Error VL53L4CX_set_GPIO_distance_threshold(
      VL53L4CX_DEV                      Dev,
      uint16_t      threshold_high,
      uint16_t      threshold_low);



    VL53L4CX_Error VL53L4CX_set_GPIO_rate_threshold(
      VL53L4CX_DEV                      Dev,
      uint16_t      threshold_high,
      uint16_t      threshold_low);



    VL53L4CX_Error VL53L4CX_set_GPIO_thresholds_from_struct(
      VL53L4CX_DEV                      Dev,
      VL53L4CX_GPIO_interrupt_config_t *pintconf);





    VL53L4CX_Error VL53L4CX_set_ref_spad_char_config(
      VL53L4CX_DEV    Dev,
      uint8_t       vcsel_period_a,
      uint32_t      phasecal_timeout_us,
      uint16_t      total_rate_target_mcps,
      uint16_t      max_count_rate_rtn_limit_mcps,
      uint16_t      min_count_rate_rtn_limit_mcps,
      uint16_t      fast_osc_frequency);




    VL53L4CX_Error VL53L4CX_set_ssc_config(
      VL53L4CX_DEV           Dev,
      VL53L4CX_ssc_config_t *pssc_cfg,
      uint16_t             fast_osc_frequency);




    VL53L4CX_Error VL53L4CX_get_spad_rate_data(
      VL53L4CX_DEV                Dev,
      VL53L4CX_spad_rate_data_t  *pspad_rates);



    uint32_t VL53L4CX_calc_crosstalk_plane_offset_with_margin(
      uint32_t     plane_offset_kcps,
      int16_t      margin_offset_kcps);



    VL53L4CX_Error VL53L4CX_low_power_auto_data_init(
      VL53L4CX_DEV                     Dev
    );



    VL53L4CX_Error VL53L4CX_low_power_auto_data_stop_range(
      VL53L4CX_DEV                     Dev
    );




    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_calc_required_samples(
      VL53L4CX_DEV                     Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_calc_new_xtalk(
      VL53L4CX_DEV        Dev,
      uint32_t        xtalk_offset_out,
      VL53L4CX_smudge_corrector_config_t  *pconfig,
      VL53L4CX_smudge_corrector_data_t    *pout,
      uint8_t         add_smudge,
      uint8_t         soft_update
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_corrector(
      VL53L4CX_DEV                     Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_data_init(
      VL53L4CX_DEV                     Dev
    );



    VL53L4CX_Error VL53L4CX_dynamic_xtalk_correction_output_init(
      VL53L4CX_LLDriverResults_t *pres
    );



    VL53L4CX_Error VL53L4CX_xtalk_cal_data_init(
      VL53L4CX_DEV                          Dev
    );



    VL53L4CX_Error VL53L4CX_config_low_power_auto_mode(
      VL53L4CX_general_config_t   *pgeneral,
      VL53L4CX_dynamic_config_t   *pdynamic,
      VL53L4CX_low_power_auto_data_t *plpadata
    );



    VL53L4CX_Error VL53L4CX_low_power_auto_setup_manual_calibration(
      VL53L4CX_DEV        Dev);



    VL53L4CX_Error VL53L4CX_low_power_auto_update_DSS(
      VL53L4CX_DEV        Dev);


    VL53L4CX_Error VL53L4CX_compute_histo_merge_nb(
      VL53L4CX_DEV        Dev,  uint8_t *histo_merge_nb);

    /* vl53lx_core_support.h functions */

    uint32_t VL53L4CX_calc_pll_period_us(
      uint16_t fast_osc_frequency);

    uint32_t VL53L4CX_duration_maths(
      uint32_t  pll_period_us,
      uint32_t  vcsel_parm_pclks,
      uint32_t  window_vclks,
      uint32_t  periods_elapsed_mclks);



    uint32_t VL53L4CX_events_per_spad_maths(
      int32_t   VL53L4CX_p_010,
      uint16_t  num_spads,
      uint32_t  duration);




    uint32_t VL53L4CX_isqrt(
      uint32_t  num);




    void VL53L4CX_hist_calc_zero_distance_phase(
      VL53L4CX_histogram_bin_data_t    *pdata);




    void VL53L4CX_hist_estimate_ambient_from_thresholded_bins(
      int32_t                      ambient_threshold_sigma,
      VL53L4CX_histogram_bin_data_t *pdata);




    void VL53L4CX_hist_remove_ambient_bins(
      VL53L4CX_histogram_bin_data_t    *pdata);




    uint32_t VL53L4CX_calc_pll_period_mm(
      uint16_t fast_osc_frequency);




    uint16_t VL53L4CX_rate_maths(
      int32_t   VL53L4CX_p_018,
      uint32_t  time_us);




    uint16_t VL53L4CX_rate_per_spad_maths(
      uint32_t  frac_bits,
      uint32_t  peak_count_rate,
      uint16_t  num_spads,
      uint32_t  max_output_value);




    int32_t VL53L4CX_range_maths(
      uint16_t  fast_osc_frequency,
      uint16_t  VL53L4CX_p_014,
      uint16_t  zero_distance_phase,
      uint8_t   fractional_bits,
      int32_t   gain_factor,
      int32_t   range_offset_mm);




    uint8_t VL53L4CX_decode_vcsel_period(
      uint8_t vcsel_period_reg);



    void VL53L4CX_copy_xtalk_bin_data_to_histogram_data_struct(
      VL53L4CX_xtalk_histogram_shape_t *pxtalk,
      VL53L4CX_histogram_bin_data_t    *phist);




    void VL53L4CX_init_histogram_bin_data_struct(
      int32_t                      bin_value,
      uint16_t                     VL53L4CX_p_021,
      VL53L4CX_histogram_bin_data_t *pdata);




    void VL53L4CX_decode_row_col(
      uint8_t   spad_number,
      uint8_t  *prow,
      uint8_t  *pcol);




    void VL53L4CX_hist_find_min_max_bin_values(
      VL53L4CX_histogram_bin_data_t   *pdata);




    void VL53L4CX_hist_estimate_ambient_from_ambient_bins(
      VL53L4CX_histogram_bin_data_t    *pdata);

    /* vl53lx_dmax.h functions */

    VL53L4CX_Error VL53L4CX_f_001(
      uint16_t                              target_reflectance,
      VL53L4CX_dmax_calibration_data_t       *pcal,
      VL53L4CX_hist_gen3_dmax_config_t       *pcfg,
      VL53L4CX_histogram_bin_data_t          *pbins,
      VL53L4CX_hist_gen3_dmax_private_data_t *pdata,
      int16_t                              *pambient_dmax_mm);




    uint32_t VL53L4CX_f_002(
      uint32_t     events_threshold,
      uint32_t     ref_signal_events,
      uint32_t   ref_distance_mm,
      uint32_t     signal_thresh_sigma);

    /* vl53lx_hist_algos_gen3.h functions */

    void VL53L4CX_f_003(
      VL53L4CX_hist_gen3_algo_private_data_t   *palgo);






    VL53L4CX_Error VL53L4CX_f_006(
      uint16_t                               ambient_threshold_events_scaler,
      int32_t                                ambient_threshold_sigma,
      int32_t                                min_ambient_threshold_events,
      uint8_t                            algo__crosstalk_compensation_enable,
      VL53L4CX_histogram_bin_data_t           *pbins,
      VL53L4CX_histogram_bin_data_t           *pxtalk,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);






    VL53L4CX_Error VL53L4CX_f_007(
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);




    VL53L4CX_Error VL53L4CX_f_008(
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);




    VL53L4CX_Error VL53L4CX_f_009(
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);




    VL53L4CX_Error VL53L4CX_f_016(
      VL53L4CX_HistTargetOrder                target_order,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);




    VL53L4CX_Error VL53L4CX_f_010(
      uint8_t                                pulse_no,
      VL53L4CX_histogram_bin_data_t           *pbins,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);



    VL53L4CX_Error VL53L4CX_f_015(
      uint8_t                                pulse_no,
      uint8_t                             clip_events,
      VL53L4CX_histogram_bin_data_t           *pbins,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);




    VL53L4CX_Error VL53L4CX_f_020(
      int16_t                            VL53L4CX_p_019,
      int16_t                            VL53L4CX_p_024,
      uint8_t                            VL53L4CX_p_030,
      uint8_t                            clip_events,
      VL53L4CX_histogram_bin_data_t       *pbins,
      uint32_t                          *pphase);




    VL53L4CX_Error VL53L4CX_f_011(
      uint8_t                                pulse_no,
      VL53L4CX_histogram_bin_data_t           *pbins,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo,
      int32_t                                pad_value,
      VL53L4CX_histogram_bin_data_t           *ppulse);




    VL53L4CX_Error VL53L4CX_f_014(
      uint8_t                       bin,
      uint8_t                       sigma_estimator__sigma_ref_mm,
      uint8_t                       VL53L4CX_p_030,
      uint8_t                       VL53L4CX_p_051,
      uint8_t                       crosstalk_compensation_enable,
      VL53L4CX_histogram_bin_data_t  *phist_data_ap,
      VL53L4CX_histogram_bin_data_t  *phist_data_zp,
      VL53L4CX_histogram_bin_data_t  *pxtalk_hist,
      uint16_t                     *psigma_est);




    void VL53L4CX_f_017(
      uint8_t                      range_id,
      uint8_t                      valid_phase_low,
      uint8_t                      valid_phase_high,
      uint16_t                     sigma_thres,
      VL53L4CX_histogram_bin_data_t *pbins,
      VL53L4CX_hist_pulse_data_t    *ppulse,
      VL53L4CX_range_data_t         *pdata);


    /* vl53lx_hist_algos_gen4.h functions */

    VL53L4CX_Error VL53L4CX_f_025(
      VL53L4CX_dmax_calibration_data_t         *pdmax_cal,
      VL53L4CX_hist_gen3_dmax_config_t         *pdmax_cfg,
      VL53L4CX_hist_post_process_config_t      *ppost_cfg,
      VL53L4CX_histogram_bin_data_t            *pbins,
      VL53L4CX_histogram_bin_data_t            *pxtalk,
      VL53L4CX_hist_gen3_algo_private_data_t   *palgo,
      VL53L4CX_hist_gen4_algo_filtered_data_t  *pfiltered,
      VL53L4CX_hist_gen3_dmax_private_data_t   *pdmax_algo,
      VL53L4CX_range_results_t                 *presults,
      uint8_t                                histo_merge_nb);




    VL53L4CX_Error VL53L4CX_f_026(
      uint8_t                                pulse_no,
      VL53L4CX_histogram_bin_data_t           *ppulse,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo,
      VL53L4CX_hist_gen4_algo_filtered_data_t *pfiltered);




    VL53L4CX_Error VL53L4CX_f_027(
      uint8_t                                pulse_no,
      uint16_t                               noise_threshold,
      VL53L4CX_hist_gen4_algo_filtered_data_t *pfiltered,
      VL53L4CX_hist_gen3_algo_private_data_t  *palgo);




    VL53L4CX_Error VL53L4CX_f_028(
      uint8_t   bin,
      int32_t   VL53L4CX_p_007,
      int32_t   VL53L4CX_p_032,
      int32_t   VL53L4CX_p_001,
      int32_t   ax,
      int32_t   bx,
      int32_t   cx,
      int32_t   VL53L4CX_p_028,
      uint8_t   VL53L4CX_p_030,
      uint32_t *pmedian_phase);

    /* vl53lx_hist_char.h functions */

    VL53L4CX_Error VL53L4CX_set_calib_config(
      VL53L4CX_DEV      Dev,
      uint8_t         vcsel_delay__a0,
      uint8_t         calib_1,
      uint8_t         calib_2,
      uint8_t         calib_3,
      uint8_t         calib_2__a0,
      uint8_t         spad_readout);




    VL53L4CX_Error VL53L4CX_set_hist_calib_pulse_delay(
      VL53L4CX_DEV      Dev,
      uint8_t         calib_delay);




    VL53L4CX_Error VL53L4CX_disable_calib_pulse_delay(
      VL53L4CX_DEV      Dev);

    /* vl53lx_hist_core.h functions */

    void  VL53L4CX_f_022(
      uint8_t                         VL53L4CX_p_032,
      uint8_t                         filter_woi,
      VL53L4CX_histogram_bin_data_t    *pbins,
      int32_t                        *pa,
      int32_t                        *pb,
      int32_t                        *pc);




    VL53L4CX_Error VL53L4CX_f_018(
      uint16_t                        vcsel_width,
      uint16_t                        fast_osc_frequency,
      uint32_t                        total_periods_elapsed,
      uint16_t                        VL53L4CX_p_004,
      VL53L4CX_range_data_t            *pdata,
      uint8_t histo_merge_nb);




    void VL53L4CX_f_019(
      uint16_t             gain_factor,
      int16_t              range_offset_mm,
      VL53L4CX_range_data_t *pdata);




    void  VL53L4CX_f_029(
      VL53L4CX_histogram_bin_data_t   *pdata,
      int32_t                        ambient_estimate_counts_per_bin);




    void  VL53L4CX_f_005(
      VL53L4CX_histogram_bin_data_t   *pxtalk,
      VL53L4CX_histogram_bin_data_t   *pbins,
      VL53L4CX_histogram_bin_data_t   *pxtalk_realigned);



    int8_t  VL53L4CX_f_030(
      VL53L4CX_histogram_bin_data_t   *pdata1,
      VL53L4CX_histogram_bin_data_t   *pdata2);



    VL53L4CX_Error  VL53L4CX_f_031(
      VL53L4CX_histogram_bin_data_t   *pidata,
      VL53L4CX_histogram_bin_data_t   *podata);

    /* vl53lx_hist_funcs.h functions */

    VL53L4CX_Error VL53L4CX_hist_process_data(
      VL53L4CX_dmax_calibration_data_t    *pdmax_cal,
      VL53L4CX_hist_gen3_dmax_config_t    *pdmax_cfg,
      VL53L4CX_hist_post_process_config_t *ppost_cfg,
      VL53L4CX_histogram_bin_data_t       *pbins,
      VL53L4CX_xtalk_histogram_data_t     *pxtalk,
      uint8_t                           *pArea1,
      uint8_t                           *pArea2,
      VL53L4CX_range_results_t            *presults,
      uint8_t                           *HistMergeNumber);




    VL53L4CX_Error VL53L4CX_hist_ambient_dmax(
      uint16_t                            target_reflectance,
      VL53L4CX_dmax_calibration_data_t     *pdmax_cal,
      VL53L4CX_hist_gen3_dmax_config_t     *pdmax_cfg,
      VL53L4CX_histogram_bin_data_t        *pbins,
      int16_t                            *pambient_dmax_mm);

    /* vl53lx_nvm.h functions */

    VL53L4CX_Error VL53L4CX_nvm_enable(
      VL53L4CX_DEV     Dev,
      uint16_t       nvm_ctrl_pulse_width,
      int32_t        nvm_power_up_delay_us);




    VL53L4CX_Error VL53L4CX_nvm_read(
      VL53L4CX_DEV     Dev,
      uint8_t        start_address,
      uint8_t        count,
      uint8_t       *pdata);




    VL53L4CX_Error VL53L4CX_nvm_disable(
      VL53L4CX_DEV     Dev);




    VL53L4CX_Error VL53L4CX_nvm_format_decode(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_decoded_nvm_data_t *pdata);




    VL53L4CX_Error VL53L4CX_nvm_decode_optical_centre(
      uint16_t                             buf_size,
      uint8_t                             *pbuffer,
      VL53L4CX_optical_centre_t             *pdata);




    VL53L4CX_Error VL53L4CX_nvm_decode_cal_peak_rate_map(
      uint16_t                             buf_size,
      uint8_t                             *pbuffer,
      VL53L4CX_cal_peak_rate_map_t          *pdata);




    VL53L4CX_Error VL53L4CX_nvm_decode_additional_offset_cal_data(
      uint16_t                             buf_size,
      uint8_t                             *pbuffer,
      VL53L4CX_additional_offset_cal_data_t *pdata);




    VL53L4CX_Error VL53L4CX_nvm_decode_fmt_range_results_data(
      uint16_t                             buf_size,
      uint8_t                             *pbuffer,
      VL53L4CX_decoded_nvm_fmt_range_data_t *pdata);




    VL53L4CX_Error VL53L4CX_nvm_decode_fmt_info(
      uint16_t                       buf_size,
      uint8_t                       *pbuffer,
      VL53L4CX_decoded_nvm_fmt_info_t *pdata);




    VL53L4CX_Error VL53L4CX_nvm_decode_ews_info(
      uint16_t                       buf_size,
      uint8_t                       *pbuffer,
      VL53L4CX_decoded_nvm_ews_info_t *pdata);




    void VL53L4CX_nvm_format_encode(
      VL53L4CX_decoded_nvm_data_t *pnvm_info,
      uint8_t                   *pnvm_data);




    VL53L4CX_Error VL53L4CX_read_nvm_raw_data(
      VL53L4CX_DEV     Dev,
      uint8_t        start_address,
      uint8_t        count,
      uint8_t       *pnvm_raw_data);




    VL53L4CX_Error VL53L4CX_read_nvm(
      VL53L4CX_DEV                 Dev,
      uint8_t                    nvm_format,
      VL53L4CX_decoded_nvm_data_t *pnvm_info);




    VL53L4CX_Error VL53L4CX_read_nvm_optical_centre(
      VL53L4CX_DEV                           Dev,
      VL53L4CX_optical_centre_t             *pcentre);




    VL53L4CX_Error VL53L4CX_read_nvm_cal_peak_rate_map(
      VL53L4CX_DEV                           Dev,
      VL53L4CX_cal_peak_rate_map_t          *pcal_data);




    VL53L4CX_Error VL53L4CX_read_nvm_additional_offset_cal_data(
      VL53L4CX_DEV                           Dev,
      VL53L4CX_additional_offset_cal_data_t *pcal_data);




    VL53L4CX_Error VL53L4CX_read_nvm_fmt_range_results_data(
      VL53L4CX_DEV                           Dev,
      uint16_t                             range_results_select,
      VL53L4CX_decoded_nvm_fmt_range_data_t *prange_data);

    /* vl53lx_register_funcs.h functions */

    VL53L4CX_Error VL53L4CX_i2c_encode_static_nvm_managed(
      VL53L4CX_static_nvm_managed_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_static_nvm_managed(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_static_nvm_managed_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_static_nvm_managed(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_static_nvm_managed_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_static_nvm_managed(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_static_nvm_managed_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_customer_nvm_managed(
      VL53L4CX_customer_nvm_managed_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_customer_nvm_managed(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_customer_nvm_managed_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_customer_nvm_managed(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_customer_nvm_managed_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_customer_nvm_managed(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_customer_nvm_managed_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_static_config(
      VL53L4CX_static_config_t    *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_static_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_static_config_t    *pdata);




    VL53L4CX_Error VL53L4CX_set_static_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_static_config_t    *pdata);




    VL53L4CX_Error VL53L4CX_get_static_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_static_config_t    *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_general_config(
      VL53L4CX_general_config_t   *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_general_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_general_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_set_general_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_general_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_get_general_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_general_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_timing_config(
      VL53L4CX_timing_config_t    *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_timing_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_timing_config_t    *pdata);




    VL53L4CX_Error VL53L4CX_set_timing_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_timing_config_t    *pdata);




    VL53L4CX_Error VL53L4CX_get_timing_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_timing_config_t    *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_dynamic_config(
      VL53L4CX_dynamic_config_t   *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_dynamic_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_dynamic_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_set_dynamic_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_dynamic_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_get_dynamic_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_dynamic_config_t   *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_system_control(
      VL53L4CX_system_control_t   *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_system_control(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_system_control_t   *pdata);




    VL53L4CX_Error VL53L4CX_set_system_control(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_system_control_t   *pdata);




    VL53L4CX_Error VL53L4CX_get_system_control(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_system_control_t   *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_system_results(
      VL53L4CX_system_results_t   *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_system_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_system_results_t   *pdata);




    VL53L4CX_Error VL53L4CX_set_system_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_system_results_t   *pdata);




    VL53L4CX_Error VL53L4CX_get_system_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_system_results_t   *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_core_results(
      VL53L4CX_core_results_t     *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_core_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_core_results_t     *pdata);




    VL53L4CX_Error VL53L4CX_set_core_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_core_results_t     *pdata);




    VL53L4CX_Error VL53L4CX_get_core_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_core_results_t     *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_debug_results(
      VL53L4CX_debug_results_t    *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_debug_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_debug_results_t    *pdata);




    VL53L4CX_Error VL53L4CX_set_debug_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_debug_results_t    *pdata);




    VL53L4CX_Error VL53L4CX_get_debug_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_debug_results_t    *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_nvm_copy_data(
      VL53L4CX_nvm_copy_data_t    *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_nvm_copy_data(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_nvm_copy_data_t    *pdata);




    VL53L4CX_Error VL53L4CX_set_nvm_copy_data(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_nvm_copy_data_t    *pdata);




    VL53L4CX_Error VL53L4CX_get_nvm_copy_data(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_nvm_copy_data_t    *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_prev_shadow_system_results(
      VL53L4CX_prev_shadow_system_results_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_prev_shadow_system_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_prev_shadow_system_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_prev_shadow_system_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_prev_shadow_system_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_prev_shadow_system_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_prev_shadow_system_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_prev_shadow_core_results(
      VL53L4CX_prev_shadow_core_results_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_prev_shadow_core_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_prev_shadow_core_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_prev_shadow_core_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_prev_shadow_core_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_prev_shadow_core_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_prev_shadow_core_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_patch_debug(
      VL53L4CX_patch_debug_t      *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_patch_debug(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_patch_debug_t      *pdata);




    VL53L4CX_Error VL53L4CX_set_patch_debug(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_patch_debug_t      *pdata);




    VL53L4CX_Error VL53L4CX_get_patch_debug(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_patch_debug_t      *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_gph_general_config(
      VL53L4CX_gph_general_config_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_gph_general_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_gph_general_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_gph_general_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_gph_general_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_gph_general_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_gph_general_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_gph_static_config(
      VL53L4CX_gph_static_config_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_gph_static_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_gph_static_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_gph_static_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_gph_static_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_gph_static_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_gph_static_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_gph_timing_config(
      VL53L4CX_gph_timing_config_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_gph_timing_config(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_gph_timing_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_gph_timing_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_gph_timing_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_gph_timing_config(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_gph_timing_config_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_fw_internal(
      VL53L4CX_fw_internal_t      *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_fw_internal(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_fw_internal_t      *pdata);




    VL53L4CX_Error VL53L4CX_set_fw_internal(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_fw_internal_t      *pdata);




    VL53L4CX_Error VL53L4CX_get_fw_internal(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_fw_internal_t      *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_patch_results(
      VL53L4CX_patch_results_t    *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_patch_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_patch_results_t    *pdata);




    VL53L4CX_Error VL53L4CX_set_patch_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_patch_results_t    *pdata);




    VL53L4CX_Error VL53L4CX_get_patch_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_patch_results_t    *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_shadow_system_results(
      VL53L4CX_shadow_system_results_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_shadow_system_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_shadow_system_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_shadow_system_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_shadow_system_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_shadow_system_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_shadow_system_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_i2c_encode_shadow_core_results(
      VL53L4CX_shadow_core_results_t  *pdata,
      uint16_t                   buf_size,
      uint8_t                   *pbuffer);




    VL53L4CX_Error VL53L4CX_i2c_decode_shadow_core_results(
      uint16_t                   buf_size,
      uint8_t                   *pbuffer,
      VL53L4CX_shadow_core_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_set_shadow_core_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_shadow_core_results_t  *pdata);




    VL53L4CX_Error VL53L4CX_get_shadow_core_results(
      VL53L4CX_DEV                 Dev,
      VL53L4CX_shadow_core_results_t  *pdata);

    /* vl53lx_sigma_estimate.h functions */

    VL53L4CX_Error  VL53L4CX_f_023(
      uint8_t       sigma_estimator__sigma_ref_mm,
      uint32_t      VL53L4CX_p_007,
      uint32_t      VL53L4CX_p_032,
      uint32_t      VL53L4CX_p_001,
      uint32_t      a_zp,
      uint32_t      c_zp,
      uint32_t      bx,
      uint32_t      ax_zp,
      uint32_t      cx_zp,
      uint32_t      VL53L4CX_p_028,
      uint16_t      fast_osc_frequency,
      uint16_t      *psigma_est);

    /* vl53lx_silicon_core.h functions */

    VL53L4CX_Error VL53L4CX_is_firmware_ready_silicon(
      VL53L4CX_DEV      Dev,
      uint8_t        *pready);

    /* vl53lx_wait.h functions */


    VL53L4CX_Error VL53L4CX_wait_for_boot_completion(
      VL53L4CX_DEV      Dev);




    VL53L4CX_Error VL53L4CX_wait_for_range_completion(
      VL53L4CX_DEV   Dev);




    VL53L4CX_Error VL53L4CX_wait_for_test_completion(
      VL53L4CX_DEV   Dev);






    VL53L4CX_Error VL53L4CX_is_boot_complete(
      VL53L4CX_DEV      Dev,
      uint8_t        *pready);



    VL53L4CX_Error VL53L4CX_is_firmware_ready(
      VL53L4CX_DEV      Dev,
      uint8_t        *pready);




    VL53L4CX_Error VL53L4CX_is_new_data_ready(
      VL53L4CX_DEV      Dev,
      uint8_t        *pready);






    VL53L4CX_Error VL53L4CX_poll_for_boot_completion(
      VL53L4CX_DEV      Dev,
      uint32_t        timeout_ms);




    VL53L4CX_Error VL53L4CX_poll_for_firmware_ready(
      VL53L4CX_DEV      Dev,
      uint32_t        timeout_ms);




    VL53L4CX_Error VL53L4CX_poll_for_range_completion(
      VL53L4CX_DEV   Dev,
      uint32_t     timeout_ms);

    /* vl53lx_xtalk.h functions */

    VL53L4CX_Error VL53L4CX_xtalk_calibration_process_data(
      VL53L4CX_xtalk_range_results_t        *pxtalk_ranges,
      VL53L4CX_xtalk_histogram_data_t       *pxtalk_shape,
      VL53L4CX_xtalk_calibration_results_t  *pxtalk_cal);




    VL53L4CX_Error VL53L4CX_f_041(
      VL53L4CX_histogram_bin_data_t        *pavg_bins,
      VL53L4CX_xtalk_algo_data_t           *pdebug,
      VL53L4CX_xtalk_range_data_t          *pxtalk_data,
      uint8_t                             histogram__window_start,
      uint8_t                             histogram__window_end,
      VL53L4CX_xtalk_histogram_shape_t     *pxtalk_shape);



    VL53L4CX_Error VL53L4CX_f_039(
      VL53L4CX_xtalk_range_results_t  *pxtalk_results,
      VL53L4CX_xtalk_algo_data_t      *pdebug,
      int16_t                       *xgradient,
      int16_t                       *ygradient);




    VL53L4CX_Error VL53L4CX_f_040(
      VL53L4CX_xtalk_range_data_t *pxtalk_data,
      VL53L4CX_xtalk_algo_data_t  *pdebug,
      uint32_t                  *xtalk_mean_offset_kcps);




    VL53L4CX_Error VL53L4CX_f_045(
      VL53L4CX_histogram_bin_data_t    *phist_data,
      VL53L4CX_xtalk_range_data_t      *pxtalk_data,
      VL53L4CX_xtalk_algo_data_t       *pdebug,
      VL53L4CX_xtalk_histogram_shape_t *pxtalk_histo);





    VL53L4CX_Error VL53L4CX_f_032(
      uint32_t                       mean_offset,
      int16_t                        xgradient,
      int16_t                        ygradient,
      int8_t                         centre_offset_x,
      int8_t                         centre_offset_y,
      uint16_t                       roi_effective_spads,
      uint8_t                        roi_centre_spad,
      uint8_t                        roi_xy_size,
      uint32_t                      *xtalk_rate_kcps);




    VL53L4CX_Error VL53L4CX_f_033(
      VL53L4CX_histogram_bin_data_t    *phist_data,
      VL53L4CX_xtalk_histogram_shape_t *pxtalk_data,
      uint32_t                        xtalk_rate_kcps,
      VL53L4CX_histogram_bin_data_t    *pxtalkcount_data);




    VL53L4CX_Error VL53L4CX_f_047(
      VL53L4CX_histogram_bin_data_t   *phist_data,
      VL53L4CX_histogram_bin_data_t   *pxtalk_data,
      uint8_t                        xtalk_bin_offset);



    VL53L4CX_Error VL53L4CX_f_044(
      VL53L4CX_histogram_bin_data_t       *pxtalk_data,
      uint32_t                           amb_threshold,
      uint8_t                            VL53L4CX_p_019,
      uint8_t                            VL53L4CX_p_024);



    VL53L4CX_Error VL53L4CX_f_046(
      VL53L4CX_customer_nvm_managed_t *pcustomer,
      VL53L4CX_dynamic_config_t       *pdyn_cfg,
      VL53L4CX_xtalk_histogram_data_t *pxtalk_shape,
      VL53L4CX_histogram_bin_data_t   *pip_hist_data,
      VL53L4CX_histogram_bin_data_t   *pop_hist_data,
      VL53L4CX_histogram_bin_data_t   *pxtalk_count_data);

    /* vl53lx_platform_ipp.h  functions */

    VL53L4CX_Error VL53L4CX_ipp_hist_process_data(
      VL53L4CX_DEV                         Dev,
      VL53L4CX_dmax_calibration_data_t    *pdmax_cal,
      VL53L4CX_hist_gen3_dmax_config_t    *pdmax_cfg,
      VL53L4CX_hist_post_process_config_t *ppost_cfg,
      VL53L4CX_histogram_bin_data_t       *pbins,
      VL53L4CX_xtalk_histogram_data_t     *pxtalk,
      uint8_t                           *pArea1,
      uint8_t                           *pArea2,
      uint8_t                           *phisto_merge_nb,
      VL53L4CX_range_results_t            *presults);




    VL53L4CX_Error VL53L4CX_ipp_hist_ambient_dmax(
      VL53L4CX_DEV                         Dev,
      uint16_t                           target_reflectance,
      VL53L4CX_dmax_calibration_data_t    *pdmax_cal,
      VL53L4CX_hist_gen3_dmax_config_t    *pdmax_cfg,
      VL53L4CX_histogram_bin_data_t       *pbins,
      int16_t                           *pambient_dmax_mm);




    VL53L4CX_Error VL53L4CX_ipp_xtalk_calibration_process_data(
      VL53L4CX_DEV                          Dev,
      VL53L4CX_xtalk_range_results_t       *pxtalk_ranges,
      VL53L4CX_xtalk_histogram_data_t      *pxtalk_shape,
      VL53L4CX_xtalk_calibration_results_t *pxtalk_cal);

    /* Helper functions */

    VL53L4CX_Error VL53L4CX_f_043(
      uint8_t                      sigma_mult,
      int32_t                      VL53L4CX_p_028,
      uint32_t                    *ambient_noise);

    VL53L4CX_Error ComputeDevicePresetMode(
      VL53L4CX_DistanceModes DistanceMode,
      VL53L4CX_DevicePresetModes *pDevicePresetMode);

    VL53L4CX_Error SetPresetModeL3CX(VL53L4CX_DEV Dev,
                                     VL53L4CX_DistanceModes DistanceMode,
                                     uint32_t inter_measurement_period_ms);

    int IsL4(VL53L4CX_DEV Dev);

    VL53L4CX_Error CheckValidRectRoi(VL53L4CX_UserRoi_t ROI);

    uint8_t ConvertStatusHisto(uint8_t FilteredRangeStatus);

    VL53L4CX_Error SetTargetData(VL53L4CX_DEV Dev,
                                 uint8_t active_results, uint8_t streamcount, uint8_t iteration,
                                 uint8_t device_status, VL53L4CX_range_data_t *presults_data,
                                 VL53L4CX_TargetRangeData_t *pRangeData);

    VL53L4CX_Error SetMeasurementData(VL53L4CX_DEV Dev,
                                      VL53L4CX_range_results_t *presults,
                                      VL53L4CX_MultiRangingData_t *pMultiRangingData);

    VL53L4CX_Error select_offset_per_vcsel(VL53L4CX_LLDriverData_t *pdev,
                                           int16_t *poffset);

    void vl53lx_diff_histo_stddev(VL53L4CX_LLDriverData_t *pdev,
                                  VL53L4CX_histogram_bin_data_t *pdata, uint8_t timing, uint8_t HighIndex,
                                  uint8_t prev_pos, int32_t *pdiff_histo_stddev);

    void vl53lx_histo_merge(VL53L4CX_DEV Dev,
                            VL53L4CX_histogram_bin_data_t *pdata);

    /* Write and read functions from I2C */

    VL53L4CX_Error VL53L4CX_WrByte(VL53L4CX_DEV Dev, uint16_t index, uint8_t data);
    VL53L4CX_Error VL53L4CX_WrWord(VL53L4CX_DEV Dev, uint16_t index, uint16_t data);
    VL53L4CX_Error VL53L4CX_WrDWord(VL53L4CX_DEV Dev, uint16_t index, uint32_t data);
    VL53L4CX_Error VL53L4CX_RdByte(VL53L4CX_DEV Dev, uint16_t index, uint8_t *data);
    VL53L4CX_Error VL53L4CX_RdWord(VL53L4CX_DEV Dev, uint16_t index, uint16_t *data);
    VL53L4CX_Error VL53L4CX_RdDWord(VL53L4CX_DEV Dev, uint16_t index, uint32_t *data);
    VL53L4CX_Error VL53L4CX_UpdateByte(VL53L4CX_DEV Dev, uint16_t index, uint8_t AndData, uint8_t OrData);

    VL53L4CX_Error VL53L4CX_WriteMulti(VL53L4CX_DEV Dev, uint16_t index, uint8_t *pdata, uint32_t count);
    VL53L4CX_Error VL53L4CX_ReadMulti(VL53L4CX_DEV Dev, uint16_t index, uint8_t *pdata, uint32_t count);

    VL53L4CX_Error VL53L4CX_I2CWrite(uint8_t DeviceAddr, uint16_t RegisterAddress, uint8_t *p_values, uint16_t size);
    VL53L4CX_Error VL53L4CX_I2CRead(uint8_t DeviceAddr, uint16_t RegisterAddress, uint8_t *p_values, uint16_t size);
    VL53L4CX_Error VL53L4CX_GetTickCount(uint32_t *ptick_count_ms);
    VL53L4CX_Error VL53L4CX_WaitUs(VL53L4CX_Dev_t *pdev, int32_t wait_us);
    VL53L4CX_Error VL53L4CX_WaitMs(VL53L4CX_Dev_t *pdev, int32_t wait_ms);

    VL53L4CX_Error VL53L4CX_WaitValueMaskEx(VL53L4CX_Dev_t *pdev, uint32_t timeout_ms, uint16_t index, uint8_t value, uint8_t mask, uint32_t poll_delay_ms);


  protected:

    /* IO Device */
    TwoWire *dev_i2c;
    /* Digital out pin */
    int xshut;
    /* Device data */
    VL53L4CX_Dev_t MyDevice;
    VL53L4CX_DEV Dev;
};

#endif /* _VL53L4CX_CLASS_H_ */
