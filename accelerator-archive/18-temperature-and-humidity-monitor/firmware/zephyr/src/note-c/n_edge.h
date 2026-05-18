// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Notecard "edge mode" type definitions

#pragma once

// For standard tracking, the data format of a single point
#define TRACKTYPE_NORMAL ""
#define TRACKTYPE_HEARTBEAT "heartbeat"
#define TRACKTYPE_USB_CHANGE "usb"
#define TRACKTYPE_NO_SAT "no-sat"

typedef struct {
#define TRACKPOINT_MEASUREMENT_TIME "mtime"
    double mtime;
#define TRACKPOINT_LAT "lat"
    double lat;
#define TRACKPOINT_LON "lon"
    double lon;
#define TRACKPOINT_TIME "time"
    uint32_t time;
#define TRACKPOINT_HDOP "dop"
    double hdop;
#define TRACKPOINT_JOURNEY_TIME "journey"
    uint32_t journeyTime;
#define TRACKPOINT_JOURNEY_COUNT "jcount"
    uint32_t journeyCount;
#define TRACKPOINT_TYPE "status"
    char trackType[32];
#define TRACKPOINT_MOTION_COUNT "motion"
    uint32_t motionCount;
#define TRACKPOINT_SECONDS "seconds"
    int32_t seconds;
#define TRACKPOINT_DISTANCE "distance"
    double distance;
#define TRACKPOINT_BEARING "bearing"
    double bearing;
#define TRACKPOINT_VELOCITY "velocity"
    double velocity;
#define TRACKPOINT_TEMPERATURE "temperature"
    double temperature;
#define TRACKPOINT_HUMIDITY "humidity"
    double humidity;
#define TRACKPOINT_PRESSURE "pressure"
    double pressure;
#define TRACKPOINT_VOLTAGE "voltage"
    double voltage;
#define TRACKPOINT_USB "usb"
    bool usb;
#define TRACKPOINT_CHARGING "charging"
    bool charging;
} TrackPoint;

// MotionPoint is the data structure that we use when tracking motion
typedef struct {
#define MOTIONPOINT_MEASUREMENT_TIME "mtime"
    double mtime;
#define MOTIONPOINT_MOVEMENTS "movements"
    char movements[250];
#define MOTIONPOINT_ORIENTATION "orientation"
    char orientation[20];
#define MOTIONPOINT_MOTION_COUNT "motion"
    uint32_t motionCount;
#define MOTIONPOINT_TILT_COUNT "tilt"
    uint32_t tiltCount;
} MotionPoint;

// LogData is the data structure that we use when capturing hub.log information
typedef struct {
#define LOGDATA_MESSAGE "message"
    char message[256];
#define LOGDATA_METHOD "method"
    char method[64];
#define LOGDATA_ALERT "alert"
    bool alert;
} LogData;

// Format of the edge entry which is the dequeued note body
#define	EDGE_NOTEFILE				"_edge.qi"
#define EDGETYPE_SCAN				"scan"
#define EDGETYPE_TRACKPOINT			"track"
#define EDGETYPE_MOTIONPOINT		"motion"
#define EDGETYPE_LOG				"log"
typedef struct {
#define EDGE_TYPE "type"
    char edgeType[32];
#define EDGE_TRACKPOINT "point"
    TrackPoint trackPoint;
#define EDGE_MOTIONPOINT "motion"
    MotionPoint motionPoint;
#define EDGE_SCAN_SECS "secs"
    uint32_t scanSecs;
#define EDGE_SCAN_METERS "meters"
    uint32_t scanMeters;
#define EDGE_SCAN_BEARING "bearing"
    uint32_t scanBearing;
#define EDGE_SCAN_BEGAN_TIME "began_when"
    uint32_t scanBeganTime;
#define EDGE_SCAN_BEGAN_LOC_TIME "began_loc_when"
    uint32_t scanBeganLocTime;
#define EDGE_SCAN_BEGAN_LOC_LAT "began_loc_lat"
    double scanBeganLocLat;
#define EDGE_SCAN_BEGAN_LOC_LON "began_loc_lon"
    double scanBeganLocLon;
#define EDGE_SCAN_BEGAN_LOC_HDOP "began_loc_dop"
    double scanBeganLocHDOP;
#define EDGE_SCAN_BEGAN_MOTION_TIME "began_motion_when"
    uint32_t scanBeganMotionTime;
#define EDGE_SCAN_ENDED_TIME "ended_when"
    uint32_t scanEndedTime;
#define EDGE_SCAN_ENDED_LOC_TIME "ended_loc_when"
    uint32_t scanEndedLocTime;
#define EDGE_SCAN_ENDED_LOC_LAT "ended_loc_lat"
    double scanEndedLocLat;
#define EDGE_SCAN_ENDED_LOC_LON "ended_loc_lon"
    double scanEndedLocLon;
#define EDGE_SCAN_ENDED_LOC_HDOP "ended_loc_dop"
    double scanEndedLocHDOP;
#define EDGE_SCAN_ENDED_MOTION_TIME "ended_motion_when"
    uint32_t scanEndedMotionTime;
#define EDGE_SCAN_LOC_LAT "loc_lat"
    double scanLocLat;
#define EDGE_SCAN_LOC_LON "loc_lon"
    double scanLocLon;
} EdgeData;

// Scan formats
#define SCAN_SEP            '\n'			// inter-reading separator
#define SCAN_TYPE_GSM       'g'             // FORMAT_2G
#define SCAN_TYPE_CDMA      'c'             // FORMAT_2G
#define SCAN_TYPE_UMTS      'u'             // FORMAT_3G
#define SCAN_TYPE_WCDMA     'w'             // FORMAT_3G
#define SCAN_TYPE_LTE       'l'             // FORMAT_4G
#define SCAN_TYPE_EMTC      'e'             // FORMAT_4G
#define SCAN_TYPE_NBIOT     'i'             // FORMAT_4G
#define SCAN_TYPE_NR        'n'             // FORMAT_5G
#define SCAN_TYPE_WIFI      'x'             // FORMAT_WIFI
#define SCAN_TYPE_CELLTIME  't'             // FORMAT_TIME
#define SCAN_TYPE_WIFITIME  's'             // FORMAT_TIME
#define SCAN_TYPE_GPS       'd'             // FORMAT_GPS
#define SCAN_FORMAT_2G      "xmcc,xmnc,xlac,xcid,xrssi"
#define SCAN_FORMAT_3G      "xmcc,xmnc,xlac,xcid,xpsc,xrscp"
#define SCAN_FORMAT_4G      "xmcc,xmnc,xtac,xcid,xpci,rssi,rsrp,rsrq,xband,xchan"
#define SCAN_FORMAT_5G      "xmcc,xmnc,xtac,xcid,xpci,rssi,rsrp,rsrq,xband,xchan"
#define SCAN_FORMAT_WIFI    "xbssid,xchannel,xfreq,rssi,snr,\"ssid\""
#define SCAN_FORMAT_TIME    "epochsecs"
#define SCAN_FORMAT_GPS     "epochsecs,olc,hdop"
