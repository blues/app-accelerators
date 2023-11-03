# Temperature and Smoke Detector

Temperature monitoring and smoke detection across a number of rooms at a facility.

![](images/MQ2_sensor.jpg)

## You Will Need

* [Sparrow Development Kit](https://shop.blues.io/products/sparrow-dev-kit)
* [MQ2 Gas and Smoke Sensor](https://www.amazon.com/Reland-Sun-MQ-2-Sensor-Module/dp/B09NN39G8X)
* [3.3-to-5v voltage converter](https://www.amazon.com/Comidox-Module-Voltage-Converter-0-9-5V/dp/B07L76KLRY)
* Soldering Iron
* 2 USB A to micro USB cables
* 3 male-to-female jumper wires

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/#set-up-notehub).

## Sparrow Setup

### Quickstart

Follow the [Sparrow Quickstart](https://dev.blues.io/quickstart/sparrow-quickstart/) pair your Sparrow reference node with the gateway and associate it with the Notehub project you just created. Note also that you only need one reference node for this project (although multiple reference nodes can be used to monitor multiple rooms). Make sure that you use the ProductUID generated in [Notehub Setup](#notehub-setup) when it comes time to issue the [`hub.set`](https://dev.blues.io/reference/notecard-api/hub-requests/#hub-set) command in the quickstart.

After you've completed the quickstart, leave the Notecarrier and Sparrow Essentials board powered and connected. These two devices will act as our gateway to Notehub, and you won't need to touch them again. The rest of this guide will focus on the Sparrow Reference Sensor node.

### MQ2 Sensor Connection

This solution makes use of the an MQ2 sensor module, which detects smoke, as well as various gasses. Using the 3 male-to-female jumper wires, connect the MQ2 sensor to the Sparrow Reference Sensor board and the v5 converter as follows:

1. Splice a male to female jumper in half and solder the exposed wires to the `GND` pad on the 5v converter. Connect the female end to `GND` on the sensor and the male end to the `GND` pin on Sparrow.
2. Splice another jumper, and solder the exposed wire on male half to `Vi` on the converter. Connect the male connector to `VBAT` on Sparrow. Solder the exposed wire on the female half to `Vo` and connect the female end to the `A0` pin on the MQ2 sensor.
3. Using an intact male-to-female jumper, connect `A0` on the sensor to the `A1` pin on Sparrow.
4. The 4th pin on the sensor is left unconnected.

## Firmware

This section shows how you build the Temperature and Smoke Detector firmware and flash it to the reference node.

1. You will need to fetch the dependencies required by the firmware. After cloning this repository, run these commands: `git submodule update --init 31-temperature-and-smoke-detector/firmware/note-c` and `git submodule update --init 31-temperature-and-smoke-detector/firmware/sparrow-lora`. This will fetch the `note-c` and `sparrow-lora` submodules that this project depends on.

2. Connect the STLINK-V3MINI to your development PC with a USB A to micro USB cable.

3. Connect the STLINK to your reference node with the 2x7 JTAG ribbon cable.

4. There are a few ways to build and flash the firmware onto the reference node. These are covered in the [Sparrow Builder's Guide](https://dev.blues.io/sparrow/sparrow-builders-guide/). Follow the steps in that guide and then return to these instructions.

> **Note**: When building the firmware with VSCode, be sure the `firmware` folder is added at the root of the workspace. This ensures the project's tasks to compile the firmware are recognized by VSCode.

5. Build and flash the firmware using whichever method you chose when following the Sparrow Builder's Guide.

6. Open a terminal emulator and connect to the STLINK's serial connection to view the logs from the device. See the documentation [here](https://dev.blues.io/sparrow/sparrow-builders-guide/#collecting-firmware-logs).

7. Start the program in debug mode (again, how you do this depends on the IDE: VS Code or STM32CubeIDE). In your terminal emulator, you should see something like this:

```
===================
===================
===== SPARROW =====
===================
Feb 16 2023 22:42:58
2037335832365003001d001f
APPLICATION HOST MODE
CONSOLE TRACE ENABLED
```

You will see a message like this after a few minutes:

```
mq2: note sent. gas: 4226, temperature: 20.2C, humidity: 35.26%
```

## Temperature Monitoring and Smoke Alerting Behavior

The firmware periodically monitors temperature and gas concentrations, and posts events with this information. Regular monitoring events are not synced immediately to Notehub, and will be delivered as often as the Notecard is configured to sync with Notehub via the [`hub.set`](https://dev.blues.io/reference/notecard-api/hub-requests/#hub-set) request.

Temperature and Gas measurements are posted to the Notefile `*#mq2.qo`, where `*` is replaced with the unique ID of the Sparrow node. An event has these properties:

```cpp
{
  "app" : "nf31",       // the application name
  "temperature" : 20.6, // ambient temperature (in &deg;C)
  "humidity" : 41.2,    // ambient humidity (in percent)
  "voltage" : 4.7       // battery voltage (in V)
}
```

Each reading taken is checked against the [configured thresholds](#configuration). If any reading is higher than the corresponding threshold, the event is sent immediately to Notehub as an alert. Alerts are distinguished from regular monitoring events by the `alert` property.

```cpp
{
  "alert" : 1,          // Signifies an alert
  "app" : "nf31",       // the application name
  "temperature" : 40.3, // ambient temperature (in &deg;C)
  "humidity" : 25.2,    // ambient humidity (in percent)
  "voltage" : 4.7       // battery voltage (in V)
}
```

The `alert` property has these values:

* `alert:1`: Indicates an alert condition regarding temperature or presence of smoke or gas has been detected. The event is immediately synced to Notehub. You can use this to signal an alert externally, such as notifying a pager other messaging service.

* `alert:2`: Signifies that the alert is still ongoing. Subsequent alerts after the initial alert have the `alert` property set to `2`, indicating that the alert is ongoing because temperature or gas levels haven't returned to normal. These events are not immediately sent to notehub, and are provided for continuous monitoring.

* `alert:3`: Signifies a stand-down alert and that temperature and gas levels have returned to normal. These events are sent immediately to notehub. This is typically used to send an external notification that normal temperature and gas levels have been reached.

When temperature and gas levels return to normal, `alert:3` is the last event sent with the `alert` property present. Subsequent events do not have the `alert` property present until a new alert condition is detected.


### Configuration

The file [`./firmware/sparrow-application/mq2/mq2.c`](./firmware/sparrow-application/mq2/mq2.c) contains a number of `#define`s that are used to configure how often temperature and gas levels are measured and the thresholds that trigger an alert:

| Name     | Default  | Unit    | Description |
|----------|----------|---------|-------------|
| `MQ2_MONITOR_PERIOD` | 60 | seconds | The period in seconds between each sensor reading.
| `ALERTS_ONLY` | false | boolean | When true, disables reporting of regular monitoring events and only alerts are sent. |
| `ALERT_TEMPERATURE` | 50.0 | C | Trigger an alert when the temperature is at least this high. Set to 0 to disable temperature alerts. |
| `ALERT_GAS_LEVEL` | 40000 | 1-65535 | Trigger an alert when the measured gas level is at least this high. Set to 0 to disable gas level alerts. |

### Calibrating the Gas Sensor

The MQ2 sensor indicates the the gas level detected by way of the the voltage produced on the A0 analog output - a higher voltage means more gas or smoke was detected. The absolute value of this voltage is not calibrated. Additionally, some sensors feature a potentiometer to adjust the sensitivity, which further changes the range of voltages the sensor outputs.

In order to determine the range of values you should test the solution in clean air, and, if possible, in a smokey environment in order to determine a suitable value for `ALERT_GAS_LEVEL`.

## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!

## Additional Resources

* [Sparrow Datasheet](https://dev.blues.io/hardware/sparrow-datasheet/)
* [Sparrow Hardware Behavior](https://dev.blues.io/sparrow/sparrow-hardware-behavior/) (e.g. what do the various Sparrow LEDs indicate?)
* [MQ2 Datasheet](https://www.mouser.com/datasheet/2/321/605-00008-MQ-2-Datasheet-370464.pdf)
