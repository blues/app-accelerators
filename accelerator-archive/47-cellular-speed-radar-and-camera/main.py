import sys
from machine import I2C, Pin, RTC, UART
import sensor
import time
import tf
import math
from collections import namedtuple

# Add /lib to sys.path so that we can import notecard.
sys.path.append('/lib')
import notecard

Config = namedtuple('Config', 'confidence_threshold speed_limit')

# TODO: Restore to 0.95.
#default_confidence_threshold = 0.95
default_confidence_threshold = 0.90
# default_speed_limit = 40
# TODO: Restore to 40.
default_speed_limit = 5

# Set to False to disable debug logging.
debug = True

def setup_notecard(product_uid):
    # Setup I2C channel for communicating with the Notecard.
    i2c = I2C(2)
    card = notecard.OpenI2C(i2c, notecard.NOTECARD_I2C_ADDRESS, 0, debug=debug)
    card.SetAppUserAgent({"app": "nf47"})
    req = {
        'req': 'hub.set',
        'product': product_uid,
        'mode': 'periodic'
    }
    card.Transaction(req)

    # Establish a template Notefile, speeders.qo, where we'll push speeder
    # events.
    #
    # The special values 21 and 24 come from here:
    # https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/#understanding-template-data-types
    #
    # "21 - for a 1 byte unsigned integer (e.g. 0 to 255)."
    # "24 - for a 4 byte unsigned integer (e.g. 0 to 4294967295)."
    req = {
        'req': 'note.template',
        'file': 'speeders.qo',
        'body': {
            'time': 24,
            'speed': 21
        }
    }
    card.Transaction(req)

    return card


def convert_datetime_to_unix_time(datetime):
    year = datetime[0]
    month = datetime[1]
    day = datetime[2]
    hour = datetime[4]
    minute = datetime[5]
    second = datetime[6]
    weekday = datetime[3]

    return time.mktime((year, month, day, hour, minute, second, weekday, 0))


def add_speeder(card, timestamp, speed):
    req = {
        'req': 'note.add',
        'file': 'speeders.qo',
        'body': {
            'time': convert_datetime_to_unix_time(timestamp),
            'speed': speed
        }
    }
    card.Transaction(req)


def setup_camera():
    sensor.reset()
    sensor.set_pixformat(sensor.GRAYSCALE)
    sensor.set_framesize(sensor.QQVGA)
    sensor.set_windowing((120, 120))
    sensor.skip_frames(time=2000)


def fetch_env_vars(card, names):
    req = {'req': 'env.get', 'names': names}
    rsp = card.Transaction(req)
    env_vars = rsp['body']

    if debug:
        print('Environment variables:')
        for var, val in env_vars.items():
            print(f'  {var}: {val}')

    return env_vars


def refresh_config(card, old_config, env_vars):
    if 'speed_limit' in env_vars:
        limit = int(env_vars['speed_limit'])
        if limit <= 0:
            print('speed_limit must greater than 0.')
            speed_limit = old_config.speed_limit
        else:
            speed_limit = limit
    else:
        # Default speed limit if no speed_limit env var.
        speed_limit = default_speed_limit

    if 'confidence_threshold' in env_vars:
        threshold = float(env_vars['confidence_threshold'])
        if threshold <= 0.0 or threshold >= 1.0:
            print('confidence_threshold must greater than 0 and less than 1.')
            confidence_threshold = old_config.confidence_threshold
        else:
            confidence_threshold = threshold
    else:
        # Default confidence threshold if no confidence_threshold env var.
        confidence_threshold = default_confidence_threshold

    return Config(confidence_threshold=confidence_threshold,
                  speed_limit=speed_limit)


def setup_rtc(card):
    print('Fetching time from Notecard with card.time...')
    req = {'req': 'card.time'}
    rsp = card.Transaction(req)

    wait_secs = 3
    max_retries = 40
    retries = 0
    while 'time' not in rsp and retries < max_retries:
        print(
            f'Notecard doesn\'t know the time yet. Will try again in {wait_secs} seconds...'
        )
        retries += 1
        # Try again in 3 seconds
        time.sleep(3)
        rsp = card.Transaction(req)

    if 'time' not in rsp:
        raise RuntimeError('Timed out trying to get time from Notecard.')

    notecard_time = rsp['time']
    print(f'Got UNIX Epoch time: {notecard_time}. Setting RTC time...')
    # time.gmtime uses an epoch of 2000-01-01 00:00:00 UTC while card.time uses
    # 1970-01-01 00:00:00 UTC. So, the epoch used by the Notecard is ahead of
    # the epoch used by MicroPython by 946684800 seconds. Nothing needs to be
    # done here, unless one wants to print the time.
    adjusted_time = notecard_time
    gmtime = time.gmtime(adjusted_time)
    year = gmtime[0]
    month = gmtime[1]
    day = gmtime[2]
    hour = gmtime[3]
    minute = gmtime[4]
    second = gmtime[5]
    weekday = gmtime[6]
    rtc = RTC()
    rtc.datetime((year, month, day, weekday, hour, minute, second, 0))

    print('RTC time set.')

    return rtc


def euclidean_distance(x1, y1, x2, y2):
    return math.sqrt((x2 - x1)**2 + (y2 - y1)**2)


class OpsModule:
    def __init__(self, uart_id, baud_rate=19200):
        self.uart = UART(uart_id, baud_rate)
        # Reset OPS module to factor defaults.
        self._send_cmd('AX')
        # Wait a second after resetting before further configuration. From the
        # data sheet: "To ensure proper saving of the data into flash it’s
        # recommended to wait 1 second before issuing any additional commands to
        # the sensor after issuing the A! and AX commands."
        time.sleep(1)
        # Return speed in miles per hour.
        self._send_cmd('US')
        # From the datasheet: "If measured data does not meet filtering criteria
        # sensor will report out a character with every sampling interval. BZ
        # will report zero value."
        self._send_cmd('BZ')

    def _send_cmd(self, cmd):
        if debug:
            print(f'Sending command to OPS module: {cmd}.')
        # Send the command bytes to the OPS module over the serial connection.
        self.uart.write(str.encode(cmd))

        rx_tries = 5
        rx_success = False
        while not rx_success and rx_tries > 0:
            rx_bytes = self.uart.readline()
            if rx_bytes is not None and len(rx_bytes) != 0:
                rx_str = str(rx_bytes)

                if rx_str is not None:
                    print(f'Response from OPS module: {rx_str}.')

                    if '{' in rx_str:
                        # If there's a response and it contains a '{' (i.e. it's
                        # JSON), it's valid.
                        rx_success = True
                        break

            # Wait a second and try again, if tries not exhausted.
            time.sleep(1)
            rx_tries -= 1

        if not rx_success:
            raise Exception(f'Failed to send command to OPS module. Command: {cmd}')


    def get_speed(self):
        while True:
            rx_bytes = self.uart.readline()
            if rx_bytes is not None and len(rx_bytes) != 0:
                rx_str = str(rx_bytes)

                if not '{' in rx_str:
                    # If there's no '{' (i.e. it's not JSON), speed data found.
                    try:
                        # The speed value can be negative, presumably to
                        # indicated direction, so we take the absolute value. We
                        # then round to the nearest integer.
                        return round(abs(float(rx_bytes)))
                    except ValueError:
                        # Bad data, try again.
                        continue

def main():
    # Uncomment this line and replace com.your-company:your-product-name with
    # your ProductUID.
    # TODO: Remove my ProductUID.
    product_uid = 'com.blues.hroche:speed_trap'
    card = setup_notecard(product_uid)
    rtc = setup_rtc(card)

    env_var_names = ['confidence_threshold', 'speed_limit']
    config = Config(confidence_threshold=default_confidence_threshold,
                    speed_limit=default_speed_limit)
    config = refresh_config(card, config, fetch_env_vars(card, env_var_names))
    last_env_refresh_ms = time.ticks_ms()
    # Check for environment variable updates every 5 minutes.
    env_refresh_period_ms = 5 * 60 * 1000

    setup_camera()

    labels, net = tf.load_builtin_model('trained')

    past_detection_centers = []
    past_detection_refresh_val = 10
    stationary_threshold = 10.0

    # The ID of the UART peripheral connected to the OPS module.
    uart_id = 1
    # The default baud rate for communicating with the OPS module is 19200.
    ops = OpsModule(uart_id, 19200)

    # Main processing loop.
    while True:
        # Get a snapshot from the OpenMV camera.
        img = sensor.snapshot()
        # Get the speed from the OPS module.
        speed = ops.get_speed()

        detection_thresholds = [(math.ceil(config.confidence_threshold * 255), 255)]
        # Run the image through the car detection model and get the list of
        # detections.
        detection_list = net.detect(img, thresholds=detection_thresholds)[1]
        # Iterate over all the car detections.
        for d in detection_list:
            [x, y, w, h] = d.rect()
            # Compute the center of the detection.
            center_x = math.floor(x + (w / 2))
            center_y = math.floor(y + (h / 2))

            is_stationary = False
            # past_detection_centers is a list of detection centers from the
            # recent past. If the current detection center is within a certain
            # distance (stationary_threshold) of a past detection center, the
            # current detection is most likely a stationary car, and we should
            # ignore it.
            for c in past_detection_centers:
                if euclidean_distance(c[0], c[1], center_x, center_y) <= stationary_threshold:
                    if debug:
                        print(f'Stationary car. Updated detection center: ({center_x}, {center_y}).')

                    # If the detection is for a stationary car, update its
                    # center coordinates in past_detection_centers, and refresh
                    # its counter. This counter is refreshed to 10 every time
                    # the stationary car is detected again. If it isn't
                    # detected, the counter is decremented. Once the counter
                    # hits zero, the center gets evicted from
                    # past_detection_centers.
                    c[0] = center_x
                    c[1] = center_y
                    c[2] = past_detection_refresh_val
                    is_stationary = True
                    # We know the car is stationary, so we don't need to bother
                    # checking it against the rest of past_detection_centers.
                    break

            # If the detection isn't for a stationary car, we check its speed.
            if not is_stationary:
                # We already have the speed, but measure it 4 more times, taking
                # the largest value. This is helpful when the cars are far from
                # the OPS module and the initial measurement might not be
                # accurate as a result.
                for _ in range(4):
                    latest_speed = ops.get_speed()
                    if latest_speed > speed:
                        speed = latest_speed

                if debug:
                    print(f'Car detected @ ({center_x}, {center_y}).')
                    print(f'Speed: {speed}.')

                # Add a note to speeders.qo if the detected car exceeded the
                # configured speed limit.
                if speed >= config.speed_limit:
                    if debug:
                        print(f'Speed limit ({config.speed_limit} mph) exceeded. Adding speeder note.')

                    add_speeder(card, rtc.datetime(), speed)
                    # If we caught a speeder, don't bother looking at any other
                    # car detections, as we can't identify individual cars and
                    # their respective speeds anyway. We can only detect if
                    # there was a car and if there was an excessive speed.
                    break

            if debug:
                # Draw a circle around the center of the detection.
                img.draw_circle((center_x, center_y, 12), thickness=2)

            # Add this new detection to past_detection_centers.
            past_detection_centers.append([center_x, center_y, past_detection_refresh_val])

        # past_detection_centers needs to be culled of stale centers, or it'll
        # grow indefinitely and we'll run out of memory. As noted above, if one
        # of the past centers hasn't been detected for
        # past_detection_refresh_val consecutive iterations, it gets evicted.
        for j, c in enumerate(past_detection_centers):
            c[2] -= 1
            if c[2] == 0:
                if debug:
                    print(f'Removing past detection center ({c[0]}, {c[1]}).')
                past_detection_centers.pop(j)


        # Periodically check for environment variable updates.
        if time.ticks_diff(time.ticks_ms(),
                           last_env_refresh_ms) >= env_refresh_period_ms:
            config = refresh_config(card, config,
                                    fetch_env_vars(card, env_var_names))
            last_env_refresh_ms = time.ticks_ms()


main()
