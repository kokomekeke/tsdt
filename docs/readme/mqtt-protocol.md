# MQTT PROTOCOL

What `scgserver` reads on its command topic and what it publishes. The topics are named
by the config, see [server-config.md](server-config.md). Everything is published at QoS 2
with the retain flag clear.

A json key the server does not know is ignored, so a message may carry more than what is
listed here.

## Commands

Each one is an object with a `command` and an `extras`.

| command | fields in `extras` | what it does |
|---|---|---|
| `TEST_SETUP` | `scenario_url`, `start_time`, `test_id` | schedules a test, the url names a tsd |
| `TEST_CANCEL` | `test_id` | drops a test, stopping it first if it is the one running, and removes its directory |
| `INT_STOP` | none | stops the transmission of the test that is running and keeps its directory |
| `INT_START` | none | read and ignored, a warning is logged |
| `TEST_START` | none | read and ignored |
| `TEST_UPDATE` | none | read and ignored |
| `TEST_END` | none | read and ignored |

`start_time` is ISO 8601 with an offset, `2026-09-02T14:30:00+02:00`.

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
| `TEST_FAILED` | gave up, the reason went out as an error message |

## Signal status

Published on the signal log topic when the state changes, and every
`status_update_period_s` while it does not.

~~~json
{ "timestamp": "2026-09-21T14:10:00.000Z", "type": "jamming", "description": {} }
~~~

`type` is `none`, `jamming` or `spoofing`. The IRD leaves the content of `description` to
us and it is an empty object for now.

## Error

Published on the error log topic, and written to the server log at the same time.

~~~json
{ "message": "no test is running" }
~~~

A rid that fails validation sends every reason in one of these, so it can be long.

## The message log

Both programs log one line per message in each direction, with the topic, the QoS, the
retain flag, the size and the payload. A payload over 256 bytes is replaced with
`[hidden]` to keep the line readable.

~~~
mqtt in [cmd/test] qos 1 retain 0 (37 bytes): {"command": "INT_STOP", "extras": {}}
~~~
