# Zephyr Firmware

This is a Zephyr-based implementation of the temperature and humidity monitor project's firmware.

## Setup

### Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

### Hardware Setup

1. Assemble Notecard and Notecarrier as described [here](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/).
2. Plug the Swan into the Notecarrier, aligning the Swan's male headers with the Notecarrier's female headers.
3. (OPTIONAL) The BME280 breakout has an LED to indicate that its powered on. This draws a significant amount of current, especially compared to the sensor itself. To maximize battery life, you can disconnect this LED by [cutting the JP1 jumper trace](https://learn.sparkfun.com/tutorials/how-to-work-with-jumper-pads-and-pcb-traces/cutting-a-trace-between-jumper-pads) (see [the schematic](https://cdn.sparkfun.com/assets/0/9/6/b/4/Qwiic_BME280_Schematic_attempt2.pdf)) on the back of the board with a hobby knife.
4. Plug one end of the Qwiic cable into one of the Notecarrier Qwiic ports and the other end into one of the Qwiic ports on the BME280 breakout board.
5. Plug the LiPo battery's JST cable into the Notecarrier port labeled "LIPO".
6. Connect the micro USB cable from your development PC to the Swan.

### Notecard Firmware Setup

The Notecard should use [firmware version 3.5.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v3-5-1-october-7th-2022) or higher. The simplest way to update firmware is to do an [over-the-air update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub).

### Swan Firmware Setup

From the command line, you'll need to pull in the [note-c](https://github.com/blues/note-c) and [notecard-env-var-manager](https://github.com/blues/notecard-env-var-manager) submodules that the firmware depends on:

```sh
$ git submodule update --init 18-temperature-and-humidity-monitor/firmware/zephyr/src/note-c
$ git submodule update --init 18-temperature-and-humidity-monitor/firmware/zephyr/src/notecard_env_var_manager
```

To build, flash, and debug the firmware, you will need

* [Visual Studio Code (VS Code)](https://code.visualstudio.com/).
* [Docker and the VS Code Dev Containers extension](https://code.visualstudio.com/docs/devcontainers/containers). The Dev Containers documentation will take you through the process of installing both Docker and the extension for VS Code.

These instructions will defer parts of the build process to the [Blues Zephyr SDK documentation](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk) (the "Zephyr SDK docs"). Though these instructions are for the [note-zephyr repo](https://github.com/blues/note-zephyr), the same patterns for building the code are used here.

1. Start VS Code and select File > Open Folder and pick this folder (`18-temperature-and-humidity-monitor/firmware/zephyr`).
1. Follow the instructions for your OS in the [Zephyr SDK docs' "Building the Dev Container" section](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk/#building-the-dev-container).
1. Edit `src/main.c` to associate the firmware with your Notehub project. Uncomment `// #define PRODUCT_UID "com.your-company:your-product-name"` and replace `com.your-company:your-product-name` with [your project's ProductUID](https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid).
1. Follow the [Zephyr SDK docs' "Building and Running" section](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk/#building-and-running).

Now, the code should be running on the Swan. If you want to look at the serial logs or debug the code, check out the [Zephyr SDK docs' "Debugging" section](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk/#debugging).

## Operation

At this point, the Swan firmware should be running and you should start to see events show up on Notehub under your project's "Events" view. There are 3 important Notefiles.

### `data.qo`

By default, the Swan will publish temperature and humidity data from the BME280 to this file every 2 minutes. For example:

```json
{ 
    "humidity": 30.705078125,
    "temperature": 24.340000152587891
}
```

You can change this interval by setting the [environment variable](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/) `monitor_interval`. The unit for `monitor_interval` is seconds. The firmware reads from the BME280 every 30 seconds, so it's not particularly useful to set `monitor_interval` lower than 30 seconds. Note: If you modify any environment variables, wait at least a minute for the changes to propagate to the Swan.

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

## Additional Resources

Though we only support using the VS Code + Dev Containers workflow described here, you can also install Zephyr and its dependencies locally. You can build, flash, and debug code in your native environment using Zephyr's [`west` tool](https://docs.zephyrproject.org/latest/develop/west/index.html). See [Zephyr's Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) for more information.

## Developer Notes

The Notecard hooks in `src/notecard.c` come from [note-zephyr](https://github.com/blues/note-zephyr).