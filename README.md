# MeshCore Projects

A collection of projects for working with [MeshCore](https://meshcore.io) mesh radio networks.

## Projects

- [meshcore-companion-mqtt](meshcore-companion-mqtt/) — Application that bridges a MeshCore companion node (via USB/serial) to an MQTT broker, exposing channels and contacts over MQTT and allowing you to send and receive messages.

- [libmeshcoremqttclient](libmeshcoremqttclient/) — C++ client library for [meshcore-companion-mqtt](meshcore-companion-mqtt/) above.

- [meshcore-mqtt-irc](meshcore-mqtt-irc/) — Virtual IRC server, letting you connect with your IRC client and talk on your node as if it were IRC. Uses [meshcore-companion-mqtt](meshcore-companion-mqtt) as the backend.

## License

BSD 3-Clause — see [LICENSE](LICENSE).
