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
| `config.json` | valid. two radios, `r0` and `r1`, two handlers each, radios and gnss mocked. every other file here is written against it |
| `config-invalid.json` | parses, breaks two rules: the broker host is empty and the fetch timeout is zero |
| `config-missing-key.json` | does not parse, `broker_port` is missing |
| `config-two-radios.json` | valid. `r0` drives one channel and `r1` two, so a lookup landing on the wrong radio reads a channel count that does not fit |

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
| `rid-second-radio-only.json` | valid. gives settings to `r1` alone, which used to make the channel check read past the end of `radio_settings` |
| `rid-source-without-settings.json` | parses, a source names `r1`, which the config has and this rid never configured |
| `rid-gain-updates.json` | valid. ramps the gain of both channels of `r0`, one of them starting after the rid does |
| `rid-gain-update-without-settings.json` | parses, a gain update names `r1`, which this rid never configured |
| `rid-gain-update-channel-out-of-range.json` | parses, a gain update sits on channel 7 of a two channel radio |
| `rid-gain-update-same-channel-twice.json` | parses, two gain updates name channel 0 of `r0` |
| `rid-gain-update-zero-step.json` | parses, `step_interval_s` of a gain update is zero |
| `rid-gain-update-past-end.json` | parses, a gain update value is set at 0.75 s of a 0.5 s rid |
| `rid-gain-update-time-does-not-advance.json` | parses, two values of a gain update are set at the same time |
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
