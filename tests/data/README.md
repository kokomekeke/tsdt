# TEST DATA

Files written to make the programs say one particular thing. Each name says what is
wrong with the file, or that nothing is. The files the programs ship with are under
`assets` and are checked separately, by `test_assets.py`.

Nothing here should be edited to make a test pass. If a file stops producing the message
the test expects, either the rule changed on purpose, and both the file and the test
should say so, or something broke.

## Configs

| file | what it is |
|---|---|
| `config.json` | valid. two radios, `r0` and `r1`, two handlers each, radios mocked. every other file here is written against it |
| `config-invalid.json` | parses, breaks two rules: the broker host is empty and the fetch timeout is zero |
| `config-missing-key.json` | does not parse, `broker_port` is missing |

`config.json` is also what the server fixture starts from. The broker host and port in
it are replaced at run time with the address of the broker in the test process, so the
values written in the file are never used.

## Rids

All of them are written against `config.json`.

| file | what it is |
|---|---|
| `rid.json` | valid. the only file in the suite with a `//` comment and a trailing comma in it, which is how the parser's own extensions get exercised |
| `rid-unknown-radio.json` | parses, names a radio the config does not have, in two places |
| `rid-negative-duration.json` | parses, `duration_s` is negative |
| `rid-channel-out-of-range.json` | parses, an interference source sits on channel 7 of a two channel radio |
| `rid-missing-channels.json` | does not parse, the `channels` key is gone |
| `not-json.json` | does not parse, it is a line of prose |

## Scenarios

| file | what it is |
|---|---|
| `scenario.json` | valid. one file output, one tone, one event |
| `scenario-missing-signal.json` | parses, an event names a signal that is not there |
| `scenario-event-past-end.json` | parses, an event runs past the end of the scenario |

The output is of type `File`, so nothing here depends on whether the build has usrp or
sidekiq support.
