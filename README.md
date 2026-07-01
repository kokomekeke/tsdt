
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

### 1. download mosquitto `https://mosquitto.org/download/` or use vcpkg (i guess)
~~~bash
# Ubuntu:
sudo apt install libmosquitto-dev
~~~

### 2. install intelOneAPI  'https://www.intel.com/content/www/us/en/docs/oneapi-toolkit/installation-guide-linux/latest/install-oneapi-toolkit-with-apt.html#INSTALL-ONEAPI-TOOLKIT-WITH-APT'


~~~bash
#Ubuntu:
# download the key to system keyring
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
| gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null

# add signed entry to apt sources and configure the APT client to use Intel repository:
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list

# System update
sudo apt update

# Install Intel oneAPI package
sudo apt install intel-oneapi-toolkit

# create oneAPI path variable (NOTE: use youre path default on ubuntu: /opt/intel/oneapi)
export oneAPIpath="/opt/intel/oneapi"
~~~

### 3. UHD Install
NOTE: Lehet érdemesebb ubuntun is inkább sourceból buildelni ezt majd meglátjuk
~~~bash
# Ubuntu:
sudo apt install libuhd-dev
~~~

**UHD Dependency match**
Ha összeakadnak az UHD verziók a rendszerben mert több is fel van telepítve érdemes lehet kikeresni a telepített verziót és kiexportálni a pathot
~~~bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
~~~

Ha nem megfelelő valamiért a gcc és a g++ verzió a linuxon akkor azt is ki kell exportálni meson setup előtt
~~~bash
export CC=gcc-13
export CXX=g++-13
~~~

TODO: Az SgxExtRoot változó nálam nem működött mezonban ezt meg kell nézni miért nem működik.
~~~bash
# NOTE: use your path to the largeshare
export sgx_ext_path="/mnt/largeshare/temp/3rdparty/sgx-ext-20250120"
~~~

**UHD linking error: (Ubuntu 24.04)**  
Nálam elhasalt az UHD és nem tudta elcatchelni az exceptiont sem, mert linkelési hiba volt.
a *meson.build*-ből kivettem a static linkelését a gcc-nek és az stdc-nek és utána futott rendesen.
~~~bash
cpp_args = []
link_args = []
# cpp_args = ['-static-libgcc', '-static-libstdc++']
# link_args = ['-static-libgcc', '-static-libstdc++']
~~~
### 4. Install CMAKE
~~~bash
# Ubumtu:
sudo apt install cmake
~~~

### 5. Install libcjson
~~~bash
# Ubuntu:
sudo apt install libcjson-dev
~~~
### 6. Build
```bash
meson setup build -DSSG:have_usrp=true -DSSG:use_oneapi=true -DIntelOneAPI:oneAPIDevPath=$oneAPIpath -DUHD:SgxExtRoot=sgx_ext_path
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
