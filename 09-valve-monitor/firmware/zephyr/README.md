This is a Zephyr-based implementation of the valve monitor project's firmware.

# Firmware Setup

Note: If you haven't already, follow the [hardware setup guide](../../#hardware).

1. Follow [Zephyr's Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).
1. `cd ~/zephyrproject`, assuming this is where you created the west workspace from step 1.
1. Create a new directory to hold the valve monitor code: `mkdir valve_monitor`.
1. Copy the contents of `~/app-accelerators/09-valve-monitor/firmware/zephyr` into this new directory: `cp -R ~/app-accelerators/09-valve-monitor/firmware/zephyr/* ./valve_monitor`. Note that this assumes you've cloned the `app-accelerators` repo into your home directory.
1. `cd valve_monitor`.
1. Modify `src/main.c` so that the `PRODUCT_UID` macro is a valid product UID corresponding to a project on [Notehub](https://notehub.io).
1. Build the code with `west build -b swan_r5`.
1. With the [STLINK-V3MINI](https://shop.blues.io/products/stlink-v3mini) connected to the Swan, flash the firmware onto the Swan with `west flash`.

If you want to debug the code running on the Swan, you can do so with gdb with `west debug`.

# Testing

Refer to the [testing section the Arduino documentation](../arduino/#testing).

# Developer Notes

The Notecard hooks in `src/notecard.c` come from [note-zephyr](https://github.com/blues/note-zephyr). [note-c](https://github.com/blues/note-c) was added here as a [git subtree](https://www.atlassian.com/git/tutorials/git-subtree).
