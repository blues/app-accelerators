# Roadway Impact Detection

Detect when a vehicle or other object collides with another object on a roadway.

## You Will Need

* USB A to micro USB cable
* [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-A](https://shop.blues.io/products/carr-al)
* [Notecard](https://blues.io/products/notecard/)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a).
2. Plug the LiPo battery's JST cable into the Notecarrier port labeled "LIPO".
3. Connect the micro USB cable from your development PC to the port on the Notecarrier.

## Notecard Firmware

The Notecard should use [firmware version 3.5.2](https://dev.blues.io/notecard/notecard-firmware-releases/#v3-5-2-november-2-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-walkthrough/updating-notecard-firmware#ota-dfu-with-notehub).

## Testing

With the Notecarrier connected to your development PC, navigate to https://dev.blues.io open the in-browser terminal, and connect to the Notecard. Then enter the JSON commands below into the terminal.

### hub.set

[API reference](https://dev.blues.io/api-reference/notecard-api/hub-requests/#hub-set)

```json
{ "req": "hub.set", "product" : "com.your-company:your-product-name", "sn": "a-serial-number", "body":{"app":"nf22"} }
```

This command sets the Notecard's ProductUID and serial number. Make sure to replace "com.your-company:your-product-name" with your ProductUID from [Notehub Setup](#notehub-setup). Also, replace "a-serial-number" with a relevant identifier for the object that the Notecard will be attached to (e.g. "mile marker 100", if this were a real world scenario).

### card.voltage

[API reference](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-voltage)

```json
{ "req": "card.voltage", "mode": "lipo" }
```

This command optimizes battery usage, assuming the power source is a LiPo battery. You can learn more about this command in the [Low Power Design documentation](https://dev.blues.io/notecard/notecard-walkthrough/low-power-design/#customizing-voltage-variable-behaviors
).

### card.motion.mode

[API reference](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-motion-mode)

```json
{ "req": "card.motion.mode", "sensitivity": 1 }
```

This command configures the Notecard's accelerometer's sensitivity (an integer, ranging from -1 to 5). The lower the sensitivity value, the less sensitive the accelerometer will be. We only want to detect drastic movements due to a collision, so we are setting the value low. Setting sensitivity to -1 is probably a better choice for the real-world application, but a sensitivity of 1 is better for local testing.

### card.motion.sync

[API reference](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-motion-sync)

```json
{ "req": "card.motion.sync", "start": true }
```

This command configures the Notecard to send a note to Notehub every time it detects motion. The `"start": true` parameter tells the Notecard to begin motion tracking.

With those commands entered, unplug the Notecarrier's USB connection so that the Notecard's running on LiPo power. With the Notecarrier resting face up on your work surface, quickly push the Notecarrier several inches to simulate a collision. Go to your project on Notehub, open the Events tab, and soon enough you should see an event like this in `_motion.qo` for the push:

```json
{
  "alert": true,
  "motion": 3,
  "movements": "1",
  "orientation": "face-up",
  "temperature": 18.5,
  "tilt": 1,
  "voltage": 5.1314402519059204
}
```

Tip: If you don't see the Notecard's green LED blinking shortly after pushing the Notecarrier, you likely didn't accelerate fast enough to exceed the sensitivity threshold we set earlier. Try accelerating faster.

Let's break down the fields in the response:

- `alert`: This will always be true when we get a `_motion.qo` note after issuing `card.motion.sync`.
- `motion`: The number of motion detections since the last note.
- `movements`: A string of base-36 characters, where each character represents the number of movements in each bucket during the sample duration. Each character will be a digit 0-9, A-Z to indicate a count of 10-35, or * to indicate a count greater than 35.
- `orientation`: The orientation of the Notecard in space.
- `temperature`: Temperature reading from the Notecard.
- `tilt`: The number of orientation changes since the last motion note.
- `voltage`: The current voltage.

### Notecard CLI

You can also use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```sh
notecard -setup config.json
```

Again, you'll need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).
