# Zephyr Firmware

This is a Zephyr-based implementation of the temperature and humidity monitor project's firmware.

## Usage

From the command line, you'll need to pull in the [note-c](https://github.com/blues/note-c) and [notecard-env-var-manager](https://github.com/blues/notecard-env-var-manager) submodules that the firmware depends on:

```sh
$ git submodule update --init 18-temperature-and-humidity-monitor/firmware/zephyr/src/note-c
$ git submodule update --init 18-temperature-and-humidity-monitor/firmware/zephyr/src/notecard_env_var_manager
```

To build, flash, and debug the firmware, you will need

* [Visual Studio Code (VS Code)](https://code.visualstudio.com/).
* [Docker and the VS Code Dev Containers extension](https://code.visualstudio.com/docs/devcontainers/containers). The Dev Containers documentation will take you through the process of installing both Docker and the extension for VS Code.

These instructions will defer parts of the build process to the [Blues Zephyr SDK documentation](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk) (the "Zephyr SDK docs"). Though these instructions are for the [note-zephyr repo](https://github.com/blues/note-zephyr), the same patterns for building the code are used here.

1. Start VS Code and select File > Open Folder and pick this folder (`18-temperature-and-humidity-monitor/firmware/zephyr`).
1. Follow the instructions for your OS in the [Zephyr SDK docs' "Building the Dev Container" section](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk/#building-the-dev-container).
1. Edit `src/main.c` to associate the firmware with your Notehub project. Uncomment `// #define PRODUCT_UID "com.your-company:your-product-name"` and replace `com.your-company:your-product-name` with [your project's ProductUID](https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid).
1. Follow the [Zephyr SDK docs' "Building and Running" section](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk/#building-and-running).

Now, the code should be running on the Swan. If you want to look at the serial logs or debug the code, check out the [Zephyr SDK docs' "Debugging" section](https://dev.blues.io/tools-and-sdks/firmware-libraries/zephyr-sdk/#debugging).

And refer to this project's [Operation documentation](../../README.md#operation) for details on the data your device is now sending.

## Additional Resources

Though we only support using the VS Code + Dev Containers workflow described here, you can also install Zephyr and its dependencies locally. You can build, flash, and debug code in your native environment using Zephyr's [`west` tool](https://docs.zephyrproject.org/latest/develop/west/index.html). See [Zephyr's Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) for more information.

## Developer Notes

The Notecard hooks in `src/notecard.c` come from [note-zephyr](https://github.com/blues/note-zephyr).
