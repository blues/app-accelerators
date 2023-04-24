# Construction Equipment Power and Location Tracker

Monitor the power state of a piece of construction equipment as well as its location, sending alerts when it leaves/enters a particular circular geofence and when main power is lost/restored.

- [Construction Equipment Power and Location Tracker](#construction-equipment-power-and-location-tracker)
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
* [Notecarrier-A](https://shop.blues.io/products/carr-al)
* [Notecard](https://blues.io/products/notecard/)
* [LiPo battery with JST cable](https://shop.blues.io/collections/accessories/products/5-000-mah-lipo-battery)
* Portable USB battery pack/charger OR USB car lighter adapter

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
{ "req": "hub.set", "product" : "com.your-company:your-product-name", "sn": "a-serial-number", "mode": "periodic", "outbound": 30, "body":{"app":"nf25"} }
```

This command sets the Notecard's ProductUID and serial number. Make sure to replace "com.your-company:your-product-name" with your ProductUID from [Notehub Setup](#notehub-setup). Also, replace "a-serial-number" with a relevant identifier for the equipment that the Notecard will be attached to (e.g. "excavator 5", if this were a real world scenario). Setting the `mode` to `periodic` and `outbound` to 30 makes it so the Notecard will periodically sync notes to Notehub every 30 minutes.

### [`card.voltage`](https://dev.blues.io/reference/notecard-api/card-requests/#card-voltage)

```json
{ "req": "card.voltage", "usb": true, "alert": true, "sync": true }
```

This command instructs the Notecard to monitor USB power and send an alert immediately when a power outage occurs and when power is restored. For more details, see the [Notecard API reference](https://dev.blues.io/reference/notecard-api/card-requests/#card-voltage) and the `Enable USB Power Alerting` example.

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

With those commands entered, unplug the Notecarrier from your computer and either

1. Plug the USB cable into the USB battery pack OR
2. Plug the USB cable into the car lighter adapter and plug the adapter into your car's lighter port.

If you chose option 1, pack up the hardware and go for a walk, preferably outside where the GPS signal will be stronger. If you chose option 2, start the car and go for a drive. Once you've returned from your journey, check your Notehub project's Events page, and you should see notes in locations.qo. Double click one to open up a detailed view and then click the JSON tab. The `body` of the note will look something like this:

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

Here, the `inside_fence` field is set to true. This means the Notecard is inside the geofence. If the Notecard is outside the geofence, instead of having `inside_fence` as false, it won't show up at all. This is an optimization of the Notecard firmware where fields that are 0 (or false) aren't transmitted and can be assumed to be 0. You can read more about that [here](https://dev.blues.io/notecard/notecard-walkthrough/json-fundamentals/#how-the-notecard-works-with-json). If you're interested in the precise latitude and longitude of the Notecard, you can examine the `best_lat` and `best_lon` fields (these are in the higher-level JSON object, not the `body`). Note that you won't see additional locations.qo notes until the Notecard moves again.

If you chose option 1, unplug the battery pack from the Notecarrier to cut power. If you chose option 2, power should have been cut when you turned the car off. Now, the Notecard should be powered by the battery and will send a note to Notehub in \_health.qo indicating that main USB power was lost. Here's an example:

```json
{"text":"USB power OFF {usb-disabled}"}
```

If you plug the Notecarrier back into the battery pack or turn your car on again, you should see another \_health.qo note come in like this:

```json
{"text":"USB power ON {usb-enabled}"}
```

### Notecard CLI

You can also use the config.json file in this directory along with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) to issue the above commands in one fell swoop:

```sh
notecard -setup config.json
```

Again, you'll need to edit config.json and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup) as well as the serial number.
