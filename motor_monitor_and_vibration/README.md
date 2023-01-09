# Motor Monitor and Vibration

Monitor a motor's activity and vibration level with a Notecard.

- [Motor Monitor and Vibration](#motor-monitor-and-vibration)
  - [Overview](#overview)
  - [Hardware you will need](#hardware-you-will-need)
  - [Setup overview](#setup-overview)
  - [Hardware Initial Setup](#hardware-initial-setup)
  - [Cloud Setup](#cloud-setup)
  - [Notecard Setup](#notecard-setup)
    - [Firmware](#firmware)
    - [Configure Notehub Project and Connection Mode](#configure-notehub-project-and-connection-mode)
    - [Configure Vibration Sensing](#configure-vibration-sensing)
      - [`card.motion.mode`](#cardmotionmode)
      - [`card.motion.track`](#cardmotiontrack)
    - [`card.motion.sync`](#cardmotionsync)
    - [`card.aux - gpio mode`](#cardaux---gpio-mode)
    - [Notecard CLI](#notecard-cli)
  - [Create a Route with JSONata](#create-a-route-with-jsonata)
  - [Configure Vibration Alerts](#configure-vibration-alerts)
  - [Blues Wireless Community](#blues-wireless-community)


## Overview

The Motor Monitor and Vibration app monitors a motor's activity (on or off) and vibration and optionally
send alerts when the vibration is not within the expected range for the motor's current activity.


## Hardware you will need

* AC to USB adapter (power brick) with a compatible micro USB cable
* (optional - for power outage events) [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-B](https://shop.blues.io/products/carr-b)
* [Notecard](https://blues.io/products/notecard/)
* A switched motor that has a 3v3 input or output that controls or indicates activity
* A pair of jumper cables


## Setup overview

With the switched motor (such as a motor and relay) already setup and ready, you first connect the Notecarrier to a computer to configure the Notecard. Once that is complete, the Notecarrier is disconnected from the computer and is securely mounted into the motor, with signalling connections made to the Notecarrier from the motor and the computer's USB connection to the Notecarrier is replaced with a power brick.

## Hardware Initial Setup

This initial setup configures the Notecard to provide activity and vibration monitoring.

1. Assemble the Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/).
2. Connect the Notecarrier to your PC with the micro USB cable.
3. (Keep the Notecard connected to the computer with the in-browser terminal active until you have completed [Notecard Setup](#notecard-setup).)

## Cloud Setup

Sign up for a free account on [Notehub.io](https://notehub.io) and create a new project.

## Notecard Setup

In these steps, you will use the in-browser terminal to configure your Notecard to report the motor state and motion events to your Notehub project. You may also skip these individual steps and use the [#Notecard-CLI] to configure the Notecard using a single command.

### Firmware

The Notecard should use [firmware version 3.5.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-1-october-7th-2022) or higher version. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

### Configure Notehub Project and Connection Mode

Set the ProductUID for the Notecard by pasting the command below into the in-browser terminal. Make sure to replace `com.your-company:your-product-name` with the ProductUID from your Notehub project, which can be found below your project's name in the dashboard at https://notehub.io. Also, replace the placeholder serial number (`"sn:" "motor-location"`) with the location of your fridge (e.g. "kitchen").

```json
{ "req": "hub.set", "mode": "periodic", "outbound": 5, "product": "com.your-company:your-product-name", "sn": "motor-location" }
```

This app uses `periodic` mode to minimize power use so that the solution can remain online for longer when mains power fails. `"outbound": 5` is the max wait time, in minutes, to sync outbound data from the Notecard. For more details, see [our Essential Requests documentation for `hub.set`](https://dev.blues.io/notecard/notecard-walkthrough/essential-requests/#notehub-configuration).

### Configure Vibration Sensing

Vibration sensing is configured using a number of distinct requests. Each request given below should be pasted into the in-browser terminal separately.

#### `card.motion.mode`

The [`card.motion.mode` requst](https://dev.blues.io/reference/notecard-api/card-requests/#card-motion-mode) enables motion sensing using the Notecard's built-in accelerometer. Motion is used to deterrmine how much the motor is vibration. The configuration below uses maximum sensitivity (`5`) and a 5 second period for each bucket of movement count.

```json
{
  "req":          "card.motion.mode",
  "start":        true,
  "sensitivity":  5,
  "seconds":      5
}
```

#### `card.motion.track`

The [`card.motiion.track`](https://dev.blues.io/reference/notecard-api/card-requests/#card-motion-track) request
configures the Notecard to write motion notes to notefile `_motion.qo` every 5 minutes. Each motion bucket (configured in `card.motion.mode` above) is 5 seconds of motion, so we use 60 of them to fill the 5 minute interval.

```json
{
  "req":       "card.motion.track",
  "minutes":   5,
  "count":     60,
  "start":     true
}
```

### `card.motion.sync`

THe [`card.motion.sync`](https://dev.blues.io/reference/notecard-api/card-requests/#card-motion-sync
) request configures how often the Notecard syncs tracking data to notehub. Here we use 5 minutes. Adjust this
according to how much data you wish to use and the responsiveness of alerts required.

```json
{
  "req":      "card.motion.sync",
  "start":    true,
  "minutes":  5
}
```

### `card.aux - gpio mode`

The [`card.aux`](https://dev.blues.io/notecard/notecard-walkthrough/advanced-notecard-configuration/#monitoring-aux-gpio-state-with-environment-variables) request configures how the Notecard uses the auxilliary GPIO pins.  Here we wish to either monitor or control the motor - which you use depends upon whether the motor provides the activity singal or whether the Notecard controls the motor's activity.

When the motor privdes an activity signal, configure `AUX1` as a input.

```json
{"req":"card.aux", "mode":"gpio", "usage": [
  "input-pulldown"
]}
```

When the motor should be controlled from the Notecard's `AUX1` pin, set the pin as an output, initially low.

```json
{"req":"card.aux", "mode":"gpio", "usage": [
  "low"
]}
```

Next we enable GPIO monitirong or control via environment variables, and sync changes to notehub.

```json
{"req": "env.set", "name": "_aux_gpio_report_enable", "text": "sync" }
```

That completes the Notecard configuration. Now you can mount the Notecarrier and Notecard to the motor.

### Notecard CLI

If you want to issue all of the above requests in one shot, you can do so with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) and the [`config.json`](./config.json) configuration script. You will need to change the `ProductUID` and `sn` property values in the `hub.set` command to match the ProductUID in your Notehub project. Once you've done that, you can configure the Notecard with:

```sh
notecard -setup config.json
```

You may also make other edit to the configuration file to change
* vibration sensitivity (`sensitivity` property in the `card.motion.mode` command)
* the number of buckets of vibration data captured
* how often motion events are captured on the Notecard
* how often events are send to Notehub
* change how often motion events are sent to the cloud

## Create a Route with JSONata

The app uses a JSONata in a Notehub route to determine the amount of vibration and to signal an alert when vibration is out of range for the current state of the motor.

1. Create a new route with notefile `_notion.qo`. Copy the contents of the `route.jsonata` from this project into the JSONata transformation text input in the route setup page.

(For testing, I used an http route to `https://website.site` to show the result of the route. You can use any route that works for your use case.)

## Configure Vibration Alerts

Vibration alerts are sent when the vibration is out of range compared to what is expected.
The vibration thresholds are set with these environment variables:

* `vibration_off`: the maximum amount of vibration expected when the motor is off
* `vibration_under`: the minimum abount of vibration expected when the motor is on
* `vibration_over`: the maximum abount of vibration expected when the motor is on

Start with these all configured to 1, which will generate alerts for any vibration. You then inspect the resulting events with the motor off and on to see typical ranges of vibration, and set the ranges accordingly.



## Blues Wireless Community

We’d love to hear about you and your project on the [Blues Wireless Community Forum](https://discuss.blues.io/)!