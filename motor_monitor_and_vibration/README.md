# Motor Monitor and Vibration

Monitor a motor's activity and vibration level with alerts using a Notecard.

TOC

## Overview

The Motor Monitor and Vibration app monitors a motor's activity (on or off) and vibration and optionally
send alerts when the vibration is not within the expected range for the motor's activity.

With the switched motor (such as a motor and relay) already setup and ready, the Notecarrier is first connected to a computer for initial configuration. Once that is complete, the Notecarrier is disconnected from the computer and is securely mounted into the motor, with signalling connections made to the Notecarrier from the motor. The computer's USB connection is replaced with a power brick.

## Hardware you will need

* AC to USB adapter (power brick) with a compatible micro USB cable
* (optional - for power outage events) [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-B](https://shop.blues.io/products/carr-b)
* [Notecard](https://blues.io/products/notecard/)
* A switched motor that has a 3v3 input or output that controls or indicates activity
* A pair of jumper cables

## Hardware Initial Setup

This initial setup is used to configure the Notecard to provide activity and vibration monitoring.

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/).
2. Connect the Notecarrier to your PC via the micro USB cable.
3. (Keep the Notecard connected to the computer with the in-browser terminal active until you have completed [Notecard Setup](#notecard-setup).)

## Cloud Setup

Sign up for a free account on [Notehub.io](https://notehub.io) and create a new project.

## Notecard Setup

In these steps, you will use the in-browser terminal to configure your Notecard to report environmental data and power outage events to your Notehub project. You may also skip these individual steps and use the [#Notecard-CLI] to configure the notecard using a single command.

### Firmware

The Notecard should use [firmware version 3.5.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-1-october-7th-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

### Configure Notehub Project and Connection Mode

Set the ProductUID for the Notecard by pasting the command below into the in-browser terminal. Make sure to replace `com.your-company:your-product-name` with the ProductUID from your Notehub project, which can be found below your project's name in the dashboard at https://notehub.io. Also, replace the placeholder serial number (`"sn:" "motor-location"`) with the location of your fridge (e.g. "kitchen").

```json
{ "req": "hub.set", "mode": "periodic", "outbound": 3, "product": "com.your-company:your-product-name", "sn": "motor-location" }
```

This app uses `periodic` mode to minimize power use so that the solution can remain online for longer when mains power fails. `"outbound": 3` is the max wait time, in minutes, to sync outbound data from the Notecard. For more details, see [our Essential Requests documentation for `hub.set`](https://dev.blues.io/notecard/notecard-walkthrough/essential-requests/#notehub-configuration).

### Configure Vibration Sensing

Vibration sensing is configured using a number of distinct commands. Each command given below should be pasted into the in-browser terminal, one by one.

#### `card.motion.mode`

The [`card.motion.mode` command](https://dev.blues.io/reference/notecard-api/card-requests/#card-motion-mode) enables motion sensing using the built-in accelerometer, which is used to deterrmine the vibration of the motor. The configuration below uses maximum sensitivity (`5`) and a 5 second period for each bucket of movement count.

```json
{
  "req":          "card.motion.mode",
  "start":        true,
  "sensitivity":  5,
  "seconds":      5
}
```

#### `card.motion.track`

The [`card.motiion.track`](https://dev.blues.io/reference/notecard-api/card-requests/#card-motion-track) command
configures the Notecard to write motion Notes to notefile `_motion.qo`. 

```json
{
  "req":       "card.motion.track",
  "minutes":   5,
  "count":     5,
  "start":     true
}
```



https://dev.blues.io/reference/notecard-api/card-requests/#card-motion-sync

{
  "req":      "card.motion.sync",
  "start":    true,
  "minutes":  60
}


https://dev.blues.io/notecard/notecard-walkthrough/advanced-notecard-configuration/#monitoring-aux-gpio-state-with-environment-variables

{"req":"card.aux", "mode":"gpio", "usage": [
  "input-pulldown"
]}

/* enable GPIO monitoring via environment variables and sync to Notehub */
{"req": "env.set", "name": "_aux_gpio_report_enable", "text": "sync" }


## Adding Power Outage Detection

By using a LiPo battery with your Notecarrier,  [TODO]


### [Optional] Enable Power Outage Alerts

Paste this command into the in-browser terminal:

```json
{ "req": "card.voltage", "mode": "lipo", "usb": true, "alert": true, "sync": true }
```

This instructs the Notecard to assume LiPo battery characteristics, monitor USB power and send an alert immediately when a power outage occurs and when power is restored. For more details, see the [Notecard API reference](https://dev.blues.io/reference/notecard-api/card-requests/#card-voltage) and the `Enable USB Power Alerting` example.


### Notecard CLI

If you want to issue all of the above requests in one shot, you can do so with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) and the config.json configuration script. You will need to change the `ProductUID` and `sn` property values in the `hub.set` command to match your ProductUID. Once you've done that, you can configure the Notecard with:

```sh
notecard -setup config.json
```

You may also make other edit to the configuration file to change
* vibration sensitivity (`sensitivity` property in the `card.motion.mode` command)
* the number of buckets of vibration data captured
* how often motion events are captured on the Notecard
* how often events are send to Notehub
* change how often motion events are sent to the cloud 


## Blues Wireless Community

We’d love to hear about you and your project on the [Blues Wireless Community Forum](https://discuss.blues.io/)!