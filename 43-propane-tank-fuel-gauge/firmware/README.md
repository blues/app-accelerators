# Propane Tank Fuel Gauge

Monitor how much propane is left in your tank, the estimated burn time remaining, and receive alerts when the fuel is running low.

- [Propane Tank Fuel Gauge](#propane-tank-fuel-gauge)
  - [Solution Overview](#solution-overview)
  - [You Will Need](#you-will-need)
    - [Hardware](#hardware)
    - [Software](#software)
  - [App Functionality](#app-functionality)
  - [Setup](#setup)
    - [Hardware Setup](#hardware-setup)
      - [Connecting the Ultrasound Sensor](#connecting-the-ultrasound-sensor)
    - [Mounting the Sensor](#mounting-the-sensor)
  - [Environment Variables](#environment-variables)
    - [Updates and Reporting](#updates-and-reporting)
    - [Fuel Gauge](#fuel-gauge)
    - [Alert Configuration](#alert-configuration)
  - [Notefiles and Events](#notefiles-and-events)
  - [Notecard APIs Used](#notecard-apis-used)

## Solution Overview

While there are various tricks to find out how much propane is left in the tank, such as poring warm water down the side of the tank, we can use connected technology to get a more precise measure of how much fuel is left in the tank.

This solution uses a $25 ultrasound sensor to measure the amount of propane in the tank. You simply glue the sensor to the base of the propane tank - no other modifications to the tank are needed.

## You Will Need

### Hardware

- [Notecard](https://shop.blues.io/collections/notecard/products/note-wbna-500)
- [Notecarrier F](https://blues.io/products/notecarrier/notecarrier-f/)
- [Swan](https://shop.blues.io/collections/swan/products/swan)
- [DS1603L ultrasound liquid level sensor](https://www.amazon.com/XKC-DS1603L-V1-Ultrasonic-Non-contact-Sensing-Apacitive/dp/B08C27QLDT)
- [Ultrasound gel](https://www.amazon.com/gp/product/B00AMGUZ70/)
- 3 male-to-male jumper wires
- A power source, such as a 5V USB phone charger.
- USB-A to Micro-USB cable

### Software

- [Visual Studio Code (VS Code)](https://code.visualstudio.com/)
- [PlatformIO extension for VS Code](https://platformio.org/install/ide?install=vscode)


## App Functionality

* The app continuously monitors the liquid level sensor for a reading. The sensor outputs a reading every 1-2 seconds.

* If the app is unable to read from the sensor for more than 15 seconds, it send a `sensor offline` alert to the notefile `alert.qo`. When the sensor resumes reporting values, the alert is cleared and a `sensor online` event is send.

* The liquid level is used to calculate the percentage of fuel remaining. A low-pass filter is used to smooth out any jitter in the sensor readings.

* The rate of change of the liquid level is used to calculate the burn rate and burn time remaining.

* With the diameter of the tank configured, fuel remaining and burn rate are reported in gallons and gallons-per-hour respectively.

* Environment variables are used to configure the tank dimensions and thresholds for fuel low alerts. The default configuration is suitable for a 20lb propane tank.


## Setup

### Hardware Setup

1. Assemble your Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/).

2. Plug the Swan into the Notecarrier, aligning the Swan's male headers with the Notecarrier's female headers.

3. Connect the ultrasound sensor to the Notecarrier as described below.

4. Temporarily or Permanently mount the ultrasound sensor to the base of your propane tank. More details below.

#### Connecting the Ultrasound Sensor

The ultrasound sensor ships with a 4-pin socket, of which 3 pins are used. Using male-to-male jumper cables, connect the sensor to the Notecarrier as follows:

| Sensor Wire  | Notecarrier Pin |
| ------------ | --------------- |
| RED          | `F_3V3`         |
| BLACK        | `GND`           |
| YELLOW       | `A5`            |
| WHITE        | No connection   |

> **Note**: The sensor can be safely used with 5v power. For a stronger signal, you can connect the sensor's RED wire to `N_VUSB` when USB power is provided to the Notecarrier.

The Swan reads values from the ultrasound sensor via a serial connection - receive only - via pin `A5`. Using an alternative serial peripheral frees up the Swan's `TX/RX` pins for use with [Notecard Outboard Firmware Updates](https://dev.blues.io/guides-and-tutorials/notecard-guides/notecard-outboard-firmware-update/).


### Mounting the Sensor

The sensor is mounted to the base of the tank. It uses ultrasound reflections from the surface of the liquid to determine the height of the liquid in the tank. Note that it's essential there are no air gaps between the sensor and the tank or it will not be able to read the liquid level.

To mount the sensor to the tank:

   1. Clean the base of the tank to remove any impurities. This creates a better surface for bonding the sensor to the tank, and removes potential sources of interference.

   2. Mount the sensor at the center point, with the flat side without the LED in contact with the base of the tank so that the other side of the sensor with the LED is facing away from the base of the tank.

> **Note**: You may want to initially mount the sensor temporarily, using ultrasound gel, while having it connected to the Notecarrier. The gel is thick and helps the sensor stick to the tank, so you can find the correct placement and verify the sensor is operating.

* When the sensor is reading the liquid height successfully, the blue LED blinks at about 1-2 second intervals. When the sensor cannot read the liquid level, the LED is on continuously. If the LED is never on, the sensor is not receiving power. Double-check the wiring, and that the micro-USB cable is connected to either the Swan's USB port, when powering via `F_3V3`, or the Notecarrier's USB port, when powering via `N_VUSB`.

* The red onboard LED of the Swan also blinks once per reading (every 1-2 seconds). Multiple blinks in quick succession indicate the sensor is not detected or is not reporting the liquid level. Should that happen, check the wiring and the mounting of the sensor, ensuring there is no air gap.


## Environment Variables

The app is configured using a number of [environment variables](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/). These control the app's reporting behavior, sets the dimensions of your propane tank and sets thresholds for low fuel alerts. The default values are dialled in for a 20lb propane tank.

### Updates and Reporting

* `environment_update_mins`: how often to check for environment variable updates, in minutes. The default value is 5 minutes.

* `report_mins`: how often to send a note to `monitor.qo` reporting the current fuel level and burn rate. The default value is 5 minutes.

### Fuel Gauge

* `empty_height_mm`: the liquid level corresponding to an empty tank, in millimeters. (1 inch is 25.4mm.) The sensor reads liquid levels down to 50mm (roughly 2 inches), although the accuracy reduces as this limit is approached. The default value is 60.

* `full_height_mm`: the liquid level corresponding to a full tank. The default value is based on a 20lb propane tank, which we estimated to be full at 305mm. After filling your propane tank, set this value to the measured height of the liquid propane. A propane tank is never completely full, since there must be a 10-20% headspace above the liquid for safety.

* `tank_diameter_mm`: The internal diameter of the tank. This is used to calculate the volume of propane remaining, in gallons, and also the burn rate in `gal/min`. The default value is set for a 20lb propane tank, 312mm / 12.3in.

* `acoustic_velocity`: the acoustic velocity of the liquid, in `m/s`. The default value is the acoustic velocity of propane, 1161 m/s. You typically do not need to change this value unless your tank contains something other than liquid propane.

### Alert Configuration

* `alert_burn_time_mins`: configures the app to send an alert when the remaining burn time is less than this. THe default value is 60 (minutes). Setting to 0 disables this alert.

* `alert_fuel_percent`: configures the app to send an alert when the amount of fuel remaining falls below this value. The default value is 10 (percent). Setting to 0 disables this alert.


## Notefiles and Events

`notify.qo`: notifications about environment variable changes and errors.

`monitor.qo`: monitoring events about the amount of fuel in the tank and burn rate.

`alert.qo`: alerts, such as sensor offline/online and fuel low alerts.


## Notecard APIs Used

The app uses these Notecard APIs:

* ['hub.set'](https://dev.blues.io/api-reference/notecard-api/hub-requests/#hub-set) - Sets the Notehub project and the connection mode. See our [Notecard walkthrough)((https://dev.blues.io/notecard/notecard-walkthrough/essential-requests/#notehub-configuration) for more details.

* [`note.template`](https://dev.blues.io/api-reference/notecard-api/note-requests/#note-template) - The Note Templates feature reduces the amount of data that is sent to the cloud. More details in our tutorial [Low Bandwidth Design](https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/).

* ['note.add'](https://dev.blues.io/api-reference/notecard-api/note-requests/#note-add) - Adds a note to a notefile, which is sent to Notehub.

* [`env.modified`](https://dev.blues.io/api-reference/notecard-api/env-requests/#env-modified) - Retrieves the time environment variables were changed. The host uses this to determine if it needs to refresh the environment variables.

* [`env.get`](https://dev.blues.io/api-reference/notecard-api/env-requests/#env-get) - Retrieves environment variables from Notecard.


Code patterns:

* The `Alert` class is used to keep track of the state of an alert, and whether the corresponding event has been sent. This pattern ensures that the event is resent should it not be delivered to the Notecard for some reason.


References

https://helpful.knobs-dials.com/index.php/Low-pass_filter
