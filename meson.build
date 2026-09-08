project(
    'scg',
    ['cpp', 'c'],
    default_options: [
        'cpp_std=c++20',
        'c_std=c11',
        'warning_level=3',
    ],
    meson_version: '>=1.1'
)
isWindows = host_machine.system() in [ 'windows' ]
fs = import('fs')


compiler = meson.get_compiler('cpp')
if compiler.get_id() == 'gcc'
    message('Compiler: GCC')
    # Ubuntu 24.04 with libuhd-dev dependency failed becouse of linking error.
    # It was necessary to delete the static link flag from meson.
    cpp_args = []
    link_args = []
    # This was the original linking strategy for gcc and std. If it's not working for you restore it.
    # cpp_args = ['-static-libgcc', '-static-libstdc++']
    # link_args = ['-static-libgcc', '-static-libstdc++']

    # non-oneapi resampler implementation is slow without these
    add_project_arguments(
        ['-O3', '-march=native', '-ffast-math'],
        language: 'cpp'
    )

elif compiler.get_id() == 'clang'
    message('Compiler: LLVM/clang')
    cpp_args = ['-stdlib=libc++']
    link_args = []

    # non-oneapi resampler implementation is slow without these
    # add_project_arguments(
    #     ['-O3', '-march=native', '-ffast-math'],
    #     language: 'cpp'
    # )

elif compiler.get_id() == 'msvc'
    message('Compiler: MSVC')
    cpp_args = ['/D_SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING', '/DNOMINMAX']
    link_args = []
endif


buildScg = get_option('build_scg')
buildScgServer = get_option('build_scgserver')
buildMqttExamples = get_option('build_mqtt_examples')
buildScgLint = get_option('build_scg_lint')


ssgSub = subproject('SSG')
commonLibDep = ssgSub.get_variable('commonLibDep')
jsonLibDep = ssgSub.get_variable('jsonLibDep')
coreLibDep = ssgSub.get_variable('coreLibDep')
ssgLibDep = ssgSub.get_variable('ssgLibDep')
ssg_defines = ssgSub.get_variable('ssg_defines')

# UHDDlls holds the uhd and the libusb dlls as [ source, name ] pairs, it is not
# defined at all when the ssg subproject is built without usrp. the copies land in
# this build directory next to the executables, which the loader searches before
# it walks PATH
foreach t : ssgSub.get_variable('UHDDlls', [])
    fs.copyfile(t[0], t[1])
endforeach

# beast is header only, so this carries an include directory and the sockets
# the asio layer under it calls into. the boost subproject sits inside ssg,
# meson resolves it by name once the ssg subproject above has been configured
if isWindows
    beastDeps = [
        subproject('Boost').get_variable('BoostHeaderDep'),
        compiler.find_library('ws2_32'),
        compiler.find_library('mswsock'),
    ]
else
    beastDeps = [ dependency('boost') ]
endif


scgLib = static_library(
    'scg_core',
    sources: [
        'src/SCG/Json/Scenario.cpp',
        'src/SCG/Json/Rid.cpp',
        'src/SCG/Json/ServerConfig.cpp',
        'src/SCG/Modules/Merger.cpp',
        'src/SCG/Modules/Sink.cpp',
        'src/SCG/Modules/Source.cpp',
        'src/SCG/Modules/Tuner.cpp',
        # 'src/SCG/Output/OutputDevice.cpp',
        'src/SCG/Output/FileOutputDevice.cpp',
        'src/SCG/Output/MockOutputDevice.cpp',
        'src/SCG/Output/UsrpB200Device.cpp',
        'src/SCG/Output/SidekiqDevice.cpp',
        'src/SCG/Scenario/OutputVariant.cpp',
        'src/SCG/Scenario/Validate.cpp',
        'src/SCG/ScenarioTransform/BuildPipeline.cpp',
        'src/SCG/ScenarioTransform/Transform.cpp',
        'src/SCG/Signal/SignalGenerator.cpp',
        'src/SCG/Sisi/RidTransform.cpp',
        'src/SCG/Utility/SignalHandler.cpp',
    ],
    include_directories: [ 'src/' ],
    dependencies: [
        commonLibDep,
        jsonLibDep,
        coreLibDep,
        ssgLibDep,
        beastDeps
    ],
    cpp_args: cpp_args + ssg_defines,
    link_args: link_args,
)
scgLibDep = declare_dependency(
    link_with: scgLib,
    include_directories: [],
    dependencies: beastDeps
)


if buildScg
    executable(
        'scg',
        sources: [
            'src/SCG/CLI/main.cpp'
        ],
        include_directories: [ 'src/' ],
        dependencies: [
            commonLibDep,
            jsonLibDep,
            coreLibDep,
            ssgLibDep,
            scgLibDep
        ],
        cpp_args: cpp_args + ssg_defines,
        link_args: link_args,
    )
endif

if buildScgLint
    executable(
        'scglint',
        sources: [
            'src/SCG/Lint/main.cpp'
        ],
        include_directories: [ 'src/' ],
        dependencies: [
            commonLibDep,
            jsonLibDep,
            coreLibDep,
            ssgLibDep,
            scgLibDep
        ],
        cpp_args: cpp_args + ssg_defines,
        link_args: link_args,
    )
endif

if buildScgServer or buildMqttExamples
    if isWindows
        mosquittoDep = dependency('mosquitto')
    else
        # Handle transitive cjson dependency which is sometimes required by mosquitto public headers (e.g. on macOS)
        cjsonDep = dependency('libcjson', required: false)
        mosquittoDep = dependency('libmosquitto')
        if cjsonDep.found()
            mosquittoDep = declare_dependency(
                dependencies: [mosquittoDep, cjsonDep]
            )
        endif
    endif

    executable(
        'scgserver',
        sources: [
            'src/SCG/Sisi/MqttClient.cpp',
            'src/SCG/Sisi/Server.cpp',
        ],
        include_directories: [ 'src/' ],
        dependencies: [
            commonLibDep,
            jsonLibDep,
            coreLibDep,
            ssgLibDep,
            scgLibDep,
            mosquittoDep
        ],
        cpp_args: cpp_args + ssg_defines,
        link_args: link_args,
    )
    executable(
        'scgcommander',
        sources: [
            'src/SCG/Sisi/MqttClient.cpp',
            'src/SCG/Sisi/Commander.cpp',
        ],
        include_directories: [ 'src/' ],
        dependencies: [
            commonLibDep,
            jsonLibDep,
            coreLibDep,
            ssgLibDep,
            scgLibDep,
            mosquittoDep
        ],
        cpp_args: cpp_args + ssg_defines,
        link_args: link_args,
    )
endif

if buildMqttExamples
    executable(
        'mqtt_publisher',
        sources: [
            'src/SCG/MqttExamples/publisher.cpp'
        ],
        dependencies: [mosquittoDep]
    )
    executable(
        'mqtt_subscriber',
        sources: [
            'src/SCG/MqttExamples/subscriber.cpp'
        ],
        dependencies: [mosquittoDep]
    )
endif

# Make exe available on path, for easier access from build dir
devenv = environment()
devenv.append('PATH', [meson.current_build_dir()])

extra_paths = get_option('devenv_paths')
foreach path : extra_paths
    devenv.prepend('PATH', path)
endforeach

meson.add_devenv(devenv, method: 'append')
