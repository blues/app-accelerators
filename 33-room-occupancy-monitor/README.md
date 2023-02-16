# Room Occupancy Monitor

Receive notifications when motion is detected in a room and when the room's door is opened or closed.

- [Room Occupancy Monitor](#room-occupancy-monitor)
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

Follow the [Sparrow Quickstart](https://dev.blues.io/quickstart/sparrow-quickstart/) to get your Sparrow reference node paired with the gateway and associated with the Notehub project you just created. Note also that we'll only need one reference node for this project, so you don't need to pair both nodes that came with the dev kit. Make sure that you use the ProductUID generated in [Notehub Setup](#notehub-setup) when it comes time to issue the `hub.set` command in the quickstart.

After you've completed the quickstart, leave the Notecarrier and essentials board powered and connected. These two devices will act as our gateway to Notehub, and we won't need to touch them again. The rest of this guide will focus on the reference node.

### Hardware

There are two primary pieces of hardware: the magnetic door switch and the PIR motion sensor attached to the Sparrow reference node's circuit board.

The magnetic door switch consists of two plastic terminals, one of which has a pair of wires coming out of it. When the terminals are brought into contact (or close proximity), an internal [reed switch](https://en.wikipedia.org/wiki/Reed_switch) is closed and the two wires are electrically connected. As shown in the SparkFun product link above, the terminal with the wires is typically mounted to a door frame and the other terminal is mounted to the door such that when the door is closed, the two terminals are adjacent, closing the switch. Since we're just testing things out, don't mount the terminals yet. Instead, keep them on your work surface and plug the wires into the A1 and GND ports of the Sparrow reference node (it doesn't matter which wire goes into which port). When the switch is closed, A1 will connect to GND. Otherwise, A1 will be driven high by an internal pull-up.

The PIR motion sensor has a small plastic dome sticking out of the front of the reference node's housing. Since this sensor is integrated into the reference node already, there's no additional wiring required. When something moves in front of the sensor, it'll detect that motion and kick off logic in the firmware to send a "motion detected" note to Notehub.

### Firmware

Next, we need to flash the reference node with the room occupancy monitor firmware.

1. Before we do anything, we need to pull in some dependencies for the firmware to work. After cloning this repository, run these commands: `git submodule update --init 33-room-occupancy-monitor/firmware/note-c` and `git submodule update --init 33-room-occupancy-monitor/firmware/sparrow-lora`. This will pull in the note-c and sparrow-lora submodules that this project depends on.
1. There are a few ways to build and flash the firmware onto the reference node. These are covered in the [Sparrow Builder's Guide](https://dev.blues.io/sparrow/sparrow-builders-guide/). Follow the steps in that guide and then return to these instructions.
1. Connect the STLINK-V3MINI to your development PC with a USB A to micro USB cable.
1. Connect the STLINK to your reference node with the 2x7 JTAG ribbon cable.
1. Build and flash the code using whichever method you selected when following the Sparrow Builder's Guide.
1. Open a terminal emulator and connect to the STLINK's serial connection to view logs. See the documentation [here](https://dev.blues.io/sparrow/sparrow-builders-guide/#collecting-firmware-logs). 
1. Start the program in debug mode (again, how you do this depends on the IDE: VS Code or STM32CubeIDE). In your terminal emulator's output, you should see something like this:

```
===================
===================
===== SPARROW =====
===================
Feb 16 2023 22:42:58
2037335832365003001d001f
APPLICATION HOST MODE
CONSOLE TRACE ENABLED
```

You may also see a message like this:

```
pir: 1 motion events sensed
```

At startup, the PIR sensor may detect motion even if there is none. This is a one-off false positive that can be ignored, and the sensor should work as intended thereafter. If you encounter additional false positives, you may want to try increasing the sensor's threshold value, which makes the sensor less sensitive. This can be done by defining the macro `PIR_THRESHOLD` at build-time (e.g. via `-DCMAKE_C_FLAGS="-DPIR_THRESHOLD=50"`). If this macro isn't defined by the user, it'll default to 100 in pir.c:

```C
#ifndef PIR_THRESHOLD
#define PIR_THRESHOLD 100
#endif
```

The value must be between 0 and 255. If you find that the sensor is not sensitive enough, you can try decreasing the threshold value.

## Testing

To test the magnetic door switch, do the following:

1. Bring the terminals together. You should see the message `dsm: Door closed.` in the serial log. Then, on the events page of your Notehub project, you should see a note that reads `{"open":false}`, indicating the door is closed.
1. Separate the terminals. You should see the message `dsm: Door open.` in the serial log. Then, on the events page of your Notehub project, you should see a note that reads `{"open":true}`, indicating the door is open.

The "File" field for each note on the events page will look something like `ID#state.qo`, where ID is a long alphanumeric string. This ID uniquely identifies the Sparrow reference node. This way, if you have multiple reference nodes, you can distinguish which monitor is which.

Now, to test the PIR motion sensor, simply wave your hand in front of the plastic dome or walk by it. You should see a note on the events page for your project like this:

```json
{
  "count": 1,
  "total": 12
}
```

`count` indicates the number of motion events since the last note, while `total` is the total number of events since the device first started running. Similar to the door state notes, the "File" for these notes is `ID#motion.qo`, where ID is that same alphanumeric string that uniquely identifies the reference node.

In order to keep traffic to Notehub reasonable, a motion note will only be sent a maximum of once every 5 minutes. So, many motion events may be coalesced into a single note. In that case, you would see a `count` value greater than 1. If you want to lengthen or shorten this interval, you can modify the value of `PIR_SUPPRESSION_MINS` in pir.c:

```C
#define PIR_SUPPRESSION_MINS        5
```

## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!

## Additional Resources

* [Sparrow Datasheet](https://dev.blues.io/hardware/sparrow-datasheet/)
* [Sparrow Hardware Behavior](https://dev.blues.io/sparrow/sparrow-hardware-behavior/) (e.g. what do the various Sparrow LEDs indicate?)
* [PIR motion sensor datasheet (PYQ 1548 / 7659)](https://media.digikey.com/pdf/Data%20Sheets/Excelitas%20PDFs/PYQ_1548_7659_07.11.2018_DS.pdf)
