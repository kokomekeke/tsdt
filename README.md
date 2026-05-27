
## Sagax Scenario Generator

There are 2 projects in the repo:

1. scenario generator command line tool with one target:
    - scg.exe: utility program used to record/playback/stream scenarios defined by a scenario.json file

2. server for SISI project, with multiple targets:
    - scgserver.exe: MQTT client server listening for commands, generating and playing scenarios, and sending logs
    - scgcommander.exe: MQTT publisher tool to control scgserver via the command line
    - mqtt_publisher.exe / mqtt_subscriber.exe: example mqtt programs that can send/receive plain text messages


## building instructions - windows

### vcpkg setup:

- Pick a location for vcpkg and install it from the git repo.
- For example C:\tools works.
- This is unironically the official method.

```
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

- After setup, put the vcpkg folder on the path.

### vcpkg install:

- vcpkg is needed to install some project dependencies.
- After vcpkg is installed, just run `vcpkg install`

### debug

- From the project root directory:

```
meson setup build-msvc -DSSG:have_usrp=true -DSSG:use_oneapi=true -Dcmake_prefix_path=%cd%\vcpkg_installed\x64-windows -Ddevenv_paths=%cd%/vcpkg_installed/x64-windows/bin
cd build-msvc
meson devenv
meson compile
```

If new dependencies are used, upload the updated `vcpkg_installed` directory to `S:\temp\3rdprat\scg_vcpkg_deps`


## build instructions - linux

1. download mosquitto `https://mosquitto.org/download/` or use vcpkg (i guess)

```
meson setup build -DSSG:have_usrp=true -DSSG:use_oneapi=true -DIntelOneAPI:oneAPIDevPath=$HOME/intel/oneapi
cd build
meson devenv
meson compile
```


## scg.exe examples

some examples of commands that should work with scg.exe

create a recording of the scenario described by `test3.json`, into the `recording_test` directory
```
scg.exe -r ..\scenarios\test3.json recording_test
```

play back the recorded scenario
```
scg.exe -p recording_test
```

stream the scenario (generate and transmit without saving)
```
scg.exe -s ..\scenarios\test3.json
```
