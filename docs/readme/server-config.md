# SERVER CONFIG

One json file, given to `scgserver` as its only argument, and to `scglint config`,
`scglint rid` and `scglint tsd` as their first. The ones that ship with the repository
are under `assets/config`.

The parser allows `//` comments and a trailing comma, so a plain json library will refuse
these files.

| key | required | what it is |
|---|---|---|
| `recording_directory` | yes | where a prepared test writes its recordings |
| `mock_radios` | yes | every radio is replaced by a device that only paces the pipeline |
| `status_update_period_s` | yes | how often the signal status is published while nothing changes |
| `start_ahead_s` | yes | how early the pipeline starts, its output held back until the exact time |
| `scenario_fetch_timeout_s` | no, 2.0 | the whole exchange with the machine a tsd or rid is read from |
| `gnss_ephemeris_file` | no, empty | rinex navigation file, needed by any rid with a gnss spoofer |
| `mock_gnss` | no, false | a gnss spoofer is recorded as silence instead of being generated |
| `radios` | yes | the radios this machine has, see below |
| `mqtt` | yes | broker and topics, see below |

## radios

Each entry has an `id`, a `type` and a `parameters` object. The id is what a rid names, so
a rid only makes sense next to the config that gives the radios their ids.

| type | parameters |
|---|---|
| `UsrpB200` | `serial` |
| `Sidekiq` | `serial`, and `handlers`, one transmit port per channel, named `A1`, `A2`, `B1`, `B2` |

## mqtt

| key | what it is |
|---|---|
| `broker_host` | the machine mosquitto runs on |
| `broker_port` | usually 1883 |
| `command_topic` | commands are read here |
| `test_log_topic` | test status messages are published here |
| `signal_log_topic` | signal status messages are published here |
| `error_log_topic` | error messages are published here |

Nothing stops two of these topics from being the same string, and the test suite points
the signal and error topics at one topic on purpose.

~~~json
{
    "recording_directory": "recordings",
    "mock_radios": true,
    "status_update_period_s": 60.0,
    "start_ahead_s": 2.0,
    "scenario_fetch_timeout_s": 2.0,
    "radios": [
        {
            "id": "r0",
            "type": "Sidekiq",
            "parameters": {
                "serial": "TEST0",
                "handlers": [ "A1", "B1" ]
            }
        }
    ],
    "mqtt": {
        "broker_host": "127.0.0.1",
        "broker_port": 1883,
        "command_topic": "cmd/test",
        "signal_log_topic": "tm/test",
        "test_log_topic": "tm/test/test",
        "error_log_topic": "tm/test"
    }
}
~~~

`scglint config <path>` checks one of these on its own.
