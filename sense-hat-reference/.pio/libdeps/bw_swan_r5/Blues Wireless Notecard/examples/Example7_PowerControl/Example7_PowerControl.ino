//
// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//
// This example contains the complete source for the Sensor Tutorial at dev.blues.io
// https://dev.blues.io/build/tutorials/sensor-tutorial/notecarrier-af/esp32/arduino-wiring/
//
// This tutorial requires a Notecarrier-F (or equivalently-wired carrier board) designed
// enable the Notecard's ATTN pin to control a host MCU's power supply.
//

#include <Notecard.h>
#include <Wire.h>

// Parameters for this example

// This is the unique Product Identifier for your device
#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif

#define myProductID PRODUCT_UID

#define notehubProductUID		PRODUCT_UID
#define notehubUploadPeriodMins	10
#define	hostSleepSeconds		60

// Arduino serial debug monitor port definitions
#define usbSerial Serial

// Notecard I2C port definitions
Notecard notecard;

// When the Notecard puts the host MCU to sleep, it enables the host to save 'state' inside the
// notecard while it's asleep, and to retrieve this state when it awakens.  These are several
// 'segments' of state that may individually be saved.
struct {
	int cycles;
} globalState;
const char globalSegmentID[] = "GLOB";

struct {
	int measurements;
} tempSensorState;
const char tempSensorSegmentID[] = "TEMP";

struct {
	int measurements;
} voltSensorState;
const char voltSensorSegmentID[] = "VOLT";

// One-time Arduino initialization
void setup()
{

	// Arduino IDE requires a delay to move the serial port over
	// from programming the MCU to the debug monitor.
    delay(2500);
    usbSerial.begin(115200);
    notecard.setDebugOutputStream(usbSerial);

    // Initialize the physical I2C I/O channel to the Notecard
    Wire.begin();
    notecard.begin();

	// Determine whether or not this is a 'clean boot', or if we're
	// restarting after having been put to sleep by the Notecard.
	NotePayloadDesc payload;
	bool retrieved = NotePayloadRetrieveAfterSleep(&payload);

	// If the payload was successfully retrieved, attempt to restore state from the payload
	if (retrieved) {

		// Restore the various state data structures
		retrieved &= NotePayloadGetSegment(&payload, globalSegmentID, &globalState, sizeof(globalState));
		retrieved &= NotePayloadGetSegment(&payload, tempSensorSegmentID, &tempSensorState, sizeof(tempSensorState));
		retrieved &= NotePayloadGetSegment(&payload, voltSensorSegmentID, &voltSensorState, sizeof(voltSensorState));

		// We're done with the payload, so we can free it
		NotePayloadFree(&payload);

	}

	// If this is our first time through, initialize the Notecard and state
	if (!retrieved) {

		// Initialize operating state
		memset(&globalState, 0, sizeof(globalState));
		memset(&tempSensorState, 0, sizeof(tempSensorState));
		memset(&voltSensorState, 0, sizeof(voltSensorState));

		// Initialize the Notecard
	    J *req = NoteNewRequest("hub.set");
		if (notehubProductUID[0]) {
		    JAddStringToObject(req, "product", notehubProductUID);
		}
	    JAddStringToObject(req, "mode", "periodic");
	    JAddNumberToObject(req, "outbound", notehubUploadPeriodMins);
	    NoteRequest(req);

		// Because many devs will be using oscilloscopes or joulescopes to closely examine power
		// consumption, it can be helpful during development to provide a stable and repeatable
		// power consumption environment. In the Notecard's default configuration, the 
		// accelerometer is 'on'.  As such, when debugging, devs may see tiny little blips from
		// time to time on the scope.  These little blips are caused by accelerometer interrupt
		// processing, when developers accidentally tap the notecard or carrier.  As such,
		// to help during development and measurement, this request disables the accelerometer.
		req = NoteNewRequest("card.motion.mode");
		JAddBoolToObject(req, "stop", true);
	    NoteRequest(req);

	}

}

void loop()
{

	// Bump the number of cycles
	globalState.cycles++;

	// Simulation of a device taking a measurement of a temperature sensor.  Because we
	// don't have an actual external hardware sensor in this example, we're just retrieving
	// the internal surface temperature of the Notecard.
	double currentTemperature = 0.0;
	J *rsp = NoteRequestResponse(NoteNewRequest("card.temp"));
	if (rsp != NULL) {
		currentTemperature = JGetNumber(rsp, "value");
		NoteDeleteResponse(rsp);
		tempSensorState.measurements++;
	}

	// Simulation of a device taking a measurement of a voltage sensor.  Because we
	// don't have an actual external hardware sensor in this example, we're just retrieving
	// the battery voltage being supplied to the Notecard.
	double currentVoltage = 0.0;
	rsp = NoteRequestResponse(NoteNewRequest("card.voltage"));
	if (rsp != NULL) {
		currentVoltage = JGetNumber(rsp, "value");
		NoteDeleteResponse(rsp);
		voltSensorState.measurements++;
	}

	// Add a note to the Notecard containing the sensor readings
    J *req = NoteNewRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", "example.qo");
        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "cycles", globalState.cycles);
            JAddNumberToObject(body, "temperature", currentTemperature);
            JAddNumberToObject(body, "temperature_measurements", tempSensorState.measurements);
            JAddNumberToObject(body, "voltage", currentVoltage);
            JAddNumberToObject(body, "voltage_measurements", voltSensorState.measurements);
            JAddItemToObject(req, "body", body);
        }
        NoteRequest(req);
    }

	// Put ourselves back to sleep for a fixed period of time
	NotePayloadDesc payload = {0, 0, 0};
	NotePayloadAddSegment(&payload, globalSegmentID, &globalState, sizeof(globalState));
	NotePayloadAddSegment(&payload, voltSensorSegmentID, &voltSensorState, sizeof(voltSensorState));
	NotePayloadAddSegment(&payload, tempSensorSegmentID, &tempSensorState, sizeof(tempSensorState));
	NotePayloadSaveAndSleep(&payload, hostSleepSeconds, NULL);

	// We should never return here, because the Notecard put us to sleep.  If we do
	// get here, it's because the Notecarrier was configured to supply power to this
	// host MCU without being switched by the ATTN pin.
	delay(15000);

}
