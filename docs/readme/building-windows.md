# BUILDING ON WINDOWS

MSVC, meson and vcpkg. The options these commands pass are in
[build-options.md](build-options.md).

## vcpkg

Pick a location, `C:\tools` works, and install it from its git repository. This is
unironically the official method.

~~~bat
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
~~~

Put the vcpkg directory on `PATH`. Then, from the root of this repository, `vcpkg install`
reads `vcpkg.json` and writes the dependencies into `vcpkg_installed`.

## Debug build

From the project root:

~~~bat
meson setup build-msvc -DSSG:have_usrp=true -DSSG:use_oneapi=true -Dcmake_prefix_path=%cd%\vcpkg_installed\x64-windows -Ddevenv_paths=%cd%/vcpkg_installed/x64-windows/debug/bin
cd build-msvc
meson devenv
meson compile
~~~

`meson devenv` opens a shell with the environment the programs need, which is what puts
`mosquitto.dll` within reach of `scgserver.exe`. Started from a plain shell it fails on
that dll.

A release build takes `vcpkg_installed/x64-windows/bin` for `devenv_paths` instead of the
`debug` one, because the two hold libraries built against different runtimes.

## Dependency snapshot

When a new dependency is added, upload the updated `vcpkg_installed` directory to
`S:\temp\3rdparty\scg_vcpkg_deps` so that the next person does not have to build it all
again.
