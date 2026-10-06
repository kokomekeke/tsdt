# ASSETS

The json files the programs are given. Nothing here is written by a test and nothing
here is only for a test; the files that exist for the test suite are under `tests/data`.

| directory | what is in it | what reads it |
|---|---|---|
| `config` | server configs | `scgserver <config>`, `scglint config`, `scglint rid`, `scglint tsd` |
| `rid` | radio interference descriptions | the `interference` field of a tsd step, `scglint rid` |
| `scenario` | scenarios, the form a rid is turned into | `scg`, `scglint scenario` |

A rid is only meaningful next to a config, because it names radios by the ids the config
gives them. Which rid goes with which config is not written down anywhere. The
`test-rid-sisi-stage-*` files go with `sisi-stage-*`, and the rest are accepted by
`config-1.json`.

`test_assets.py` in the test suite checks that every file here still passes the rules it
has to follow. That is a check on the data, not on the code.


## No tsd here

A TEST_SETUP command names a tsd, not a rid, and every step of the tsd names its rid by
an absolute url. An absolute url only holds on the machine it was written for, so a tsd
cannot ship with the repository the way the files here do. Write one next to the rids it
plays and give `scgserver` or `scglint tsd` the path to that.

## A note on the json

The parser allows `//` comments and a trailing comma, and most of these files use
comments. That is not standard json, so a tool that reads them with a plain json library
will refuse them.
