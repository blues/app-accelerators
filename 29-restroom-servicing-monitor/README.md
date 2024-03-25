# Restroom Servicing Monitor

**Warning: This project uses Sparrow, a Blues product that is no longer under active development. We are working on updating this project to the successors of Sparrow: [Notecard LoRa](https://blues.com/notecard-lora/) and the [LoRaWAN Starter Kit](https://shop.blues.com/products/blues-starter-kit-lorawan). In the meantime, if you would like assistance building a Restroom Servicing Monitor feel free to reach out on [our community forum](https://discuss.blues.com/).**

Receive updates on restroom cleanliness so that it can be serviced when needed.

## You Will Need

* [Blues Starter Kit for LoRaWAN](https://shop.blues.com/products/blues-starter-kit-lorawan)
* 2 USB A to micro USB cables
* Breadboard
* [3 push buttons](https://www.sparkfun.com/products/14460)
* Jumper wires

## Notehub Setup

Sign up for a free account on [notehub.io](https://notehub.io) and [create a new project](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-pi/#set-up-notehub).

## Notecard Setup

### Quickstart

Follow the [LoRaWAN Quickstart](https://dev.blues.io/quickstart/lorawan-quickstart/) Make sure that you use the ProductUID generated in Notehub Setup when it comes time to issue the hub.set command in the quickstart.

After you've completed the quickstart, leave the Notecarrier plugged into the USB and the web terminal open.  We will be sending more commands to configure the Notecard for this application.


### Hardware

Before we set up the Notecard for the restroom servicing monitor, let's wire up the hardware. The hardware interface is 3 push buttons: 1 for a bad rating (restroom needs service), 1 for an ok rating (restroom cleanliness deteriorating), and 1 for a good rating (no service needed). These buttons are wired to AUX pins on the Notecard. Those pins will be configured with internal pull-ups so that the pin reads a digital 1 when the button isn't pushed. When the button is pushed, it connects the corresponding AUX pin to GND so that it reads a digital 0. Using the breadboard, buttons, and jumper wires, wire up the hardware as shown in the diagram below.

![breadboard wiring](assets/nf29_breadboard.jpg "Breadboard Wiring")

The labels off the breadboard (A1-A3 and GND) correspond to pins on the Notecarrier. Connect these to the breadboard as shown with jumper wires.

### Notecard Setup

Next, we need to set up the Notecard.  For this project we are using the Notecard without any microprocessor host, instead using the built-in features of the Notecard as described in the documentation. This will automatically send a note when the buttons are pressed.
To enable this feature, we go back to the web terminal we used in the Quickstart, and issue the following command:
```json
{
    "req": "card.aux",
    "mode":"gpio", 
    "usage": ["input-pullup", "input-pullup", "input-pullup", "off"],
    "sync":true,
    "file":"gpio.qo"
}
```
This will set each of the three AUX pins we are using to input, with a pull up resistor, and to trigger on each state change, with about 1 second of debounce.  It will also send a note each time a transition occurs.  In Notehub we will filter only the falling state before routing the data to a dashboard.   

## Notehub Route Setup


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

* [Sparrow Datasheet](https://dev.blues.io/datasheets/sparrow-datasheet/)
* [Sparrow Hardware Behavior](https://dev.blues.io/sparrow/sparrow-hardware-behavior/) (e.g. what do the various Sparrow LEDs indicate?)
