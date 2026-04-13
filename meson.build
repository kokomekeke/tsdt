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

compiler = meson.get_compiler('cpp')
if compiler.get_id() == 'gcc'
    message('Compiler: GCC')
    cpp_args = ['-static-libgcc', '-static-libstdc++']
    link_args = ['-static-libgcc', '-static-libstdc++']

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

ssgSub = subproject('SSG')
coreLibDep = ssgSub.get_variable('coreLibDep')
ssgLibDep = ssgSub.get_variable('ssgLibDep')
ssg_defines = ssgSub.get_variable('ssg_defines')

executable(
    'SCG',
    sources: [
        'src/SCG/JsonParsing/OutputParserRegistry.cpp',
        'src/SCG/JsonParsing/Parsers.cpp',
        'src/SCG/JsonParsing/SignalParserRegistry.cpp',
        'src/SCG/Modules/Merger.cpp',
        'src/SCG/Modules/Source.cpp',
        'src/SCG/Modules/Tuner.cpp',
        'src/SCG/Output/OutputDevice.cpp',
        'src/SCG/Scenario/OutputVariant.cpp',
        'src/SCG/ScenarioTransform/BuildPipeline.cpp',
        'src/SCG/ScenarioTransform/Transform.cpp',
        'src/SCG/Signal/SignalGenerator.cpp',
        'src/SCG/CLI/main.cpp'
    ],
    include_directories: [ 'src/' ],
    dependencies: [ coreLibDep, ssgLibDep ],
    cpp_args: cpp_args + ssg_defines,
    link_args: link_args,
)

# Make exe available on path, for easier access from build dir
devenv = environment()
devenv.append('PATH', [meson.current_build_dir()])
meson.add_devenv(devenv, method: 'append')

