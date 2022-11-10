
// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/******************************************************************************
 * Copyright (c) 2020, STMicroelectronics - All Rights Reserved

 This file is part of VL53L4CX and is dual licensed,
 either GPL-2.0+
 or 'BSD 3-clause "New" or "Revised" License' , at your option.
 ******************************************************************************
 */





#ifndef _VL53L4CX_PLATFORM_USER_CONFIG_H_
#define _VL53L4CX_PLATFORM_USER_CONFIG_H_

#define    VL53L4CX_BYTES_PER_WORD              2
#define    VL53L4CX_BYTES_PER_DWORD             4


#define VL53L4CX_BOOT_COMPLETION_POLLING_TIMEOUT_MS     500
#define VL53L4CX_RANGE_COMPLETION_POLLING_TIMEOUT_MS   2000
#define VL53L4CX_TEST_COMPLETION_POLLING_TIMEOUT_MS   60000

#define VL53L4CX_POLLING_DELAY_MS                         1


#define VL53L4CX_TUNINGPARM_PUBLIC_PAGE_BASE_ADDRESS  0x8000
#define VL53L4CX_TUNINGPARM_PRIVATE_PAGE_BASE_ADDRESS 0xC000

#define VL53L4CX_GAIN_FACTOR__STANDARD_DEFAULT       0x0800

#define VL53L4CX_GAIN_FACTOR__HISTOGRAM_DEFAULT      0x0800



#define VL53L4CX_OFFSET_CAL_MIN_EFFECTIVE_SPADS  0x0500


#define VL53L4CX_OFFSET_CAL_MAX_PRE_PEAK_RATE_MCPS   0x1900


#define VL53L4CX_OFFSET_CAL_MAX_SIGMA_MM             0x0040



#define VL53L4CX_ZONE_CAL_MAX_PRE_PEAK_RATE_MCPS     0x1900


#define VL53L4CX_ZONE_CAL_MAX_SIGMA_MM               0x0040



#define VL53L4CX_XTALK_EXTRACT_MAX_SIGMA_MM          0x008C


#define VL53L4CX_MAX_USER_ZONES                5

#define VL53L4CX_MAX_RANGE_RESULTS              4



#define VL53L4CX_MAX_STRING_LENGTH 512

#endif


