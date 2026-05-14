# libmeshcoremqttclient

A C++ client library for interacting with `meshcore-companion-mqtt` on MQTT. Handles all the JSON parsing, MQTT connection details, etc. for you.

## Dependencies

- **Drift Standard Library (DSL)** with libevent support (`drift/dsl.h`)
- **libmosquitto** — Mosquitto MQTT client library
- **univalue** — JSON parsing (included as a subdirectory for Windows, use system-wide library for Linux)

## Building

Windows: A Visual Studio 2022 solution is provided (`meshcore.sln`).
Linux: A CMake file is included, create a `build` directory and compile as usual for CMake.
