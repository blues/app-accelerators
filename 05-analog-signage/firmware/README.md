# Analog Signage Remote Control Firmware

The Analog Signage Demo's firmware is built using [CircuitPython](https://circuiptpython.org). To run the firmware on your device, make sure you have the following pre-requisites installed:

## Prerequisites

1. Download the .UF2 or .BIN for your MCU from CircuitPython.org. For example, the release files for the FeatherS2 can be found [here](https://circuitpython.org/board/unexpectedmaker_feathers2/).
2. Follow the instructions at [CircuitPython.org](https://learn.adafruit.com/welcome-to-circuitpython/installing-circuitpython) to program your device.
3. Download the [Adafruit Libraries bundle](https://circuitpython.org/libraries)
4. Unzip the bundle, find the `adafruit_requests.mpy` file and drag it into the `lib` folder of the `CIRCUITPY` drive mounted to your machine.
5. Download the Notecard Python library from [GitHub](https://github.com/blues/note-python)

  ![note-python Download](https://dev.blues.io/_next/static/media/note-python.c43d4b57.png)

1. In the downloaded `note-python` directory, drag the `notecard` folder into the `lib` folder of the `CIRCUITPY` drive mounted to your machine. See [here](https://dev.blues.io/guides-and-tutorials/collecting-sensor-data/notecarrier-f/

  ![note-python installation for CircuitPython](https://dev.blues.io/_next/static/media/install-note-python.68e5d1f7.png)


## Firmware Overview

The `.py` files in this directory contain the complete working source for this demo.

- `secrets.py` contains Wi-Fi SSID and Password information, as well as the IP address and [API key for your Vestaboard](https://docs.vestaboard.com/local). You will need to update the placeholder values in this file before you run the app.
- `code.py` is the main application and should either be incorporated into the `code.py` file of your MCU or dragged onto the `CIRCUITPY` drive to overwrite the default file. This file contains application logic for:
  - Connecting to the Notecard and configuring its connection (don't forget to set your [Notehub ProductUID](https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid) in this app or using the REPL at [dev.blues.io](https://dev.blues.io)).
  - Connecting to a local Wi-Fi network.
  - Communicating with the Vestaboard over the local Wi-Fi Network.
  - Polling for environment variable changes and updating local application state when new variables are detected.
  - Mapping environment variable values to the [array](https://docs.vestaboard.com/local) and [character code format](https://docs.vestaboard.com/characters) expected by the Vestaboard API.
  - Sending updated information to the Vestaboard.
- `vestaboard.py` contains helper objects for board setup as well as a character codes dictionary that maps letters, digits, characters and colors to the Vestaboard character code format.
- `feathers2.py` contains helper functions for working with devices on the FeatherS2. This file and references to it can be deleted if using another Wi-Fi host.
