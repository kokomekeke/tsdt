# SCG, THE COMMAND LINE TOOL

`scg` takes one scenario json and does one of three things with it. The modes are
mutually exclusive.

| mode | arguments | what happens |
|---|---|---|
| `-r`, `--record` | scenario json, output directory | the signal is generated and written to iq files |
| `-p`, `--playback` | a directory written by `-r` | those files are transmitted |
| `-s`, `--stream` | scenario json | generated and transmitted at once, nothing is saved |

`--help` and `--version` work as usual.

Record the scenario described by `test-3.json` into `recording_test`:

~~~bat
scg.exe -r ..\assets\scenario\test-3.json recording_test
~~~

Play that recording back:

~~~bat
scg.exe -p recording_test
~~~

Stream the scenario, generating and transmitting without saving:

~~~bat
scg.exe -s ..\assets\scenario\test-3.json
~~~

Streaming asks the machine to generate in real time, recording does not, so a scenario
too heavy to stream can still be recorded and played back.

A recording is the duration times the sample rate, so it grows quickly: the rids under
`assets/rid` are ten seconds at 1.92 megasamples a second, which is three hundred
megabytes.

`scglint scenario <path>` parses and validates a scenario without running anything.
