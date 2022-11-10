//
// Copyright 2019 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//
// This is the simplest example of how a device may send commands to the Notecard over a serial
// port by using nothing but simple "print line" functions targeting that Arduino serial port.
//

// If the Notecard is connected to a serial port, define it here.  For example, if you are using
// the Adafruit Feather NRF52840 Express, the RX/TX pins (and thus the Notecard) are on Serial1.
// If however you are using an M5Stack Basic Core IoT Development Kit, you would connect the
// R2 pin to the Notecard's TX pin, and the M5Stack's T2 pin to the Notecard's RX pin, and then
// would use Serial2.
//
// Note that both of these definitions are optional; just prefix either line with // to remove it.
//  Remove txRxPinsSerial if you wired your Notecard using I2C SDA/SCL pins instead of serial RX/TX

#if defined(ARDUINO_ARCH_AVR) && not defined(HAVE_HWSERIAL1)
#define txRxPinsSerial Serial
#elif defined(ARDUINO_ARCH_STM32) && not defined(HAVE_HWSERIAL1)
#define txRxPinsSerial Serial
#else
#define txRxPinsSerial Serial1
#endif

#ifdef ARDUINO_NRF52840_FEATHER
#include "Adafruit_TinyUSB.h"
#endif

// This is the unique Product Identifier for your device.  This Product ID tells the Notecard what
// type of device has embedded the Notecard, and by extension which vendor or customer is in charge
// of "managing" it.  In order to set this value, you must first register with notehub.io and
// "claim" a unique product ID for your device.  It could be something as simple as as your email
// address in reverse, such as "com.gmail.smith.lisa.test-device" or "com.outlook.gates.bill.demo"

// This is the unique Product Identifier for your device
#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif
#define myProductID PRODUCT_UID
#define myLiveDemo  true

// One-time Arduino initialization
void setup()
{

    // Initialize the serial port being used by the Notecard, and send a newline to clear out any data
    // that the Arduino software may have pending so that we always start sending commands "cleanly".
    // By delaying for 250ms, we ensure the any pending commands can be processed or discarded. We use
    // the speed of 9600 because the Notecard's RX/TX pins are always configured for that speed.
    txRxPinsSerial.begin(9600);
    txRxPinsSerial.println("\n");
    delay(250);

    // This command (required) causes the data to be delivered to the Project on notehub.io that has claimed
    // this Product ID.  (see above)
    if (myProductID[0]) {
        txRxPinsSerial.println("{\"req\":\"hub.set\",\"product\":\"" myProductID "\"}");
    }
    // This command determines how often the Notecard connects to the service.  If "continuous" the Notecard
    // immediately establishes a session with the service at notehub.io, and keeps it active continuously.
    // Because of the power requirements of a continuous connection, a battery powered device would instead
    // only sample its sensors occasionally, and would only upload to the service on a periodic basis.
#if myLiveDemo
    txRxPinsSerial.println("{\"req\":\"hub.set\",\"mode\":\"continuous\"}");
#else
    txRxPinsSerial.println("{\"req\":\"hub.set\",\"mode\":\"periodic\",\"outbound\":60}");
#endif

}

// In the Arduino main loop which is called repeatedly, add outbound data every 15 seconds
void loop()
{

    // Count the simulated measurements that we send to the cloud, and stop the demo before long.
    static unsigned eventCounter = 0;
    if (eventCounter++ > 25) {
        return;
    }

    // Simulate a temperature reading, between 5.0 and 35.0 degrees C
    double temperature = (double) random(50, 350) / 10.0;

    // Simulate a voltage reading, between 3.1 and 4.2 degrees
    double voltage = (double) random(31, 42) / 10.0;

    // Add a "note" to the Notecard, in the default data notefile. The "body" of the note is
    // JSON object completely of our own design, and is passed straight through as-is to notehub.io.
    // (Note that we add the "sync" flag for demonstration purposes to upload the data instantaneously,
    // so that if you are looking at this on notehub.io you will see the data appearing 'live'.)
    // Note that we use a somewhat convoluted way of displaying a floating point number because %f
    // isn't supported in many versions of Arduino (newlib).
    char message[150];
    snprintf(message, sizeof(message),
             "{"
             "\"req\":\"note.add\""
             ","
             "\"sync\":true"
             ","
             "\"body\":{\"temp\":%d.%02d,\"voltage\":%d.%02d,\"count\":%d}"
             "}",
             (int)temperature, abs(((int)(temperature*100.0)%100)),
             (int)voltage, (int)(voltage*100.0)%100,
             eventCounter);
    txRxPinsSerial.println(message);

    // Delay between simulated measurements
#if myLiveDemo
    delay(15*1000);     // 15 seconds
#else
    delay(15*60*1000);  // 15 minutes
#endif

}
