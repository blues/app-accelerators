# Notehub Starter App Backend

This document details the various Notehub constructs that the Sparrow Reference Web App app uses to retrieve the data for gateways and nodes.

- [Nodes](#nodes)
- [Gateway Environment Variables](#gateway-environment-variables)
- [Note Databases](#note-databases)

# Nodes

## Determining the Nodes paired with a Gateway

The `sensors.db` file contains notes for each device paired with the gateway. The ID of each node is the ID of the device paired with the gateway. See [sensors.db Notefile](#sensorsdb-notefile).

## Notefiles and Sensor Readings

Sensor readings are sent as notes from the gateway device the node is paired with. The `id` of the note is named after the ID of the sensor node, followed by a hash and then the type of sensor reading. For example, an event with the properties

```json
 "2032374632365005001f000d#data.qo": {
    "body": {
        "count": 19,
        "sensor": "2F Ray's Bath SPARROW [87JFH688+2H]"
    },
    "device": "dev:868050040065365",
    ...
}
```

is a sensor reading from Node `2032374632365005001f000d`. The type of sensor reading is [`data`](#data-sensor), which is a test of device connectivity and information transfer from the gateway to the node.

## Sensor Types

| Sensor Name                       | Description                            | Example Data                                                                                                                                         |
| --------------------------------- | -------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| [data](#data-sensor)              | Device connectivity diagnostic details | <code>{ "count": 19, <br/>&emsp;&emsp;"sensor": "2F Ray's Bath SPARROW [87JFH688+2H]" }</code>                                                       |
| [air](#air-sensor)                | Temperature, Pressure, Humidity        | <code>{ "humidity": 27.234375,<br/>&emsp;&emsp;"pressure": 101152,<br/>&emsp;&emsp;"temperature": 22.6875,<br/>&emsp;&emsp;"voltage": 2.733 }</code> |
| [motion](#external-motion-sensor) | External Motion                        | <code>{ "count": 1,<br/>&emsp;&emsp;"total": 281 }</code>                                                                                            |

### Data Sensor

Notefile: `<device-id>#data.qo`

The data sensor (or "ping" sensor as it's called in the source code) is a diagnostic sensor used to send a "ping" message to the gateway.

<pre>{ "count": 19, <br/>&emsp;&emsp;"sensor": "2F Ray's Bath SPARROW [87JFH688+2H]" }</pre>

Each time a ping message is sent from the device, the count is incremented. If subsequent `data` events from the same device are not in sequence, this is an indication that messages may have been lost.

The `loc` property from `config.db` for the device is sent as part of the message. This can be used to verify that the location has been propagated through the Gateway to the device.

### Air Sensor

Notefile: `<device-id>#air.qo`

The air sensor reads air temperature, atmospheric pressure and humidity. It also reports the device power voltage.

Example event body

```json
{
  "humidity": 43.84375,
  "pressure": 102599.07,
  "temperature": 21.40625,
  "voltage": 3.3
}
```

Body schema

| property    | type  | description           | unit    |
| ----------- | ----- | --------------------- | ------- |
| humidity    | float | Air humidity          | %       |
| pressure    | float | Air pressure          | Pascals |
| temperature | float | Air temperature       | &deg;C  |
| voltage     | float | device supply voltage | V       |

### External Motion Sensor

Notefile: `<device-id>#motion.qo`

The PIR sensor senses external motion. Not to be confused with the accelerometer on Notecard, which detects device motion.

Each time a motion event is detected, the sensor increases the count. The number of detected motion events since the prevoius reading is sent as part of the sensor reading, along with the total number of motion events since the device was reset.

Example event body

```json
{
    "event": "a2520276-ec0f-414c-a2a0-69ee65769775",
    "session": "934d56ad-5264-4b0e-9fce-dc3787c79481",
    "best_id": "tool-couch-flower",
    "device": "dev:0d683572025c",
    "sn": "tool-couch-flower",
    "product": "product:net.ozzie.ray:sparrow",
    "received": 1641937157.092061,
    "routed": 1641937157,
    "req": "note.add",
    "when": 1641937152,
    "file": "20323746323650050033000d#motion.qo",
    "body": {
        "count": 2,
        "total": 12
    },
    ...
}
```

Body schema

| property | type   | description                                                             | unit |
| -------- | ------ | ----------------------------------------------------------------------- | ---- |
| count    | int_32 | The number of motion events detected since the previous sensor reading. | -    |
| total    | int_32 | The number of motion events detected since the device was reset         | -    |

# Gateway Environment Variables

- [`env_update_mins`](#env_update_mins)
- [`pairing_timeout_mins`](#pairing_timeout_mins)
- [`sensordb_update_mins`](#sensordb_update_mins)
- [`sensordb_reset_counts`](#sensordb_reset_counts)

## `env_update_mins`

Default value: 5 (minutes)
This variable specifies the number of minutes between asking the Notecard "has any update to the environment variables been received from the Notehub?" And if an update has occurred, the updated env vars (below) are pulled-in from the Notecard and are processed.

It's generally advised to not set this to a much lower value. Whenever the Gateway is doing some "overhead" task such as reading and processing and updating environment variables on the Notecard, it is "offline" with respect to LoRa and thus will miss any LoRa message that is sent to it during that period. As such, we try to do housekeeping tasks as seldomly as we can, and we try to do it when we seem to be idle.

## `pairing_timeout_mins`

Default value: 60 (minutes)
Defines how long the gateway will stay in pairing mode unattended before leaving pairing mode.

When the user walks over to the gateway and taps the blue button, it toggles Pairing mode on/off. It stays "on" for a while simply because you are likely to be walking around your home pairing one device after another.

However, if you left the gateway indefinitely in pairing mode, it might inadvertently allow someone else (your neighbor) to pair their devices with your gateway. This is likely not what you intend - and so pairing mode turns "off" after this timeout just in case you forget to do so.

## `sensordb_update_mins`

Default value: 60 (minutes)

The sensor database (`sensors.db`) contains statistics about the operations of each sensor, with each NoteID being the ID of the sensor node whose stats it contains.

The upside of keeping this database up-to-date is that the Notehub (and by extension the customer's cloud application) can see the message counts, RSSI, and so on, of each sensor node. The downside, as I noted above, is that whenever the gateway is spending time moving statistics from memory into the Notecard via `note.update`, it isn't listening to the network for LoRa activity.

## `sensordb_reset_counts`

Used to reset the statistics in `sensors.db`.

Normally, the "message count" statistics in the sensor database (`sensors.db`) just keep counting upward. Messages received, messages lost, and so on. Sometimes you will move nodes around, or even move the gateway around, and you'd just like to reset the statistics.

The way that you do this is to set this env var to the current unix epoch time. The Notecard will notice that this value has changed since the last time it checked, and will reset all the counters to 0.

# Note Databases

- [`config.db`](#configdb-notefile) - stores configuration details about each sensor node, such as its name and location
- [`sensors.db`](#sensorsdb-notefile) - stores connectivity details about each sensor node, such as signal strength

## `config.db` Notefile

The gateway uses this notefile in a read-only manner, to receive configuration information that it may use for itself and will pass-on to the nodes themselves. Although not strictly required, it is recommended that the cloud app use the HTTPS API to add or update notes within this database to distinguish one node from another, as the node IDs are fairly obscure.

The NoteID of each note is the node ID. The two (optional) fields currently in the body of each note are

- `name`: the human-readable name of the node
- `loc`: the Open Location Code string indicating the geolocation of the node

## `sensors.db` Notefile

The sensor database `sensors.db` contains statistics about the operations of each node, with each NoteID being the ID of the node whose stats it contains.

When a node is paired, the database is updated.
Otherwise, device metrics are updated at the interval given by the [`sensordb_update_mins`](#sensordb_update_mins) environment variable for the gateway.

This is an example of a note in the database file

```json
{
    "file": "sensors.db",
    "note": "20323746323650050033000d",
    "updates": 3,
    "body": {
        "gateway_rssi": -43,
        "gateway_snr": 7,
        "lost": 0,
        "received": 22,
        "sensor_ltp": -4,
        "sensor_rssi": -7,
        "sensor_snr": 7,
        "sensor_txp": -4,
        "voltage": 3.3,
        "when": 1641937118
    }
    ...
}
```

| property     | type   | description                                                  | unit |
| ------------ | ------ | ------------------------------------------------------------ | ---- |
| gateway_rssi | int_8  | Reciever's perspective of the Sender's signal strength       | dBm  |
| gateway_snr  | int_8  | Reciever's perspective of the Sender's signal to noise ratio | dB   |
| sensor_rssi  | int_8  | Node's perspective of the Gateway signal strength            | dBm  |
| sensor_snr   | int_8  | Node's perspective of the Gateway signal to noise ratio      | dB   |
| sensor_txp   | int_8  | Sender's transmit power level                                | dBm  |
| sensor_ltp   | int_8  | Node lowest transmit power attempted                         | dBm  |
| voltage      | float  | Core voltate of the node                                     | V    |
| lost         | int_32 | Count of messages lost                                       | -    |
| recieved     | int_32 | Count of messages received                                   | -    |
| when         | int_32 | Unix Epoc timestamp                                          | ms   |
