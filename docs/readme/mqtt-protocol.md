# MQTT PROTOCOL

What `scgserver` reads on its command topic and what it publishes. The topics are named
by the config, see [server-config.md](server-config.md). The test status and the signal
status are published at QoS 2, the session log at QoS 1, all with the retain flag clear.

A json key the server does not know is ignored, so a message may carry more than what is
listed here.

## Commands

Each one is an object with a `command` and an `extras`.

| command | fields in `extras` | what it does |
|---|---|---|
| `TEST_SETUP` | `scenario_url`, `start_time`, `test_id` | schedules a test, the url names a tsd |
| `TEST_START` | `test_id` | authorizes a scheduled test, until its start time |
| `TEST_CANCEL` | `test_id` | drops a test, stopping it first if it is the one running |
| `INT_STOP` | none | stops the transmission of the test that is running, the test fails |
| `INT_START` | none | read and ignored, a warning is logged |
| `TEST_UPDATE` | none | read and ignored |
| `TEST_END` | none | read and ignored |

`start_time` is ISO 8601 with an offset, `2026-09-02T14:30:00+02:00`.

The pipeline of a test starts `start_ahead_s` before the start time either way, but hands
no sample to the radios until `TEST_START` came. A test that is not authorized by its start
time fails. The directory of a test is removed once it is completed, failed or cancelled.

~~~json
{
    "command": "TEST_SETUP",
    "extras": {
        "scenario_url": "file:///tmp/tsd.json",
        "start_time": "2026-09-21T16:10:00+02:00",
        "test_id": "1"
    }
}
~~~

## Test status

Published on the test log topic whenever a test changes state.

~~~json
{ "timestamp": "2026-09-21T14:10:00.000Z", "test_id": "1", "test_status": "TEST_RUNNING" }
~~~

| value | when |
|---|---|
| `TEST_INIT` | the command was accepted |
| `TEST_PREPARE` | the recordings are being written |
| `TEST_READY` | prepared, waiting for its start time |
| `TEST_RUNNING` | the first samples went to the radio, the timestamp is that moment |
| `TEST_COMPLETED` | finished |
| `TEST_FAILED` | gave up, or the `TEST_SETUP` was refused. the reason went out as a session log. a setup whose test id is already scheduled gets no status |

## Signal status

Published on the signal log topic when the state changes, and every
`status_update_period_s` while it does not.

~~~json
{ "timestamp": "2026-09-21T14:10:00.000Z", "type": "jamming", "description": {} }
~~~

`type` is `none`, `jamming` or `spoofing`. The IRD leaves the content of `description` to
us and it is an empty object for now.

## Session log

TN5-IRD-LOG. Published on the error log topic, and written to the server log at the same
time. `session_id` is the test id, it is left out when the event belongs to no test.
`severity` is `error` for anything that keeps a test from being scheduled or played, and
`warning` for a cancel or a stop that had nothing to act on.

~~~json
{
    "event_id": "d6a14e55-610c-42dc-89d4-6d367abf37e2",
    "timestamp": "2026-09-21T14:10:00.000Z",
    "session_id": "1",
    "subsystem": "rsg",
    "severity": "error",
    "message": "could not prepare test \"1\": ..."
}
~~~

A rid that fails validation sends every reason in one of these, so it can be long.

## The message log

Both programs log one line per message in each direction, with the topic, the QoS, the
retain flag, the size and the payload. The server only does this while `log_mqtt` is true
in its config. A payload over 256 bytes is replaced with
`[hidden]` to keep the line readable.

~~~
mqtt in [cmd/test] qos 1 retain 0 (37 bytes): {"command": "INT_STOP", "extras": {}}
~~~
