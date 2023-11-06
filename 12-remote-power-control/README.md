# Remote Power Control

Remote control power, and monitor line and load power quality and report power failures.

## Solution Overview

This solution uses two Dr. Wattson monitoring boards to monitor AC line power and AC load power, with the load power switched on or off using a remote controllable relay. If you do not wish to monitor AC line power going into the relay this solution can be deployed with just one Dr. Wattson instance to monitor load power and report power failures.

The solution comprises hardware, firmware, Notehub environment variables and jsonata scripts to provide power quality monitoring, power control and power quality alerts.

## You Will Need

  * [Blues Starter Kit](https://shop.blues.io/collections/blues-starter-kits), that contains amongst other items
    * [Notecarrier F](https://shop.blues.io/products/notecarrier-f)
    * [Swan](https://shop.blues.io/collections/swan)
    * [Notecard](https://shop.blues.io/collections/notecard)
    * [Molex Cellular Antenna](https://shop.blues.io/collections/accessories/products/flexible-cellular-or-wi-fi-antenna)
  * [LiPo battery](https://shop.blues.io/collections/accessories/products/5-000-mah-lipo-battery)
  * [Digital Loggers IoT Relay](https://www.digital-loggers.com/iot2.html) or similar.
  * 2 x [Dr. Wattson Energy Monitoring Board](https://www.upbeatlabs.com/wattson/)
  * 2 x [ProtoStax Enclosure for Dr. Wattson](https://www.protostax.com/products/) or similar enclosure
  * 2 x [female-to-JST qwiic cable assembly](https://www.adafruit.com/product/4397)

For each Dr. Wattson energy monitor board you will need:

  * Male-to-female grounded extension cable or suitable cables to wire an IEC or NEMA AC inlet and outlet to Dr. Wattson. 16 gauge is recommended as the minimum dimension. Please select a suitable wiring gauge for the maximum load required from the equipment being monitored.

  * Corded female NEMA socket for USB power, 18-gauge diameter minimum, such as a spliced male-to-female 18-gauge extension cable.

  * 2 18-gauge color-coded insulated wires to power the Dr. Wattson board. (This can be taken from the extension cable used to build the USB power outlet.)

Tools required:

  * Soldering iron (to melt and bridge solder jumpers on the Dr. Wattson board for I2C address configuration)
  * Wire cutter and stripper
  * Spade connectors or solder
  * Crimper to crimp AC wires to spade connectors (if used)
  * Heat shrink tubing and a source of heat, such as a heat gun
  * Philips screwdriver


## Dr. Wattson Energy Monitor Build

The Dr. Wattson energy monitoring board monitors power by being looped into the mains AC wiring both for input and output of the relay used to control power to your equipment. Additionally, solder jumpers are configured to select the I2C address of the Dr. Wattson board.

> **Note**: As the solution uses two Dr. Wattson boards connected to one Notecard, both boards must be configured with different I2C address.  In this guide we use address `0x74` for AC input (line) power and address `0x75` for AC output (load) power, which corresponds with monitor instance 1 and monitor instance 2.

Please see [Dr. Wattson Energy Monitor build](../08-power-quality-monitor/drwattson-build.md) for build instructions.

With the build complete, you will have two power monitors, each with an AC inlet and two AC outlets, like this

![Dr. Wattson monitor complete](images/wiring-10-complete.jpg)


## Hardware Setup

1. Assemble the Notecard, Notecarrier and antenna as described in our [quickstart tutorial](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/).

2. On the Notecarrier, ensure the `DFU` DIP switch set to `ON`, which maps `AUX RX/TX` over to `F_TX/F_RX` so that notifications can be sent to the host via a serial connection. (see [`card.aux.serial`](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-aux-serial))

3. Similarly, set the Notecarrier's `SWITCHED` DIP switch to the `ON` position.

4. Connect the two I2C Qwiic cables between the Notecarrier and Dr. Wattson boards:

   * With the Dr. Wattson board laid out with the 8 pins pointing at you, connect the jumper connectors as follows:
      ```
      BLACK  RED  NC  NC  NC  NC  BLUE  YELLOW
      ```
    ![](images/drwattson-i2c.jpg)

    * Insert the Qwiic JST connector into one of the `F_I2C` connectors on the edge of the Notecarrier-F next to the USB port. You may also connect it to the I2C connector on the Swan.

5. Connect the Swan to your computer using a micro-USB cable. This is so that the firmware can be uploaded to the Swan.

6. Connect the Notecarrier to your computer using a micro-USB cable. This is only necessary if you later use the in-browser terminal to configure the Notecard.

7. Insert the JST connector on the LiPo battery into the socket marked "LIPO" on the Notecard.


## Notehub

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

You may choose to deploy one instance of this solution, or deploy several instances to monitor and control multiple devices. When monitoring multiple devices at a facility, it is useful to group them into a Fleet. For more details, see [The Fleet Administrator's Guide](https://dev.blues.io/guides-and-tutorials/fleet-admin-guide/).

## Application Firmware

The application firmware found under the [firmware](./firmware/) folder can be built using these development environments:

* PlatformIO extension for Visual Studio Code
* Arduino extension for Visual Studio Code
* Arduino IDE

We recommend using one of the VS Code extensions, since they are easier to set up and use, and provide a comprehensive development experience. However, if you're familiar with the Arduino IDE, that can be used as well but requires a little more setup.

### PlatformIO extension for VS Code

There is no special setup required for the project beyond what is normally required to configure a PlatformIO project in VSCode. [This tutorial](https://dev.blues.io/quickstart/swan-quickstart/#using-the-vs-code-platformio-extension) explains how to install and use the PlatformIO.

The PlatformIO project is located in the `firmware` folder, where you'll find `platformio.ini` that configures the project, including libraries required, location of the sources and compile-time definitions required.

### Arduino Extension for VS Code

The source code for the Arduino project is under [`firmware/notepower/`](firmware/notepower/) in this repository. We have included the correct configuration in `.vscode/arduino.json` which selects the Swan board as the build target and configures the required compiler options.

Before building the project, you will need to install the required [libraries](#libraries) listed below.

### Arduino IDE

Before compiling and uploading the sketch, be sure to install the STM32Duino board support package. The tutorial [Using the Arduino IDE](https://dev.blues.io/swan/using-arduino-ide-with-swan/) in the Swan documentation shows how to install support for Swan in Arduino IDE and how to compile and upload firmware.

You will also need to install the required libraries, and increase the serial receive buffer size, detailed below.

#### Increasing the Serial Receive Buffer Size

The Arduino framework by default provides a very small Serial input buffer, which means that if a developer wishes to use the serial port in a way that receives a large volume of data quickly, the data will be truncated and missed.

The workaround, which is required by this sketch, is to add a compiler flag that increases the serial buffer size.

  1. Close the Arduino IDE if it is currently open.

  2. Find the location of the `platform.txt` file for the board that you are building for. When building for Swan, which is an STM32 board supported by STM32Duino, this is located at
      Mac: `~/Library/Arduino15/packages/STMicroelectronics/datasheets/stm32/2.3.0`
      Windows: `%HOME$/AppData/Local/Arduino15/packages/STMicroelectronics/datasheets/stm32/2.3.0`

  3. Create a file in that directory called `platform.local.txt` containing this line:

```
compiler.cpp.extra_flags=-DSERIAL_RX_BUFFER_SIZE=4096
```

This will increase the receive buffer size to what you need for this sketch.

#### Libraries

When using the Arduino extension for VS Code, or the Arduino IDE, install these libraries using the Library Manager before building the sketch:

* UpbeatLabs MCP39F521
* Blues Wireless Notecard

#### Arduino IDE - Compiling/Uploading

To compile and upload the power monitoring firmware, open the sketch at [`firmware/notepower/notepower.ino`](firmware/notepower/notepower.ino) from this repo.

### Configuring the ProductUID

There are two ways to configure the ProductUID created in the Notehub setup above - either using the in-browser terminal to send a request to the Notecard, or by editing the firmware source code. For more details on what the ProductUID is and how it set it please see [this guide](https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid).

#### Using the In-browser terminal

1. Connect the Notecarrier to your computer using a micro USB cable.
2. Launch the in-browser terminal at [blues.dev](https://dev.blues.io/terminal/)
3. Click the "USB Notecard" button under "Connect a Notecard".
4. Select the Notecard to connect to and click "Connect".
5. The terminal will display the firmware version of Notecard.
6. You can now enter a request to set the ProductUID and Serial Number of the device.

```json
{"req":"hub.set", "product":"<your-productUID-from-notehub>", "sn":"<facility-equipment>-monitor"}
```

You can also omit the serial number and use Notehub to set it:

1. Open the project in Notehub.
2. From the list of devices, double click the device that has the serial number you want to set.
3. In the "Summary" tab, use the pencil icon to edit the Serial Number field.

#### Editing the Source Code

You can also set the ProductUID in the source code. Open `app.h` in your IDE and edit the line

```c++
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
```

pasting in the ProductUID from your notehub project between the first pair of quotes.


## Electrical Connections

With the Dr. Wattson boards looped into the flow of power via extension cables, the supply is connected to the first monitor board, which supplies power to the relay. The relay output provides power to the equipment you want to monitor and control, via the second monitor board.

For ease of identification, use the board with the lower I2C address to monitor input power, and the higher I2C address to monitor output power. We recommend using addresses `0x74` and `0x75`.

![](images/electrical-schematic.png)

During development and testing, you will typically power the Notecarrier and Swan via USB cables from your computer. When the application is deployed, you will use a USB power adapter plugged into the first monitor USB outlet.

## Testing

To ensure the setup is working as expected, it's a good idea to test the application before deploying it in a real-world setting.

For testing, you will need a suitable load as the equipment to monitor and control. We used a desk lamp with off, low, medium and high settings that allows easy visual indication when the relay is off, and has variable power settings for power use and quality monitoring.

## Configure Monitoring

You configure the app using a number of environment variables. Configuration includes how often regular power monitoring events are sent, and the thresholds for anomalous behavior that trigger an alert.

Alerts are sent when the current, voltage, or power use is outside the configured range or when a change greater than a given percent is detected.

These are the environment variables that you can configure according your use case:

* `heartbeat_mins`: how many minutes between sending power notifications. The default is`0` which means do not send regular power monitoring events, only send alerts. Sending a heartbeat event periodically provides regular monitoring of the source supply and equipment supply.

* `alert_under_voltage`, `alert_over_voltage`: send an alert when the measured voltage is above or below the specified values in Volts. The default setting is `0` where no alerts are sent regardless of the measured voltage.
* `alert_change_voltage_percent`: send an alert when the voltage changes by more than the given percent. The default value is `15`, which sends an alert when a 15% or greater change is detected. Set to `0` to disable percentage change alerts.

* `alert_under_current_amps`, `alert_over_current_amps`: send an alert when the measured current is above or below the specified values in Amps. The default setting is `0` where no alerts are sent regardless of the measured current.
`alert_change_current_percent`: send an alert when the measured current changes by more than the given percent. The default value is `15`, which sends an alert when a 15% or greater change is detected. Set to `0` to disable percentage change alerts.

* `alert_under_power_watts`, `alert_over_power_watts`: send an alert when the measured power is above or below the specified values in Watts. The default setting is `0` where no alerts are sent regardless of the measured power.
* `alert_change_power_percent`: send an alert when the measured power changes by more than the given percent. The default value is `15`, which sends an alert when a 15% or greater change is detected. Set to `0` to disable percentage change alerts.

These environment variables are set in Notehub, either per-device, per-fleet or per-project. For example, if you want all monitored equipment to send power monitoring events every 5 minutes, you would set `heartbeat_mins` to `5` at the project level in Notehub.

Please see our tutorial [Understanding Environment Variables](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/) for a fuller description of how to set environment variables in Notehub.

### Instance number

Each power monitor is given an instance number from 1-4. The instance numbers correspond to the I2C address of the monitoring board:

* instance 1: I2C address `0x74`
* instance 2: I2C address `0x75`
* instance 3: I2C address `0x76`
* instance 4: I2C address `0x77`

For example, should your two monitors be configured at addresses `0x74` and `0x75` then these will be reported as instance 1 and instance 2.

## Equipment Control Lines

4 digital outputs act as control lines. Each Dr. Wattson instance is associated with a GPIO pin that controls power to that monitor:

* instance 1: pin D10
* instance 2: pin D11
* instance 3: pin D12
* instance 4: pin D13

In this solution, we want to control power to instance 2 via the relay. This is why D11 (control line 2) is connected to the relay's control signal in the schematic above.

To use a the relay's control line, set the environment variable `switch2=1` to drive the pin high and close the relay, supplying power to the equipment, or `switch2=0` to drive the pin low, opening the relay and cutting power. When a control line is configured, events from that monitoring instance will include an `active` property indicating whether the control line is high (`true`) or low (`false`).

## Configuring Power Control Alerts

In addition to setting the state of a control line, you can use environment variables to add additional behaviors that correlate the measured power with the state of the relay.  Specifically, setting the environment variable `alert_power_activity_2=source` indicates that monitor 2 is a source of power with the expectation that voltage, current and power are all zero when the pin is low, and within the configured ranges when the pin is high. Without this, the monitor would send undervoltage/current/power alerts when the relay is switched off.

These variables describe how long it takes for the monitored equipment to startup or shutdown. Power monitoring alerts are suppressed during the startup and shutdown period after a control line changes state.

* `power_activity_startup_secs_[1-4]`: the duration in seconds for how long it takes the power level to stabilize when the equipment is started. Alerts are suppressed for this period when a line goes from inactive to active. The default value is `0`.

* `power_activity_shutdown_secs_[1-4]`: the duration in seconds for how long it takes for the power level to stabilize when the equipment is stopped. Alerts are suppressed for this period when a line goes from active to inactive. The default value is `0`.


## Example Configuration

With a 120v supply, this configuration monitors input power to the relay, and the relay's output power  sends alerts when either fails:

| Environment variable name | Value  | Description                                    |
| :------------------------ | :----: | :------------------------------------------------------------|
| `heartbeat_mins`          | 5      | Send power monitoring events every 5 minutes. |
| `alert_under_voltage`     | 100    | Send an alert when voltage is less than 100v. |
| `alert_over_voltage`      | 135    | Send an alert when voltage is above 135v. |
| `switch2`                 | true/false | Control power to the equipment to be on or off. |
| `alert_power_activity_2`  | supply | Correlate the equipment power with the state of the relay. |

## Events

A power monitoring event is sent every `heartbeat_mins` minutes or when an alert is sent.
Events are sent to the notefile `power.qo`, and have this structure in the event body:

```json
{
    "instance": 1,          // which of the connected Dr. Wattson monitors reported the event
    "current": 0.2846,      // Line RMS current (A)
    "frequency": 59.8125,   // Line AC frequency (Hz)
    "power": 7.9,           // Line Power (Watts)
    "voltage": 118.6        // Line RMS voltage (V)
}
```

Regular monitoring events are not immediately synched to Notehub, but are sent once per hour, as given by the `outbound` property in the `hub.set` request. You can change this behavior by setting the preprocessor symbol `SYNC_POWER_MONITORING_NOTES` in [`app.h`](./firmware/notepower/app.h#L20).

The event body also includes these named values:

* `reactivePower`: The measured reactive power (in VAR).
* `apparentPower`: The measured apparent power (in VA).
* `powerFactor`: The power factor - active power divided by apparent power.
* `active`: set to True to indicate the corresponding instance pin is active.

> Note: When a property in an event is zero, or false, it is not present in the event routed to notehub. For more details see [How the Notecard works with JSON](https://dev.blues.io/notecard/notecard-walkthrough/json-fundamentals/#how-the-notecard-works-with-json).

### Alerts

When the device detects an alert condition, such as over or under voltage, current or power, or detects a change in these greater than the configured percentage, an alert event is sent immediately to Notehub. An alert event has the same properties as a power monitoring event, with the additional property `alert` that lists the comma-separated reasons for the alert. Depending upon the cause of the alert, you may see one or more of these values present:

* `undervoltage`, `overvoltage`: the measured RMS voltage is not within the bounds set by the environment variables `alert_under_voltage` and `alert_over_voltage`.

* `voltage`: the measured voltage changed by more than the percent specified in the environment variable `alert_change_voltage_percent`.

* `undercurrent`, `overcurrent`: the measured RMS current is not within the bounds set by the environment variables `alert_under_current_amps`, `alert_over_current_amps`.

* `current`: the measured current changed by more than the percent specified in the environment variable `alert_change_current_percent`.

* `underpower`, `overpower`: the measured active power is not within the bounds set by the environment variables  `alert_under_power_watts` and `alert_over_power_watts`.

* `power`: the measured active power changed by more than the percent specified in the environment variable `alert_change_power_percent`.


#### Power Control Alerts

These alerts are produced for a given instance when the corresponding [`alert_power_activity_X`](#configuring-power-activity-alerts) variable is set.

* `novoltage`,`nocurrent`: no voltage or current is detected when the control pin is high, indicating a supply or load should be present, but isn't.

* `inactivevoltage`, `inactivecurrent`: voltage or current is detected when the control pin is low, indicating the supply or load should be off, but isn't.


## Routing Data out of Notehub

Now that we have power monitoring events and alerts available, routing them from Notehub is our next step.

Notehub supports forwarding data to a wide range of API endpoints by using the Route feature. This can be used to forward your power monitoring data to external dashboards and alerts to a realtime notification service.  Here, we will use Twilio SMS API to send a notification of an alert to a phone number.

For an introduction to Twilio SMS routes, please see our [Twilio SMS Guide](https://dev.blues.io/guides-and-tutorials/twilio-sms-guide/).

1. Fill out the required fields for the Twilio SMS route, including "from" and "to" phone numbers, where "from" is your virtual Twilio number, and "to" is the number of the phone that receives the alerts. We will not be using placeholders for these numbers, but will use a placeholder for the message, so set the message field to `[.body.customMessage]`.

2. Under "Notefiles", choose "Selected Notefiles", and check `power.qo` from "Include These Notefiles".

3. Under "Data", select "JSONata Expression" and copy and paste the contents of [jsonata/route.jsonata](jsonata/route.jsonata) into the text field "Insert your JSONata expression here".

4. Click "Save Changes".

### Testing the Route

The ideal test is to use the app firmware to generate alerts. However, it's also possible to simulate an event by pasting these JSON snippets into the the in-browser terminal.

This is a regular power monitoring event. It does not generate an SMS alert.

```json
{ "req": "note.add", "file":"power.qo", "sync": true, "body": {
  "current":2.34, "frequency":60, "power":56.67, "voltage":230.12, "instance":2, "active":true,
}}
```

This is an alert event (due to the presence of the `alert` property), which will result in an SMS message being sent to the phone number in the "to" field.

```json
{ "req": "note.add", "file":"power.qo", "sync": true, "body": {
  "current":2.34, "frequency":60, "power":56.67, "voltage":230.12, "alert":"overcurrent,power", "instance":2, "active":true
}}
```

This will send a SMS that looks like this:

> Power alert from machine-12 equipment powered: yes: overcurrent,power. 120V, 25A, 3000W.

These are the parts of the message:

* The first part of the message indicates which device the alert pertains to by its serial number, here "machine-12".

* The next part indicates which monitor instance generated the alert, here it is "equipment", with "utility supply" as the other option. These names relate to the instance numbers, and here we are using 1 and 2 for utility supply and equipment, respectively. If your monitors are configured with different I2C addresses, you will need to edit the names "instance-3" and "instance-4" in the jsonata script.

* The activity state comes next, indicating the state of the relay.

* The alerts are next. Here, "overcurrent" and "power" alerts indicate that the measured current was higher than the `alert_over_current_amps` environment variable, and that power changed by more than `alert_change_power_percent`.

* Finally, we have the power information, showing the measured voltage, current and power at the time of the alert.

When utility power fails, you will receive an `undervoltage` alert from instance 1 (utility supply) with the measured voltage close to 0. If the equipment is also active at that time you will receive a `novoltage` alert from instance 2 (equipment).

## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!
