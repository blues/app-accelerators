from machine import I2C, Pin
import notecard 
from notecard import hub, note
import time

# Config
# productUID = "com.my-company.my-name:my-project"

# Global Variables for Interupt
debounce_time=0
sendDoorChange = False

def doorChange():
    print("Door", ("closed","open")[door.value()])
    rsp = note.add(nCard,file = "door.qo", body = {"open": bool(door.value()), "closed": not door.value()}, port = 11, sync = True)  # Door is low when closed (pull up input)
    print(rsp)
    
def doorCallback(pin):
    global sendDoorChange, debounce_time
    if (time.ticks_ms()-debounce_time) > 500:
        sendDoorChange=True
        debounce_time=time.ticks_ms()

# Create I2C instance 
i2c=I2C(0, freq=400000)

# Connect to Notecard   
nCard = notecard.OpenI2C(i2c, 0, 0) 

# Set ProductUID 

hub.set(nCard, product=productUID, mode="minimum") # We use minimum so we can sync and reset PIR count together

# Setup templates

rsp = note.template(nCard, file="door.qo", body={"open": True, "closed": True}, port = 11, compact = True)
print(rsp)

door = Pin(26, Pin.IN, Pin.PULL_UP)

door.irq(trigger=Pin.IRQ_FALLING|Pin.IRQ_RISING, handler=doorCallback)


while True:
    machine.disable_irq
    if sendDoorChange:
        sendDoorChange = False
        doorChange()
    machine.enable_irq
#    machine.lightsleep(60000)  # This should be uncommented, but with the current (1.23) version of micropython, it crashes USB Serial
                                # Uncomment when running without usb in use.
                                # https://github.com/orgs/micropython/discussions/14401
