# Fridge Fleet Monitor


LoRa-based temperature, humidity and door open/close state for a fleet of refrigerators.


To run this project yourself you'll need to:

* [Purchase the necessary hardware and configure it](#hardware).
* [Flash the project's firmware](#firmware).
* [Install the hardware](#hardware-installation).
* [Run the project's web application](#web-application).

## Hardware 

The following hardware is required to run the Fridge Fleet Monitor.

* [Blues Starter Kit for LoRaWAN](https://shop.blues.com/products/blues-starter-kit-lorawan)
* [Raspberry Pi Pico](https://www.adafruit.com/product/4864)
* [Adafruit magnetic contact switch (door sensor) x 2](https://www.adafruit.com/product/375)
* [DHT-11 Temperature and Humidity Sensor](https://www.amazon.com/SHILLEHTEK-Temperature-Humidity-Sensor-Module/dp/B0CN5RP8SL)


## Firmware

This project runs on CircuitPython so you need to install it onto your Raspberry Pi Pico.  Follow the instructions from Adafruit: [Installing CircuitPython](https://learn.adafruit.com/getting-started-with-raspberry-pi-pico-circuitpython/circuitpython)

Once this is installed, the pico will present as a mass storage device that you can install libraries and firmware onto.  First install the [note-python](https://dev.blues.io/tools-and-sdks/firmware-libraries/python-library/#circuitpython-and-micropython) library, and then the [Adafruit CircuitPython DHT Library](https://github.com/adafruit/Adafruit_CircuitPython_DHT).

Finally copy code.py from the code repository onto the Pico.

TODO: We should replace this sensor


## Hardware Installation

With the hardware assembled and the proper firmware flashed, install each Sparrow node inside of the refrigerator where it will be monitoring conditions.

It's recommended, if possible, to attach the Sparrow node close enough to the door of the fridge that the magnetic switches can be positioned next to each other on the exterior of the door. 

To attach the Sparrow node inside the fridge, use Command Strips or some other adhesive strip attached to the front of the node, and wipe down the area to place with node with rubbing alcohol prior to attaching for best adhesion.

![Sparrow node mounted inside refrigerator on side wall](images/readme-sparrow-mounted-fridge.jpg)

_Sparrow node installed in a refrigeration unit._

![Sparrow node mounted inside freezer on side wall](images/readme-sparrow-mounted-freezer.jpg)

_Sparrow node installed in a freezer unit._

> **NOTE**: Be advised batteries are not an optimal power source in cold conditions, and it's recommended to have a steady state power source for long term fridge fleet monitor projects. Also consider drilling holes through the back of the Sparrow node enclosure to allow for the contact sensor wires to pass through while protecting the rest of the device's innards.

Likewise, to install the door sensors, attach each piece of the sensor on one side of the door via Command Strips and ensure they line up as closely as possible (notice in the example photos below several Command Strips were stacked together to make sure the switch pieces were as level with each other as possible). See images below.

![Sparrow door switches aligned together when door is closed](images/readme-door-switches-closed.jpg)

Notice multiple Command Strips raise the connected contact switch sensor on the fridge so they're close to level with the switch on the door's edge.

![Sparrow door switches separated when door is opened](images/readme-door-switches-open.jpg)

Here's how it looks opened. The rubber seal of the door does not interfere with or impede the contact switches from correctly reporting to the Sparrow node inside the fridge at all.

Now that the harware is installed, the last task left is to set up the web app.
## Web Application

The Fridge Fleet Monitor's web application is based on the [Sparrow Reference Web App](https://github.com/blues/sparrow-reference-web-app). 

Refer to the [`README.md`](https://github.com/blues/app-accelerators/tree/main/02-fridge-fleet-monitor/web-app/README.md) file in this project's [`web-app`](https://github.com/blues/app-accelerators/tree/main/02-fridge-fleet-monitor/web-app) folder for full setup instructions.

If all goes well, you'll end up with a Fridge Fleet Monitor dashboard similar to these.

![Fridge Fleet Monitor web app dashboard](images/readme-refrigerator-fleet-monitor-dashboard.png)

_Fridge Fleet Monitor main dashboard._

![Fridge Fleet Monitor node details page](images/readme-fridge-fleet-monitor-node-details.png)

_Fridge Fleet Monitor node details._



