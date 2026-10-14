# BUILDING ON LINUX

Written against Ubuntu. The options these commands pass are in
[build-options.md](build-options.md).

There are two builds, one for USRP and one for Sidekiq, because the radio support is
chosen at configure time.

## 1. mosquitto

~~~bash
sudo apt install libmosquitto-dev
~~~

Or from [mosquitto.org/download](https://mosquitto.org/download/).

## 2. Intel oneAPI

The [apt instructions are here](https://www.intel.com/content/www/us/en/docs/oneapi-toolkit/installation-guide-linux/latest/install-oneapi-toolkit-with-apt.html#INSTALL-ONEAPI-TOOLKIT-WITH-APT).

~~~bash
# the key into the system keyring
wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB \
| gpg --dearmor | sudo tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null

# the signed entry into the apt sources
echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list

sudo apt update
sudo apt install intel-oneapi-toolkit

# the default install path on ubuntu. worth putting in ~/.bashrc
export oneAPIpath="/opt/intel/oneapi"
~~~

## 3. UHD

~~~bash
sudo apt install libuhd-dev
~~~

Building it from source may turn out to be the better option on Ubuntu as well.

## 4. cmake and libcjson

~~~bash
sudo apt install cmake libcjson-dev
~~~

## 5. The largeshare path

The radio support reads its vendor libraries from the share:

~~~bash
# use your own mount point. worth putting in ~/.bashrc
export sgx_ext_path="/mnt/largeshare/temp/3rdparty/sgx-ext-20250120"
~~~

## 6. Build with USRP

~~~bash
meson setup build-uhd -DSSG:have_usrp=true -DSSG:use_oneapi=true -DIntelOneAPI:oneAPIDevPath=$oneAPIpath -DUHD:SgxExtRoot=$sgx_ext_path
cd build-uhd
meson devenv
meson compile
~~~

## 7. Build with Sidekiq

~~~bash
export CC=gcc-13
export CXX=g++-13

export sgx_ext_path="/mnt/largeshare/temp/3rdparty/sgx-ext-20250120"
export oneAPIpath="/opt/intel/oneapi"

meson setup build-sidekiq -DSSG:have_sidekiq=true -DSSG:use_oneapi=true -DIntelOneAPI:oneAPIDevPath=$oneAPIpath -DSidekiq:SgxExtRoot=$sgx_ext_path
cd build-sidekiq
meson devenv
meson compile
~~~

## Known issues

**Several UHD versions installed.** When they collide, find the one that is wanted and
export its paths before `meson setup`:

~~~bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
~~~

**The wrong gcc.** Export the versions to use before `meson setup`:

~~~bash
export CC=gcc-13
export CXX=g++-13
~~~

**UHD linking error on Ubuntu 24.04.** UHD failed and its exception could not even be
caught, because the real problem was a linking error. Taking the static linking of gcc
and stdc++ out of `meson.build` fixed it:

~~~python
cpp_args = []
link_args = []
# cpp_args = ['-static-libgcc', '-static-libstdc++']
# link_args = ['-static-libgcc', '-static-libstdc++']
~~~
