# meshcore-mqtt-irc

A virtual IRC server that bridges a [MeshCore](https://meshcore.co.uk/) mesh radio node to standard IRC clients via MQTT in conjunction with [meshcore-mqtt](https://github.com/fdlamotte/meshcore_mqtt). Connect any IRC client to it and chat on the mesh network as if it were an IRC network.

## How it works

First, you need `meshcore-mqtt` up and running; then `meshcore-mqtt-irc` connects to the same MQTT broker. It subscribes to the node's topics to learn about channels, contacts, and messages, then exposes all of that over a local IRC server.

- MeshCore channels appear as IRC channels (e.g. `#General`, `#meshcore`)
- MeshCore contacts appear as IRC users
- Your own node's identity becomes the IRC client's identity — nick changes from IRC are not supported
- Messages sent from IRC are forwarded to the mesh; messages received from the mesh appear in IRC

The server waits until it has received the initial channel list, contacts list, and self-info from the MQTT broker before accepting any IRC connections.

## Why run over MQTT instead of a direction connection?

Using MQTT lets several programs all share the one node instead of having to buy a bunch of them.

## AI Notice

Only a small amount of AI was used in this program, such as generating irc_numerics.h and some serialization/deserialization as well as writing the first version of this README.md

## IRC Support

This is a partial IRC implementation. The following commands are supported:

### Registration (pre-login)

| Command | Notes |
|---------|-------|
| `NICK` | Accepted but ignored — your nick is taken from the MeshCore node name |
| `USER` | Accepted but ignored — identity comes from the node |
| `CAP` | Silently accepted; no capabilities are negotiated |

### Always available

| Command | Notes |
|---------|-------|
| `PING` | Responded to with `PONG` |
| `PONG` | Accepted (used to respond to server keepalives) |
| `QUIT` | Disconnects the client |

### After login

| Command | Notes |
|---------|-------|
| `JOIN #channel` | Joins the view of an existing MeshCore channel. Only channels known from the node's config are available; you cannot create new channels from IRC |
| `PART #channel` | **Not supported** — channel membership is determined by the MeshCore node config, not the IRC client |
| `PRIVMSG #channel msg` | Sends a message to a MeshCore channel |
| `PRIVMSG nick msg` | Sends a direct message to a MeshCore contact (requires their public key to be known); nick can also be a full hostmask |
| `NOTICE #channel msg` | Sends a NOTICE to a channel; transmitted on the mesh with a special prefix |
| `NOTICE nick msg` | Sends a NOTICE to a contact; nick can also be a full hostmask |
| `NAMES [#channel]` | Lists users in a channel (or all channels) |
| `WHOIS nick` | Returns the user's MeshCore username, public key, idle time, and channels |
| `MODE #channel` | Returns the channel mode (`+nt` or `+nts` for private channels); setting modes is not supported |
| `MODE yournick` | Returns your user mode (`+`); setting modes is not supported |
| `TOPIC #channel` | Always returns "No topic is set"; setting topics is not supported |
| `USERS` | Lists all known MeshCore users with their IRC nick, MeshCore username, and public key |
| `ISON nick [nick ...]` | Checks which nicks are currently online |
| `USERHOST nick [nick ...]` | Returns hostmask and away status for the given nicks |
| `VERSION` | Returns the server version |
| `NICK` | Tells you to change it in MeshCore |

## Hostmask format

Each MeshCore user appears on IRC with the hostmask:

```
irc_nick!meshcore@<pubkey>
```

The IRC nick is the MeshCore node name sanitized to valid IRC characters (non-alphanumeric characters replaced with `_`, leading/trailing hyphens removed). The username is always `meshcore` and the host is the node's full public key.

## Auto-join and voice

On login, the client is automatically joined to all channels known from the MeshCore node. Users who have sent a message within the configured idle window are given voice (`+v`) in channels so they are visually distinct in IRC clients. Voice is removed when a user goes idle, and users are automatically parted from channels (and eventually expired from the server) if they have not been seen for the configured durations.

## Configuration

Copy `meshcore-irc.conf.example` to `meshcore-irc.conf` and edit (place in same directory as executable).

If no config file is found, it will try to run on defaults.

## Dependencies

- **Drift Standard Library (DSL)** with libevent support (`drift/dsl.h`)
- **libmosquitto** — Mosquitto MQTT client library
- **univalue** — JSON parsing (included as a subdirectory for Windows, use system-wide library for Linux)
- **libevent** — async I/O (via DSL)

## Building

Windows: A Visual Studio 2022 solution is provided (`meshcore-mqtt-irc.sln`).
Linux: A CMake file is included, create a `build` directory and compile as usual for CMake.

## MQTT topics

The server subscribes to `{TopicPrefix}/#` and uses the following topics:

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `{prefix}/self_info` | subscribe | Own node name and public key |
| `{prefix}/contacts` | subscribe | Full contacts list |
| `{prefix}/new_contact` | subscribe | Newly discovered contact |
| `{prefix}/channel_info` | subscribe | Channel names and keys |
| `{prefix}/advertisement` | subscribe | Node advertisements (updates last-seen time) |
| `{prefix}/message/channel/{idx}` | subscribe | Incoming channel messages |
| `{prefix}/message/direct/{...}` | subscribe | Incoming direct messages |
| `{prefix}/command/get_self_info` | publish | Request own node info |
| `{prefix}/command/get_contacts` | publish | Request contacts list |
| `{prefix}/command/get_channels` | publish | Request channel list |
| `{prefix}/command/send_chan_msg` | publish | Send a channel message |
| `{prefix}/command/send_msg` | publish | Send a direct message |
