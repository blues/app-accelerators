from machine import I2C, Pin, Timer
import notecard 
from notecard import card, hub, note
import time

# Config
PIRSupressionMins = 5
# productUID = "com.my-company.my-name:my-project"

debounce_time=0
sendButtonPress = False


    
    
def sendRating():
    rating = "ok"

    print("Sent", rating, " rating.")
    rsp = note.add(nCard,file = "rating.qo", body = {"rating": rating} , port = 13, sync = True)  # Door is low when closed (pull up input)
    print(rsp)
    
def buttonCallback(pin):
    global sendButtonPress, debounce_time
    if (time.ticks_ms()-debounce_time) > 500:
        sendButtonPress=True
        debounce_time=time.ticks_ms()


# Create I2C instance 
i2c=I2C(0, freq=400000)

# Connect to Notecard   
nCard = notecard.OpenI2C(i2c, 0, 0) 

# Set ProductUID 

hub.set(nCard, product=productUID, mode="minimum") # We use minimum so we can sync and reset PIR count together

# Setup templates
rsp = note.template(nCard, file="rating.qo", body={"rating": 21}, port = 13, compact = True)
print(rsp)

poor = Pin(26, Pin.IN, Pin.PULL_DOWN)
ok = Pin(8, Pin.IN, Pin.PULL_DOWN)
good = Pin(7, Pin.IN, Pin.PULL_DOWN)

poor.irq(trigger=Pin.IRQ_RISING, handler=buttonCallback)
ok.irq(trigger=Pin.IRQ_RISING, handler=buttonCallback)
good.irq(trigger=Pin.IRQ_RISING, handler=buttonCallback)


while True:
    machine.disable_irq
    if sendButtonPress:
        sendButtonPress = False
        sendRating()
    machine.enable_irq
#    machine.lightsleep(60000)  # This should be uncommented, but with the current (1.23) version of micropython, it crashes USB Serial
                                # Uncomment when running without usb in use.
                                # https://github.com/orgs/micropython/discussions/14401
