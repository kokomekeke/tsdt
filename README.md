
## Sagax Scenario Generator

There are 2 targets.

- scg.exe: Command line utility to stream/record/playback scenarios
- scgclient.exe: MQTT client, listens for commands and publishes logs

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


## build instructions - linux

idk lol



