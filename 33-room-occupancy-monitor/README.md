# Room Occupancy Monitor

Receive notifications when motion is detected in a room and when the room's door is opened or closed.

## You Will Need

* Lora Development Kit
* Raspberry Pi Pico
* USB A to micro USB cable
* [Magnetic Door Switch Set](https://www.sparkfun.com/products/13247)
* PIR Sensor

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/#set-up-notehub).

## Pico Setup

### Hardware

There are two primary pieces of hardware: the magnetic door switch and a PIR motion sensor.

The magnetic door switch consists of two plastic terminals, one of which has a pair of wires coming out of it. When the terminals are brought into contact (or close proximity), an internal [reed switch](https://en.wikipedia.org/wiki/Reed_switch) is closed and the two wires are electrically connected. As shown in the SparkFun product link above, the terminal with the wires is typically mounted to a door frame and the other terminal is mounted to the door such that when the door is closed, the two terminals are adjacent, closing the switch. Since we're just testing things out, don't mount the terminals yet. Instead, keep them on your work surface and plug the wires into the GP22 and GND ports of the Pico (it doesn't matter which wire goes into which port). When the switch is closed, GP22 will connect to GND. Otherwise, GP22 will be driven high by an internal pull-up.

The PIR motion sensor 

### Firmware



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

```c
#define PIR_SUPPRESSION_MINS        5
```

## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!

## Additional Resources

* [Sparrow Datasheet](https://dev.blues.io/datasheets/sparrow-datasheet/)
* [Sparrow Hardware Behavior](https://dev.blues.io/sparrow/sparrow-hardware-behavior/) (e.g. what do the various Sparrow LEDs indicate?)
* [PIR motion sensor datasheet (PYQ 1548 / 7659)](https://media.digikey.com/pdf/Data%20Sheets/Excelitas%20PDFs/PYQ_1548_7659_07.11.2018_DS.pdf)
