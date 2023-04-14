# Sump Level Monitor

Monitor water level changes in a sump using a Notecard and a float switch.

- [Sump Level Monitor](#sump-level-monitor)
  - [You Will Need](#you-will-need)
  - [Notehub Setup](#notehub-setup)
  - [Hardware Setup](#hardware-setup)
  - [Notecard Firmware Setup](#notecard-firmware-setup)
  - [Notecard Configuration](#notecard-configuration)
    - [`hub.set`](#hubset)
    - [`card.aux`](#cardaux)
    - [Notecard CLI](#notecard-cli)
  - [Testing](#testing)
  - [Going Further](#going-further)

## You Will Need

* USB A to micro USB cable
* [Notecarrier-A](https://shop.blues.io/products/carr-al) or [Notecarrier-B](https://shop.blues.io/products/carr-b) (for the Notecarrier-B you'll need a separate antenna, which comes with the Notecarrier-B in our shop)
* [Notecard](https://blues.io/products/notecard/)
* [Sump Alarm Three Wire Float Switch](https://www.sumpalarm.com/products/float-switch-for-use-septic-system-sump-pump-water-tank-see-all-five-cable-length-options)
* Soldering iron and solder flux
* Strip of male header pins

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here for the Notecarrier A](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/) or [here for the Notecarrier B](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-b/).
2. Strip ~1/2" of the insulation off the float switch's brown and black wires. Twist the strands of each wire tightly and then use a soldering iron to [tin each wire](https://www.youtube.com/watch?v=pRPF4wpXX9Q). We will not be using the remaining blue wire.
3. With the float switch wires tinned, break off two male header pins and solder a header onto each switch wire so that you can easily connect the switch to the Notecarrier's headers.
4. Plug the header from one of the float switch's wires (it doesn't matter which one) into the GND header on the Notecarrier.
5. Plug the remaining float switch wire into AUX1 on the Notecarrier. With this configuration, when the switch is closed, AUX1 will be connected go ground.

## Notecard Firmware Setup

The Notecard should use [firmware version 3.3.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-3-1-may-23rd-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

## Notecard Configuration

With the hardware assembled and firmware updated, it's time to configure the Notecard. First, connect the Notecarrier to your development machine with the USB A to micro USB cable. Using the in-browser terminal on [dev.blues.io](https://dev.blues.io/), connect to the Notecard and enter this command:

### [`hub.set`](https://dev.blues.io/reference/notecard-api/hub-requests/#hub-set)

```json
{ "req": "hub.set", "product": "com.your-company:your-product-name", "body":{"app":"nf21"} }
```

Make sure to replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).

### [`card.aux`](https://dev.blues.io/reference/notecard-api/card-requests/#card-aux)

```json
{ "req": "card.aux", "mode": "gpio", "usage": ["input-pullup", "", "", ""], "sync": true, "file": "switch.qo" }
```

This command configures the Notecard's AUX pins, which you can read more about in our documentation [here](https://dev.blues.io/notecard/notecard-walkthrough/advanced-notecard-configuration/#using-aux-gpio-mode). The `usage` parameter accepts an array of 4 values, one for each AUX pin. The first element corresponds to AUX1, the next to AUX2, and so on. Here, we are configuring AUX1 to act as an input with a pull-up. Thus, when the switch is open, AUX1 will be driven high. When the switch is closed, it'll be driven low. The `sync` parameter will cause the Notecard to sync a note to Notehub whenever the input on AUX1 changes (either low to high or high to low). The `file` parameter instructs the Notecard to sync these updates to a Notefile called switch.qo.

### Notecard CLI

You can also use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```sh
notecard -setup config.json
```

Again, you'll need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).

## Testing

When using the black and brown wires of the float switch, the switch closes when it floats up to a certain level (really, when the float passes a certain angle relative to a fixed point, see [this helpful video from the switch manufacturer](https://www.youtube.com/watch?v=TKwO1jg0erk&t=26s)). We can simulate this by simply flipping the float switch 180 degrees. When you do this, you should hear a steel ball inside the float switch slide into a new position. The switch is now closed, and you should see a switch.qo note come into your project's Events tab on Notehub like this:

```json
{
  "power": true,
  "state": [
    { "low": true },
    {},
    {},
    {}
  ]
}
```

This note indicates that AUX1, the first element of the `state` array, has been driven low, since the switch has closed and connected AUX1 to ground. If we flip the float switch again, we should see a note like this:

```json
{
  "power": true,
  "state": [
    { "high": true },
    {},
    {},
    {}
  ]
}
```

The switch was opened, and AUX1 is now reading high from its pull-up.

## Going Further

- In a real sump, the float switch will change orientation and close/open with rising and falling water level. When the water rises to a certain level (the exact level is dependent on how much slack you've given the float), you'll get the first kind of note, indicating low voltage on AUX1. When the water falls back to a certain level, you'll get the second kind of note, indicating high voltage on AUX1. You can then act on this signal as appropriate for your use case.
- In the real world, you'd want your alert system to be resilient to power outages, so you'll want to hook up a [LiPo battery with a JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery) to the JST port labeled LIPO on the Notecarrier. 
- If you want to wire up more float switches to the same Notecard, you can easily do so by using the additional AUX pins. This could be useful in a particularly deep sump/vessel and/or when you want more granularity in terms of knowing the current fluid level.
