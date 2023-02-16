# Door State Monitor

Receive notifications when a door is opened or closed.

- [Door State Monitor](#door-state-monitor)
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
* [Magnetic Door Switch Set](https://www.sparkfun.com/products/13247)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/#set-up-notehub).

## Sparrow Setup

### Quickstart

Follow the [Sparrow Quickstart](https://dev.blues.io/quickstart/sparrow-quickstart/) to get your Sparrow devices paired with each other and associated with the Notehub project you just created. Make sure that you use the ProductUID generated in [Notehub Setup](#notehub-setup) when it comes time to issue the `hub.set` command in the quickstart. Note also that we'll only need one reference node for this project, so you don't need to pair both nodes that came with the dev kit.

After you've completed the quickstart, leave the Notecarrier and essentials board powered and connected. These two devices will act as our gateway to Notehub, and we won't need to touch them again. The rest of this guide will focus on the reference node.

### Hardware

Before we set up the custom firmware needed for the door state monitor, let's wire up the hardware. The magnetic door switch consists of two plastic terminals, one of which has a pair of wires coming out of it. When the terminals are brought into contact (or close proximity), an internal [reed switch](https://en.wikipedia.org/wiki/Reed_switch) is closed and the two wires are electrically connected. As shown in the SparkFun product link above, the terminal with the wires is typically mounted to a door frame and the other terminal is mounted to the door such that when the door is closed, the two terminals are adjacent, closing the switch. Since we're just testing things out, don't mount the terminals yet. Instead, keep them on your work surface and plug the wires into the A1 and GND ports of the Sparrow reference node (it doesn't matter which wire goes into which port). When the switch is closed, A1 will connect to GND. Otherwise, A1 will be driven high by an internal pull-up.

### Firmware

Next, we need to flash the reference node with the door state monitor firmware.

1. Before we do anything, we need to pull in some dependencies for the firmware to work. After cloning this repository, run these commands: `git submodule update --init 28-door-state-monitor/firmware/note-c` and `git submodule update --init 28-door-state-monitor/firmware/sparrow-lora`. This will pull in the note-c and sparrow-lora submodules that this project depends on.
1. There are a few ways to build and flash the firmware onto the reference node. These are covered in the [Sparrow Builder's Guide](https://dev.blues.io/sparrow/sparrow-builders-guide/). Follow the steps in that guide and then return to these instructions.
1. Connect the STLINK-V3MINI to your development PC with a USB A to micro USB cable.
1. Connect the STLINK to your reference node with the 2x7 JTAG ribbon cable.
1. Build and flash the code using whichever method you selected when following the Sparrow Builder's Guide.
1. Open a terminal emulator and connect to the STLINK's serial connection to view logs. See the documentation [here](https://dev.blues.io/sparrow/sparrow-builders-guide/#collecting-firmware-logs). 
1. Start the program in debug mode (again, how you do this depends on the IDE: VS Code or STM32CubeIDE). In your terminal emulator's output, you should see something like this:

```
===================
===== SPARROW =====
===================
Feb 15 2023 19:29:29
2037335832365003001d001f
APPLICATION HOST MODE
CONSOLE TRACE ENABLED
dsm: Door initially open.
dsm activated with 1440s activation period and 1s poll interval
dsm now DEACTIVATED (Nothing to do.)
```

"dsm" stands for door state monitor. You'll also see a message like this at startup:

```
dsm: Sending door state heartbeat.
```

Every 30 minutes, the app sends the state of the door to Notehub, which acts as a heartbeat so that we know everything's still working.

## Testing

1. Bring the terminals together. You should see the message `dsm: Door closed.` in the serial log. Then, on the events page of your Notehub project, you should see a note that reads `{"open":false}`, indicating the door is closed.
1. Separate the terminals. You should see the message `dsm: Door open.` in the serial log. Then, on the events page of your Notehub project, you should see a note that reads `{"open":true}`, indicating the door is open.

The "File" field for each note on the events page will look something like `ID#state.qo`, where ID is a long alphanumeric string. This ID uniquely identifies the Sparrow reference node. This way, if you have multiple door state monitors, you can distinguish which monitor is which.

## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!

## Additional Resources

* [Sparrow Datasheet](https://dev.blues.io/hardware/sparrow-datasheet/)
* [Sparrow Hardware Behavior](https://dev.blues.io/sparrow/sparrow-hardware-behavior/) (e.g. what do the various Sparrow LEDs indicate?)
