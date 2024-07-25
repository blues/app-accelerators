from machine import I2C, Pin, Timer
import notecard 
from notecard import card, hub, note
import time

# Config
PIRSupressionMins = 5
# productUID = "com.my-company.my-name:my-project"

debounce_time=0
pir_debounce_time=0

pirCount = 0
pirTotal = 0
sendPIR = False
sendDoorChange = False

def sendPIRCount():
    # Reset PIR count and send note
    global pirTotal
    global pirCount
    
    pirTotal += pirCount
    rsp = note.add(nCard,file = "motion.qo", body = {"count": pirCount, "total": pirTotal}, port = 12, sync = True)
    print(rsp)
    pirCount = 0
    
    
def doorChange():
    print("Door", ("closed","open")[door.value()])
    rsp = note.add(nCard,file = "door.qo", body = {"open": door.value(), "closed": not door.value()}, port = 11, sync = True)  # Door is low when closed (pull up input)
    print(rsp)
    
def doorCallback(pin):
    global sendDoorChange, debounce_time
    if (time.ticks_ms()-debounce_time) > 500:
        sendDoorChange=True
        debounce_time=time.ticks_ms()
def pirCallback(pin):
    global pirCount, pir_debounce_time
    if (time.ticks_ms()-pir_debounce_time) > 500:
        pirCount += 1
        pir_debounce_time=time.ticks_ms()
    
def pirTimer(timer = None):
    global sendPIR
    sendPIR=True

# Create I2C instance 
i2c=I2C(0, freq=400000)

# Connect to Notecard   
nCard = notecard.OpenI2C(i2c, 0, 0) 

# Set ProductUID 

hub.set(nCard, product=productUID, mode="minimum") # We use minimum so we can sync and reset PIR count together

# Setup templates
rsp = note.template(nCard, file="motion.qo", body={"count": 21, "total": 21}, port = 12, compact = True)
print(rsp)
rsp = note.template(nCard, file="door.qo", body={"open": True, "closed": True}, port = 11, compact = True)
print(rsp)

pir = Pin(22, Pin.IN, Pin.PULL_DOWN)
door = Pin(21, Pin.IN, Pin.PULL_UP)

timer = Timer(mode=Timer.PERIODIC, period=(PIRSupressionMins * 60 * 1000), callback=pirTimer)
door.irq(trigger=Pin.IRQ_FALLING|Pin.IRQ_RISING, handler=doorCallback)
pir.irq(trigger=Pin.IRQ_RISING, handler=pirCallback)

while True:
    machine.disable_irq
    if sendPIR:
        print("PIR Count: ", pirCount, " PIR Total: ", pirTotal)
        sendPIR = False
        sendPIRCount()
    if sendDoorChange:
        sendDoorChange = False
        doorChange()
    machine.enable_irq
#    machine.lightsleep(60000)  # This should be uncommented, but with the current (1.23) version of micropython, it crashes USB Serial
                                # Uncomment when running without usb in use.
                                # https://github.com/orgs/micropython/discussions/14401
