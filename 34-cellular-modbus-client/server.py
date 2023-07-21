"""
Pymodbus Simulator Example. Modified by Hayden Roche for Blues.

An example of using simulator datastore with json interface.

usage: server_simulator.py [-h]
                       [--log {critical,error,warning,info,debug}]
                       [--port PORT]

Command line options for examples

options:
  -h, --help            show this help message and exit
  --log {critical,error,warning,info,debug}
                        "critical", "error", "warning", "info" or "debug"
  --port PORT           the serial port to use
"""

import argparse
import asyncio
import logging

from pymodbus import pymodbus_apply_logging_config
from pymodbus.datastore import ModbusServerContext, ModbusSimulatorContext
from pymodbus.device import ModbusDeviceIdentification
from pymodbus.server import (StartAsyncSerialServer, StartAsyncTcpServer)
from pymodbus.transaction import (ModbusSocketFramer, ModbusRtuFramer)

_logger = logging.getLogger(__name__)

config = {
    "setup": {
        "co size": 100,
        "di size": 100,
        "hr size": 100,
        "ir size": 100,
        "shared blocks": False,
        "type exception": True,
        "defaults": {
            "value": {
                "bits": 0x0000,
                "uint16": 0,
                "uint32": 0,
                "float32": 0,
                "string": "X",
            },
            "action": {
                "bits": None,
                "uint16": None,
                "uint32": None,
                "float32": None,
                "string": None,
            },
        },
    },
    "invalid": [],
    "write": [[0, 3], [300, 304]],
    "bits": [{
        "addr": 0,
        "value": 0x70fa
    }, {
        "addr": 1,
        "value": 0x1234
    }, {
        "addr": 2,
        "value": 0x5678
    }, {
        "addr": 3,
        "value": 0x9abc
    }, {
        "addr": 100,
        "value": 0xdead
    }],
    "uint16": [{
        "addr": 200,
        "value": 0x4567
    }, {
        "addr": 201,
        "value": 0x89ab
    }, {
        "addr": 300,
        "value": 0x0123
    }, {
        "addr": 301,
        "value": 0xabcd
    }, {
        "addr": 302,
        "value": 0xffff
    }, {
        "addr": 303,
        "value": 0xffff
    }, {
        "addr": 304,
        "value": 0x0000
    }],
    "uint32": [],
    "float32": [],
    "string": [],
    "repeat": [],
}


def get_commandline(cmdline=None):
    """Read and validate command line arguments"""
    parser = argparse.ArgumentParser(description="Run server simulator.")
    parser.add_argument(
        "--log",
        choices=["critical", "error", "warning", "info", "debug"],
        help="set log level, default is info",
        default="info",
        type=str,
    )
    parser.add_argument("--port", help="serial port", required=True, type=str)
    args = parser.parse_args(cmdline)

    return args


def setup_simulator(setup=None, actions=None, cmdline=None):
    """Run server setup."""
    args = get_commandline(cmdline=cmdline)
    pymodbus_apply_logging_config(args.log)
    _logger.setLevel(args.log.upper())
    args.framer = ModbusRtuFramer

    _logger.info("### Create datastore")
    if not setup:
        setup = config
    context = ModbusSimulatorContext(setup, actions)
    args.context = ModbusServerContext(slaves=context, single=True)
    args.identity = ModbusDeviceIdentification(
        info_name={
            "VendorName": "Pymodbus",
            "ProductCode": "PM",
            "VendorUrl": "https://github.com/pymodbus-dev/pymodbus/",
            "ProductName": "Pymodbus Server",
            "ModelName": "Pymodbus Server",
            "MajorMinorRevision": "test",
        })
    return args


async def run_server_simulator(args):
    """Run server."""
    _logger.info("### start server simulator")

    pymodbus_apply_logging_config(args.log.upper())
    await StartAsyncSerialServer(context=args.context,
                                 port=args.port,
                                 framer=args.framer,
                                 baudrate=115200)


if __name__ == "__main__":
    run_args = setup_simulator()
    asyncio.run(run_server_simulator(run_args), debug=True)
