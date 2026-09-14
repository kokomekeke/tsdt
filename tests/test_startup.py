"""How the programs come up and what they return when they cannot."""


import pytest

from helpers.binaries import ROOT

DATA = ROOT / "tests" / "data"


# scgserver opens scgserver.log in the working directory, so everything here runs in the
# directory of the test and not in the one the repository sits in


@pytest.mark.parametrize("program", ["scgserver", "scglint"])
@pytest.mark.parametrize("argument", ["--help", "--version"])
def test_help_and_version_succeed(build, tmp_path, program, argument):
    """These come back through the parser instead of calling exit, so they return 0."""
    result = build.run(program, [argument], cwd=tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (result.stdout + result.stderr).strip() != ""


def test_no_arguments_is_reported(build, tmp_path):
    result = build.run("scgserver", [], cwd=tmp_path)
    assert result.returncode == 1, result.stdout + result.stderr


def test_a_missing_config_is_reported(build, tmp_path):
    result = build.run("scgserver", [tmp_path / "nope.json"], cwd=tmp_path)
    output = result.stdout + result.stderr
    assert result.returncode == 1, output
    assert "failed to open config file" in output, output


def test_a_config_that_is_not_json_is_reported(build, tmp_path):
    path = tmp_path / "notjson.json"
    path.write_text("this is not json", encoding="utf-8")

    result = build.run("scgserver", [path], cwd=tmp_path)
    assert result.returncode == 1, result.stdout + result.stderr


def test_a_config_that_fails_validation_is_reported(build, tmp_path):
    result = build.run("scgserver", [DATA / "config-invalid.json"], cwd=tmp_path)
    output = result.stdout + result.stderr
    assert result.returncode == 1, output
    assert "broker_host must not be empty" in output, output
    assert "scenario_fetch_timeout_s must be positive" in output, output


def test_the_environment_is_the_one_meson_devenv_gives(build):
    """Not a check on the programs, a check that the harness found the dlls.

    scgserver loads mosquitto.dll, which sits in the vcpkg directory and is not copied
    next to the executables, so it only starts with that directory on PATH. That is what
    devenv_paths puts there and what this asks for.
    """
    assert build.from_devenv, (
        "meson devenv could not be asked about the build directory, "
        "the fallback was used instead"
    )
