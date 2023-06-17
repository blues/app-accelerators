#!/bin/bash

#
# This script builds all PlatformIO-based firmware in the repo.
#

script_dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
root_dir=$script_dir/..

pio_app_dirs=(
    "$root_dir/01-indoor-floor-level-tracker/firmware"
    "$root_dir/04-low-power-digital-signage/firmware"
    "$root_dir/08-power-quality-monitor/firmware"
    "$root_dir/09-valve-monitor/firmware/arduino"
    "$root_dir/10-flow-rate-monitor/firmware"
    "$root_dir/11-generator-activity-monitor/firmware"
    "$root_dir/12-remote-power-control/firmware"
    "$root_dir/13-tool-usage-cycle-tracking/firmware"
    "$root_dir/18-temperature-and-humidity-monitor/firmware/arduino"
    "$root_dir/35-CAN-vehicle-monitor/firmware"
)

for app_dir in ${pio_app_dirs[@]}; do
    echo "Building firmware in $app_dir..."

    pio run --project-dir $app_dir
    ret=$?
    if [ $ret -ne 0 ]; then
        printf "pio run failed (%d).\n" "$ret"
        exit $ret
    fi
done

echo "All PlatformIO-based firmware built successfully!"
