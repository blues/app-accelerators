# Freight Car Door Motion and Location

Track the location of a Freight Car and determine the number of times the door was opened and closed. Battery powered solution with an expected lifespan of 10 years.

- [Freight Car Door Motion and Location](#freight-car-door-motion-and-location)
  - [You will need](#you-will-need)
  - [Notehub Setup](#notehub-setup)
  - [Hardware Setup](#hardware-setup)
  - [Notecard Firmware](#notecard-firmware)
  - [Notecard Configuration](#notecard-configuration)
    - [`card.voltage`](#cardvoltage)
    - [`card.location.mode`](#cardlocationmode)
    - [`card.location.track`](#cardlocationtrack)
    - [`card.aux` GPIO mode](#cardaux-gpio-mode)
  - [Detecting when the door is opened and closed](#detecting-when-the-door-is-opened-and-closed)
    - [Hall Effect Sensor](#hall-effect-sensor)
    - [Magnetic Door Switch](#magnetic-door-switch)
  - [Notecard CLI](#notecard-cli)


## You will need

* USB A to micro USB cable
* [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-B](https://shop.blues.io/products/carr-b)
* [Notecard WBNA](https://shop.blues.io/collections/notecard/products/note-wbna-500)
* To sense the door opening, either
  * [Hall Effect Sensor](https://www.amazon.com/HiLetgo-NJK-5002C-Proximity-3-Wires-Normally/dp/B01MZYYCLH/)
* or
  * [Magnetic Door Sensor](https://www.amazon.com/Gufastore-Surface-Magnetic-Security-Adhesive/dp/B07F314V3Z)
* Dupont jumper cables (male-female)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-b).
2. Plug the LiPo battery's JST cable into the Notecarrier port labeled "LIPO".
3. Connect the micro USB cable from your development PC to the port on the Notecarrier. This is to both charge the LiPo battery and configure the Notecard.

## Notecard Firmware

The Notecard should use [firmware version 3.5.2](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-2-november-2nd-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).


## Notecard Configuration

With the Notecarrier connected to your development PC, navigate to https://dev.blues.io, open the in-browser terminal and connect to the Notecard. Then enter this command in the terminal:

```json
{ "req": "hub.set", "product" : "com.your-company:your-product-name", "sn": "a-serial-number" }
```

Make sure to replace "com.your-company:your-product-name" with your ProductUID from [Notehub Setup](#notehub-setup). Also, replace "a-serial-number" with the serial number of other relevant identifier for this particular tracker (e.g. "freight car 2").

### [`card.voltage`]()

Next, enter this command to optimize voltage ranges for the LiPo battery

```json
{ "req": "card.voltage", "mode": "lipo" }
```


And then this command

```json
{ "req": "hub.set", "mode": "periodic", "inbound": 1440, "outbound": 60 }
```

This will have the notecard send any outbound messages once per hour. If there are no messages to send, the Notecard does not power on the modem, preserving battery life. It also checks for incoming messages once per day.

You can read more about this command in the [Low Power Design documentation](https://dev.blues.io/notecard/notecard-walkthrough/low-power-design/#customizing-voltage-variable-behaviors


### [`card.location.mode`](https://dev.blues.io/reference/notecard-api/card-requests/#card-location-mode)

This command tracks location daily, switching to once every 2 days when the battery is low. When powered via USB, location is reported every 30 minutes, since power use is not a concern.

```json
{
 "req": "card.location.mode",
 "mode": "periodic",
 "vseconds": "usb:30;high:86400;normal:86400;low:178200;dead:0"
}
```

### [`card.location.track`](https://dev.blues.io/reference/notecard-api/card-requests/#card-location-track)

This command enables automatic location tracking to Notefile `_track.qo`.

```json
{
 "req": "card.location.track",
 "start": true
}
```

You can optionally add the property `"sync":true` which configures the Notecard to perform an immediate sync with Notehub when location is reported. Without this, the Notecard will sync within the next hour.

### `card.aux` [GPIO mode](https://dev.blues.io/notecard/notecard-walkthrough/advanced-notecard-configuration/#using-aux-gpio-mode)

This command uses AUX GPIO mode to configure the notecard to count the number of times the door has been opened.

```json
{
    "req": "card.aux",
    "mode": "gpio",
    "usage": ["count-pulldown"],
    "file": "door.qo"
}
```

## Detecting when the door is opened and closed

Detecting whether the car door is open can be done using either a Hall Effect sensor and a magnet, or a magnetic door switch.

Which you use in your implementation will depend upon the mounting options available for each type of sensor. All other things being equal, the magnetic door switch is a better choice since it consumes no current while the door is closed. The Hall Effect sensor has a built-in LED which will drain the battery quicker.


### Hall Effect Sensor

The sensor is attached to the Notecarrier using this pinout:

* Brown (V+) to `VMAIN`
* Black (Signal) to `AUX1`
* Blue (V-) to `GND`

You may choose to crimp your own cable ends, or simply splice the wires on the sensor with a Dupont female connectors.

Note that the Hall Effect sensor only detects one pole of the magnet, usually the North pole. Be sure to test your sensor with the magnet by moving the magnet up to the sensor and away, and verify that you see events posted to `door.qo` in Notehub, confirming that the sensor detected the "open/close" event.

### Magnetic Door Switch

The switch operates like a relay, with COM, Normally Closed and Normally Open terminals. Since the door is closed for most of the time, we only want to sink current through the switch when the door is open to reduce power use.

The door switch is wired to the Notecarrier as follows:

* `VIO` on Notecarrier to the `COM` terminal on the switch
* `AUX1` on Notecarrier to the `NO` terminal on the switch

Once the wiring is complete, test the connection as follows:

1. bring the two parts of the switch into close proximity
2. simulate opening and closing the door by moving one part away and then back again to make contact.

You should see events posted to `door.qo` in Notehub. If you don't, you may need to perform a manual `sync` by typing `sync` into the in-browser terminal, or alternatively adding `"sync":true` to the `card.aux` command so that the Notecard syncs with Notehub when the door is opened.

## Notecard CLI

You can use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```
notecard -setup config.json
```

Before doing this, you will need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).