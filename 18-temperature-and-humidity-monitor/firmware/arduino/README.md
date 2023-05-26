# Arduino Firmware

This is a Arduino-based implementation of the temperature and humidity monitor project's firmware.

## Usage

1. Open Visual Studio Code.
2. Click the PlatformIO icon on the left hand side and open this project's `firmware/arduino` folder with Quick Access > PIO Home > Open > Open Project.
3. Open the file `src/main.cpp`. Uncomment this line

```c
#define PRODUCT_UID "com.your-company:your-product-name"
```

and replace `com.your-company:your-product-name` with your ProductUID from [Notehub Setup](#notehub-setup).

4. Click the PlatformIO icon on the left hand side again and click "Build" under Project Tasks > bw_swan_r5 > General. You should see "SUCCESS" in the terminal output pane. The firmware image is now ready to be flashed to the Swan.
5. Press and hold the button labeled "BOOT" on the Swan, and, while holding BOOT, press and release the button labeled "RST", and finally release the BOOT button. The Swan is now ready to be flashed.
6. Click "Upload" under Project Tasks > bw_swan_r5 > General. You should see "SUCCESS" in the terminal output pane.

With the firmware returning, refer to this project's [Operation documentation](../../README.md#operation) for details on the data your device is now sending.
