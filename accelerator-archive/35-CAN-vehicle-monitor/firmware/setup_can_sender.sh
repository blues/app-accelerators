#!/bin/bash

#
# Run this script on Linux to set up the interface for sending CAN bus packets
# using this USB to CAN converter from Innomaker:
#
# https://www.amazon.com/gp/product/B09K3LL93Q/
#
# This script manipulates interfaces, so it needs root access. When run, it will
# prompt the user for their password in order to use sudo.
#
# See https://github.com/INNO-MAKER/usb2can for more from Innomaker, including
# code for sending CAN bus commands with other operating systems (e.g. Windows).
#
# Usage: ./setup_can_sender.sh [-d|--device DEVICE] [-b|--baud-rate RATE]
#
# Options:
#    -d|--device:    The interface name of the USB to CAN converter (e.g. can0).
#                    (default: can0)
#    -b|--baud-rate: The baud rate of the CAN bus. Must match the other devices
#                    on the bus.
#                    (default: 250000)
#

function run_cmd() {
    $@
    if [ $? != 0 ]; then
        echo "Command $@ failed."
        exit 1
    fi
}

DEVICE="can0"
BAUD_RATE=250000
while [[ $# -gt 0 ]]; do
  case $1 in
    -d|--device)
      DEVICE="$2"
      shift
      shift
      ;;
    -b|--baud-rate)
      BAUD_RATE="$2"
      shift
      shift
      ;;
  esac
done

run_cmd "sudo ifconfig $DEVICE down"
run_cmd "sudo ip link set $DEVICE type can bitrate $BAUD_RATE"
run_cmd "sudo ifconfig $DEVICE txqueuelen 100000"
run_cmd "sudo ifconfig $DEVICE up"

echo "CAN sender interface ready."
