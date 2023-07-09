# Propane Tank Fuel Gauge

Monitor how much propane is left in the tank and the estimated burn time remaining, and receive alerts when the fuel is running low.

## Solution Overview

While there are various tricks to find out how much propane is left in the tank, such as poring warm water down the side of the tank, we can use technology to give you a more precise measure of how much fuel is left in the tank.

This solution uses a $20 ultrasound sensor to measure the amount of propane left in the tank. You glue the sensor below the base of the propane tank - no other modifications to the tank are needed.


## Mounting the Sensor

The sensor uses ultrasound reflections to determine the height of the liquid in the tank. It's essential that there are no air gaps between the sensor and the tank. The sensor is supplied with A/B epoxy that creates an air-free seal between the tank and the sensor, as well as permanently affixing the sensor to the tank. 

Mount the sensor at the center point, with the flat side without the LED in contact with the base of teh tank.  (The other side of the sensor with the LED should be facing away from the base of the tank.)


Firmware Design



Hardware

https://www.amazon.com/XKC-DS1603L-V1-Ultrasonic-Non-contact-Sensing-Apacitive/dp/B08C27QLDT


Sensor wiring
- RED, VCC (can use 3.3 or 5v)
- BLACK, GND
- Yellow, RX


Notecard APIs
`note.template`
`env.modified`
`env.get`


Code patterns:
* The `Alert` class is used to keep track of the state of an alert, and whether the corresponding event has been sent. This pattern ensures that the event is resent should


References

https://helpful.knobs-dials.com/index.php/Low-pass_filter
