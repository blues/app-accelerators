# Motor Monitor and Vibration

Monitor a motor's activity and vibration level with a Notecard.

## Overview

The Motor Monitor and Vibration app monitors a motor's activity (on or off) and vibration and optionally
send alerts when the vibration is not within the expected range for the motor's current activity.


## Hardware you will need

* AC to USB adapter (power brick) with a compatible micro USB cable
* (optional - for power outage events) [LiPo battery with JST cable](https://shop.blues.com/products/5-000-mah-lipo-battery?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link)
* [Notecarrier B](https://shop.blues.com/products/carr-b?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link)
* [Notecard](https://blues.com/products/notecard/)
* A switched motor that has a 3v3 input or output that controls or indicates activity. (See the next section for details.)
* A pair of jumper cables

## Motor Choice and Testing

This project illustrates an app that monitors an industrial motor for activity and vibration, and can in principle operate with almost any kind of motor. In an industrial setting this would typically be an electric motor already installed at a facility. In addition to the motor, a 3.3-5v logic signal is used to indicate when the motor is active. In cases where this is not already present, it can be retrofitted using an optocoupler placed in parallel with the motor's electrical supply on the input side, which switches a 3.3v line from the Notecarrier on the output side, indicating when the motor is active.

During initial testing and evaluation, you probably don't have an industrial motor sitting on your desk! Fortunately, the project can be used with something smaller, such as a [mains-powered desk fan](https://www.amazon.com/AmazonBasics-Speed-Air-Circulator-7-Inch/dp/B082MY2MX3/ref=sr_1_7). For the motor activity signal, you could use the optocoupler as outlined above. Alternatively, you may simply use an [IoT relay](https://www.digital-loggers.com/iot2.html) to control the motor activity from the Notecard.

## Setup overview

With the motor and activity line already set up and ready, you will first connect the Notecarrier to a computer to configure the Notecard. Once that is complete, the Notecarrier is disconnected from the computer and is securely mounted onto the motor. The motor activity signal is connected to the Notecarrier from the motor and the Notecarrier is supplied via a USB power brick.

## Hardware Initial Setup

This initial setup configures the Notecard to provide activity and vibration monitoring.

1. Assemble the Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-x/).
2. Connect the Notecarrier to your PC with the micro USB cable. Keep the Notecard connected to the computer with the in-browser terminal active until you have completed [Notecard Setup](#notecard-setup).

## Cloud Setup

Sign up for a free account on [Notehub.io](https://notehub.io) and create a new project.

## Notecard Setup

In these steps, you will use the in-browser terminal to configure your Notecard to report the motor state and motion events to your Notehub project. You may also skip these individual steps and use the [#Notecard-CLI] to configure the Notecard using a single command.

### Firmware

The Notecard should use [firmware version 3.5.1](https://dev.blues.io/notecard/notecard-firmware-releases/#v3-5-1-october-7-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-walkthrough/updating-notecard-firmware#ota-dfu-with-notehub).

### Configure Notehub Project and Connection Mode

Set the ProductUID for the Notecard by pasting the command below into the in-browser terminal. Make sure to replace `com.your-company:your-product-name` with the ProductUID from your Notehub project, which can be found below your project's name in the dashboard at https://notehub.io. Also, replace the placeholder serial number (`"sn":"motor-location"`) with the location or other identifier for your motor.

```json
{ "req": "hub.set", "mode": "periodic", "outbound": 5, "product": "com.your-company:your-product-name", "sn": "motor-location", "body":{"app":"nf20"} }
```

This app uses `continuous` mode with immediate sync for maximum responsiveness and minimal wait time sending data to the cloud. For more details, see [our Essential Requests documentation for `hub.set`](https://dev.blues.io/notecard/notecard-walkthrough/essential-requests/#notehub-configuration).

### Configure Activity Sensing

This section configures the Notecard to sense when the motor is active.

Activity sensing is configured using a number of distinct requests. Each request given below should be pasted into the in-browser terminal separately.

#### `card.aux - gpio mode`

The [`card.aux`](https://dev.blues.io/notecard/notecard-walkthrough/working-with-the-notecard-aux-pins#monitoring-aux-gpio-state-with-environment-variables) request configures how the Notecard uses the auxiliary GPIO pins.  Here we wish to either monitor or control the motor - which you use depends upon whether the motor provides the activity signal or whether the Notecard controls the motor's activity.

When the motor provides an activity signal, configure `AUX1` as a input.

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

Next we enable GPIO monitoring or control via environment variables, and sync changes to notehub.

```json
{"req": "env.set", "name": "_aux_gpio_report_enable", "text": "sync" }
```

### Configure Vibration Sensing

Vibration sensing is configured using a number of distinct requests. Each request given below should be pasted into the in-browser terminal separately.

#### card.motion.mode

The [`card.motion.mode`](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-motion-mode) request enables motion sensing using the Notecard's built-in accelerometer. Motion is used to determine how much the motor is vibrating. The configuration below uses maximum sensitivity (`5`) and a 5 second period for each bucket of movement count.

```json
{
  "req":          "card.motion.mode",
  "start":        true,
  "sensitivity":  5,
  "seconds":      5
}
```

#### card.motion.track

The [`card.motion.track`](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-motion-track) request
configures the Notecard to write motion notes to notefile `_motion.qo` every 5 minutes. Each motion bucket (configured in `card.motion.mode` above) is 5 seconds of motion, so we use 60 of them to fill the 5 minute interval.

```json
{
  "req":       "card.motion.track",
  "minutes":   5,
  "count":     60,
  "start":     true
}
```

#### card.motion.sync

The [`card.motion.sync`](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-motion-sync
) request configures how often the Notecard syncs tracking data to notehub. Here we use 5 minutes. Adjust this
according to how much data you wish to use and the responsiveness of alerts required.

```json
{
  "req":      "card.motion.sync",
  "start":    true,
  "minutes":  5
}
```

### Notecard CLI

If you want to issue all of the above requests in one shot, you can do so with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) and the [`config.json`](https://github.com/blues/app-accelerators/blob/main/20-motor-monitor-and-vibration/config.json) configuration script. You will need to change the `ProductUID` and `sn` property values in the `hub.set` command to match the ProductUID in your Notehub project. Once you've done that, you can configure the Notecard with:

```sh
notecard -setup config.json
```

You may also make other edit to the configuration file to change
* vibration sensitivity (`sensitivity` property in the `card.motion.mode` command)
* the number of buckets of vibration data captured
* how often motion events are captured on the Notecard
* how often motion events are sent to Notehub

### Notecard Configuration Complete

That completes the Notecard configuration. Now you can disconnect the Notecarrier from the computer's USB port and power it from the USB power brick and mount the Notecarrier and Notecard to the motor.


## Create a Route with JSONata

The app uses a JSONata transform in a Notehub route to determine the amount of vibration, and to signal an alert when vibration is out of range for the current state of the motor.

1. Create a new route (such as an HTTP route) with notefile `_motion.qo`. Copy the contents of the `route.jsonata` from this project into the JSONata transformation text input in the route setup page.

2. Configure the remaining route details. For HTTP routes, this includes setting the URL of the endpoint that routed events are delivered to. (For testing, I used an http route to `https://website.site` to show the result of the route. You can use any route that works for your use case.)

For more details Notehub and routes, see [Routing Data with Notehub](https://dev.blues.io/notehub/notehub-walkthrough/#routing-data-with-notehub).

## Configure Vibration Alerts in Notehub

Vibration alerts are sent when the detected vibration is out of range compared to what is expected when the motor is active or not.

The vibration thresholds are set with these environment variables:

* `vibration_off`: the maximum amount of vibration expected when the motor is off
* `vibration_under`: the minimum amount of vibration expected when the motor is on
* `vibration_over`: the maximum amount of vibration expected when the motor is on

Environment variables are set using Notehub, either per device or per fleet, described in more detail in [our tutorial](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/#setting-a-notehub-device-variable).

Initially, start with these all configured to the value 1, which will generate alerts for any vibration. You then inspect the resulting events with the motor off and on to see typical ranges of vibration. With this information, you can then set the threshold variables accordingly.


## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.com/)!
