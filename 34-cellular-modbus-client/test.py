import argparse
import json
from notehub_token import get_token
from pathlib import Path
import requests
import shlex
import subprocess
import sys
import time
import os

test_cases = [
    {
        "name": "Read coils",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 0,
                "func": 1,
                "data": {
                    "addr": 0,
                    "num_bits": 16
                }
            }
        },
        "response": {
            "bits": [250, 112],
            "seq_num": 0
        }
    },
    {
        "name": "Read discrete inputs",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 1,
                "func": 2,
                "data": {
                    "addr": 0,
                    "num_bits": 16
                }
            }
        },
        "response": {
            "bits": [173, 222],
            "seq_num": 1
        }
    },
    {
        "name": "Write coils",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 2,
                "func": 15,
                "data": {
                    "addr": 0,
                    "num_bits": 16,
                    "coil_bytes": [99, 171]
                }
            }
        },
        "response": {
            "seq_num": 2
        }
    },
    {
        "name": "Read back written coils",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 2,
                "func": 1,
                "data": {
                    "addr": 0,
                    "num_bits": 16,
                }
            }
        },
        "response": {
            "bits": [99, 171],
            "seq_num": 2
        }
    },
    {
        "name": "Write single coil",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 3,
                "func": 5,
                "data": {
                    "addr": 2,
                    "val": 1,
                }
            }
        },
        "response": {
            "seq_num": 3
        }
    },
    {
        "name": "Read back written coil",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 4,
                "func": 1,
                "data": {
                    "addr": 2,
                    "num_bits": 1,
                }
            }
        },
        "response": {
            "bits": [1],
            "seq_num": 4
        }
    },
    {
        "name": "Read holding registers",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 5,
                "func": 3,
                "data": {
                    "addr": 0,
                    "num_regs": 2
                }
            }
        },
        "response": {
            "regs": [291, 43981],
            "seq_num": 5
        }
    },
    {
        "name": "Read input registers",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 6,
                "func": 4,
                "data": {
                    "addr": 0,
                    "num_regs": 2
                }
            }
        },
        "response": {
            "regs": [17767, 35243],
            "seq_num": 6
        }
    },
    {
        "name": "Write holding registers",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 7,
                "func": 16,
                "data": {
                    "addr": 2,
                    "vals": [4369, 8738]
                }
            }
        },
        "response": {
            "seq_num": 7
        }
    },
    {
        "name": "Read back written holding registers",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 8,
                "func": 3,
                "data": {
                    "addr": 2,
                    "num_regs": 2
                }
            }
        },
        "response": {
            "regs": [4369, 8738],
            "seq_num": 8
        }
    },
    {
        "name": "Write single holding register",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 9,
                "func": 6,
                "data": {
                    "addr": 4,
                    "val": 199
                }
            }
        },
        "response": {
            "seq_num": 9
        }
    },
    {
        "name": "Read back written holding register",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 10,
                "func": 3,
                "data": {
                    "addr": 4,
                    "num_regs": 1
                }
            }
        },
        "response": {
            "regs": [199],
            "seq_num": 10
        }
    },
    {
        "name": "Read coils from bad address",
        "request": {
            "body": {
                "server_addr": 1,
                "seq_num": 11,
                "func": 1,
                "data": {
                    "addr": 999,
                    "num_bits": 16
                }
            }
        },
        "response": {
            "error": "Illegal data address",
            "seq_num": 11
        }
    },
    {
        "name": "Missing server address",
        "request": {
            "body": {
                "seq_num": 12,
                "func": 1,
                "data": {
                    "addr": 999,
                    "num_bits": 16
                }
            }
        },
        "response": {
            "error": "No server_addr field in request.",
            "seq_num": 12
        }
    },
    {
        "name": "Missing sequence number",
        "request": {
            "body": {
                "server_addr": 1,
                "func": 1,
                "data": {
                    "addr": 999,
                    "num_bits": 16
                }
            }
        },
        "response": {
            "error": "No seq_num field in request.",
            "seq_num": -1
        }
    },
]


def send_request(access_token, project_uid, device_uid, request):
    headers = {
        'Authorization': f'Bearer {access_token}',
    }
    url = f"https://api.notefile.net/v1/projects/{project_uid}/devices/{device_uid}/notes/requests.qi"

    notehub_response = requests.post(url, headers=headers, json=request)
    if notehub_response.ok:
        print(f"Request added to requests.qi.")
    else:
        raise RuntimeError(
            f"Failed to add request to requests.qi ({notehub_response.status_code}): {notehub_response.text}"
        )


def get_response(access_token, project_uid, device_uid, after_time):
    url = f'https://api.notefile.net/v1/projects/{project_uid}/events?pageSize=1&startDate={after_time}'
    params = {'files': 'responses.qo'}
    headers = {'Authorization': f'Bearer {access_token}'}

    max_num_tries = 5
    tries = 0
    while tries != max_num_tries:
        response = requests.get(url, params=params, headers=headers)
        response_json = response.json()
        num_events_received = len(response_json["events"])
        tries += 1

        if num_events_received > 0:
            print("Got response.")
            break
        else:
            sleep_seconds = 3
            print(
                f"Waiting for response. Checking again in {sleep_seconds} seconds..."
            )
            time.sleep(sleep_seconds)

    if num_events_received != 1:
        raise RuntimeError(
            f"Tried {max_num_tries} times to get response but failed.")

    return response_json['events'][0]['body']


def run_tests(project_uid, device_uid, client_id, client_secret):
    script_directory = Path(__file__).resolve().parent
    token_file = Path(script_directory, "access_token")
    token = get_token(project_uid, client_id, client_secret, token_file)

    for case in test_cases:
        test_case_name = case['name']
        print(f'\nRunning test case "{test_case_name}".')
        start_time = int(time.time())
        send_request(token, project_uid, device_uid, case['request'])
        response = get_response(token, project_uid, device_uid, start_time)
        expected_response = case['response']

        if response != expected_response:
            print("Received response differs from expected.")
            print(f"Received: {response}")
            print(f"Expected: {expected_response}")

            raise RuntimeError("Received response differs from expected.")
        else:
            print("Response matches expected.")


def start_server(serial_port):
    script_directory = Path(__file__).resolve().parent
    server_log_file = Path(script_directory, "modbus_server.log")

    with open(server_log_file, 'w') as log_file:
        process = subprocess.Popen(
            shlex.split(f"python server.py --log debug --port {serial_port}"),
            stdout=log_file,
            stderr=log_file)

    return process


def main(args):
    print("Starting Modbus server simulator...")
    server_process = start_server(args.serial_port)
    # Give the server a couple seconds to spin up.
    time.sleep(3)

    try:
        run_tests(args.project_uid, args.device_uid, args.client_id,
                  args.client_secret)
    finally:
        print("Killing server...")
        server_process.terminate()

    print("All tests passed!")


if __name__ == '__main__':
    serial_port = os.environ.get("NF34_SERIAL_PORT")
    client_id = os.environ.get("NF34_CLIENT_ID")
    client_secret = os.environ.get("NF34_CLIENT_SECRET")
    device_uid = os.environ.get("NF34_DEVICE_UID")
    project_uid = os.environ.get("NF34_PROJECT_UID")

    parser = argparse.ArgumentParser(
        description='Run the Notecard-based Modbus client tests.')
    parser.add_argument(
        '--serial-port',
        required=not serial_port,
        help='The serial port '
        'corresponding to the USB to RS-485 converter (e.g. /dev/ttyUSB0. The '
        'Modbus server will communicate with the client over this port.')
    parser.add_argument('--project-uid',
                        required=not project_uid,
                        help='The ProjectUID of the Notehub project.')
    parser.add_argument('--device-uid',
                        required=not device_uid,
                        help='The DeviceUID of the Notecard.')
    parser.add_argument(
        '--client-id',
        required=not client_id,
        help='The client ID associated with the Notehub project.')
    parser.add_argument(
        '--client-secret',
        required=not client_secret,
        help='The client secret associated with the Notehub project.')
    args = parser.parse_args()

    args.serial_port = args.serial_port or serial_port
    args.client_id = args.client_id or client_id
    args.client_secret = args.client_secret or client_secret
    args.device_uid = args.device_uid or device_uid
    args.project_uid = args.project_uid or project_uid

    main(args)
