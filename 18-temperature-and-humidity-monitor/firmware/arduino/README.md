# Temperature and Humidity Monitor

Monitor temperature and humidity and send alerts using a Notecard and a BME280 sensor.

- [Temperature and Humidity Monitor](#temperature-and-humidity-monitor)
  - [You Will Need](#you-will-need)
    - [Hardware](#hardware)
    - [Software](#software)
  - [Notehub Setup](#notehub-setup)
  - [Hardware Setup](#hardware-setup)
  - [Notecard Firmware Setup](#notecard-firmware-setup)
  - [Swan Firmware Setup](#swan-firmware-setup)
  - [Operation](#operation)
    - [data.qo](#dataqo)
    - [alarm.qo](#alarmqo)
    - [_health.qo](#_healthqo)
    - [Simulating a Power Outage](#simulating-a-power-outage)

## You Will Need

### Hardware

* AC to USB adapter (power brick)
* USB A to micro USB cable
* [LiPo battery with JST cable](https://shop.blues.io/products/5-000-mah-lipo-battery)
* [Notecarrier-F](https://shop.blues.io/collections/notecarrier/products/notecarrier-f)
* [Notecard](https://blues.io/products/notecard/)
* [Swan MCU](https://blues.io/products/swan/)
* [SparkFun Atmospheric Sensor Breakout - BME280 (Qwiic)](https://www.sparkfun.com/products/15440)
* [Qwiic Cable](https://www.sparkfun.com/products/14426)

### Software

* [Visual Studio Code](https://code.visualstudio.com/)
* [PlatformIO](https://platformio.org/)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/).
2. Plug the Swan into the Notecarrier, aligning the Swan's male headers with the Notecarrier's female headers.
3. (OPTIONAL) The BME280 breakout has an LED to indicate that its powered on. This draws a significant amount of current, especially compared to the sensor itself. To maximize battery life, you can disconnect this LED by [cutting the JP1 jumper trace](https://learn.sparkfun.com/tutorials/how-to-work-with-jumper-pads-and-pcb-traces/cutting-a-trace-between-jumper-pads) (see [the schematic](https://cdn.sparkfun.com/assets/0/9/6/b/4/Qwiic_BME280_Schematic_attempt2.pdf)) on the back of the board with a hobby knife.
4. Plug one end of the Qwiic cable into one of the Notecarrier Qwiic ports and the other end into one of the Qwiic ports on the BME280 breakout board.
5. Plug the LiPo battery's JST cable into the Notecarrier port labeled "LIPO".
6. Connect the micro USB cable from your development PC to the Swan.

## Notecard Firmware Setup

The Notecard should use [firmware version 3.5.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-1-october-7th-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

## Swan Firmware Setup

1. Open Visual Studio Code.
2. Click the PlatformIO icon on the left hand side and open this project's `firmware` folder with Quick Access > PIO Home > Open > Open Project.
3. Open the file `src/main.cpp`. Uncomment this line

```c
#define PRODUCT_UID "com.your-company:your-product-name"
```

and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).

4. Click the PlatformIO icon on the left hand side again and click "Build" under Project Tasks > bw_swan_r5 > General. You should see "SUCCESS" in the terminal output pane. The firmware image is now ready to be flashed to the Swan.
5. Press and hold the button labeled "BOOT" on the Swan, and, while holding BOOT, press and release the button labeled "RST", and finally release the BOOT button. The Swan is now ready to be flashed.
6. Click "Upload" under Project Tasks > bw_swan_r5 > General. You should see "SUCCESS" in the terminal output pane.

## Operation

At this point, the Swan firmware should be running and you should start to see events show up on Notehub under your project's "Events" view. There are 3 important Notefiles.

### `data.qo`

By default, the Swan will publish temperature and humidity data from the BME280 to this file every 3 minutes. For example:

```json
{ 
    "humidity": 30.705078125,
    "temperature": 24.340000152587891
}
```

You can change this interval by setting the environment variable `monitor_interval`. The unit for `monitor_interval` is seconds. The firmware reads from the BME280 every minute, so it's not particularly useful to set `monitor_interval` lower than 60 seconds. Note: If you modify any environment variables, wait at least 30 seconds for the changes to propagate to the Swan.

### `alarm.qo`

By default, the firmware will send an alarm if the temperature falls outside the range `[5, 33]`C or the humidity falls outside the range `[20, 70]`% (relative humidity). For example:


```json
{
    "humidity":
    {
        "status": "high",
        "value": 31.19921875
    },
    "temperature":
    {
        "status": "ok",
        "value": 23.20999908447266
    }
}
```

The `status` field indicates the reason for the alarm. `low` means the value is below the minimum, `high` means the value is above the maximum, and `ok` means the value is in range. You should only ever see an `alarm.qo` note if at least one of the values is not `ok`.

You can use these environment variables to change the range:

* `temperature_threshold_min`: Lower bound of temperature range.
* `temperature_threshold_max`: Upper bound of temperature range.
* `humidity_threshold_min`: Lower bound of humidity range.
* `humidity_threshold_max`: Upper bound of humidity range.

### `_health.qo`

Notes in this file indicate when USB power is lost or restored. When USB power is lost, you should see a note like this:

```json
{
    "text": "USB power OFF {usb-disabled}"
}
```

And when USB power is restored, you should see:

```json
{
    "text": "USB power ON {usb-enabled}"
}
```

### Simulating a Power Outage

The LiPo battery acts as backup power in the event that USB power is lost. Once the battery is charged, you can test this functionality by doing the following:

1. Insert the power brick into a suitable electrical outlet and provide power to the Notecarrier via the micro USB cable.
2. Remove the power brick from the electrical outlet to simulate a power outage. The Notecard is now being powered by the battery.
3. After a short time, you will see a "USB power OFF" note in `_health.qo` on Notehub as mentioned in the previous section.
4. Reinsert the power brick into a suitable electrical outlet to simulate power being restored.
5. After a short time, you will see a "USB power ON" note in `_health.qo` on Notehub as mentioned in the previous section.
