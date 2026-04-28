project(
    'mosquitto',
    ['cpp', 'c'],
    default_options: [
        'cpp_std=c++20',
        'c_std=c11',
        'warning_level=3',
    ],
    meson_version: '>=1.1'
)

cjsonSub = subproject('cjson')
cjsonDep = declare_dependency(
    dependencies: [
        cjsonSub.get_variable('libcjson_dep'),
        cjsonSub.get_variable('libcjson_utils_dep')
    ]
)

if host_machine.system() in [ 'windows' ]

    is64Bit = host_machine.cpu_family() in [ 'x86_64' ]
    cCompiler = meson.get_compiler('c')
    isDebug = get_option('optimization') in [ '0', 'g' ]
    isStaticVscrt = get_option('b_vscrt') == 'static_from_buildtype'

    arch = is64Bit ? 'x64' : 'x86'
    rd = isDebug ? 'debug' : 'release'

    mosquittoDir = join_paths(get_option('mosquitto_dir'), '2.1.2-windows-@0@-@1@'.format(arch, rd))
    mosquittoIncDir = join_paths(mosquittoDir, 'include')
    mosquittoLibDir = join_paths(mosquittoDir, 'lib')
    mosquittoBinDir = join_paths(mosquittoDir, 'bin')

    if not isStaticVscrt
        # mosquittoDlls = [
        #     join_paths(mosquittoBinDir, 'mosquitto.dll'),
        #     join_paths(mosquittoBinDir, 'mosquitto_acl_file.dll'),
        #     join_paths(mosquittoBinDir, 'mosquitto_common.dll'),
        #     join_paths(mosquittoBinDir, 'mosquitto_dynamic_security.dll'),
        #     join_paths(mosquittoBinDir, 'mosquitto_password_file.dll'),
        #     join_paths(mosquittoBinDir, 'mosquitto_persist_sqlite.dll'),
        #     join_paths(mosquittoBinDir, 'mosquitto_sparkplug_aware.dll'),
        #     join_paths(mosquittoBinDir, 'mosquittopp.dll')
        # ]
        devenv = environment()
        devenv.append('PATH', [mosquittoBinDir])
        meson.add_devenv(devenv, method: 'append')
    else
        # mosquittoDlls = []
    endif

    mosquittoLib = cCompiler.find_library('mosquitto', dirs: [mosquittoLibDir], required: true)
    mosquittoDep = declare_dependency(
        include_directories : include_directories(mosquittoIncDir),
        dependencies : [
            mosquittoLib,
            cjsonDep
        ]
    )
else
    # mosquittoDlls = []
    mosquittoDep = declare_dependency(
        dependencies: [
            dependency('libmosquitto'),
            cjsonDep
        ]
    )
endif



