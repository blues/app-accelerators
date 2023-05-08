# Cellular-Connected Electronic Kiosk

Download and run a kiosk application without an Internet connection, using a simple Python script.

- [Cellular-Connected Electronic Kiosk](#cellular-connected-electronic-kiosk)
  - [You Will Need](#you-will-need)
  - [Notehub Setup](#notehub-setup)
    - [Route](#route)
    - [Environment Variables](#environment-variables)
      - [`kiosk_content`](#kiosk-content)
      - [`kiosk_content_version`](#kiosk-content-version)
      - [`kiosk_download_time`](#kiosk-download-time)
      - [`kiosk_data`](#kiosk-data)
  - [microSd Card Setup](#microsd-card-setup)
  - [Hardware Setup](#hardware-setup)
    - [Notecard and Notecarrier](#notecard-and-notecarrier)
    - [Smarti Pi Case and Display](#smarti-pi-case-and-display)
    - [Keyboard and Mouse](#keyboard-and-mouse)
  - [Booting for the First Time](#booting-for-the-first-time)
  - [Notecard Firmware](#notecard-firmware)
  - [Kiosk Software](#software-setup)
    - [Dependencies](#dependencies)
    - [Running](#running)

## You Will Need

- [Raspberry Pi 4B](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/)
- [CanaKit 3.5A Raspberry Pi 4 Power Supply (USB-C)](https://www.amazon.com/CanaKit-Raspberry-Power-Supply-USB-C/dp/B07TYQRXTK/)
- microSD Card (8GB minimum) and a MicroSD to SD Memory Card Adapter (your development PC must have an SD card reader slot)
- [Notecard](https://shop.blues.io/collections/notecard/products/note-wbna-500)
- [Notecarrier Pi Hat](https://shop.blues.io/products/carr-pi)
- Micro USB cable
- [7" Touchscreen Display for Raspberry Pi](https://www.adafruit.com/product/2718)
- [Wireless Keyboard and Mouse Combo](https://www.adafruit.com/product/1738)
- [Smarti Pi Touch Pro for Raspberry Pi and Official 7" Display](https://www.adafruit.com/product/4951)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

### Route

Next, you need to create a [proxy route](https://dev.blues.io/notehub/notehub-walkthrough/#routing-data-with-notehub) in order to download the kiosk web app. The web app should be bundled in a .zip file and stored online somewhere (e.g. in an AWS S3 bucket). Unzipping the .zip file should produce, at minimum, a directory named `resources` with an HTML page to display named `index.htm`. Read [this guide](https://dev.blues.io/notecard/notecard-walkthrough/web-transactions/) to set up the route.

![Route config](images/route.png)

### Environment Variables

There are 4 [environment variables](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/) used in this project. You can set them by navigating to your Notehub project page > select your fleet name from under Fleets > Settings > Environment:

![Environment variables](images/env_vars.png)

#### `kiosk_content`

This is the name of the .zip file that will be downloaded via the proxy route. For example, if you configure your route with the URL https://my-kiosk.s3.amazonaws.com and the .zip file is downloadable via a GET request to https://my-kiosk.s3.amazonaws.com/kiosk.zip, you should set `kiosk_content` to `kiosk.zip`. Changing this variable will cause the Python script to download and use the file specified by the new value.

#### `kiosk_content_version`

This is a version number for the file specified by `kiosk_content`. If you update the content, you can increment this number to cause the Python script to download and use the new content.

#### `kiosk_download_time`

This variable specifies the hour of the day (using 24-hour time) when the Python script should check for updates to the `kiosk_content`. If, for example, `kiosk_download_time` is set to 9, the script will check for updates to the content at 9:00 AM local time. If the content file name or version has changed, the script will download and use the new content. If this variable is set to the special value `now`, any change to the content file name or version will cause an immediate download of the new content, instead of waiting for a specific hour of the day.

#### `kiosk_data`

This can be set to any valid JSON object. It will then be stored in the `resources` directory alongside `index.htm` in the file `data.js`. The contents of `data.js` looks like this:

```js
var data = <value of kiosk_data goes here>
````

For example, if you set `kiosk_data` to `{"message":"Hello world!"}`, `data.js` would look like this:

```js
var data = {"message":"Hello world!"}
````

Your web application can then access and use this data however you want. In this way, `kiosk_data` acts as a way of passing dynamic data to the web app without needing to modify the contents of the .zip file and re-download the whole thing.

## microSD Card Setup

Before installing the Raspberry Pi into the Smart Pi case, you need to install [Raspberry Pi OS](https://www.raspberrypi.com/software/) onto the microSD card by following the steps in [this official video](https://www.youtube.com/watch?v=ntaXWS8Lk34) from the Raspberry Pi Foundation. Alternatively, you can follow text-based documentation [here](https://projects.raspberrypi.org/en/projects/raspberry-pi-setting-up/2).

With the microSD card ready to go, plug it into the Pi:

![microSD card installation](images/pi-sd.png)

## Hardware Setup

### Notecard and Notecarrier

To connect the Notecard and Pi Hat to the Pi, follow [the section "Connect Your Notecard and Notecarrier" from the Blues Quickstart guide](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#connect-your-notecard-and-notecarrier).

### Smarti Pi Case and Display

To put together the Pi, display, and case, you will follow [this documentation from Smarti Pi](https://cdn.shopify.com/s/files/1/0793/8029/files/touch_pro_assembly_instructions.pdf?v=1640377735), with a few additional steps to accommodate the Notecard hardware.

At Step 7, make sure to install the Notecarrier Pi Hat with the Notecard onto the Pi's headers (don't forget to attach the antenna to the Notecard as well):

![Pi Hat on Pi's headers](images/pi_hat.jpg)

Everything should fit, even with the standoffs attached to the Pi. If you want to manually send commands to the Notecard for any reason (e.g. debugging) via the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/), you will also need to use the micro USB cable to connect the Notecarrier's micro USB port to one of the Pi's USB ports:

![Notecarrier USB connection](images/notecarrier_usb_connection.jpg)

At this point, the assembly should look something like this:

![Fully assembled](images/full_assembly.jpg)

From here, you can continue with the rest of the steps in the Smarti Pi documentation.

### Keyboard and Mouse

Install the AAA batteries into the keyboard and mouse, and plug the USB dongle into a free USB port on the Pi. Note that this single dongle is used for both the keyboard and mouse.

## Booting for the First Time

Plug the USB-C power supply into the exposed USB-C port on the case and let the Pi boot up. Complete the setup steps as described in [this documentation](https://projects.raspberrypi.org/en/projects/raspberry-pi-setting-up/4).

## Notecard Firmware

The Notecard should use [firmware version 5.1.1](https://dev.blues.io/notecard/notecard-firmware-updates/#v5-1-1-april-5th-2023) or higher. The simplest way to update firmware is to do an [over-the-air (OTA) update](https://dev.blues.io/notecard/notecard-firmware-updates/#ota-dfu-with-notehub) via Notehub.

Please note: Your [Notecard will need to be assigned to your Notehub project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notecard) to update its firmware OTA.

## Kiosk Software

### Dependencies

To run the kiosk software, you'll need at least Python 3.8. The default Python version that comes with the OS should be sufficient, but you can check the version at the command line with `python --version`. You will also need to install a couple of Python packages. From this directory, `27-cellular-connected-electronic-kiosk`, run `pip install -r requirements.txt` to install them.

### Running

`kiosk.py` is a Python script that runs the application. With `python kiosk.py --help`, you'll see this help message:

```
usage: kiosk.py [-h] [--data-dir DATA_DIR] --product PRODUCT --route ROUTE

Run the Notecard-based kiosk app.

optional arguments:
  -h, --help           show this help message and exit
  --data-dir DATA_DIR  Root directory for downloaded content and other files created by this script.
  --product PRODUCT    ProductUID for the Notehub project.
  --route ROUTE        Alias for a Proxy Route in Notehub that will be used to download content.
```

- `--data-dir` is optional; if you don't specify it, the default data directory will be `~/kiosk-data/`.
- `--product` should be your [Notehub project's ProductUID](https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid).
- `--route` should be the `alias` of the proxy route you created.

After launching the script with `python kiosk.py --data-dir <path to your data directory> --product <ProductUID> --route <route alias>`, it will begin downloading the .zip file via the proxy route. Once downloaded, it will unzip the file and open `resources/index.htm` in a browser window.
