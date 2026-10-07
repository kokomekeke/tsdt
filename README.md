# SAGAX SCENARIO GENERATOR

The repository has the command line scenario generator, the Sisi generator, and tools related.
They are both here because they share the underlying engine.

| target | what it is |
|---|---|
| `scg` | records, plays back or streams a scenario described by a scenario json |
| `scgserver` | MQTT server, reads commands, prepares and plays tests, publishes status |
| `scgcommander` | console client that sends those commands through a broker |
| `scglint` | parses and validates scenario, config, rid and tsd json, without hardware |
| `mqtt_publisher`, `mqtt_subscriber` | examples that send and read plain text on a topic |

## Documentation

| document | what it covers |
|---|---|
| [docs/readme/building-windows.md](docs/readme/building-windows.md) | vcpkg and the msvc build |
| [docs/readme/building-linux.md](docs/readme/building-linux.md) | apt packages, oneAPI, UHD, Sidekiq, both builds |
| [docs/readme/build-options.md](docs/readme/build-options.md) | every meson option the build commands pass |
| [docs/readme/scg-cli.md](docs/readme/scg-cli.md) | `scg`, its three modes and what each writes |
| [docs/readme/scgserver.md](docs/readme/scgserver.md) | running the server and the commander against a broker |
| [docs/readme/server-config.md](docs/readme/server-config.md) | every key of a server config |
| [docs/readme/mqtt-protocol.md](docs/readme/mqtt-protocol.md) | the commands read and the messages published |
| [assets/README.md](assets/README.md) | the json files the programs ship with |
| [tools/README.md](tools/README.md) | the conformance harness, the broker container, the client mocker |
| [tests/README.md](tests/README.md) | the end to end test suite |
| [docs/public/overview.md](docs/public/overview.md) | what the pipeline is and how a scenario describes one |
| [docs/public/cdr.md](docs/public/cdr.md) | the interference generators the pipeline can build |
