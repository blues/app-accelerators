import sys
from machine import I2C, Pin, RTC
import sensor
import time
import tf
import math
from collections import namedtuple

# Add /lib to sys.path so that we can import notecard.
sys.path.append('/lib')
import notecard

Config = namedtuple('Config', 'confidence_threshold publish_period')

default_publish_period = 5
default_confidence_interval = 95


def setup_notecard(product_uid):
    # Setup I2C channel for communicating with the Notecard.
    i2c = I2C(2)
    card = notecard.OpenI2C(i2c, notecard.NOTECARD_I2C_ADDRESS, 0, debug=True)
    card.SetAppUserAgent({"app": "nf40"})
    req = {
        'req': 'hub.set',
        'product': product_uid,
        'mode': 'periodic',
        'outbound': default_publish_period
    }
    card.Transaction(req)

    # Establish a template Notefile, events.qo, where we'll push car events.
    #
    # The special value 24 comes from here:
    # https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/#understanding-template-data-types
    #
    # "24 - for a 4 byte unsigned integer (e.g. 0 to 4294967295). Available as
    #  of v3.3.1."
    req = {
        'req': 'note.template',
        'file': 'events.qo',
        'body': {
            'start': 24,
            'end': 24
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


def add_event(card, start_time, end_time):
    req = {
        'req': 'note.add',
        'file': 'events.qo',
        'body': {
            'start': convert_datetime_to_unix_time(start_time),
            'end': convert_datetime_to_unix_time(end_time)
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

    print('Environment variables:')
    for var, val in env_vars.items():
        print(f'  {var}: {val}')

    return env_vars


def refresh_config(card, old_config, env_vars):
    if 'publish_period' in env_vars:
        period = int(env_vars['publish_period'])
        if period <= 0:
            print('publish_period must greater than 0.')
            publish_period = old_config.publish_period
        else:
            publish_period = period

            if publish_period != old_config.publish_period:
                # Publish period changed. Need to change outbound sync interval
                # accordingly.
                req = {'req': 'hub.set', 'outbound': publish_period}
                card.Transaction(req)
    else:
        # Default publish period if no publish_period env var.
        publish_period = default_publish_period

    if 'confidence_threshold' in env_vars:
        threshold = float(env_vars['confidence_threshold'])
        if threshold <= 0.0 or threshold >= 1.0:
            print('confidence_threshold must greater than 0 and less than 1.')
            confidence_threshold = old_config.confidence_threshold
        else:
            confidence_threshold = threshold
    else:
        # Default confidence threshold if no confidence_threshold env var.
        confidence_threshold = 0.95

    return Config(confidence_threshold=confidence_threshold,
                  publish_period=publish_period)


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

    print(f'RTC time set.')

    return rtc


def main():
    # Uncomment this line and replace com.your-company:your-product-name with your
    # ProductUID.
    # product_uid = 'com.your-company:your-product-name'
    card = setup_notecard(product_uid)
    rtc = setup_rtc(card)

    env_var_names = ['publish_period', 'confidence_threshold']
    config = Config(confidence_threshold=default_confidence_interval,
                    publish_period=default_publish_period)
    config = refresh_config(card, config, fetch_env_vars(card, env_var_names))
    last_env_refresh_ms = time.ticks_ms()
    env_refresh_period_ms = 3 * 60 * 1000

    setup_camera()

    labels, net = tf.load_builtin_model('trained')
    clock = time.clock()

    event_start_threshold = 1
    event_end_threshold = 12
    consecutive_detections = 0
    consecutive_no_detections = 0
    event_count = 0
    event_happening = False

    while True:
        img = sensor.snapshot()

        for i, detection_list in enumerate(
                net.detect(img,
                           thresholds=[
                               (math.ceil(config.confidence_threshold * 255),
                                255)
                           ])):
            if (i == 0): continue  # background class
            if (len(detection_list) == 0):
                consecutive_detections = 0
                consecutive_no_detections += 1
            else:
                consecutive_detections += 1
                consecutive_no_detections = 0

                for d in detection_list:
                    [x, y, w, h] = d.rect()
                    center_x = math.floor(x + (w / 2))
                    center_y = math.floor(y + (h / 2))
                    print('Car detected @ (%d, %d).' % (center_x, center_y))
                    img.draw_circle((center_x, center_y, 12), thickness=2)

        if consecutive_detections >= event_start_threshold and not event_happening:
            event_start_time = rtc.datetime()
            print('Event started.')
            event_happening = True
            event_count += 1

        if event_happening and consecutive_no_detections >= event_end_threshold:
            event_end_time = rtc.datetime()
            print(f'Event ended. Event count is now {event_count}.')
            event_happening = False
            add_event(card, event_start_time, event_end_time)

        if time.ticks_diff(time.ticks_ms(),
                           last_env_refresh_ms) >= env_refresh_period_ms:
            config = refresh_config(card, config,
                                    fetch_env_vars(card, env_var_names))
            last_env_refresh_ms = time.ticks_ms()


main()
