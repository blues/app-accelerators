# Valve Monitor

Monitor and control the open/closed state of a valve, and measure liquid flow state.

![The valve monitor project’s web application and hardware](images/banner.png)

To run this project yourself you’ll need to:

* [Configure Notehub](#notehub).
* [Purchase and assemble the necessary hardware](#hardware).
* [Flash the project’s firmware](#firmware)
* [Run the project’s web application](#web-application).

## Notehub

The Valve Monitor project runs on top of Notehub. As such, to run this sample
you’ll need to create a Notehub account, start a new Notehub project, and configure
a handful of environment variables.

### Creating a New Project

1. Sign up for a free [Notehub](https://notehub.io) account, if you don’t have one
already.
1. Click the blue **Create Project** button in the top-right corner of the screen.
1. Give the project a name, such as “ValveMonitor”, and then click the next
**Create Project** button.
![Creating a project in Notehub with a name](images/notehub-create-project.png)

### Configuring Environment Variables

The Valve Monitor project uses [environment variables](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/)
for storing project settings that need to be shared and synchronized across
devices.

You can set this project’s environment variables at the [fleet](https://dev.blues.io/api-reference/glossary/#fleet)
level or device level, where fleet-level variables affect all devices in the
fleet, and device-level variables are specific to an individual device.

Notehub creates an initial fleet for you when you start a new project, and you
can use that for setting your initial variables.

1. Navigate to your fleet in the navigation menu, and then click the **Settings**
link.
![Location of Notehub fleet settings](images/notehub-fleet-settings.png)
1. Click the **Environment** tab.
1. Scroll down to the **Fleet environment variables** heading, and define the
four variables below.
    1. `flow_rate_alarm_threshold_max`: (number) The maximum expected flow rate
    from your device, in mL / min. Flow-rate readings over this amount will trigger
    an alarm.
    1. `flow_rate_alarm_threshold_min`: (number) The minimum expected flow rate
    from your device, in mL / min. Flow-rate readings under this amount will trigger
    an alarm.
    1. `monitor_interval`: (integer) How often to take readings from the device’s
    flow-rate monitor, in seconds.
1. With all three defined, click the blue **Save** button.
![Notehub fleet environment variables fully defined](images/notehub-env-vars-defined.png)

With this your Notehub backend is fully configured, and you’re ready to start
setting up your hardware.

## Hardware

The following hardware is required to run the Valve Monitor project.

* A [Blues Starter Kit](https://shop.blues.com/collections/blues-starter-kits?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link).
* A flow meter, such as this [GREDIA Quick Connect Water Flow Sensor](https://www.amazon.com/dp/B07RD4JXLY/ref=cm_sw_r_api_i_652XGBZAS7RN06BSSHRT_0).
* A solenoid valve, such as this [DIGITEN Inlet Feed Water Solenoid Valve](https://www.amazon.com/dp/B016MP1HX0/ref=cm_sw_r_api_i_6PW0RXB6569QB10RY51V_0).
  * If you do get the DIGITEN valve, make sure you also have [spade cables](https://www.amazon.com/dp/B08F784R9W/ref=cm_sw_r_api_i_3GMNPATKY77AAC247J9S_0)
  for connecting the valve to power.
* Tubing for moving liquid throughout your hardware, such as this
[1/4 OD silicone tubing](https://www.amazon.com/dp/B09H4RNGGG/ref=cm_sw_r_api_i_H171CC4D2EDPPM23X8A4_0?th=1).
* A power breakout, such as this [HiLetgo power breakout](https://www.amazon.com/dp/B07X9SQKL6/ref=cm_sw_r_api_i_5JYD92FH343E04VKPMR5_0)
for supplying correct voltage to this project’s various components.
* An I2C level shifter, such as [this one from HiLetgo](https://www.amazon.com/dp/B07F7W91LC/ref=cm_sw_r_api_i_FZPJ7VRY2329ARNQ3W22_0), to
translate the flow meter’s pulses from 5V logic to 3.3V logic for the Swan
microcontroller.
* A MOSFET driver, such as this one from [HiLetgo](https://www.amazon.com/dp/B01I1J14MO/ref=cm_sw_r_api_i_8YRY25Q7R9HGV1ZPHERP_0),
is required to allow the Swan to switch on high-power devices such as the
solenoid, without needing to send that power from the microcontroller. (The
Swan can only send about 0.066 Watts (3.3V*20mA) out of a GPIO.)

Additionally you may wish to also use the following:

* (Optional) A manual valve such as this [YZM Quick Connector](https://www.amazon.com/dp/B077H2JWSZ/ref=cm_sw_r_api_i_38J4S2VWFKAZBVEA8GCM_0)
to make it easier to start/stop the flow of liquid while testing.
* (Optional) An enclosure for your hardware such as this
[outdoor-friendly enclosure from Sixfab](https://sixfab.com/product/raspberry-pi-ip54-outdoor-iot-project-enclosure/).

Once you have all of your hardware you’ll next need to assemble the pieces.
To start, complete the [Notecard and Notecarrier-F quickstart guide](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/), which will help
you connect your Notecard, Notecarrier, and Swan.

![The Notecard, Notecarrier, and Swan connected](images/notecard-and-notecarrier.jpg)

From there you’ll need to connect both your flow meter and solenoid valve
to the Notecarrier. If you’re using this project’s recommended hardware, here’s a [wiring diagram that shows how all the components connect](https://miro.com/app/board/uXjVPL8v4hE=/).

![Wiring diagram of the various valve monitor components](images/valve-app-wiring-diagram.png)

Regardless of which hardware you use, you’ll need to have the following pins
wired to your Notecarrier:

* The solenoid valve’s signal line must be connected to the Notecarrier’s `F_D10`
pin. The Swan uses this pin to open and close the solenoid.
* The flow meter’s signal line must be be connected to the Notecarrier’s `F_D6`
pin. The Swan uses this pin to read the flow rate from the flow meter.
* The Notecarrier’s `F_D13` pin must be wired to the Notecarrier’s `ATTN` pin. The
Swan uses this to [respond to interrupts](https://dev.blues.io/guides-and-tutorials/notecard-guides/attention-pin-guide/)
whenever the Notecard receives a command to open or close the solenoid valve.

When everything is put together your build should look something like this.

![The Notecarrier connected to the solenoid and the valve monitor](images/full-build-no-case.jpg)

If you’re also using a case like the [recommended one from Sixfab](https://sixfab.com/product/raspberry-pi-ip54-outdoor-iot-project-enclosure/),
your build can look like this when placed within the enclosure.

![The full build within a Sixfab enclosure](images/full-build-in-case.jpg)

With the hardware assembled, your next step is getting the project’s firmware running on
your device.

## Firmware

This project has firmware implemented with both Arduino and Zephyr. To get your
firmware up and running on the Swan, you can follow the documentation for either
implementation:

- [Arduino](firmware/arduino)
- [Zephyr](firmware/zephyr)

## Web Application

The Valve Monitor project’s web application allows you to view flow rates, open and
close solenoid valves, set alarm thresholds, and manage environment variables in a
browser.

![The Valve Monitor project’s web application](images/web-app.png)

As a final step, complete the [web app’s setup guide](web-app/) to get the app running
on your development machine.
