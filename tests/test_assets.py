"""The json files that ship in assets, checked against the rules they have to follow.

This is a question about the data, not about the program. A failure here means a file in
the repository is wrong, not that the code broke. The linter itself is tested in
test_lint.py, against files written for that.

A rid is only meaningful next to a config, and which rid goes with which config is
written down nowhere, so each rid is tried against every config and has to be accepted by
at least one of them.
"""

import pytest

from helpers.binaries import ROOT

ASSETS = ROOT / "assets"
CONFIGS = sorted((ASSETS / "config").glob("*.json"))
RIDS = sorted((ASSETS / "rid").glob("*.json"))
SCENARIOS = sorted((ASSETS / "scenario").glob("*.json"))

EXIT_OK = 0
EXIT_INVALID = 1
EXIT_ERROR = 2

def test_there_are_assets_to_check():
    assert CONFIGS, "no configs under assets/config"
    assert RIDS, "no rids under assets/rid"
    assert SCENARIOS, "no scenarios under assets/scenario"


@pytest.mark.parametrize("path", CONFIGS, ids=[p.name for p in CONFIGS])
def test_a_config_is_valid(build, path):
    """A config asking for radios this build cannot drive is rejected, and rightly so.

    A live config names real cards, so it only passes in a build with support for them.
    Everything else in the file still has to be accepted.
    """
    result = build.run("scglint", ["config", path])
    output = result.stdout + result.stderr
    if result.returncode == EXIT_OK:
        return
    assert result.returncode == EXIT_INVALID, output
    reported = [line for line in output.splitlines() if "ERROR" in line and "config:" not in line]
    assert reported, output
    for line in reported:
        assert "this build has no" in line, output


@pytest.mark.parametrize("path", SCENARIOS, ids=[p.name for p in SCENARIOS])
def test_a_scenario_is_valid(build, path):
    result = build.run("scglint", ["scenario", path])
    assert result.returncode == EXIT_OK, result.stdout + result.stderr


@pytest.mark.parametrize("path", RIDS, ids=[p.name for p in RIDS])
def test_a_rid_is_valid_against_one_of_the_configs(build, path):
    reports = []
    for config in CONFIGS:
        result = build.run("scglint", ["rid", config, path.as_uri()])
        if result.returncode == EXIT_OK:
            return
        reports.append(f"--- {config.name}\n{result.stdout}{result.stderr}")
    raise AssertionError(
        f"{path.name} was not accepted with any config in assets/config\n"
        + "\n".join(reports)
    )
