# Cargo Tracker

Monitor an asset's location via GPS and detect when it enters or leaves a geofenced region.

- [Cargo Tracker](#cargo-tracker)
  - [You Will Need](#you-will-need)
  - [Notehub Setup](#notehub-setup)
  - [Hardware Setup](#hardware-setup)
  - [Testing](#testing)
    - [`hub.set`](#hubset)
    - [`card.voltage`](#cardvoltage)
    - [`card.location.mode`](#cardlocationmode)
    - [`card.location.track`](#cardlocationtrack)
    - [Notecard CLI](#notecard-cli)

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

## Testing

With the Notecarrier connected to your development PC, navigate to https://dev.blues.io open the in-browser terminal, and connect to the Notecard. Then enter the JSON commands below into the terminal.

### [`hub.set`](https://dev.blues.io/reference/notecard-api/hub-requests/#hub-set)

```json
{ "req": "hub.set", "product" : "com.your-company:your-product-name", "sn": "a-serial-number", "mode": "periodic", "outbound": 30, "body":{"app":"nf14"} }
```

This command sets the Notecard's ProductUID and serial number. Make sure to replace "com.your-company:your-product-name" with your ProductUID from [Notehub Setup](#notehub-setup). Also, replace "a-serial-number" with a relevant identifier for the object that the Notecard will be attached to (e.g. "cow 257", if this were a real world scenario). Setting the `mode` to `periodic` and `outbound` to 30 makes it so the Notecard will periodically sync notes to Notehub every 30 minutes.

### [`card.voltage`](https://dev.blues.io/reference/notecard-api/card-requests/#card-voltage)

```json
{ "req": "card.voltage", "mode": "lipo" }
```

This command instructs the Notecard to assume LiPo battery characteristics to better conserve power.

### [`card.location.mode`](https://dev.blues.io/reference/notecard-api/card-requests/#card-location-mode)

```json
{ "req": "card.location.mode", "mode": "periodic", "seconds": 180, "lat": 34.0782, "lon": -118.2606, "max": 500 }
```

This command tells the Notecard to sample its GPS location every 180 seconds, so long as motion has been detected (i.e. it won't be sampled if the Notecard hasn't moved). It also sets up a circular [geofence](https://en.wikipedia.org/wiki/Geo-fence) centered at latitude (`lat`) 34.0782 and longitude (`lon`) -118.2606 (Los Angeles, CA -- adjust this according to your location!). The `max` parameter controls the radius of the geofence. In this case, it's 500 meters. Whenever the Notecard's location transitions from inside this region to outside it, or vice-versa, the Notecard will immediately sync a note to Notehub. Note that this sync is _immediate_, meaning the outbound wait time set with `hub.set` is ignored.

### [`card.location.track`](https://dev.blues.io/reference/notecard-api/card-requests/#card-location-track)

```json
{ "req": "card.location.track", "start": true, "file": "locations.qo" }
```

Aside from transitions out of and into the geofence you just configured, you'll also want generate periodic notes to track the Notecard's location. This command makes it so the location sampled every 180 seconds gets added to the Notefile locations.qo.

With those commands entered, unplug the Notecarrier's USB connection so that the Notecard's running on LiPo power. In lieu of affixing the hardware to an actual piece of cargo (e.g. a shipping container), simply move it to a different area in your home/office. If you're able to go outside, that's even better, as the GPS signal should be stronger outdoors. Back on your Notehub project's Events page, you should see a note come in to locations.qo. Double click that note to open up a detailed view and then click the JSON tab. The `body` of the note will look something like this:

```json
{
    "bearing": 117.899284,
    "distance": 36.43759,
    "inside_fence": true,
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

Here, the `inside_fence` field is set to true. This means the Notecard is inside the geofence. If the Notecard is outside the geofence, instead of having `inside_fence` as false, it won't show up at all. This is an optimization of the Notecard firmware where fields that are 0 (or false) aren't transmitted and can be assumed to be 0. You can read more about that [here](https://dev.blues.io/notecard/notecard-walkthrough/json-fundamentals/#how-the-notecard-works-with-json). If you're interested in the precise latitude and longitude of the Notecard, you can examine the `best_lat` and `best_lon` fields (these are in the higher-level JSON object, not the `body`).

Note that you won't see additional locations.qo notes until the Notecard moves again.

### Notecard CLI

You can also use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```sh
notecard -setup config.json
```

Again, you'll need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup) as well as the serial number.
