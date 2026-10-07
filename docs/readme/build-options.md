# BUILD OPTIONS

Options of this project are passed plain, options of a subproject are prefixed with its
name. `meson configure <build dir>` lists the current value of every one of them.

## This project

| option | default | what it does |
|---|---|---|
| `build_scg` | true | build `scg`, the command line tool |
| `build_scgserver` | true | build `scgserver` and `scgcommander` |
| `build_scg_lint` | true | build `scglint`, the hardware free json validator |
| `build_mqtt_examples` | true | build `mqtt_publisher` and `mqtt_subscriber` |
| `devenv_paths` | empty | directories `meson devenv` puts on `PATH`, which is how the dlls are found on windows |

## SSG, the pipeline subproject

| option | default | what it does |
|---|---|---|
| `SSG:have_usrp` | false | USRP support, needs UHD |
| `SSG:have_sidekiq` | false | Sidekiq support, needs the Sidekiq libraries |
| `SSG:use_oneapi` | false | use Intel oneAPI and IPP instead of FFTW3 |
| `SSG:build_tests` | true | build the SSG test executables |

Without `have_usrp` or `have_sidekiq` the matching device is left out of the build, and a
rid naming one of those radios is refused unless `mock_radios` is set in the config. See
[server-config.md](server-config.md).

## Dependency paths

| option | where it is needed | what it points at |
|---|---|---|
| `cmake_prefix_path` | windows | `vcpkg_installed/x64-windows` |
| `IntelOneAPI:oneAPIDevPath` | linux, with `use_oneapi` | the oneAPI install, `/opt/intel/oneapi` |
| `UHD:SgxExtRoot` | linux, with `have_usrp` | the `sgx-ext` directory on the largeshare |
| `Sidekiq:SgxExtRoot` | linux, with `have_sidekiq` | the same directory |
