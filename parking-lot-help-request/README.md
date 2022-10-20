# Parking Lot Help Request

Host-free low latency notification system to alert attendants in a parking lot or facility that customers need assistance. To run this project youself you'll need to:

* [Purchase the necessary hardware and configure it](#hardware).
* [Configure the project's Notecard](#notecard-firmware).
* [Set up a Twilio account and configure the data to route from Notehub to Twilio](#twilio-firmware)

## Hardware

The following hardware is required to run the Parking Lot Help Request application.

* [Notecarrier A with pre-soldered headers](https://shop.blues.io/products/carr-al)
* [Notecard LTE Cat-1](https://shop.blues.io/products/note-wbna-500) (for use in North America)
* [Sparkfun RGB LED breakout - WS2812B](https://www.sparkfun.com/products/13282)
* [Twidec 12mm momentary push button with pre-soldered wires](https://www.amazon.com/gp/product/B08JHW8BPV/ref=ppx_yo_dt_b_asin_title_o00_s00?ie=UTF8&th=1)
* [LiPo battery](https://www.adafruit.com/product/2011)
* [Solar panel with JST connectors (any of these are compatible)](https://www.seeedstudio.com/catalogsearch/result/?q=solar%20panels)
* [Breadboard](https://www.adafruit.com/product/64)
* [Male to male jumper wires](https://www.adafruit.com/product/758)
* [Break-away 0.1" pin strip male headers](https://www.adafruit.com/product/392)

### Hardware Assembly 

1. After purchasing your hardware, use the [Notecard and Notecarrier Quickstart documentation](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-a/) to assemble your Notecarrier A and Notecard, including how to set up a Notehub project.
2. Break off 3 male pins in a strip and solder to each side of the Sparkfun LED breakout. (This [video from Adafruit](https://www.youtube.com/watch?v=Z0joOKaQ43A&ab_channel=AdafruitIndustries) provides a good example of how to solder male pins to a board like the one the LED comes in.)
3. Cut a male jumper wire in half and use a wire stripper to strip at least 1/2" of coating off of both the ends of the jumper wire and each wire connected to the Twidec push button. Also snip off the soldered tip of the button's wires so the filaments are free floating.
4. Use the "Western Union" X style to wrap the exposed jumper wires together with the Twidec wires and solder them together.
5. (Optional: Add a small sleeve of heat shrink tubing to each wire before soldering together and after soldering's complete, slide the heat shrink over the exposed wires and use a heat gun to shrink the wrap and protect the wires from the elements.) (A great video that shows how do all of this - wires to heat shrink - is available [here](https://www.youtube.com/watch?v=Zu3TYBs65FM&ab_channel=ChrisFix).)
6. Slide the LED into a breadboard.
7. Attach the Notecarrier A to the breadboard by plugging a jumper wire from the A's `GND` pin to the `-` pin on the breadboard.
8. Attach the Notecarrier A to the LED breakout by plugging jumper wires into it's headers in the following configuration:
   1. `VIO` -> `V5`
   2. `AUX2` -> `DI`
   3. `-` -> `GND` (this will be a wire in the `-` of the breaboard to the `GND` pin in the LED)
9. Attach the newly soldered button wires to the Notecarrier and breadboard:
   1.  Red wire -> `AUX1` on the Notecarrier A
   2.  Black wire -> `-` on the breadboard
10. Plug the LiPo battery and solar panel into the Notecarrier A's JST connectors for each piece of hardware
11. Now it's time to program the Notecard!

## Notecard Firmware

Although this is a "host-free" project because the Notecard itself can be configured to send notes to Notehub when the help button is pressed, the Notecard still needs to be set up to do so.

To properly program the Notecard refer to the [`README.md`](firmware/README.md#configure-the-notecard) file in this project's `firmware` folder for full instructions under the **Configure the Notecard** section.

## Twilio Firmware

tbd