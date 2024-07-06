import notecard
import alarm
import board
import busio
import time
import microcontroller
import digitalio
from notecard import card, hub, note
import adafruit_dht 

# Config Values
# Uncomment this line and replace com.your-company:your-product-name with your
# ProductUID.

#productUID      = "com.my-company.my-name:my-project"       # Product ID
dht_11_pin      = board.GP22                                 # DHT 11 Data Pin
scl_pin         = board.GP5                                  # Notecard SCL Pin
sda_pin         = board.GP4                                  # Notecard SDA Pin
door_sensor_pin = board.GP28                                 # Door Sensor Pin
wake_delay_time = 300                                        # Temperature Read Wake Time Delay


def readTempHumidity():
    print("Getting Temperature")
    # Get stable DHT11 reading
    temp = 0
    prevtemp = 0
    hum = 0
    prevhum = 0
    
    # Read DHT11 Sensor once a second up to ten times until there is two consecutive readings
    # with the same value for both, to ensure sensor is stable
    loopcount = 0
    while (loopcount < 10):
        sensor.measure()
        temp = sensor.temperature
        hum = sensor.humidity
        if (loopcount !=0 and temp == prevtemp and hum == prevhum):
            validTemp = True
            break
        
        prevtemp = temp
        prevhum = hum
        loopcount += 1
        time.sleep(1)
    
    if validTemp:
        print("Got Valid Temperature and Humidity Reading\n")
        # Don't sync, as we might add a door reading note too.
        note.add(nCard,file = "readings.qo", body = {"temperature": temp, "humidity": hum}, port = 10)


sensor = adafruit_dht.DHT11(dht_11_pin)

getTemp = False
getDoor = False
validTemp = False
doSleep = False

# Create I2C instance
i2c = busio.I2C(scl=scl_pin, sda=sda_pin)

# Connect to Notecard
print("Setup Notecard")
nCard = notecard.OpenI2C(i2c, 0, 0)

if productUID == "":
    raise AssertionError("productUID must be set")

hub.set(nCard, product=productUID, mode="periodic", outbound=60, inbound=60)

door = digitalio.DigitalInOut(door_sensor_pin)
door.direction=digitalio.Direction.INPUT
door.pull=digitalio.Pull.UP

# See if this is a first start or a wakeup
wake_time = time.monotonic()
if not alarm.wake_alarm:
    print("First Boot")
    # if alarm object - first time startup setup templates, and all data
    rsp = note.template(nCard, file="readings.qo", body={"temperature": 11, "humidity": 21}, compact = True, port = 10)
    rsp = note.template(nCard, file="door.qo", body={"open": True, "closed": True}, compact = True, port = 11)

    # Force temperature collection on First Boot.
    getTemp = True

else:
    # Check how we woke up    
    if isinstance(alarm.wake_alarm, alarm.time.TimeAlarm):
        print("Timeout Wakeup")
        getTemp = True
    else:
        print("Door Wakeup. Door Value: ", door.value)
        # if door is open(true), send note even is timeout not set
        if door.value:
            print("Sending Door Open note")
            note.add(nCard,file = "door.qo", body = {"open": door.value, "closed": not door.value}, port = 11, sync = True)
        else:
            doSleep = True

if getTemp:
    readTempHumidity()

# Always 
note.add(nCard,file = "door.qo", body = {"open": door.value, "closed": not door.value}, port = 11)
hub.sync(nCard)

if door.value:
    print("Door Open")
    
while not doSleep:
    # Door is currently open
    # idle not full sleep, until door closed
    time.sleep(1)
    if time.monotonic() >= wake_time + wake_delay_time:
        # Door has been open longer than temperature read time delay, read and send temp and reset time.
        wake_time = time.monotonic()
        readTempHumidity()
        hub.sync(nCard)
    if not door.value:
        print("Door Closed")
        # door now closed
        # send note
        note.add(nCard,file = "door.qo", body = {"open": door.value, "closed": not door.value}, port = 11, sync = True)
        # let sleep
        time.sleep(1)
        doSleep = True


# Now to sleep until timeout, or until door opened.
# Reset state variables.
doSleep = False
getTemp = False
# Remove door pin so it can be used for Wakeup Pin Alarm
door.deinit()

print("Sleeping\n")
pin_alarm_door = alarm.pin.PinAlarm(pin=door_sensor_pin, value=True, edge=True)
time_alarm = alarm.time.TimeAlarm(monotonic_time=wake_time + wake_delay_time) # Wake 60 minutes after last wake time or temperature read (if door left open).
alarm.exit_and_deep_sleep_until_alarms(pin_alarm_door, time_alarm)