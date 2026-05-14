# meshcore-companion-mqtt

A daemon that bridges a [MeshCore](https://meshcore.io/) mesh radio (connected via USB/serial) to an MQTT broker. It exposes node state as MQTT topics and accepts commands via MQTT to send messages and query device information.
This program is very much inspired by [meshcore-mqtt](https://github.com/ipnet-mesh/meshcore-mqtt), and you'll notice the MQTT interface is very similar to it.

## AI Notice

Only a small amount of AI was used in this program, such as converting Python data definitions to C++ structs and some serialization/deserialization as well as writing the first version of this README.md

## Configuration

Copy `meshcore-companion-mqtt.conf.example` to `meshcore-companion-mqtt.conf` in the same folder as your binary and edit as needed.

### `[MeshCore]` section

| Key | Default | Description |
|-----|---------|-------------|
| `Device` | `COM3:` / `/dev/ttyUSB0` | Serial port the MeshCore node is connected to |
| `ExpireUnseenContacts` | `3d (3 days)` | Remove contacts not seen within this duration. Supports suffixes: `h` (hours), `d` (days), `w` (weeks). e.g. `3d` |
| `MaxContactsListSize` | `500` | Maximum number of contacts to keep in memory. When full, the least-recently-seen contact is dropped |
| `DelayBetweenMessages` | `2000` | Minimum delay between outgoing messages, in milliseconds |
| `LogToConsole` | `false` | Log MeshCore serial traffic to the console |
| `LogToFile` | `false` | Log MeshCore serial traffic to `logs/companion.meshcore.log` |

### `[MQTT]` section

| Key | Default | Description |
|-----|---------|-------------|
| `Host` | `127.0.0.1` | MQTT broker hostname or IP |
| `Port` | `1883` | MQTT broker port |
| `Username` | _(none)_ | MQTT username |
| `Password` | _(none)_ | MQTT password |
| `TopicPrefix` | `meshmqtt` | Prefix applied to all published and subscribed topics |
| `LogToConsole` | `false` | Log MQTT traffic to the console |
| `LogToFile` | `false` | Log MQTT traffic to `logs/companion.mqtt.log` |

---

## Published Topics

All topics are prefixed with the configured `TopicPrefix` (default: `meshmqtt`).

### `{prefix}/self_info` — retained

Published when the node's self-info is received. Contains the identity and radio configuration of the local MeshCore node.

```json
{
  "name": "MyNode",
  "public_key": "aabbccdd...",
  "advertisement_type": 1,
  "tx_power": 20,
  "max_tx_power": 22,
  "latitude": 37.123456,
  "longitude": -122.123456,
  "multi_acks": 0,
  "advertisement_location_policy": 1,
  "telemetry_mode": 0,
  "manual_add_contacts": 0,
  "radio_frequency": 915.0,
  "radio_bandwidth": 125.0,
  "radio_spreading_factor": 10,
  "radio_coding_rate": 5
}
```

### `{prefix}/device_info` — retained

Published when hardware/firmware information is received from the node.

```json
{
  "firmware_version": 10,
  "max_contacts": 200,
  "max_channels": 8,
  "ble_pin": 123456,
  "firmware_build": "abc123",
  "model": "T-Beam",
  "version": "2.1.0",
  "client_repeat": 1,
  "path_hash_mode": 0
}
```

### `{prefix}/contacts` — retained

Published after a full or partial contacts refresh. The payload is a JSON object keyed by each contact's full public key (hex).

```json
{
  "aabbccdd...": {
    "type": 1,
    "public_key": "aabbccdd...",
    "name": "Alice",
    "latitude": 37.123456,
    "longitude": -122.123456,
    "flags": 0,
    "out_path_len": 1,
    "last_seen": 1700000000
  }
}
```

`type` values: `0` = none, `1` = chat, `2` = repeater, `3` = room.

### `{prefix}/new_contact` — not retained

Published when a previously unknown contact is seen for the first time, after the initial contacts list has been sent. Same fields as a single entry in `{prefix}/contacts`.

```json
{
  "type": 1,
  "public_key": "aabbccdd...",
  "name": "Bob",
  "latitude": 0.0,
  "longitude": 0.0,
  "flags": 0,
  "out_path_len": 2,
  "last_seen": 1700000000
}
```

### `{prefix}/channel_info/{index}` — retained

Published for each channel (index 0–39) when channel info is received.

```json
{
  "channel_index": 0,
  "name": "General",
  "secret": "0011223344556677..."
}
```

### `{prefix}/message/direct/{pubkey_prefix}` — not retained

Published when a direct (private) message is received. `pubkey_prefix` is the 12-character hex prefix of the sender's public key. `public_key` is included if the sender is in the contacts list.

```json
{
  "public_key": "aabbccdd...",
  "public_key_prefix": "aabbccdd1122",
  "txt_type": 0,
  "timestamp": 1700000000,
  "path_length": 1,
  "message": "Hello!"
}
```

`txt_type` values: `0` = plain text, `1` = CLI data, `2` = signed plain text.

### `{prefix}/message/channel/{channel_index}` — not retained

Published when a channel message is received.

```json
{
  "channel_index": 0,
  "path_length": 2,
  "txt_type": 0,
  "timestamp": 1700000000,
  "from": "Alice",
  "message": "Hello everyone!"
}
```

### `{prefix}/advertisement` — not retained

Published when a node advertisement packet is received.

```json
{
  "public_key": "aabbccdd..."
}
```

---

## Commands

Commands are sent by publishing a JSON payload to `{prefix}/command/{command_name}`.

### `get_self_info`

Requests the node's self-info. The response is published to `{prefix}/self_info`.

```
Topic:   meshmqtt/command/get_self_info
Payload: {}
```

### `get_device_info`

Requests hardware/firmware info from the node. The response is published to `{prefix}/device_info`.

```
Topic:   meshmqtt/command/get_device_info
Payload: {}
```

### `get_contacts`

Returns the current contacts list. If `force_refresh` is `true` or no contacts are cached, a fresh list is fetched from the device. The response is published to `{prefix}/contacts`.

```
Topic:   meshmqtt/command/get_contacts
Payload: {}

# Force a fresh fetch from the device:
Payload: { "force_refresh": true }
```

### `get_channels`

Returns info for all channels (0–39). If `force_refresh` is `true` or no channels are cached, they are re-fetched from the device. Responses are published to `{prefix}/channel_info/{index}`.

```
Topic:   meshmqtt/command/get_channels
Payload: {}

# Force a fresh fetch from the device:
Payload: { "force_refresh": true }
```

### `get_channel`

Returns info for a single channel. `channel` is required and must be 0–39. If `force_refresh` is `true` or the channel is not cached, it is re-fetched from the device. The response is published to `{prefix}/channel_info/{index}`.

```
Topic:   meshmqtt/command/get_channel
Payload: { "channel_index": 0 }

# Force a fresh fetch from the device:
Payload: { "channel_index": 0, "force_refresh": true }
```

### `send_channel_msg`

Sends a text message to a channel. `channel` must be 0–39 and `message` must be non-empty.

```
Topic:   meshmqtt/command/send_channel_msg
Payload: { "channel_index": 0, "message": "Hello channel!" }
```

Failed sends are automatically retried up to 3 times.

### `send_direct_msg`

Sends a direct message to a contact. `destination` must be either a full 64-character hex public key or a 12-character hex public key prefix. `message` must be non-empty and is truncated to 160 bytes. `txt_type` is optional and defaults to `0` (plain text).

```
Topic:   meshmqtt/command/send_direct_msg

# Using a full public key:
Payload: { "destination": "aabbccdd...(64 hex chars)", "message": "Hello!" }

# Using a public key prefix:
Payload: { "destination": "aabbccdd1122", "message": "Hello!" }

# With an explicit text type:
Payload: { "destination": "aabbccdd1122", "message": "Hello!", "txt_type": 0 }
```

`txt_type` values: `0` = plain text, `1` = CLI data, `2` = signed plain text.

Failed sends are automatically retried up to 3 times.

## Dependencies

- **Drift Standard Library (DSL)** with libevent support (`drift/dsl.h`)
- **libmosquitto** — Mosquitto MQTT client library
- **univalue** — JSON parsing (included as a subdirectory for Windows, use system-wide library for Linux)
