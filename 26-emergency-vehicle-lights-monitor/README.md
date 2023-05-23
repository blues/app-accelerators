# Emergency Vehicle Lights Monitor

Monitor the activity of an emergency vehicle in real time.

## Solution Overview

This solution uses the Notecard's `card.aux` functionality in GPIO mode to sense up to 4 digital inputs that indicate when the vehicle is on, and when the emergency lights and/or siren are on.

- [Emergency Vehicle Lights Monitor](#emergency-vehicle-lights-monitor)
  - [Solution Overview](#solution-overview)
  - [You Will Need](#you-will-need)
  - [Notehub Setup](#notehub-setup)
  - [Hardware Setup](#hardware-setup)
  - [Testing](#testing)
    - [`hub.set`](#hubset)
    - [`card.voltage`](#cardvoltage)
    - [`card.aux` GPIO mode](#cardaux-gpio-mode)
    - [Optional Location Tracking](#optional-location-tracking)
      - [`card.location.mode`](#cardlocationmode)
      - [`card.location.track`](#cardlocationtrack)
    - [Notecard CLI](#notecard-cli)

## You Will Need

* USB A to micro USB cable
* [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-B](https://shop.blues.io/products/carr-b)
* [Notecard](https://blues.io/products/notecard/)

To demonstrate the app, we created a demo based on a toy ambulance with 4 momentary buttons that are used to simulate a real emergency vehicle. See [Build.md](Build.md) for the steps to build this.

In a real-world scenario, you would tap into the vehicle's dash power and switches for emergency lights and siren. As the voltage used by the vehicle is typically 12v or higher, you will need to use an optocoupler per signal to isolate the voltages used by the microelectronics and the vehicle.

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-b).
2. Plug the LiPo battery's JST cable into the Notecarrier port labeled "LIPO".
3. Connect the micro USB cable from your development PC to the port on the Notecarrier.

## Testing

With the Notecarrier connected to your development PC, navigate to https://dev.blues.io open the in-browser terminal, and connect to the Notecard. Then enter the JSON commands below into the terminal.

### [`hub.set`](https://dev.blues.io/reference/notecard-api/hub-requests/#hub-set)

```json
{ "req": "hub.set", "product" : "com.your-company:your-product-name", "sn": "a-serial-number", "mode": "periodic", "outbound": 30, "body": {"app":"nf26"} }
```

This command sets the Notecard's ProductUID and serial number. Make sure to replace "com.your-company:your-product-name" with your ProductUID from [Notehub Setup](#notehub-setup). Also, replace "a-serial-number" with a relevant identifier for the object that the Notecard will be attached to (e.g. "emergency vehicle 15", if this were a real world scenario). Setting the `mode` to `periodic` and `outbound` to 30 makes it so the Notecard will periodically sync notes to Notehub every 30 minutes.

### [`card.voltage`](https://dev.blues.io/reference/notecard-api/card-requests/#card-voltage)

```json
{ "req": "card.voltage", "mode": "lipo" }
```

This command instructs the Notecard to assume LiPo battery characteristics to better conserve power.

### [`card.aux` GPIO mode](https://dev.blues.io/reference/notecard-api/card-requests/#card-aux)

The `card.aux` Notecard request configures how the notecard uses the auxiliary pins. For this app, use pins `AUX1` through `AUX4` as digital inputs to sense vehicle, emergency lights and siren activity, or when buttons are pushed in the example build.

```json
{
  "req": "card.aux",
  "sync": true,
  "mode": "gpio",
  "usage": ["count-pulldown", "count-pulldown", "count-pulldown", "count-pulldown"]
}
```

### Optional Location Tracking

This section is optional. Send these requests to the Notecard if you wish to enable GPS location tracking.

#### [`card.location.mode`](https://dev.blues.io/reference/notecard-api/card-requests/#card-location-mode)

```json
{ "req": "card.location.mode", "mode": "periodic", "seconds": 60 }
```

This command tells the Notecard to sample its GPS location every 60 seconds, so long as motion has been detected (i.e. it won't be sampled if the Notecard hasn't moved).

#### [`card.location.track`](https://dev.blues.io/reference/notecard-api/card-requests/#card-location-track)

```json
{ "req": "card.location.track", "start": true, "file": "locations.qo" }
```

This request configures the Notecard to generate periodic notes to track the Notecard's location. The location sampled every 60 seconds is added to the Notefile `locations.qo`.

With those commands entered, unplug the Notecarrier's USB connection so that the Notecard's running on LiPo power. In lieu of affixing the hardware to an actual piece of cargo (e.g. a shipping container), simply move it to a different area in your home/office. If you're able to go outside, that's even better, as the GPS signal should be stronger outdoors. Back on your Notehub project's Events page, you should see a note come in to locations.qo. Double click that note to open up a detailed view and then click the JSON tab. The `body` of the note will look something like this:

```json
{
    "bearing": 117.899284,
    "distance": 36.43759,
    "jcount": 2,
    "journey": 1675032377,
    "motion": 4,
    "seconds": 88,
    "temperature": 23.5,
    "time": 1675032598,
    "usb": true,
    "velocity": 0.41406354,
    "voltage": 5.1054688
}
```

If you're interested in the precise latitude and longitude of the Notecard, you can examine the `best_lat` and `best_lon` fields (these are in the higher-level JSON object, not the `body`).

Note that you won't see additional `locations.qo` notes until the Notecard moves again.


### Notecard CLI

You can also use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```sh
notecard -setup config.json
```

Again, you'll need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup) as well as the serial number.
