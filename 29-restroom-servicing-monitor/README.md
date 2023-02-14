# Restroom Servicing Monitor

Receive updates on restroom cleanliness so that it can be serviced when needed.

- [Restroom Servicing Monitor](#restroom-servicing-monitor)
  - [You Will Need](#you-will-need)
  - [Notehub Setup](#notehub-setup)
  - [Sparrow Setup](#sparrow-setup)
    - [Quickstart](#quickstart)
    - [Hardware](#hardware)
    - [Firmware](#firmware)
  - [Testing](#testing)
  - [Blues Community](#blues-community)
  - [Additional Resources](#additional-resources)

## You Will Need

* [Sparrow Development Kit](https://shop.blues.io/products/sparrow-dev-kit)
* 2 USB A to micro USB cables
* Breadboard
* [3 push buttons](https://www.sparkfun.com/products/14460)
* Jumper wires

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Sparrow Setup

### Quickstart

Follow the [Sparrow Quickstart](https://dev.blues.io/quickstart/sparrow-quickstart/) to get your Sparrow devices paired with each other and associated with the Notehub project you just created. Make sure that you use the ProductUID generated in [Notehub Setup](#notehub-setup) when it comes time to issue the `hub.set` command in the quickstart. Note also that we'll only need one reference node for this project, so you don't need to pair both nodes that came with the dev kit.

After you've completed the quickstart, leave the Notecarrier and essentials board powered and connected. These two devices will act as our gateway to Notehub, and we won't need to touch them again. The rest of this guide will focus on the reference node.

### Hardware

Before we set up the custom firmware needed for the restroom servicing monitor, let's wire up the hardware. The hardware interface is 3 push buttons: 1 for a bad rating (restroom needs service), 1 for an ok rating (restroom cleanliness deteriorating), and 1 for a good rating (no service needed). These buttons are wired to GPIO pins on the reference node. Those pins use internal pull-ups so that the pin reads a digital 1 when the button isn't pushed. When the button is pushed, it connects the corresponding GPIO pin to GND so that it reads a digital 0. Using the breadboard, buttons, and jumper wires, wire up the hardware as shown in the diagram below.

![breadboard wiring](assets/nf29_breadboard.jpg "Breadboard Wiring")

The labels off the breadboard (A1-A3 and GND) correspond to pins on the reference node. Connect these to the breadboard as shown with jumper wires.

### Firmware

Next, we need to flash the reference node with the restroom servicing monitor firmware.

1. Before we do anything, we need to pull in some dependencies for the firmware to work. After cloning this repository, run `git submodule update --init`. This will pull in the note-c and sparrow-lora submodules that this project depends on.
1. Install [Visual Studio Code](https://code.visualstudio.com/). We will program the reference node from VS Code.
1. Add the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) to VS Code. Building and flashing the firmware will take place inside a Docker container specifically designed for these tasks (refer to firmware/Dockerfile for details of what's in the container).
1. Connect the STLINK-V3MINI to your development PC with a USB A to micro USB cable.
1. Connect the STLINK to your reference node with the 2x7 JTAG ribbon cable.
1. Navigate to File > Open Folder and select the firmware folder.
1. In the bottom left corner, click the lavender icon (it looks like "><") and hit Reopen in Container. The first time you do this, the Docker container will have to be built, which will take several minutes.
1. Once the container is built, build the firmware with the hotkey `CTRL + SHIFT + B`. Alternatively, you can navigate to Terminal > Run Task > Sparrow: Build Firmware using CMake and Make to accomplish the same thing.
1. Navigate to Terminal > Run Task > Sparrow: Flash Firmware using STM32_Programmer_CLI to flash the firmware onto the reference node. If you get an error about not being able to find the device to program, try clicking the lavender >< icon in the bottom right again and selecting Rebuild Container. This error can occur if the reference node was connected to the STLINK _after_ opening the container.
1. Open a terminal emulator and connect to the STLINK's serial connection to view logs. See the documentation [here](https://dev.blues.io/sparrow/sparrow-builders-guide/#collecting-firmware-logs). 
1. Open the Run and Debug view on the left-hand toolbar (or use the hotkey `CTRL + SHIFT + D`) and hit the green play button next to Cortex Debug. This restarts the firmware in debug mode, which enables logging. In your terminal emulator's output, you should see something like this:

```
===================
===================
===== SPARROW =====
===================
Feb 13 2023 23:36:53
2037335832365003001d001f
APPLICATION HOST MODE
CONSOLE TRACE ENABLED
rsm activated with 1440s activation period and 15s poll interval
rsm now DEACTIVATED (rsm: nothing to do)
```

"rsm" stands for restroom servicing monitor.

## Testing

To test things out, we'll press each button, observe the serial log, and then observe the note that was sent to Notehub.

1. Press the button connected to A1 (bad rating). You should see `Sent bad rating.` in the serial log. On your Notehub project's Events page, you should see a note come into ID#ratings.qo that looks like this, where ID is a string of characters that uniquely identifies the reference node (e.g. 2037335832365003001d001f#ratings.qo):

```json
{
    "rating": "bad"
}
```

1. Press the button connected to A2 (ok rating). You'll see `Sent ok rating.` in the serial log and the Notehub event should have a `rating` field with the string "ok".
1. Press the button connected to A3 (good rating). You'll see `Sent good rating.` in the serial log and the Notehub event should have a `rating` field with the string "good".

## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!

## Additional Resources

* [Sparrow Datasheet](https://dev.blues.io/hardware/sparrow-datasheet/)
* [Sparrow Builder's Guide](https://dev.blues.io/sparrow/sparrow-builders-guide/)
* [Sparrow Hardware Behavior](https://dev.blues.io/sparrow/sparrow-hardware-behavior/) (e.g. what do the various Sparrow LEDs indicate?)
