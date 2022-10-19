# Parking Lot Help Request Firmware

This project is meant to be host-free because the Notecard itself can be configured to send notes to Notehub when the help button is pressed.

So instead of true firmware setup, this will guide you through creating a reusable Notecard configuration script which can be executed through the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/) or through the online webREPL available at https://dev.blues.io/notecard-playground/.

### Configure the Notecard

There are two main ways to program a standalone Notecard that will not be interacting with a host microcontroller: [the Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/): a downloadable CLI designed for the Notecard, or the [Notecard Playground](https://dev.blues.io/notecard-playground/): an in-browser terminal that emulates much of the functionality of the Notecard CLI - both of which are accessible through the Blues [Developer Experience site](https://www.dev.blues.io).

> **NOTE**: If you do choose to use the in-browser terminal, you'll need to do so on a Chrome-based web browser like Chrome or Edge that supports the web serial API. 

![Screenshot of the in-browser Notecard Playground on dev.blues.io](./readme-notecard-playground.png)

The instructions to configure the Notecard will assume the Notecard CLI has been downloaded and installed locally, but will also work with a very slight modification if you choose to use the in-browser Notecard Playground instead.

1. Before you can configure your Notecard, you must set up a free Notehub.io account and create a project where the Notecard will send its data to. Follow the [Set Up Notehub quickstart](https://dev.blues.io/quickstart/notecard-quickstart/notecard-and-notecarrier-f/#set-up-notehub) to sign up and create your first project. Be sure to copy the Project UID after creating it - you'll need it for the configuration script shortly.
2. Download and install the [Notecard CLI](https://dev.blues.io/tools-and-sdks/notecard-cli/#installation) from the Blues Developer Experience site.
3. Plug the Notecarrier containing the Notecard into your computer with a USB-A to microUSB cable.
4. Copy the JSON file example in this folder named `parking-lot-help-request-config-script.json` to your local machine.
5. Replace the `product` and `sn` placeholders in that script with your Notehub Project UID and preferred Notecard serial number (e.g. `Lot G`), and resave the file.
6. Upload the configuration script to the Notecard via the Notecard CLI by running the following command:

```bash 
$ notecard -setup parking-lot-help-request-config-script.json`
```
7. This wil execute all the commands line by line against the Notecard and then it will be all set as far as firmware configuration goes.

If you'd prefer to upload this script using the in-browser Notecard Playground instead of downloading the CLI, simply connect your Notecard via the microUSB and copy/paste the whole config file as one block of text like in the screenshot below.

![Pasting in the Notecard config script in the in-browser Notecard playground](./readme-notecard-playground-script.png)

### Transform Notehub data with JSONata for Twilio

Now, the second part of this document covers the data transformation that will need to happen inside of the Notehub project before a button press event is routed out to Twilio to trigger an SMS alert.

```json
(
    $from := "+1800XXXXXXX";
    $to := "+1404XXXXXXX";
    $best_device_id := best_id ? best_id : device;
    $body := function(){
        (
            $join([
                "Alert! ",
                $best_device_id,
                " requires assistance. The exact coordinates are ",
                $string($round(best_lon, 6)),
                ", ",
                $string($round(best_lat, 6)),
                " at ",
                $fromMillis(when * 1000,
                "[M01]/[D01]/[Y0001] [h#1]:[m01][P]",
                "-0400"),
                " ET."
            ])
        )
    };
    $result := "&Body=" & $body() & "&From=" & $from & "&To=" & $to & "&";
)
```