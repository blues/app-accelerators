# Sign and Door Tilt Sensor

Detect when a sign is knocked over or a door is opened/closed.

## You Will Need

* USB A to micro USB cable
* [LiPo battery with JST cable](https://shop.blues.com/products/5-000-mah-lipo-battery)
* [Notecarrier-A](https://shop.blues.com/products/carr-al)
* [Notecard](https://blues.com/products/notecard/)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a).
2. Plug the LiPo battery's JST cable into the Notecarrier port labeled "LIPO".
3. Connect the micro USB cable from your development PC to the port on the Notecarrier.

## Notecard Firmware

The Notecard should use [firmware version 3.5.2](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-2-november-2nd-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

## Testing

With the Notecarrier connected to your development PC, navigate to https://dev.blues.io open the in-browser terminal, and connect to the Notecard. Then, enter this command in the terminal:

```json
{ "req": "hub.set", "product" : "com.your-company:your-product-name", "sn": "a-serial-number", "body": {"app":"nf19"} }
```

Make sure to replace "com.your-company:your-product-name" with your ProductUID from [Notehub Setup](#notehub-setup). Also, replace "a-serial-number" with the serial number of other relevant identifier for this particular sign/door (e.g. "refrigerator door").

Next, enter this command to optimize battery, assuming our power source is the LiPo battery:

```json
{ "req": "card.voltage", "mode": "lipo" }
```

You can read more about this command in the [Low Power Design documentation](https://dev.blues.io/notecard/notecard-walkthrough/low-power-design/#customizing-voltage-variable-behaviors
).

Finally, enter this command to have the Notecard send a note to Notehub every time it detects motion:

```json
{ "req": "card.motion.sync", "start": true, "threshold": 0 }
```

The `"start": true` parameter tells the Notecard to begin motion tracking. The `"threshold": 0` parameter makes it so the Notecard will sync motion event notes to Notehub only when its orientation changes. So, for example, if we've mounted our device to a sign and that sign gets bumped but doesn't tilt/tip over, no motion event will be synced. If the sign falls over, an event will be synced. You can read more about these parameters in [our card.motion.sync documentation](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-motion-sync).

With those commands entered, unplug the Notecarrier's USB connection so that the Notecard's running on LiPo power. Flip the Notecarrier over on your work surface. Go to your project on Notehub, open the Events tab, and soon enough you should see an event like this in `_motion.qo` for the flip:

```json
{
  "alert": true,
  "motion": 1,
  "movements": "1",
  "orientation": "face-down",
  "temperature": 17.375,
  "tilt": 1,
  "voltage": 5.1286890300781243
}
```

Let's break down the fields in the response:

- `alert`: This will always be true when we get a `_motion.qo` note after issuing `card.motion.sync`.
- `motion`: The number of motion detections since the last note.
- `movements`: A string of base-36 characters, where each character represents the number of movements in each bucket during the sample duration. Each character will be a digit 0-9, A-Z to indicate a count of 10-35, or * to indicate a count greater than 35.
- `orientation`: The orientation of the Notecard in space. In the above example, I flipped the Notecarrier over so that the Notecard was resting against my table, so the orientation is "face-down."
- `temperature`: Temperature reading from the Notecard.
- `tilt`: The number of orientation changes since the last motion note.
- `voltage`: The current voltage.

### Notecard CLI

You can also use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```sh
notecard -setup config.json
```

Again, you'll need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).
