// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "UpbeatLabs_MCP39F521.h"

#pragma once

#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// When set to true, all power monitoring notes are synced immediately to Notehub.
// When set to false, only alerts are synched immediately.
#ifndef SYNC_POWER_MONITORING_NOTES
#define SYNC_POWER_MONITORING_NOTES        (false)
#endif

// Notefile/Note definitions
#define	DATA_FILENAME			        "power.qo"
#define DATA_FIELD_APP			      "app"
#define DATA_FIELD_MAX_VOLTAGE    "max_voltage"
#define DATA_FIELD_MAX_CURRENT    "max_current"
#define DATA_FIELD_MAX_POWER      "max_power"
#define DATA_FIELD_VOLTAGE		    "last_voltage"
#define DATA_FIELD_CURRENT		    "last_current"
#define DATA_FIELD_POWER		      "last_power"
#define DATA_FIELD_FREQUENCY	    "frequency"
#define DATA_FIELD_REACTIVE       "reactivePower"
#define DATA_FIELD_APPARENT       "apparentPower"
#define DATA_FIELD_POWERFACTOR    "powerFactor"
#define DATA_FIELD_APP_NAME       "nf8"

#define ALERT_FILENAME            "alert.qo"
#define ALERT_FIELD_REASON        "reason"

#define	MCP_I2C_ADDRESS_BASE	0x74

// Nominal voltage/current for 0.  Detection is not totally precise and there is some inherent noise/drift so these are 
// set to be just above 0.
#define ZERO_VOLTS (5)
#define ZERO_AMPS (0.3)
