# Refrigerator Monitor

Monitor refrigerator temperature, humidity, and power using a Notecard and a BME280 sensor.

- [Refrigerator Monitor](#refrigerator-monitor)
  - [You Will Need](#you-will-need)
  - [Hardware Setup](#hardware-setup)
  - [Cloud Setup](#cloud-setup)
  - [Notecard Setup](#notecard-setup)
    - [Firmware](#firmware)
    - [Configure Notehub Project and Connection Mode](#configure-notehub-project-and-connection-mode)
    - [Configure Periodic Environmental Measurements](#configure-periodic-environmental-measurements)
    - [Configure Power Outage Alerts](#configure-power-outage-alerts)
  - [System Test](#system-test)
    - [Setup](#setup)
    - [Viewing Environmental Data](#viewing-environmental-data)
    - [Simulating a Power Outage](#simulating-a-power-outage)
  - [Blues Wireless Community](#blues-wireless-community)

## You Will Need

* AC to USB adapter (power brick)
* USB A to micro USB cable
* [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-A](https://shop.blues.io/collections/notecarrier/products/carr-al)
* [Notecard](https://blues.io/products/notecard/)
* [SparkFun Atmospheric Sensor Breakout - BME280 (Qwiic)](https://www.sparkfun.com/products/15440)
* [Qwiic Cable](https://www.sparkfun.com/products/14426)

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/).
2. Keep the Notecard connected to the computer with the in-browser terminal active until you have completed [Notecard Setup](#notecard-setup).
3. Plug one end of the Qwiic cable into one of the Notecarrier Qwiic ports and the other end into one of the Qwiic ports on the BME280 breakout board.
4. Connect the Notecarrier to your PC via the micro USB cable.
5. (OPTIONAL) The BME280 breakout has an LED to indicate that its powered on. This draws a significant amount of current, especially compared to the sensor itself. To maximize battery life, you can disconnect this LED by [cutting the JP1 jumper trace](https://learn.sparkfun.com/tutorials/how-to-work-with-jumper-pads-and-pcb-traces/cutting-a-trace-between-jumper-pads) (see [the schematic](https://cdn.sparkfun.com/assets/0/9/6/b/4/Qwiic_BME280_Schematic_attempt2.pdf)) on the back of the board with a hobby knife.

## Cloud Setup

Sign up for a free account on [Notehub.io](https://notehub.io) and create a new project.

## Notecard Setup

In these steps, you will use the in-browser terminal to configure your Notecard to report environmental data and power outage events to your Notehub project.

### Firmware

The Notecard should use [firmware version 3.5.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-1-october-7th-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

### Configure Notehub Project and Connection Mode

Set the ProductUID for the Notecard by pasting the command below into the in-browser terminal. Make sure to replace `com.your-company:your-product-name` with the ProductUID from your Notehub project, which can be found below your project's name in the dashboard at https://notehub.io. Also, replace the placeholder serial number (`"sn:" "fridge-location"`) with the location of your fridge (e.g. "kitchen").

```json
{ "req": "hub.set", "mode": "periodic", "outbound": 3, "product": "com.your-company:your-product-name", "sn": "fridge-location" }
```

This app uses `periodic` mode to minimize power use so that the solution can remain online for longer when mains power fails. `"outbound": 3` is the max wait time, in minutes, to sync outbound data from the Notecard. For more details, see [our Essential Requests documentation for `hub.set`](https://dev.blues.io/notecard/notecard-walkthrough/essential-requests/#notehub-configuration).

### Configure Periodic Environmental Measurements

Paste this command into the in-browser terminal:

```json
{ "req": "card.aux", "mode":"track" }
```

Turning on track mode will augment the environmental data with pressure and humidity readings in addition to temperature.

Next, paste this command into the in-browser terminal:

```json
{ "req": "card.temp", "seconds": 180 }
```

This will cause the Notecard to push a note to the `_temp.qo` Notefile with environmental data every 3 minutes. You can modify this interval to suit your application's needs. Note that this matches the 3 minute settings we specified for `outbound` in the `hub.set` request above.

### Configure Power Outage Alerts

Paste this command into the in-browser terminal:

```json
{ "req": "card.voltage", "mode": "lipo", "usb": true, "alert": true, "sync": true }
```

This instructs the Notecard to assume LiPo battery characteristics, monitor USB power and send an alert immediately when a power outage occurs and when power is restored. For more details, see the [Notecard API reference](https://dev.blues.io/reference/notecard-api/card-requests/#card-voltage) and the `Enable USB Power Alerting` example.

### Notecard CLI

If you want to issue all of the above requests in one shot, you can do so with the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) and the config.json configuration script. You will need to change the ProductUID in the `hub.set` command to match your ProductUID. Once you've done that, you can configure the Notecard with:

```sh
notecard -setup config.json
```

## System Test

Ideally, to test this application, you'd put your Notecarrier, Notecard, and sensor into a refrigerator. However, if you're just prototyping this at home, it's unlikely you're going to have access to mains power inside your fridge. So, feel free to follow these steps anywhere near an outlet where you can connect to power. You'll verify that you're receiving temperature and humidity data on Notehub, and you'll also test the backup battery functionality.

### Setup

1. Insert the power brick into a suitable electrical outlet and provide power to the Notecarrier via the micro USB cable.
2. Plug the JST connector from the LiPo battery into the port labeled LIPO on the Notecarrier.
3. Wait for the Notecard to connect to the cloud. When connected, there will be a new session event in Notehub. (look for recent `_session.qo` events.)
4. Wait for the battery to fully charge.

### Viewing Environmental Data

Open your project on Notehub and navigate to the Events page using the menu on the left hand side. You should see periodic `_temp.qo` notes like this:

```json
{"count":13,"humidity":32.806084,"pressure":100820.59,"temperature":21.802795,"voltage":5.1367188}
```

### Simulating a Power Outage

1. Remove the power brick from the electrical outlet to simulate a power outage. The Notecard is now being powered by the battery.
2. After a short time, you will see a  `_health.qo` event in Notehub with the body

```json
{"text":"USB power OFF {usb-disabled}"}
```

This event indicates that the Notecard is no longer powered from the power brick.

3. Reinsert the power brick into a suitable electrical outlet to simulate power being restored.
4. After a short time, you will see a  `_health.qo` event in Notehub with the body

```json
{"text":"USB power ON {usb-enabled}"}
```

This event indicates that the Notecard is again powered by the power brick.

## Blues Wireless Community

We’d love to hear about you and your project on the [Blues Wireless Community Forum](https://discuss.blues.io/)!
