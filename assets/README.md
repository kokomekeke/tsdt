# ASSETS

The files the programs are given. Nothing here is written by a test and nothing here is
only for a test; the files that exist for the test suite are under `tests/data`.

| directory | what is in it | what reads it |
|---|---|---|
| `config` | server configs | `scgserver <config>`, `scglint config`, `scglint rid`, `scglint tsd` |
| `rid` | radio interference descriptions | the `interference` field of a tsd step, `scglint rid` |
| `tsd` | test scenario definitions, the document a TEST_SETUP names | `scgserver`, `scglint tsd` |
| `scenario` | scenarios, the form a rid is turned into | `scg`, `scglint scenario` |
| `rinex` | a gnss navigation file | `gnss_ephemeris_file` of a config, `ephemerisFile` of a `GnssSpoofing` signal |

## Configs

| config | what it is |
|---|---|
| `config-1.json` | mocked radios, for the `test-rid-1` to `test-rid-3` rids |
| `sisi-stage-mock.json` | the stage machine with its radios and gnss mocked, broker on this machine |
| `sisi-stage-live.json` | the same radios driven for real, broker on 10.1.1.81 |
| `sisi-neurontest-mock.json` | `sisi-stage-mock.json` with the broker on 10.1.2.44 and gnss not mocked |
| `sisi-neurontest-live.json` | the same file as `sisi-stage-live.json` |
| `sisi-stage-hadipari.json` | the stage radios and the gnss engine driven for real, the ephemeris from `rinex` at its path on the stage machine |

A rid is only meaningful next to a config, because it names radios by the ids the config
gives them. Which rid goes with which config is not written down anywhere else:

| rid | config |
|---|---|
| `test-rid-1` to `test-rid-3` | `config-1.json` |
| `test-rid-sisi-stage-1` to `test-rid-sisi-stage-4` | `sisi-stage-*`, `sisi-neurontest-*` |
| `test-rid-sisi-hadipari` | `sisi-stage-hadipari.json` |
| `gps_spoofer_l1` | none yet, it came from the harness and no config here accepts it |

A live config names sidekiq cards, so `scglint` only accepts it from a build with sidekiq
support.

`test_assets.py` in the test suite checks that every config, rid and scenario here still
passes the rules it has to follow. That is a check on the data, not on the code. The tsds
and the rinex file are not checked.

## Tsds

Every step of a tsd names its rid by url, and the server only reads absolute ones, so a
tsd only works on the machine its urls were written for.

| tsd | its steps |
|---|---|
| `l1-jamming-urban.json` | `test-rid-sisi-stage-1`, by its path on the stage machine |
| `l1-jamming-hadipari.json` | `test-rid-sisi-hadipari`, by its path on the stage machine |
| `gps-spoofing-highway.json` | `rid/gps_spoofer_l1.json`, a relative url as the harness writes it, which the server refuses |
| `urban-canyon-baseline.json` | no rid at all, which the server refuses as a test with nothing to play |

The ones from the harness keep its own fields as well, `image` and `driver_instructions`
among them, which the server reads past.

## Rinex

`BRDC00IGS_R_20211700000_01D_MN.rnx` is the IGS broadcast navigation file of the 19th of
June 2021, for every constellation. The gnss engine moves its ephemerides to the time it
generates for, so an old file still places the satellites.

## A note on the json

The parser allows `//` comments and a trailing comma, and most of these files use
comments. That is not standard json, so a tool that reads them with a plain json library
will refuse them.
