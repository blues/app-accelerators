# Door State Monitor

Receive notifications when a door is opened or closed.

## You Will Need

* [Notecard LoRa](https://shop.blues.com/products/notecard-lora)
* [Notecarrier A](https://shop.blues.com/products/carr-al)
* 2 USB A to micro USB cables
* [Magnetic Door Switch Set](https://www.sparkfun.com/products/13247)

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/#set-up-notehub).

## Notecard Setup

### Quickstart

Follow the [Notecard Quickstart](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/). Make sure that you use the ProductUID generated in [Notehub Setup](https://dev.blues.io/accelerators/restroom-servicing-monitor/#notehub-setup) when it comes time to issue the hub.set command in the quickstart. 

After you've completed the quickstart, leave the Notecarrier plugged into the USB and the web terminal open.  We will be sending more commands to configure the Notecard for this application.


### Hardware

Before we set up the custom firmware needed for the door state monitor, let's wire up the hardware. The magnetic door switch consists of two plastic terminals, one of which has a pair of wires coming out of it. When the terminals are brought into contact (or close proximity), an internal [reed switch](https://en.wikipedia.org/wiki/Reed_switch) is closed and the two wires are electrically connected. 

As shown in the SparkFun product link above, the terminal with the wires is typically mounted to a door frame and the other terminal is mounted to the door such that when the door is closed, the two terminals are adjacent, closing the switch. 

Since we're just testing things out, don't mount the terminals yet. Instead, keep them on your work surface and plug the wires into the AUX1 and VIO ports of the Notecarrier. When the switch is closed, A1 will connect to VIO. Otherwise, A1 will be driven low by an internal pull-down.

### Notecard Setup

Next, we need to set up the Notecard.  For this project we are using the Notecard without any microprocessor host, instead using the built-in features of the Notecard as described in the [documentation](https://dev.blues.io/notecard/notecard-walkthrough/advanced-notecard-configuration/#sending-notes-based-on-aux-pin-state-changes). This will automatically send a note when the state of the door changes.

To enable this feature, we go back to the web terminal we used in the Quickstart, and issue the following command:

```
{ 
    "req": "card.aux", 
    "mode":"gpio", 
    "usage": ["input-pulldown", "off", "off", "off"], 
    "sync":true, 
    "file":"gpio.qo" 
}
```

This will set the AUX1 pin we are using to input, with a pull-down resistor, and to send a note every time the state changes.  The input is debounced by 1 second, meaning that the state needs to be stable for one second before a change is acknowledged.

## Testing

1. Bring the terminals together. On the events page of your Notehub project, you should see a note that reads `{"power":true,"state":[{"high":true},{},{},{}]}`, indicating the door is closed.
1. Separate the terminals. On the events page of your Notehub project, you should see a note that reads `{"power":true,"state":[{"low":true},{},{},{}]}`, indicating the door is open.


## Blues Community

We’d love to hear about you and your project on the [Blues Community Forum](https://discuss.blues.io/)!

## Additional Resources

* [Notecard Lora Datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-lora/)
