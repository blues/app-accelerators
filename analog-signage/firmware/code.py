import time

import adafruit_requests
import board
import notecard
import socketpool
import json
import wifi

import feathers2
import vestaboard
from secrets import secrets

# Variables for environment variable polling
ENV_POLL_SECS = 1
next_poll_sec = 0
last_modified_time = 0

application_state = {
    "lines": {},
    "variables_updated": False
}

# Make sure the 2nd LDO is turned on
feathers2.enable_LDO2(True)

i2c = board.I2C()
card = notecard.OpenI2C(i2c, 0, 0, debug=False)

req = {"req": "hub.set", "mode": "continuous", "sync": True}
card.Transaction(req)


def fetch_environment_variables():
    req = {"req": "env.get", "names": ["line_1", "line_2", "line_3", "line_4"]}
    rsp = card.Transaction(req)

    if rsp is not None:
        env_vars = rsp["body"]

        for key in env_vars:
            try:
                line_var = json.loads(env_vars[key])
                if key not in application_state["lines"]:
                    application_state["lines"][key] = line_var
                    application_state["variables_updated"] = True
                elif application_state["lines"][key] != line_var:
                    application_state["lines"][key] = line_var
                    application_state["variables_updated"] = True
            except ValueError:
                print("Invalid JSON object in environment variable")


def poll_environment_variables(next_poll, last_modified):
    if time.monotonic() < next_poll:
        return False, next_poll, last_modified

    next_poll = time.monotonic() + ENV_POLL_SECS

    rsp = card.Transaction({"req": "env.modified"})

    if rsp is None:
        return False, next_poll, last_modified

    if last_modified_time == rsp["time"]:
        return False, next_poll, last_modified

    return True, next_poll, rsp["time"]


def map_env_vars():
    updated_board = vestaboard.base_board

    for key, value in application_state["lines"].items():
        row_state = []
        key_index = int(key[-1]) + 1

        if value['delay'] is 'true':
            row_state.extend([vestaboard.character_codes['red'], 0])
        else:
            row_state.extend([vestaboard.character_codes['green'], 0])

        for _, char in enumerate(value['city']):
            row_state.append(vestaboard.character_codes[char])
        row_state.extend([0, 0, 0])

        for _, char in enumerate(value['track']):
            row_state.append(vestaboard.character_codes[char])
        row_state.extend([0, 0, 0, 0, 0])

        for _, char in enumerate(value['time']):
            row_state.append(vestaboard.character_codes[char])
        row_state.extend([0,0,0,0,0])

        updated_board[key_index] = row_state

    return updated_board


def setup_board(req):
    print("Setting up Vestaboard")

    board_state = str(vestaboard.base_board)
    board_headers = {"X-Vestaboard-Local-Api-Key": secrets['vestaboard_key']}
    response = req.post(secrets['vestaboard_url'], headers=board_headers, data=board_state)

    print(response.status_code, ' - ' if response.text else '', response.text)


def update_board(req, board_data):
    print("Updating Vestaboard")

    board_headers = {"X-Vestaboard-Local-Api-Key": secrets['vestaboard_key']}
    response = req.post(secrets['vestaboard_url'], headers=board_headers, data=str(board_data))

    print(response.status_code, ' - ' if response.text else '', response.text)


fetch_environment_variables()

# Say hello
print("\nAnalog Display Demo")
print("-------------------\n")

# Turn on the internal blue LED
feathers2.led_set(True)

print("joining Wi-Fi network...")
wifi.radio.connect(ssid=secrets['ssid'], password=secrets['passwd'])

print("IP addr:", wifi.radio.ipv4_address)

pool = socketpool.SocketPool(wifi.radio)
request = adafruit_requests.Session(pool)

# For first run, set the board default display
setup_board(request)

while True:
    update_env_vars, next_poll_sec, last_modified_time = poll_environment_variables(next_poll_sec, last_modified_time)

    if update_env_vars:
        fetch_environment_variables()

        # Remap env vars text to Vestaboard character codes
        board_data = map_env_vars()

        # Update Display
        update_board(request, board_data)

        application_state["variables_updated"] = False
