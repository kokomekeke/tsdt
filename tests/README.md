# TESTS

End to end tests for `scgserver` and `scglint`. They run the built programs, so a build
has to exist, and the build directory is a required argument.

Nothing outside pytest is needed. The broker and the http servers the tests need run
inside the test process, so there is no mosquitto to install and no container to start.

~~~bash
pip install -r tests/requirements.txt
python -m pytest tests --build-dir=build-msvc
~~~

On linux the same line with the directory that build sits in.

## What is where

| file | what it covers |
|---|---|
| `test_fetch.py` | every way reading a tsd and the rid of a step can go, over mqtt and through `scglint` |
| `test_lint.py` | `scglint`, against the files in `data` and the tsds the tests write |
| `test_assets.py` | the json files in `assets`, a check on the data rather than on the code |
| `test_commands.py` | commands the server has to refuse, and cancelling |
| `test_stop.py` | INT_STOP, which ends the transmission and keeps the directory |
| `test_schedule.py` | the overlap rule and the recording queue rule |
| `test_startup.py` | exit codes, `--help`, `--version`, bad config files |

| helper | what it is |
|---|---|
| `helpers/binaries.py` | finds the programs and the environment `meson devenv` sets up |
| `helpers/mqttbroker.py` | a small MQTT 3.1.1 broker that runs in the test process |
| `helpers/httpmock.py` | an http server that answers badly on purpose |
| `helpers/rid.py` | writes short rid files so recordings cost nothing |
| `helpers/tsd.py` | writes the tsd that names those rids, which is what a command points at |
| `helpers/server.py` | runs one `scgserver` with a config of its own |
| `data/` | json files written to make the programs say one particular thing, see `data/README.md` |

## Two kinds of json

`data` holds files that exist to test the programs. Each one is written to produce one
message, and the name says which. `assets` holds the files the programs ship with, and
`test_assets.py` checks those still pass the rules they have to follow. A failure there
means a file in the repository is wrong, not that the code broke.

Nothing in `data` is generated at run time, and nothing in `assets` is used to test the
linter. The rid files the server tests schedule are a third thing again: those are
written into a temporary directory by `helpers/rid.py`, because their duration and
sample rate are chosen by the test that needs them.

## Two documents per test

A TEST_SETUP command names a tsd, and every step of the tsd names the rid it plays by
url. So a test that schedules anything needs two files, and both are written into the
temporary directory: `helpers/rid.py` writes the rid and `helpers/tsd.py` wraps it in a
tsd of one step. The `tsd_path` fixture does both and is what most tests pass to
`setup`.

A step url has to be absolute, which is why these files cannot live under `assets`. The
tsds the tests write point at a rid in the same temporary directory with a `file://`
url, so only the document under test travels over http.

## How a test runs the server

Each test that needs the server gets its own. It is given a config written into the
temporary directory of that test, so the recordings land there instead of in the
repository, and command and error topics of its own, so an answer from an earlier test
cannot be mistaken for its own.

A test publishes a command, then waits for a message on its error topic with a deadline.
A test that expects the command to be accepted waits for no message instead, and then
looks for the line in the server log that says the test was scheduled.

## Disk

A test that schedules a test has it recorded, because the server records everything that
is waiting. The size of a recording is the duration times the sample rate, and
`bandwidth_hz` in a rid is the sample rate, so the files the tests write are short and
narrow: 192 kHz, or 48 kHz where the scenario has to be long for the rule being tested.
The files under `assets/rid` are ten seconds at 1.92 megasamples a second, which is
three hundred megabytes a recording. A whole run leaves about 80 MB in the pytest temporary
directory. pytest keeps the last three runs and removes the rest.

## The dlls on windows

`scgserver.exe` does not start from the build directory on its own. It loads
`mosquitto.dll`, which sits in the vcpkg directory and is not copied next to the
executables the way the uhd ones are. The directory holding it goes on `PATH` through
the `devenv_paths` option, and the tests ask `meson devenv` for that environment and
give it to every process they start, so `meson` has to be on the path.

The option has to name the directory of the same build the import libraries come from,
`vcpkg_installed/x64-windows/debug/bin` for a debug build and
`vcpkg_installed/x64-windows/bin` for a release one.

~~~bash
meson configure build-msvc -Ddevenv_paths=C:/sgx/scg/vcpkg_installed/x64-windows/debug/bin
~~~

`test_startup.py` fails on purpose when the harness had to fall back to guessing those
paths itself, so this cannot go unnoticed.

## The http mock by hand

`helpers/httpmock.py` can be used outside the tests. Each instance answers one way:

| mode | what it does |
|---|---|
| `serve` | 200 with the file, 404 when it is missing |
| `stall` | accepts the connection, reads the request, then says nothing |
| `slow` | 200 with real headers, then the body a few bytes at a time |
| `status` | answers with the code and reason it is given |
| `big` | 200 with a body larger than the 64 MB limit |
| `redirect` | 302 with a Location header |

~~~python
from helpers.httpmock import HttpMock

with HttpMock(mode="stall", seconds=30.0) as mock:
    print(mock.url("rid.json"))
~~~

The port is picked by the operating system, so nothing has to be free beforehand.

## What is not covered

`scg`, the command line tool, is not tested. A test of it writes real wav files and
takes as long as the scenario it is given.

Stopping the server with a signal is not tested either. On windows a test cannot send a
real Ctrl+C to a child process without putting it in a console group of its own, and
Ctrl+Break arrives as a signal the server does not listen for.
