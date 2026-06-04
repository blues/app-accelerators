# Lone Worker Panic & Fall Detection Safety Beacon

![Lone Worker Panic & Fall Detection Safety Beacon banner](banner.png)

<Note>

This reference application is intended to provide inspiration and help you get started quickly. It uses specific hardware choices that may not match your own implementation. Focus on the sections most relevant to your use case. If you'd like to discuss your project and whether it's a good fit for Blues, [feel free to reach out](https://blues.com/landing-pages/accelerators-contact-us/?accelerator=Lone%20Worker%20Panic%20%26%20Fall%20Detection%20Safety%20Beacon).

</Note>

This project is a wearable [safety assurance](https://blues.com/safety-assurance/) device for utility linemen, oilfield pumpers, field service technicians, and solo contractors. A single [Notecard for Skylo](https://shop.blues.com/products/notecard-for-skylo?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link) — one M.2 module that carries cellular, WiFi, and Skylo satellite radios and fails over between them automatically — turns a belt-clip enclosure into a cellular-first, satellite-backed distress beacon — detecting falls and accepting an explicit panic-button press — that can reach a dispatcher from the middle of nowhere, exactly where lone-worker incidents happen.

## 1. Project Overview

<Warning>

**Not a certified life-safety device.** This is a proof-of-concept reference design intended to demonstrate Blues hardware and firmware patterns. It is not a certified personal emergency response system (PERS), not a classified man-down or lone-worker protection device under any regulatory scheme, and has not been evaluated for fail-safe or safety-critical operation. It must **supplement, not replace** — established lone-worker safety procedures, mandatory check-in protocols, required PPE, and any regulatory or contractual safety obligations that apply to your operation. Never deploy this design as a sole means of worker protection.

</Warning>

**The problem.** A utility lineman working an isolated substation, an oilfield pumper checking a remote wellhead at night, a field-service tech in a basement boiler room at 2 AM — each of these workers shares a common vulnerability: if something goes wrong, nobody will know for hours. Lone-worker incidents don't always announce themselves. Falls from elevation are silent. Heart events are silent. The worker simply stops moving, and no one notices until a shift check-in is missed or a buddy does a welfare call.

The gap isn't awareness — most safety-conscious operations already require check-in procedures. The gap is *automatic, continuous* monitoring that doesn't depend on the worker remembering to push a button every 30 minutes. What these workers need is a device that monitors for the physical signatures of an incident — sudden free-fall, violent deceleration on impact, prolonged motionlessness after a fall, and raises an alarm without any action on the part of the worker. The panic button is a secondary escape valve: an explicit human override for situations where the physics don't look like a fall but the worker knows something is very wrong.

This project is that device — a wearable safety beacon built on two core detection modes: automatic fall detection and explicit panic-button input. The onboard Cygnet STM32L433 host runs a two-stage fall-detection algorithm on the LIS3DH accelerometer sampled at ~100 Hz (a 10-sample inner loop runs at 10 milliseconds intervals so free-fall phases as short as 80 milliseconds are always observed; Notecard I/O and state checks run at the outer ~10 Hz cadence; see [Section 6](#7-firmware-design) for the sampling design), monitors a held-down panic button with debounce logic, and drives a haptic motor to acknowledge every confirmed event. On fall or panic, the firmware immediately queues a compact emergency Note carrying the Notecard's cached location and transmits it with `sync:true` — no GPS wait before the alert goes out. A non-blocking background GPS search then runs without suspending fall or button monitoring; if a fresh fix arrives within the timeout window, a follow-up `beacon_location.qo` Note is queued with the event-time coordinates. See [Section 6](#7-firmware-design) for the full two-Note flow.

**Why Notecard.** Cellular coverage is not a given for the environments where lone-worker incidents happen. A substation at the edge of a service area, a gas compressor station in a rural county, a mine portal — these are precisely the places where a worker is most isolated *and* where cellular signal is most likely to be marginal or absent. Relying on cellular alone creates the dangerous assumption that signal is available when it's needed most.

<NewToBlues/>

That's the reason [Notecard for Skylo](https://shop.blues.com/products/notecard-for-skylo?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link) (NOTE-NBGLWX) is the foundation of this design, not as a nice-to-have, but as the architectural foundation. It carries three radios on one M.2 module — cellular (LTE-M / NB-IoT / GPRS), WiFi, and satellite over the [Skylo](https://www.skylo.tech/) non-terrestrial network (NTN) — and selects among them automatically. The cellular path covers the vast majority of activations — cellular is broadly deployed, even in surprisingly rural areas. But when cellular genuinely fails, the Skylo satellite link is there, on the same board: no companion module, no second device to wire in. Skylo covers supported regions (see the [Notecard for Skylo datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/) for the current coverage footprint); within that footprint, a device with an unobstructed view of the sky can deliver a distress message to the [Blues Notehub](https://blues.com/notehub/) cloud service even when every terrestrial network is unavailable. Satellite coverage is not guaranteed in every no-cellular location — it depends on the Skylo coverage region, sky-view geometry, and antenna orientation, but for the substations, oilfields, and rural worksites this design targets, it provides the safety margin that cellular alone cannot.

The firmware sets a single [`card.transport`](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-transport) preference of `wifi-cell-ntn`: the Notecard prefers WiFi where a provisioned AP is reachable, falls back to cellular (the de-facto primary for a roaming worker), and falls back again to Skylo satellite when every terrestrial network is unavailable. Failover happens inside the Notecard; the host firmware never branches on which network is live. The WiFi path is an opportunistic bonus — a field tech standing near a facility WiFi AP may sync an alert without any cellular usage at all — but it is only active when network credentials have been provisioned on the Notecard and a compatible AP is within range.

**Deployment scenario.** The beacon ships as a self-contained unit in a rugged belt-clip enclosure, powered by a 3.7V LiPo battery. Workers clip it onto their belt or hard-hat band like a pager. Worker IDs are pre-provisioned per device in Notehub before deployment; changes to `worker_id` propagate to the device on the next inbound sync, which defaults to every 2 hours, not immediately at shift start. Falls and panics generate immediate alerts. No app, no phone pairing, no worker attention required — just clip it on and go.

## 2. System Architecture

![System architecture: wearable I/O (LIS3DH accel, panic button, haptic motor) → Notecarrier CX with Cygnet host and Notecard for Skylo (cellular / WiFi / satellite) → cellular or Skylo satellite → Notehub → dispatch / paging / compliance](diagrams/01-system-architecture.svg)

**Device-side responsibilities.** The whole point of this device is that it must never miss the moment something goes wrong, so the Cygnet STM32L433 host on the Notecarrier CX never sleeps. It runs a dual-cadence loop instead: a fast inner loop reads the LIS3DH every 10 milliseconds (matching the sensor's 100 Hz ODR) while an outer ~10 Hz cadence handles Notecard I/O, GPS polling, the panic-button debounce, and the DRV2605L haptic feedback. The instant the two-stage algorithm confirms a fall — or the worker holds the button — the host queues the alert Note with the Notecard's cached location, triggers an immediate sync, and starts a non-blocking GPS search that runs in the background without ever pausing fall detection. If a fresh fix arrives within the window, a follow-up `beacon_location.qo` Note carries the event-time coordinates. All Notecard communication stays on I²C — no AT commands, no serial framing, no session management for the firmware to babysit.

**Notecard responsibilities.** The Notecard holds the daily flush schedule via [`hub.set`](https://dev.blues.io/api-reference/notecard-api/hub-requests/#hub-set) but treats any `sync:true` alert Note as an immediate interrupt — the dispatcher hears about a fall in the same minute it happens, not at the next scheduled outbound window. The Notecard also owns the location story: once the host calls `card.location.mode`, the Notecard caches each GPS fix and embeds it in every subsequent compact template Note via `_lat`/`_lon`, so the firmware never has to pass coordinates in its Note body.

When the worker walks into a coverage hole and cellular fails, Notecard for Skylo quietly switches to its onboard Skylo satellite (NTN) radio and tries the satellite path. The failover is transparent to both the firmware and to Notehub — the same Note, the same template, the same event structure arrives regardless of which radio carried it.

**Notehub responsibilities.** [Notehub](https://notehub.io) ingests every Note across both transports, stores each event, and fans out to whatever dispatch system the operator has configured. `beacon_alert.qo` events and their paired `beacon_location.qo` follow-ups land at the dispatcher's real-time endpoint. [Environment variables](https://dev.blues.io/guides-and-tutorials/notecard-guides/understanding-environment-variables/) flow the other direction on each inbound sync — a safety supervisor can retune the free-fall threshold or change a worker ID from the Notehub console without ever opening an enclosure.

**Routing to the cloud (high level).** Notehub supports HTTP, MQTT, AWS, Azure, and several other destinations; route setup is project-specific. See the [Notehub routing docs](https://dev.blues.io/notehub/notehub-walkthrough/#routing-data-with-notehub) — this project ships no specific downstream endpoint. A typical deployment routes both `beacon_alert.qo` and `beacon_location.qo` to the same real-time dispatch endpoint, joined by device UID and `event_id`.

## 3. Technical Summary

**What you'll have when you're done:** a wearable beacon that detects falls and panic-button presses, transmits alerts to Notehub over cellular or satellite, and provides haptic feedback on every event.

**Prerequisites:** Arduino IDE or `arduino-cli`, a Notehub account, and a Notecarrier CX fully assembled per the wiring in [Section 4](#5-wiring-and-assembly).

**Step 1: Install dependencies**

```bash
# Install the STM32 core
arduino-cli core install STMicroelectronics:stm32

# Install required libraries
arduino-cli lib install "Blues Wireless Notecard" \
                        "SparkFun LIS3DH Arduino Library" \
                        "Adafruit DRV2605 Library" \
                        "Adafruit BusIO"
```

**Step 2: Configure and compile**

1. Open `firmware/lone_worker_beacon/lone_worker_beacon.ino` in the Arduino IDE or a text editor.
2. In `lone_worker_beacon_helpers.h`, find the line `#define PRODUCT_UID` and replace the placeholder with your Notehub project's ProductUID (from [notehub.io](https://notehub.io) under project settings).
3. Compile (the FQBN below matches `firmware/lone_worker_beacon/sketch.yaml`, which the Arduino IDE picks up automatically when invoked from the sketch directory):

```bash
arduino-cli compile -b STMicroelectronics:stm32:Blues:pnum=CYGNET firmware/
```

**Step 3: Flash to Notecarrier CX**

```bash
arduino-cli upload -b STMicroelectronics:stm32:Blues:pnum=CYGNET \
                   -p /dev/ttyUSB0 firmware/
```

(Replace `/dev/ttyUSB0` with your platform's serial port; use `COM3` on Windows, find the port in Arduino IDE's Tools menu.)

**Step 4: Test and verify**

- Power the beacon; the Notecard claims itself to your project on first sync.
- Hold the panic button for 2+ seconds. You should feel a triple haptic buzz (alert queued).
- Check Notehub: navigate to your project's Devices tab, select your device, and view the Events log. A `beacon_alert.qo` event with `"type":"panic"` should appear within 30–90 seconds.
- Drop the beacon 50–80 cm onto a padded surface. You should feel a double buzz (fall detected) and see a `beacon_alert.qo` with `"type":"fall"` in the Events log.

If no event appears, check [Section 10 (Troubleshooting)](#10-troubleshooting).

Here is a sample Note this device emits:

```json
{
  "uid": "d84e3a...",
  "device": "dev:000000000000000",
  "file": "beacon_alert.qo",
  "received": 1714582020,
  "best_lat": 40.71280,
  "best_lon": -74.00601,
  "best_location": "New York NY",
  "body": {
    "type": "fall",
    "worker_id": "lineman-042",
    "event_id": 7,
    "voltage": 3.71,
    "loc_age_s": 142
  }
}
```

## 4. Hardware Requirements

| Part | Qty | Rationale |
|------|-----|-----------|
| [Notecarrier CX](https://shop.blues.com/products/notecarrier-cx?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link) | 1 | Integrated carrier with an embedded Cygnet STM32L433 host — no separate MCU needed. I²C, SPI, analog, and GPIO headers support the full sensor stack. |
| [Notecard for Skylo (NOTE-NBGLWX)](https://shop.blues.com/products/notecard-for-skylo?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link) ([datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/)) | 1 | One M.2 module carrying cellular (LTE-M / NB-IoT / GPRS, Quectel BG95-S5 modem), WiFi (Silicon Labs WFM200S), and Skylo satellite (NTN) radios, plus an integrated GPS/GNSS. Seats in the Notecarrier CX's M.2 slot. The firmware's `card.transport` `wifi-cell-ntn` setting makes it prefer WiFi, fall back to cellular, and fall back again to the Skylo satellite network when every terrestrial network is unavailable — no companion module, no second device, automatic failover. Requires the antennas below. |
| Skylo-certified LTE/satellite antenna included with Notecard for Skylo (u.FL) | 1 | Connects to the `MAIN` u.FL port and carries **both** the terrestrial cellular signal and the Skylo satellite link — a single antenna for both networks. Use only the Skylo-certified antenna supplied with Notecard for Skylo; substituting an uncertified antenna risks regulatory non-compliance and link failure. A belt-worn beacon needs this antenna where it can see the sky: position it against the top (sky-facing) wall of the polycarbonate enclosure (polycarbonate is RF-transparent, so no external routing or bulkhead is required), and in the northern hemisphere a southward orientation improves Skylo link margin. The same placement that enables satellite fallback serves cellular as well. |
| Passive GPS/GNSS antenna (u.FL) per the [Notecard for Skylo datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/) | 1 | Connects to the `GPS` u.FL port for the Notecard's own GNSS time/location — this is the device's location source, and `card.location` draws from it for the coordinates embedded in alert Notes. Adhere it alongside the main antenna on the top (sky-facing) interior wall of the polycarbonate enclosure for best acquisition geometry during GPS-on events. |
| [Blues Mojo](https://shop.blues.com/products/mojo?utm_source=dev-blues&utm_medium=web&utm_campaign=store-link) | 1 | Coulomb-counter on the LiPo rail for ground-truth current and energy measurement during bench validation. |
| [SparkFun Triple Axis Accelerometer Breakout — LIS3DH (SEN-13963)](https://www.sparkfun.com/products/13963) | 1 | 3-axis MEMS accelerometer at 0x18 on I²C. Configured at 100 Hz ODR (10 milliseconds hardware sample period). The firmware samples at ~100 Hz via a 10-sample inner loop (one read every 10 milliseconds), matching the sensor ODR and reliably catching free-fall phases as short as 80 ms. ±4g range provides headroom for both normal impacts and genuine falls. Built-in free-fall and shock interrupt hardware is a production upgrade path. |
| [Adafruit DRV2605L Haptic Motor Controller (#2305)](https://www.adafruit.com/product/2305) | 1 | I²C haptic driver with 123 built-in waveform effects. Drives the ERM motor directly; no transistor or PWM circuit needed. Supports both ERM and LRA motors. |
| [Adafruit Vibrating Mini Motor Disc (#1201)](https://www.adafruit.com/product/1201) | 1 | Small flat ERM disc motor. Gives distinct, wrist-perceptible confirmation buzzes at fall and panic events. Wires directly to the DRV2605L output terminals. |
| [SparkFun Momentary Push Button Switch — 12mm Square (COM-09190)](https://www.sparkfun.com/products/9190) | 1 | Panic input, wired to D9 with firmware INPUT_PULLUP. Choose a cap that can be operated with a gloved hand for field deployability. |
| 3.7V LiPo battery, 1200 mAh, JST-PH 2-pin (e.g. [Adafruit #258](https://www.adafruit.com/product/258)) | 1 | Powers the device. Runtime depends on alert frequency and transport conditions — validate with Mojo before sizing for deployment. Use a cell with built-in protection circuitry. |
| [Adafruit JST PH 2-Pin Cable — Female Connector 100mm (#261)](https://www.adafruit.com/product/261) | 1 | Adapts the LiPo cell's JST-PH male plug to bare wire leads for connection to the Mojo `BAT+` and `GND` solder pads. Required to complete the LiPo→Mojo power path without cutting or modifying the LiPo cell's factory connector. |
| Polycarbonate project enclosure, ≥120 × 80 × 40 mm interior, IP54+ (e.g., Hammond 1553JGYBK) | 1 | Protects the board stack, battery, and connectors in a field-deployable package. The Notecarrier CX (83 × 63 mm) sets the minimum floor area. **Must be polycarbonate (non-metal)** — metal enclosures block cellular, WiFi, GNSS, and satellite signals. Requires cutouts for the panic button and the LiPo JST connector for battery access; Notecard for Skylo's `MAIN` and `GPS` u.FL antennas adhere to the interior enclosure walls without external routing. IP54 or better is recommended for outdoor field use. Verify interior dimensions against your specific board-and-battery stack before ordering. **No charging circuit is included in this BOM**. See [Section 9](#11-limitations-and-next-steps) for rationale; plan enclosure cutouts for any charging connector you add downstream. |

Notecard for Skylo ships with an active global SIM including 500 MB of cellular data and 10 years of service, **plus** 10 KB of bundled Skylo satellite data — no activation fees, no monthly commitment, and no separate satellite provider subscription. Additional satellite data is billed per byte (see the [Notecard for Skylo datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/) for current pricing).

<Note>

**Installer-supplied, deployment-specific item.** Belt clip or wearable mounting hardware — attaches the beacon to a worker's belt, hard-hat band, or safety vest. Select a clip rated for the enclosure weight (~300 g fully loaded). Some enclosure families include an optional clip-arm accessory; a spring-steel belt clip can be mounted to the enclosure exterior with M3 screws.

</Note>

<Note>

**Charging and power access.** No LiPo charging circuit, dock, or power-switch hardware is included in this BOM or wiring. The project runs on a bare LiPo cell until depleted. A production wearable needs a USB-C LiPo charging circuit integrated into the enclosure, plus overcharge and short-circuit protection if not already provided by the cell's built-in circuitry. See [Section 9](#11-limitations-and-next-steps) for details; adding a charging path is the expected next step for anyone moving from bench validation toward a field-deployed unit.

</Note>

## 5. Wiring and Assembly

![Wiring: LIS3DH + DRV2605L on shared I²C; panic button to D9 INPUT_PULLUP; Notecard for Skylo MAIN (cellular+satellite) + GPS u.FL antennas; LiPo 3.7 V → Mojo → +VBAT](diagrams/02-wiring-assembly.svg)

The whole stack — Notecarrier CX, Notecard for Skylo, accelerometer, haptic driver, and panic button — has to fit inside a belt-clip enclosure that a worker will forget they're wearing. Every host I/O lands on the [Notecarrier CX](https://dev.blues.io/datasheets/notecarrier-datasheet/notecarrier-cx-v1-7/) dual 16-pin headers; Notecard for Skylo seats into the M.2 slot and talks to the Cygnet host over the carrier's internal I²C. Because cellular, WiFi, and Skylo satellite all live on that one module, there is no companion satellite device to wire in — only the two u.FL antennas described below. The Mojo sits inline between the LiPo JST connector and the Notecarrier CX `+VBAT` pad during bench validation.

All I²C peripherals (LIS3DH and DRV2605L) share the SDA/SCL bus exposed on the Notecarrier CX headers. On-board pull-ups are provided by the carrier; no external resistors are needed for the I²C lines.

Pin-by-pin:

- **+3V3** → LIS3DH `VIN`, DRV2605L `VIN` (both are 3.3V-native; see breakout datasheets for exact voltage range).
- **GND** → LIS3DH `GND`, DRV2605L `GND`, one leg of the panic button.
- **SDA** → LIS3DH `SDA`, DRV2605L `SDA`.
- **SCL** → LIS3DH `SCL`, DRV2605L `SCL`.
- **D9** → other leg of the panic button (firmware uses `INPUT_PULLUP`; active-low).
- **DRV2605L `MOTOR+` / `MOTOR-`** → Adafruit Vibrating Motor Disc red and blue wires (polarity matches the driver output; swap if motor doesn't run).
- **Power path (LiPo → Mojo → Notecarrier CX).** The Mojo sits inline on the battery rail as a coulomb counter. Wire it as follows:
  1. Plug the JST-PH female pigtail (BOM item) onto the LiPo's JST-PH male connector. This gives you two bare wire leads: `+` (red) and `−` (black).
  2. **LiPo `+` lead** → Mojo `BAT+` solder pad (or screw terminal marked `BAT`).
  3. **LiPo `−` lead** → Mojo `GND` solder pad. The Mojo's `GND` terminal is common to both the battery-negative rail and the load-negative rail.
  4. **Mojo `LOAD+` output** → Notecarrier CX **`+VBAT`** pad (completes the positive supply to the board stack).
  5. **Mojo `GND` terminal** → Notecarrier CX **`GND`** header pin (completes the ground-return path; without this connection the circuit is open).
- **Notecard for Skylo antennas** → `MAIN` u.FL port → included Skylo-certified antenna (carries both cellular and satellite); `GPS` u.FL port → passive GPS/GNSS antenna. Both adhere to the top (sky-facing) interior wall of the polycarbonate enclosure (see the antenna Note below). No companion satellite module and no JST cable are involved — all three radios are on Notecard for Skylo itself.

<Note>

**Antenna placement and connector mapping.** Notecard for Skylo uses two u.FL ports. Connect the included Skylo-certified antenna to the `MAIN` port — it carries **both** the cellular signal and the Skylo satellite link, so a single antenna serves both networks — and connect the passive GPS/GNSS antenna to the `GPS` port. Adhere both elements to the top (sky-facing) interior wall of the polycarbonate enclosure: polycarbonate is RF-transparent, so no external pigtail routing or bulkhead is needed. A belt-worn beacon only reaches the Skylo satellite network when its `MAIN` antenna can see the sky, so orient that wall upward when the unit is worn; in the northern hemisphere a southward orientation improves Skylo link margin. Use only the Skylo-certified antenna supplied with Notecard for Skylo on the `MAIN` port — substituting an uncertified antenna risks regulatory non-compliance and link failure. The `GPS` port is the location source for all `card.location` data embedded in alert Notes.

</Note>

<Note>

**Charging is out of scope for this POC.** No charging circuit, dock, or inductive coil is wired in this build. Route the LiPo JST connector to an accessible point on the enclosure wall so the battery can be swapped or a bench charger connected without fully disassembling the unit. Do not seal the JST connector inside the enclosure with no external access path.

</Note>

The LIS3DH's SDO/SA0 pin sets the I²C address. Leave it unconnected or pulled to GND for address 0x18 (the firmware default). The DRV2605L address (0x5A) is fixed; no conflict.

## 6. Notehub Setup

**Detailed walkthrough for first-time users:**

1. **Create a project.** Sign up at [notehub.io](https://notehub.io). Click **Create a Project** → name it (e.g., "Lone Worker Beacons") → select your region → confirm. Copy the **ProductUID** displayed on the project details card. Paste this into `firmware/lone_worker_beacon/lone_worker_beacon_helpers.h` as the `PRODUCT_UID` macro value before flashing.

2. **Claim the Notecard.** Power the beacon with a Notecard inserted and provisioned SIM. On first cellular or satellite session the Notecard associates with your project automatically. Verify in Notehub: navigate to **Devices** → click on your device's UID — it should appear within 30–90 seconds.

3. **Complete the initial non-NTN sync.** Before any Skylo satellite (NTN) transmission is possible, Notecard for Skylo must complete at least one successful cellular or WiFi sync to associate with your project and register the compact template definitions in Notehub. The firmware's `card.transport` `wifi-cell-ntn` setting performs that first sync over cellular/WiFi automatically, so commission each beacon where it has terrestrial coverage — even if it will routinely operate over satellite — and confirm a sync lands in Notehub before deploying. See the [`card.transport` API reference](https://dev.blues.io/api-reference/notecard-api/card-requests/#card-transport) for details.

4. **Create a Fleet per region or team.** [Fleets](https://dev.blues.io/guides-and-tutorials/fleet-admin-guide/) group devices for shared configuration. A natural structure is one fleet per crew or site — all devices in a fleet share the same detection thresholds, with per-device worker ID overrides. [Smart Fleets](https://dev.blues.io/notehub/notehub-walkthrough/#using-smart-fleet-rules) can route a device to a different fleet automatically based on its reported location if workers cross territories.

5. **Set environment variables.** All variables below are optional; firmware defaults apply until overridden. To set an env var in Notehub: open your project → **Devices** tab → click your device → **Environment** → **Fleet Environment** (applies to all devices in the fleet) or **Device Environment** (overrides fleet settings for this device alone) → add key-value pairs. Values propagate to the Notecard's local cache on the next inbound sync (default every 2 hours), then to the host on the following `env.get` poll (also up to 2 hours). Total propagation time: up to 4 hours. No re-flashing is required.

   | Variable | Default | Purpose |
   |---|---|---|
   | `worker_id` | `worker-001` | Human-readable worker or device identifier included in every Note. Maximum 24 characters; longer values are silently truncated on-device to preserve compact-packet payload size. Pre-provision this per device before deployment; changes propagate to the device on the next inbound sync (up to 2 hours by default), not immediately at shift start. |
   | `freefall_g` | `0.55` | Total acceleration magnitude (g) below which free-fall phase is declared. Clamped to 0.10–0.90 g. |
   | `impact_g` | `2.5` | Total acceleration magnitude (g) above which fall impact is confirmed. Clamped to 1.50–8.00 g. |
   | `fall_window_ms` | `500` | Milliseconds after a free-fall episode during which an impact must be detected to confirm the fall. Clamped to 100–2000 ms. |
   | `freefall_min_ms` | `80` | Minimum milliseconds the device must remain in free-fall before the impact window opens. Shorter free-falls (stumbles, tool drops) are ignored. Clamped to 20–500 ms. |
   | `panic_hold_ms` | `2000` | Milliseconds the button must be held before a panic alert fires. Prevents accidental triggers from gloved hands. Clamped to 500–10000 ms. |

6. **Configure routes** (optional for now; use for live dispatch). Go to **Routes** → **Create Route** → Name it (e.g., "Dispatch Alerts") → select **Event** → filter by file `beacon_alert.qo` → select a destination (HTTP, SMTP, Slack, etc.). **Important:** Add a **second route** for `beacon_location.qo` to the **same** destination. When a fresh GPS fix arrives during the 90-second background search, `beacon_location.qo` carries event-time coordinates that supersede the cached location in the paired `beacon_alert.qo`. Downstream systems must pair the two Notes by `(device, event_id)` — `event_id` resets to 0 on each power cycle and is not globally unique on its own. The `(device, event_id)` join key is unambiguous across repeated alerts, retries, and network reordering.

## 7. Firmware Design

The firmware is split into three small files so the safety-critical detection paths stay legible. All three live directly under `firmware/`:
- [`firmware/lone_worker_beacon/lone_worker_beacon.ino`](firmware/lone_worker_beacon/lone_worker_beacon.ino) — constants, globals, `setup()`, `loop()`.
- [`firmware/lone_worker_beacon/lone_worker_beacon_helpers.h`](firmware/lone_worker_beacon/lone_worker_beacon_helpers.h) — shared `#define` constants, `extern` declarations, env-var clamp ranges, function prototypes.
- [`firmware/lone_worker_beacon/lone_worker_beacon_helpers.cpp`](firmware/lone_worker_beacon/lone_worker_beacon_helpers.cpp) — all helper function implementations.

**Dependencies:**
- Arduino core for STM32 ([`stm32duino/Arduino_Core_STM32`](https://github.com/stm32duino/Arduino_Core_STM32)) — installed via Arduino IDE Boards Manager.
- [`Blues Wireless Notecard`](https://github.com/blues/note-arduino) (the `note-arduino` library). Install via Arduino Library Manager: `arduino-cli lib install "Blues Wireless Notecard"`.
- [`SparkFun LIS3DH Arduino Library`](https://github.com/sparkfun/SparkFun_LIS3DH_Arduino_Library). Install via Library Manager: `arduino-cli lib install "SparkFun LIS3DH Arduino Library"`.
- [`Adafruit DRV2605 Library`](https://github.com/adafruit/Adafruit_DRV2605_Library) and its `Adafruit BusIO` dependency. Install via Library Manager.

### Modules

| Responsibility | Function |
|---|---|
| Notecard configuration (`hub.set` + one-time `card.transport` `wifi-cell-ntn` for cellular→satellite fallback) at boot; returns fault state | `notecardConfigure()` |
| Compact template registration (2 templates, retried; returns fault state) | `defineTemplates()` |
| Environment variable refresh with clamp validation; called at boot and every 2 h | `fetchEnvVars()` |
| LIS3DH setup (100 Hz, ±4g) | `initAccel()` |
| DRV2605L setup (ERM library 1) | `initHaptic()` |
| Two-stage fall detection | `pollFallDetection()` |
| Hold-to-confirm panic button | `checkPanicButton()` |
| Start non-blocking GPS search after an alert | `beginGpsSearch()` |
| Advance GPS search; queue `beacon_location.qo` on fresh fix | `pollGpsSearch()` |
| Alert Note with immediate sync (cached location + `loc_age_s` + `event_id`) | `sendAlert()` |
| Arm non-blocking haptic pulse sequence | `triggerHaptic()` |
| Advance haptic state machine (called every loop pass) | `pollHaptic()` |

### Fall Detection Algorithm

The firmware uses a two-stage software algorithm sampled at ~100 Hz: ten `pollFallDetection()` calls run per outer loop pass, spaced 10 milliseconds apart in a fast inner loop (matching the LIS3DH's 100 Hz ODR). At this rate the 80 milliseconds minimum free-fall duration guard (`DEFAULT_FREEFALL_MIN_MS`) spans ~8 consecutive samples, providing meaningful noise rejection. Notecard I/O, GPS polling, and haptic state advance once per outer pass at the ~10 Hz outer cadence. A single-stage threshold check (just watching for a spike) generates too many false positives from everyday bumps; requiring a free-fall phase before the impact check reduces nuisance alerts from walking into a doorframe or dropping a tool.

**Stage 1 — Free-fall.** On each of the 10 inner-loop reads (spaced 10 milliseconds apart), the firmware computes total acceleration magnitude: `|a| = √(ax² + ay² + az²)`. When total-g drops below `freefall_g` (default 0.55g) and stays there for at least `freefall_min_ms` (default 80ms, remotely configurable via Notehub), the firmware exits Stage 1 and opens an impact-watch window. At ~100 Hz sampling the 80 milliseconds guard spans ~8 consecutive readings — meaningful noise rejection. A genuine free-fall from ~30 cm bench height lasts well over 100 ms.

**Stage 2 — Impact.** Within `fall_window_ms` (default 500ms) of the free-fall phase ending, if total-g exceeds `impact_g` (default 2.5g), the fall is confirmed. The window closes automatically if no impact arrives — preventing a brief stumble or a tool being set down from generating a false alert.

### Event Payload Design

Both Notefiles use [compact templates](https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design#working-with-note-templates) with `"format":"compact"`. This is a hard requirement for Skylo NTN (satellite) transport — compact Notes use fixed-length binary encoding, keeping the satellite packet well inside the 256-byte maximum payload limit and minimizing per-message satellite cost. The `_lat`/`_lon` keywords in the template body instruct the Notecard to embed its cached GPS location automatically; the firmware does not need to explicitly pass coordinates in the `note.add` body.

Both templates include an `event_id` field — a monotonic counter incremented once per new alert dispatch, **not** per retry. The same `event_id` value appears in both `beacon_alert.qo` and its paired `beacon_location.qo`. Downstream systems must use `(device, event_id)` as the join key — `event_id` resets to 0 on each power cycle and is not globally unique on its own. The scoped `(device, event_id)` pair is stable across repeated alerts of the same type, across retries, and across network reordering.

Example `beacon_alert.qo` event as it appears in Notehub (initial alert, cached location):

```json
{
  "uid": "d84e3a...",
  "device": "dev:000000000000000",
  "file": "beacon_alert.qo",
  "received": 1714582020,
  "best_lat": 40.71280,
  "best_lon": -74.00601,
  "best_location": "New York NY",
  "body": {
    "type": "fall",
    "worker_id": "lineman-042",
    "event_id": 7,
    "voltage": 3.71,
    "loc_age_s": 142
  }
}
```

Example `beacon_location.qo` follow-up event (fresh event-time fix, sent only if GPS acquires within 90 seconds):

```json
{
  "uid": "d84e3b...",
  "device": "dev:000000000000000",
  "file": "beacon_location.qo",
  "received": 1714582087,
  "best_lat": 40.71294,
  "best_lon": -74.00589,
  "best_location": "New York NY",
  "body": {
    "type": "fall",
    "worker_id": "lineman-042",
    "event_id": 7
  }
}
```

The `type` field in `beacon_alert.qo` carries one of two values: `fall` (two-stage algorithm confirmed) or `panic` (button held). `loc_age_s` is the age in seconds of the Notecard's cached GPS fix at the moment the alert was queued (−1.0 if no fix was available). `event_id` resets to 0 on each power cycle; downstream correlation should use `(device, event_id)` as the join key. When `beacon_location.qo` is also present (matching `event_id`), its coordinates supersede the cached location in `beacon_alert.qo` for mapping and dispatch response.

### Low-Power Strategy

**This POC keeps the host MCU awake continuously — this is a deliberate design choice, not an oversight.** The device must detect a fall or button press at any moment, so `card.attn` sleep mode (which cuts host power entirely) is incompatible with the monitoring requirement. Instead, the host MCU remains awake continuously, running a dual-cadence loop: the LIS3DH is sampled at ~100 Hz via a 10-sample inner loop (10 milliseconds between reads), and Notecard/GPS/haptic state advances at the outer ~10 Hz cadence. The inner-loop delays pace the outer cadence without a separate top-level `delay()` call. The battery-life penalty is real and significant: the STM32L433 at its default clock draws approximately 10–15 mA active, making host idle the dominant draw at steady state. See the [power validation table](#9-validation-and-testing) for per-state figures and validate total runtime with Mojo before sizing a battery for deployment.

Notecard for Skylo runs in `periodic` mode with `outbound: 1440` (daily flush) and `inbound: 120` (2-hour environment-variable refresh). `notecardConfigure()` also issues a one-time `card.transport` `wifi-cell-ntn` so the Notecard prefers WiFi, then cellular, then Skylo satellite (NTN) — the failover is handled inside the Notecard, with no firmware branching. All emergency Notes carry `sync:true`, which bypasses the outbound interval and triggers an immediate session over whichever radio is reachable — the daily flush is a backstop for any queued data that sync:true did not deliver. Between sessions the Notecard sits in its own low-power idle state (~8 µA), regardless of which radio it last used. GPS is off by default and turned on only during alert events (fall, panic) — continuous GPS would consume an additional 30+ mA and is unnecessary given the design's event-driven location update cadence.

A production implementation should use STM32L433 low-power STOP2 mode with GPIO wakeup on the LIS3DH hardware interrupt (INT1) and button pin — dropping host idle to ~2–3 µA while still catching every fall event in real time. See [Limitations](#11-limitations-and-next-steps).

### Retry and Error Handling

**Startup path.** Both `notecardConfigure()` and `defineTemplates()` return a boolean. `notecardConfigure()` retries `hub.set` up to five times to handle the cold-boot I²C race. `defineTemplates()` retries each of the two template registrations (`beacon_alert.qo`, `beacon_location.qo`) up to three times. If either function returns false, `g_setupFault` is latched in `setup()`, and a distinctive slow double-buzz haptic pattern fires so a misconfigured device is obvious at power-on without needing debug serial. A missing or empty `PRODUCT_UID` is also caught at runtime and added to the fault latch.

A startup fault — missing or empty `PRODUCT_UID`, `hub.set` failure after five retries, any template registration failure, or LIS3DH initialization failure — is treated as unrecoverable. After the slow double-buzz pattern fires, `setup()` enters an infinite halt loop that repeats the fault buzz every 5 seconds; the beacon never enters `loop()` and no alerts are ever sent. This hard-stop behavior is intentional for a safety device: arming a beacon whose Notes cannot route, or whose compact Skylo NTN transport may be broken, gives a false sense of protection. The recurring buzz makes an unconfigured device unmistakably obvious at power-on — verify `PRODUCT_UID`, Notecard connectivity, and template registration, then power-cycle before deploying.

**Sensor degradation.** `initAccel()` and `initHaptic()` each return a boolean; the main loop checks `g_accelReady` and `g_hapticReady` before calling the relevant functions. A missing or unresponsive LIS3DH at boot is treated as a hard fault — `g_setupFault` is latched alongside `g_accelFaultLatched`, the distinctive slow double-buzz fault pattern fires, and `setup()` enters the infinite halt loop. This ensures a beacon that cannot perform fall detection is immediately obvious at power-on and cannot be silently deployed as panic-only. A missing DRV2605L skips haptic feedback without crashing the loop. An accelerometer fault that develops after boot (consecutive bad reads beyond `ACCEL_FAIL_THRESHOLD` after `ACCEL_REINIT_MAX` failed reinitialisations) latches `g_accelFaultLatched` and clears `g_accelReady`, permanently disabling fall detection until power-cycle. The device then emits a single buzz every 30 seconds so an operator can recognize the unit has dropped to degraded panic-only mode.

**Notecard response errors.** All `requestAndResponse()` calls check for a `NULL` response and call `notecard.responseError()` before accessing fields; failed transactions are skipped and the loop continues with stale values. `note.add` failures for alerts are retried up to `ALERT_RETRY_MAX` (3) times with 500 milliseconds spacing via the non-blocking retry queue; each retry attempt carries the original `event_id` and `loc_age_s` so no context is lost.

**Important — queueing failure vs. delivery failure.** Only Notes that `note.add` *successfully accepts* are stored inside the Notecard and retried by the Notecard for cellular or satellite delivery. If the firmware's retry budget is exhausted before `note.add` succeeds, for example because the Notecard is temporarily unreachable on I²C — the alert is **dropped** and is never delivered to Notehub. The firmware logs this via `DEBUG_PRINTLN` but takes no further action. If stronger guarantees are required, persist unsent alerts in non-volatile storage until `note.add` succeeds, or trigger a local fault (e.g., a distinctive haptic pattern) when the retry budget is exceeded. An operator who suspects a missed alert should issue `{"req":"hub.status"}` from the blues.dev In-Browser Terminal to check the last sync time, pending Note count, and transport-layer error.

**GPS acquisition.** The non-blocking GPS state machine polls `card.location` at 2-second intervals (throttled from the 10 Hz loop rate) for up to `DEFAULT_GPS_TIMEOUT_SEC` (90 seconds). Before the alert Note is queued, the firmware captures the current cache epoch into a per-alert local variable (`thisCacheEpoch`); this value is passed to `sendAlert()` to compute `loc_age_s` and, when no GPS search is already active, is copied into `g_gpsCacheEpoch` (the freshness baseline the search uses). A fix is accepted only when its epoch post-dates that baseline, preventing a stale cached fix from being mistaken for a fresh acquisition. On a fresh fix, a `beacon_location.qo` Note is queued immediately with `sync:true`. The timeout check uses elapsed time (`millis() - start >= interval`) rather than an absolute deadline to remain correct across the 49.7-day `millis()` rollover. If no fresh fix arrives within the timeout, GPS is disabled and only the initial `beacon_alert.qo` stands. Only one GPS enrichment window can be active at a time — if a second alert fires during the 90-second window (possible because the 60-second cooldown is shorter than the GPS timeout), `beginGpsSearch()` returns immediately and the second alert receives its cached location only; no `beacon_location.qo` is queued for it.

**Alert rate limiting and suppression.** `DEFAULT_ALERT_COOLDOWN_SEC` (60 seconds) gates fall and panic alerts. If a fall or panic event occurs within 60 seconds of the previous alert, no Note is queued and no GPS acquisition is attempted. A suppressed fall produces no local indication; a suppressed panic produces a single haptic buzz so the worker knows the hold was registered (distinct from the triple-buzz that confirms an alert was accepted). This prevents alert storms from a tumbling device without requiring any worker action.

### Key Code Snippet 1: Compact Template with _lat/_lon

The `format: "compact"` and `port` arguments are required for Skylo NTN (satellite) transport. The `_lat`/`_lon` template fields tell the Notecard to embed its cached best-available location (GNSS or cell-derived) in the compact packet automatically — the firmware never passes coordinates in the Note body. The `event_id` field uses type hint `14` (4-byte signed int32, per the Blues compact-template encoding table) and is written to both `beacon_alert.qo` and `beacon_location.qo` with the same value; downstream systems join the two Notes using `(device, event_id)` as the key.

```cpp
J *req  = notecard.newRequest("note.template");
JAddStringToObject(req, "file",   "beacon_alert.qo");
JAddNumberToObject(req, "port",   50);
JAddStringToObject(req, "format", "compact");
J *body = JAddObjectToObject(req, "body");
JAddStringToObject(body, "type",      "s");    // variable-length string hint
JAddStringToObject(body, "worker_id", "s");
JAddNumberToObject(body, "event_id",  14);     // 4-byte signed int32 correlation key
JAddNumberToObject(body, "voltage",   14.1);   // 4-byte float
JAddNumberToObject(body, "_lat",      14.1);   // GPS lat from Notecard cache
JAddNumberToObject(body, "_lon",      14.1);   // GPS lon from Notecard cache
notecard.sendRequest(req);
```

### Key Code Snippet 2: Immediate-Sync Alert

`sync:true` tells the Notecard to attempt a session immediately, bypassing the periodic outbound interval. Notecard for Skylo selects the radio per its `card.transport` `wifi-cell-ntn` preference; if WiFi and cellular both fail, the Note routes over Skylo satellite automatically.

```cpp
J *req  = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "beacon_alert.qo");
JAddBoolToObject(req,   "sync", true);
J *body = JAddObjectToObject(req, "body");
JAddStringToObject(body, "type",      "fall");
JAddStringToObject(body, "worker_id", "lineman-042");
JAddNumberToObject(body, "event_id",  (double)thisEventId);  // monotonic; matches beacon_location.qo
JAddNumberToObject(body, "voltage",   3.71);
notecard.sendRequest(req);
```

### Key Code Snippet 3: Two-Stage Fall Confirmation

Free-fall (low-g) followed by impact (high-g) within a short window. Both must occur in sequence; either alone does not confirm a fall.

```cpp
float totalG = sqrtf(ax*ax + ay*ay + az*az);

// Stage 1: free-fall phase — total-g drops below threshold for minimum duration
if (!g_inFreefall && totalG < g_freefallG) {
    g_inFreefall    = true;
    g_freefallStart = millis();
} else if (g_inFreefall && totalG >= g_freefallG) {
    if ((millis() - g_freefallStart) >= g_freefallMinMs) {
        g_watchingImpact    = true;
        g_impactWindowStart = millis();   // record start; compare elapsed (wraparound-safe)
    }
    g_inFreefall = false;
}

// Stage 2: impact detection — high-g spike within the window
if (g_watchingImpact) {
    if ((millis() - g_impactWindowStart) >= g_fallWindowMs) {
        g_watchingImpact = false;   // window expired — not a fall
    } else if (totalG > g_impactG) {
        g_watchingImpact = false;
        return true;   // confirmed fall
    }
}
```

### Key Code Snippet 4: Two-Note GPS Flow

The firmware uses a non-blocking GPS design: the initial alert Note is queued immediately so it can transmit without waiting for a fix, while GPS acquisition runs in the background without pausing fall detection or button monitoring.

**Step 1 — `sendAlert()`.** Before the alert Note is sent, `loop()` computes `loc_age_s` once from a live `card.time` call and the pre-alert cache epoch — capturing the fix age at the moment the event fired. This value is passed directly to `sendAlert()` and stored in the retry-queue entry so every subsequent retry reports the same original fix age, not an age relative to the retry timestamp. The alert Note itself is queued with the Notecard's cached location embedded via `_lat`/`_lon`, `sync:true`, and the same `event_id` on every attempt. `beginGpsSearch()` is called only after `sendAlert()` returns `true`, confirming the Note was queued. This ordering guarantees no GPS search is ever orphaned by a failed `note.add`: if `sendAlert()` fails, `enqueueAlert()` adds the alert (including its `event_id` and pre-computed `loc_age_s`) to the non-blocking retry queue, and `beginGpsSearch()` fires automatically once the retry succeeds inside `pollAlertRetry()`.

**Step 2 — `pollGpsSearch()`.** Called once per outer loop pass. Throttles `card.location` polls to once per 2 seconds (GNSS fixes update far slower than the outer loop rate). Accepts only a fix whose epoch post-dates the pre-alert cache snapshot to prevent a stale cached fix from being mistaken for a new acquisition. On a fresh fix, queues `beacon_location.qo` with `sync:true` and the same `event_id` as the initial alert; downstream dispatch joins the two Notes using `(device, event_id)` as the key.

```cpp
// Step 1: compute loc_age_s once at event-fire time, then assign event_id and
// queue the alert; start GPS only after the alert is confirmed queued.
// Capturing loc_age_s here (not inside sendAlert()) ensures every retry
// reports the original fix age, not the age relative to the retry timestamp.
float thisLocAgeS = /* card.time − thisCacheEpoch, or −1.0 if no fix */ ...;
uint32_t thisEventId = ++g_alertEventId;   // monotonic; incremented once per new alert
bool sent = sendAlert(alertType, thisEventId, thisLocAgeS);   // cached loc + event_id
if (sent) {
    g_lastAlertMs = now;
    beginGpsSearch(alertType, thisEventId);      // enables continuous GPS; returns immediately
} else {
    // Preserve event-time locAgeS in the queue so all retries report the
    // original fix age — not the (growing) age at retry time.
    enqueueAlert(alertType, thisCacheEpoch, thisEventId, thisLocAgeS);
}

// Step 2: background fix acquisition (called from loop() at the outer ~10 Hz cadence)
void pollGpsSearch() {
    if (!g_gpsSearching) return;
    // Timeout: compare elapsed time, not absolute deadline (wraparound-safe)
    if ((millis() - g_gpsSearchStart) >= (DEFAULT_GPS_TIMEOUT_SEC * 1000UL)) {
        // disable GPS; initial alert location stands
        disableGps();  return;
    }
    if ((millis() - g_gpsLastPollMs) < 2000UL) return;  // throttle polls
    g_gpsLastPollMs = millis();

    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
    uint32_t fixTime = (uint32_t)JGetNumber(rsp, "time");
    // ...
    if (fixTime > g_gpsCacheEpoch && (lat != 0.0 || lon != 0.0)) {
        // Fresh fix: queue follow-up note with event-time coordinates + same event_id
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "beacon_location.qo");
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "type",      g_gpsAlertType);
        JAddStringToObject(body, "worker_id", g_workerId);
        JAddNumberToObject(body, "event_id",  (double)g_gpsEventId);  // matches beacon_alert.qo
        // _lat/_lon embedded automatically by compact template at note.add time
        notecard.requestAndResponse(req);
        disableGps();
    }
}
```

If GPS times out, only the initial `beacon_alert.qo` Note is sent; the cached location (which may be GNSS-derived, cell-derived, stale, or empty) stands. The `loc_age_s` field in the initial alert records how old the cached fix was at alert time, letting dispatch judge location freshness independently.

## 8. Data Flow

![Data flow: 100 Hz LIS3DH inner loop / 10 Hz outer → fall and panic detection → beacon_alert.qo (sync:true with cached location) and beacon_location.qo (GPS follow-up) → Notehub, paired by event_id](diagrams/03-data-flow.svg)

**Collected.** On every outer loop pass (~10 Hz): ten LIS3DH accelerometer samples read at 10 milliseconds intervals (100 Hz effective), with the two-stage fall-detection state machine evaluated on each. On each alert event: battery voltage (`card.voltage`), cached-fix age at alert time (`loc_age_s`), and a monotonic `event_id`. GPS search runs non-blocking in the background after an alert; a fresh fix, if acquired, produces a follow-up Note carrying the same `event_id`.

**Location accuracy.** When an alert fires, `beacon_alert.qo` is queued immediately with the Notecard's current cached location embedded via `_lat`/`_lon` and `loc_age_s` recording the fix age in seconds (−1 if unknown). Concurrently, a non-blocking GPS search polls `card.location` at 2-second intervals for up to 90 seconds. If a fix whose epoch post-dates the pre-alert cache snapshot is acquired, a `beacon_location.qo` Note is queued with the event-time coordinates. If GPS times out, only the initial alert Note is sent and the cached location (which may be GNSS-derived, cell-derived, stale, or empty) stands.

**Transmitted.**
- `beacon_alert.qo` — emitted on confirmed fall or panic. `sync:true` triggers immediate cellular (or satellite) transmission. Contains: alert type, worker ID, `event_id` (monotonic correlation key), battery voltage, `loc_age_s` (cached-fix age at alert time), and cached location via compact template `_lat`/`_lon`.
- `beacon_location.qo` — emitted after a confirmed alert when a fresh GPS fix arrives within the timeout window. `sync:true`. Contains: alert type (echoes the triggering event), worker ID, `event_id` (matches the paired `beacon_alert.qo`), and fresh event-time coordinates via `_lat`/`_lon`. This Note is optional — it is queued only if GPS succeeds during the background search window.

**Routed.** Both Notefiles arrive at Notehub regardless of whether they came via WiFi, cellular, or Skylo satellite — the transport is transparent in the event structure. Downstream systems should pair `beacon_alert.qo` and `beacon_location.qo` using `(device, event_id)` as the join key — `event_id` is device-local, resets to 0 on each power cycle, and is not unique across devices on its own. The initial alert carries the best-available cached location, and a subsequent `beacon_location.qo` with the same `event_id` (from the same device) supersedes it with event-time coordinates when GPS acquires within the 90-second background window. Pairing on `(device, event_id)` is unambiguous across repeated alert types, retries, and network reordering.

**Alert triggers:**
- `fall` — two-stage algorithm: free-fall phase followed by impact within the detection window. Suppressed if within 60 seconds of the previous alert.
- `panic` — panic button held for `panic_hold_ms` (default 2 seconds). Suppressed if within 60 seconds of the previous alert.

## 9. Validation and Testing

**Expected steady-state behavior.** In normal operation, a healthy beacon produces zero `beacon_alert.qo` events and zero `beacon_location.qo` events. To verify that Notecard provisioning, template registration, and connectivity are all working after assembly, trigger a test fall or panic (see below) and confirm the event appears in Notehub within session-establishment time (typically 30–90 seconds on cellular). If no event appears, check the Notecard's sync status via the [Blues In-Browser Terminal](https://dev.blues.io/terminal/) (`{"req":"hub.status"}`) to see the last sync time, pending Note count, and transport-layer error.

**Simulating a fall.** On the bench, a realistic fall simulation: hold the device at chest height and drop it onto a padded surface from 50–80 cm. The firmware's default thresholds (0.55g free-fall, 2.5g impact) are tuned for human-body-scale falls. You should see the haptic motor pulse twice (non-blocking, monitoring continues during the buzz sequence) and a `beacon_alert.qo` Note with `"type":"fall"` appear in Notehub within session-establishment time, typically 30–90 seconds in cellular conditions. If the device acquires a fresh GPS fix during the background search window, a follow-up `beacon_location.qo` Note will appear shortly after with the event-time coordinates. In poor GNSS conditions (indoors, obstructed sky view) the background search times out after 90 seconds and only the initial alert Note is sent.

**Simulating a panic.** Hold the button for 2+ seconds. After the 30 milliseconds debounce settles on the press edge, the haptic motor emits one click to confirm the press was registered. When the hold threshold is reached, the firmware evaluates the 60-second alert cooldown before queuing anything: if the cooldown has expired, the panic alert is accepted and three haptic buzzes (non-blocking, each fires 220 milliseconds apart without pausing button monitoring) confirm the alert is queued for transmission. The triple-buzz means the alert has been accepted for transmission handling — either directly into the Notecard's outbound queue (if `note.add` succeeded) or into the firmware's local retry queue for delivery as soon as the Notecard is reachable (if `note.add` failed transiently). If the device is still within the cooldown window, a single buzz acknowledges the hold without queuing an alert — use Notehub's event log to distinguish a suppressed panic from a queued one. A `beacon_alert.qo` with `"type":"panic"` should arrive in Notehub within session-establishment time. If GPS acquires a fresh fix during the background search, a `beacon_location.qo` follow-up Note will appear as well.

**Simulating satellite failover.** With the device outdoors (the `MAIN` antenna has sky view), force a satellite session by temporarily restricting Notecard for Skylo to NTN-only (via `{"req":"card.transport","method":"ntn"}` issued from the blues.dev In-Browser Terminal). Trigger a panic. The alert should arrive in Notehub over the satellite path — verifiable in the event metadata, which will show `"transport":"ntn"`. Reset transport afterward to restore automatic failover: `{"req":"card.transport","method":"wifi-cell-ntn"}`.

**Power validation with Mojo.** The [Mojo](https://dev.blues.io/datasheets/mojo-datasheet/) sits inline between the LiPo and the Notecarrier CX `+VBAT` pad. It accumulates the charge consumed by the board stack and makes the reading available over its Qwiic I²C link. **The beacon firmware does not read the Mojo** — it is a bench measurement instrument only. To retrieve the mAh reading during validation, connect a separate Qwiic-capable host (a SparkFun RedBoard Qwiic, a Notecarrier AL with its own sketch, or any I²C host with a Qwiic port) running the Mojo readout sketch to the Mojo's Qwiic connector. Run that host alongside the beacon during your validation window; its USB serial output gives you real-time mAh accumulation without touching the beacon firmware.

**Battery runtime estimate (1200 mAh LiPo, based on measured and published figures):**

Assuming steady-state idle (no alerts) and daily cellular sync:
- Steady-state draw: ~10–20 mA (host + Notecard idle)
- Daily sync duration: ~2 minutes (session setup + Note transmission)
- Daily sync energy: ~0.28 mAh (Notecard sync cost)
- **Idle energy per 24h: ~240–480 mAh (steady-state) + ~0.28 mAh (sync) = ~240–480 mAh/day**
- **1200 mAh battery: ~2.5–5 days on steady-state + 1 sync/day, with zero alerts**

Alert overhead (per event):
- GPS acquisition (if outdoors, 90 seconds typical): +30–50 mA × 1.5 minutes = ~0.75–1.25 mAh per alert
- Cellular sync for alert Note: ~0.28 mAh
- **Total per alert: ~1–1.5 mAh**
- At one alert per day: ~2.5–5 days total

**Validation with Mojo is mandatory before deployment** — measure your actual steady-state draw and per-alert overhead. WiFi and satellite transports consume different energy profiles; sync cadence and GPS timeout settings affect total runtime.

**Published Notecard for Skylo figures (from Blues documentation)**

| Component | State | Published figure |
|---|---|---|
| Notecard for Skylo (NOTE-NBGLWX) | Idle, radio off (between sessions) | ~8 µA ([datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/)) |
| Notecard for Skylo (NOTE-NBGLWX) | Network session (cellular or satellite) | ~250 mA average from the onboard BG95-S5 modem; brief peaks up to ~2 A for a few ms on a 2G transmit burst ([datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/)) |

**Estimated whole-device figures — validate with Mojo before sizing for deployment**

| State | Estimated draw | Notes |
|---|---|---|
| Host awake, Notecard idle (steady-state background) | ~10–20 mA | STM32L433 at default clock; not from Blues datasheet — disable debug Serial to reduce clock load |
| GNSS acquisition active (up to 90 seconds per alert event) | +30–50 mA above baseline | Notecard GNSS module draw; validate with Mojo |
| Haptic motor active (ERM disc, ~0.5 seconds per buzz) | +50–80 mA above baseline | DRV2605L + motor; validate with Mojo |
| Alert event (GPS acquisition + cellular sync) | ~40–70 mA for up to 90 seconds | Dominant transient; rare in normal operation; validate with Mojo |

A productive bench exercise: run the device for 2 hours on a known-capacity LiPo and Note the Mojo's mAh reading. Trigger several test falls and panics and compare the per-sync energy spikes against the alert count. Steady-state draw between alerts should stay close to the host-idle estimate (~10–20 mA). If steady-state draw is well above 20 mA, the host MCU is likely running at full clock — consider disabling debug serial output, which forces the STM32 to maintain its USB clock. Use Mojo-measured average current, not the steady-state estimate — to project battery runtime for your specific alert frequency and transport conditions.

## 10. Troubleshooting

**Device does not appear in Notehub.**
- Verify the `PRODUCT_UID` in `lone_worker_beacon_helpers.h` matches your Notehub project's ProductUID exactly.
- Check that the Notecard has a provisioned SIM (should ship with one; confirm at [Blues shop](https://shop.blues.com)).
- Ensure cellular or WiFi coverage is available. If indoors and no WiFi is provisioned, power cycle and move to a window or open area.
- From the [Blues In-Browser Terminal](https://dev.blues.io/terminal/), issue `{"req":"hub.status"}`. If `last_sync` is very recent, the Notecard is communicating. If `last_sync` is old or `status` is `error`, the Notecard cannot reach cellular or Notehub.

**Panic button or fall detection does not trigger.**
- Check the haptic motor for signs of life: power on the beacon, wait 5 seconds for boot to complete, then hold the panic button for 3+ seconds. You should feel a vibration within 1–2 seconds of pressing. If no vibration, check the motor's wiring to the DRV2605L and that the DRV2605L is seated on I²C (address 0x5A).
- Verify the LIS3DH is responding: after boot, if you see no double-buzz (fault pattern), the accelerometer initialized. Try triggering a fall: drop the beacon 50–80 cm onto a pillow or padded surface. If still no double-buzz on alert, check the LIS3DH's I²C wiring (SDA, SCL, 0x18 address) and that the SDO pin is grounded or left floating.
- Check the startup fault pattern: a slow double-buzz (buzz, pause, buzz) repeating every 5 seconds indicates a boot-time configuration error (missing `PRODUCT_UID`, failed `hub.set`, or template registration failure). Verify the `PRODUCT_UID` and power-cycle the beacon.

**Alerts appear in Notehub but coordinates are missing or stale.**
- `loc_age_s: -1.0` means the Notecard had no cached GPS fix when the alert fired. This is normal indoors. If outdoors with a clear sky view and `loc_age_s` is still -1 after multiple alerts, the Notecard may not have acquired a GPS fix since power-on. Run `{"req":"card.location"}` from the [Blues In-Browser Terminal](https://dev.blues.io/terminal/) to force a fix attempt and check the response.
- No `beacon_location.qo` follow-up Note means GPS did not acquire a fresh fix within the 90-second background search window. This is normal indoors or under heavy tree cover. If GPS should be working, trigger a test alert and check the Notecard's `card.location` response for lock time and signal quality.

**Battery drains too quickly.**
- Verify the host MCU is not stuck in a high-clock state. From the Arduino IDE Serial Monitor (115200 baud, after uncommenting `#define DEBUG_SERIAL` in the .ino file), you should see periodic log messages (one per outer loop pass, ~10 per second) with healthy current draws. If the Serial Monitor is active and you see frequent output, the debug serial is forcing the STM32 to maintain its USB clock — disable `DEBUG_SERIAL` to reduce MCU idle from ~10–15 mA to ~5–10 mA.
- Confirm the Notecard is in low-power periodic mode: issue `{"req":"hub.get"}` from the [Blues In-Browser Terminal](https://dev.blues.io/terminal/). If `outbound` is greater than 1440 (24 hours) or `inbound` is less than 120 (2 hours), sync cadence may be excessive. See [Section 5, step 5](#6-notehub-setup) for recommended settings.
- Use Mojo (see [Section 8](#9-validation-and-testing)) to measure actual current in different states (idle, fall/panic event, GPS search, sync). Compare against the estimated figures in the power validation table. If measured idle is >25 mA, a peripheral may be drawing unexpectedly — check I²C for clock-stretching issues or peripheral misconfigurations.

**Skylo satellite transmission fails or never completes.**
- The Skylo NTN path is only active after Notecard for Skylo has completed at least one successful cellular or WiFi sync to associate with your project and deliver the compact template definitions to Notehub. If the beacon launches in a no-cellular area, it cannot use satellite until it has found cellular (or WiFi) coverage at least once. Move the device to coverage, power it on, wait for a sync (check Notehub device status), then move back to the satellite-only zone.
- Verify the `MAIN` antenna orientation: it should face the sky (polycarbonate enclosure top is sufficient); in the northern hemisphere, southward orientation improves link margin. If the antenna is buried against a body or oriented away from the sky, Skylo acquisition may fail.
- Check Skylo coverage: the Skylo network covers specific regions (see the [Notecard for Skylo datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-nbglwx/)). If you're outside the coverage footprint, NTN transmission will fail silently.
- From the [Blues In-Browser Terminal](https://dev.blues.io/terminal/), issue `{"req":"card.transport","method":"ntn"}` to force satellite-only operation (cellular and WiFi disabled). Trigger a test panic from a location with sky view. Check the event metadata: if `"transport":"ntn"` is present, the satellite path is working. Re-enable automatic failover: `{"req":"card.transport","method":"wifi-cell-ntn"}`.

**Fall detection generates false positives during normal work.**
- Adjust the `freefall_g` and `impact_g` thresholds via environment variables (see [Section 5, step 5](#6-notehub-setup)). Increase `freefall_g` (e.g., 0.65 or 0.75) to require a deeper free-fall phase before the impact window opens. Increase `impact_g` (e.g., 3.0 or 3.5) to require a larger acceleration spike to confirm impact. Both changes reduce sensitivity and may suppress legitimate falls — validate with your specific worker activity profile.
- Shorten the `fall_window_ms` (e.g., 300 milliseconds instead of 500 milliseconds) to close the impact window sooner, requiring impact to occur more tightly coupled to the free-fall phase. This rejects impact spikes that occur seconds after a bump.
- Run a 24-hour learning period with each worker activity profile and log the Notecard's accelerometer telemetry (see README Section 6). Identify the baseline g-profile of normal work (walking, climbing, tool swings) and set thresholds to sit just above the highest "false positive" peak observed during normal use.

**Multiple alerts fire from a single fall.**
- The 60-second alert cooldown (`DEFAULT_ALERT_COOLDOWN_SEC`) prevents alert storms. A second fall detected within 60 seconds of the previous alert is suppressed — the button feels one buzz (acknowledgment of the press) instead of three (alert accepted). This is intentional. If you need multiple alerts for a tumbling device, increase the cooldown in the firmware or set it via an environment variable (when supported in a future revision).

## 11. Limitations and Next Steps

This is a reference design, not a finished safety product. Several things a real lone-worker fleet would demand have been left for a production team to add — most notably a certified safety claim and a sleeping host MCU — and they are listed below as scope choices, not surprises. The forward-looking work that turns this into a deployable wearable follows the limitations.

### Simplified for the POC

The simplifications below are scope choices, not surprises — each names something a real lone-worker fleet would add before deployment.

**Not a certified life-safety device.** This proof-of-concept has not been evaluated for fail-safe operation or certified under any personal-emergency-response or lone-worker protection standard. Validate all alert behaviors against your own safety requirements, and treat this design as a starting point rather than a production safety system. Supplement it with — **do not use it to replace** — mandatory check-in procedures, required PPE, and any regulated safety systems that apply to your operation.

**Host MCU never sleeps.** The firmware runs a dual-cadence polling loop: the LIS3DH is sampled at ~100 Hz via a 10-sample inner loop (10 milliseconds between reads), and Notecard/GPS/haptic state advances at the outer ~10 Hz cadence. A production implementation should use STM32L433 STOP2 mode with GPIO interrupt wakeup from the LIS3DH INT1 pin and the panic button, dropping host idle draw from ~10+ mA to ~2–3 µA. The LIS3DH's hardware free-fall interrupt handles Stage 1 detection in silicon without host MCU involvement — the host only wakes when the interrupt fires. **This is the single biggest power optimization available and the most important production change.**

**Fall detection is software-sampled.** The LIS3DH is sampled at ~100 Hz via the inner loop (10 reads per outer pass, 10 milliseconds apart), matching the sensor ODR and reliably catching free-fall phases as short as 80 ms. The remaining limitation is that Stage 1 (free-fall detection) is implemented in host firmware rather than the LIS3DH hardware interrupt registers, so the host MCU must stay awake continuously. Using the LIS3DH INT1 free-fall interrupt (via `INT1_CFG`, `INT1_THS`, `INT1_DURATION` registers) is the production upgrade: it offloads Stage 1 entirely to the accelerometer silicon, allows the STM32L433 to sleep in STOP2 between events, and eliminates the polling overhead.

**Fall detection thresholds are heuristic.** The default 0.55g free-fall / 2.5g impact thresholds cover textbook falls from standing height onto hard surfaces. They will produce false negatives for soft-surface landings (carpeted floors, mud) where the impact spike is attenuated, and may produce false positives during vigorous physical work involving overhead tool swings. Production deployments should run a calibration period with each worker activity profile before enabling real-time dispatch.

**No cancel flow after panic.** The current firmware has no mechanism for a worker to cancel a panic alert once it's been confirmed and sent. A production device should include a multi-step cancel: button press within 60 seconds of a panic, haptic confirmation, and a `cancel` Note that the dispatch system can act on.

**GPS follow-up Note is optional and serialized.** When a fall or panic alert fires, the initial `beacon_alert.qo` Note is queued immediately with the Notecard's cached location. The background GPS search then runs for up to `DEFAULT_GPS_TIMEOUT_SEC` (90 seconds) without blocking the detection loop. If a fresh fix arrives, a `beacon_location.qo` Note is queued with the event-time coordinates and the same `event_id`. If GPS times out — because the device is indoors, under heavy tree cover, or the Notecard has no sky view — only the initial alert Note is delivered and its cached location (which may be stale or empty) stands. Dispatch should treat a missing `beacon_location.qo` (matching `event_id`) as an indication that the event-time position is unknown, **not** that the alert failed. Only one GPS enrichment window can run at a time — a second alert that fires during the 90-second window (the 60-second cooldown is shorter than the GPS timeout) receives its cached location only and does not get a `beacon_location.qo` follow-up. A production system that requires fresh GPS for every alert should serialize the alert cadence (e.g., extend the cooldown to match the GPS timeout) or implement a per-alert GPS job queue.

**No data encryption.** Notes travel over TLS between Notecard and Notehub; the Notefile body is not additionally encrypted. For sensitive safety applications with worker location data, consider using `beacon_alert.qos` (`.qos` suffix enables encrypted transport at the Notecard level).

**Skylo NTN requires an initial non-NTN sync.** The Skylo satellite (NTN) path is not available until Notecard for Skylo has completed at least one successful cellular or WiFi session to associate with Notehub and register the Notefile templates. A device that ships directly into a no-cellular zone will be unable to send via satellite until it has found cellular (or WiFi) coverage at least once. Pre-provisioning during QA on a cellular-capable bench is the standard mitigation.

**Satellite payload budget.** The Notecard for Skylo bundle includes 10 KB of Skylo satellite data, and the Skylo NTN link enforces a hard 256-byte maximum per Note. The compact alert template is well under that ceiling; frequent triggering in a no-cellular environment will consume the bundle faster. Monitor satellite usage via [Notehub billing/usage data](https://dev.blues.io/notehub/notehub-walkthrough/#configuring-your-billing-account).

**Single I²C bus for all peripherals.** The LIS3DH and DRV2605L share the bus with the internal Notecard connection. A severe I²C lockup (e.g., a partially-completed transaction interrupted by a reset) could block all communication. A production design should include bus-error recovery and a hardware watchdog.

**No charging subsystem.** This POC documents a bare LiPo cell; no charger, dock, cradle, or inductive charging coil is included in the BOM or wiring. A production wearable needs an appropriate charging path, for example, a USB-C LiPo charging circuit integrated into the enclosure — plus overcharge and short-circuit protection if not already provided by the cell's built-in circuitry. Rechargeable operation is a natural next step but is out of scope for the POC.

**Mojo is bench-only in this POC.** The firmware does not read the Mojo's charge accumulation register over Qwiic — the Mojo is a bench measurement instrument only. A production extension could include a `mah_consumed` field in `beacon_alert.qo` for fleet-level battery-health monitoring, read via the Mojo's Qwiic I²C link.

### Production Next Steps

The forward-looking work that turns this into a deployable wearable follows, roughly from the most impactful power and safety changes to per-worker refinements.

**STM32L433 STOP2 low-power mode** with LIS3DH INT1 hardware interrupt wakeup is the single most impactful power optimization, dropping host idle from ~10–15 mA to ~2–3 µA.

**LIS3DH hardware free-fall and shock detection** configured via the `INT1_CFG`, `INT1_THS`, and `INT1_DURATION` registers offloads Stage 1 detection entirely to the accelerometer silicon.

**A cancel-alert flow** gives the worker a way out of a false alarm: a post-panic confirmation cancel within N seconds, routed as a `cancel` event on `beacon_alert.qo` carrying the same `event_id` as the alert being cancelled.

**Worker check-in acknowledgment** closes the loop back to the worker: a dispatcher can send a Notehub [Signal](https://dev.blues.io/api-reference/glossary/#signal) back to the device, triggering a distinctive haptic pattern so the worker knows their alert was received.

**Field-upgradeable firmware** via [Notecard Outboard DFU](https://dev.blues.io/notehub/host-firmware-updates/notecard-outboard-firmware-update/) lets threshold recipes be pushed to the whole fleet without a physical re-flash.

**A `.qos` encrypted Notefile** protects worker location data in privacy-sensitive jurisdictions.

**Per-worker baseline calibration** records each worker's typical activity vibration profile via a 24-hour learning period, then tunes `impact_g` and `freefall_g` individually.

## 12. Summary

The lineman at the edge of the substation, the pumper at the rural wellhead, the field tech in the 2 AM boiler room — each of them now clips on a device that does what no check-in procedure ever could: it watches them automatically, with no worker action required, and reaches a dispatcher even when cellular goes dark. The two-stage fall algorithm rejects everyday bumps without losing genuine falls; the panic button is there for the situations that don't look like physics; Notecard for Skylo's onboard satellite radio covers the specific sites where cellular fails first and matters most — no companion module, no second device. The cellular path handles the vast majority of activations quickly and inexpensively; the satellite path is the safety margin underneath it. That combination, in a belt-clip enclosure, is the practical shape of lone-worker safety assurance — supplementing, not replacing, the procedures and PPE that came before it.
