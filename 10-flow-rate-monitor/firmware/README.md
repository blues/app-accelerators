# Flow Rate Sensor

This directory contains Arduino-based firmware for a flow rate measurement
utility. The firmware source code is identical to the valve-monitor firmware in
this repository. However, the Visual Studio Code project here sets the macro
USE_VALVE to 0.

## Requirements

- Visual Studio Code with the [PlatformIO IDE](https://platformio.org/platformio-ide)
installed.
- [Notecard](https://blues.io/products/notecard/).
- [Swan MCU](https://blues.io/products/swan/).
- [Notecarrier F](https://blues.io/products/notecarrier/notecarrier-f/).
- Male to male jumper wires.
- A micro USB data cable.

## Setup

- [Plug the Notecard into the Notecarrier F](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/).
- Plug the Swan into the Feather-compatible headers on the Notecarrier.
- Connect the ATTN and F_D13 pins on the Notecarrier.
- Connect the F_D6 pin of the Notecarrier to the signal pin of your flow rate
meter.
- Connect the Swan to your development machine with the micro USB cable.
- Open Visual Studio Code and navigate to the PlatformIO menu using the icon
on the leftmost side.
- Select "Open" and open 10-flow-rate-monitor/firmware.
- Edit main.cpp so to define a valid product UID:

```C
// Replace with your product UID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"
```

- In the Project Tasks pane of the PlatformIO view, click Build.
- Hold down the BOOT button on the Swan, press and release the RST button, and
release the BOOT button. The Swan is now ready to receive the firmware image
from PlatformIO via the micro USB cable.
- In the Project Tasks pane of the PlatformIO view, click Upload.
- At the bottom of the VS Code window, click the tiny plug icon to open the
serial console. You should see messages from the Swan being logged.
