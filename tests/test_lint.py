"""scglint against files written for this and nothing else.

Every file under tests/data is here to make the linter say one particular thing, and the
name says which. The files that ship with the repository are checked in test_assets.py,
which is a different question: that one asks whether the data is good, this one asks
whether the program can tell.
"""

import pytest

from helpers.binaries import ROOT

DATA = ROOT / "tests" / "data"
CONFIG = DATA / "config.json"

EXIT_OK = 0
EXIT_INVALID = 1
EXIT_ERROR = 2


def lint(build, args):
    result = build.run("scglint", args)
    return result.returncode, result.stdout + result.stderr


def test_a_valid_config_is_accepted(build):
    code, output = lint(build, ["config", CONFIG])
    assert code == EXIT_OK, output


def test_a_valid_scenario_is_accepted(build):
    code, output = lint(build, ["scenario", DATA / "scenario.json"])
    assert code == EXIT_OK, output


def test_a_valid_rid_is_accepted(build):
    """This file carries a comment and a trailing comma, which the parser allows."""
    code, output = lint(build, ["rid", CONFIG, (DATA / "rid.json").as_uri()])
    assert code == EXIT_OK, output


# name of the file, the text the report has to carry
BAD_CONFIGS = [
    ("config-invalid.json", "mqtt.broker_host must not be empty"),
]

BAD_SCENARIOS = [
    ("scenario-missing-signal.json", "event[0]: missing signal: nosuchsignal"),
    ("scenario-event-past-end.json", "event[0]: end time > duration"),
]

BAD_RIDS = [
    ("rid-unknown-radio.json", "radioSettings[0]: unknown radioId: nosuchradio"),
    ("rid-negative-duration.json", "globalSettings.duration_s must be positive"),
    ("rid-channel-out-of-range.json", "interferenceSources[0]: channel id out of range: 7"),
]

UNREADABLE = [
    ("config", "config-missing-key.json", "failed to parse config file"),
    ("rid", "rid-missing-channels.json", "radio_settings"),
    ("rid", "not-json.json", "failed to parse rid file"),
]


@pytest.mark.parametrize("name,expected", BAD_CONFIGS, ids=[c[0] for c in BAD_CONFIGS])
def test_a_config_that_breaks_a_rule_is_reported(build, name, expected):
    code, output = lint(build, ["config", DATA / name])
    assert code == EXIT_INVALID, output
    assert expected in output, output


@pytest.mark.parametrize("name,expected", BAD_SCENARIOS, ids=[c[0] for c in BAD_SCENARIOS])
def test_a_scenario_that_breaks_a_rule_is_reported(build, name, expected):
    code, output = lint(build, ["scenario", DATA / name])
    assert code == EXIT_INVALID, output
    assert expected in output, output


@pytest.mark.parametrize("name,expected", BAD_RIDS, ids=[c[0] for c in BAD_RIDS])
def test_a_rid_that_breaks_a_rule_is_reported(build, name, expected):
    code, output = lint(build, ["rid", CONFIG, (DATA / name).as_uri()])
    assert code == EXIT_INVALID, output
    assert expected in output, output


@pytest.mark.parametrize("command,name,expected", UNREADABLE, ids=[c[1] for c in UNREADABLE])
def test_a_file_that_cannot_be_read_is_reported(build, command, name, expected):
    """A file the parser cannot get through is a different exit code from an invalid one."""
    target = (DATA / name).as_uri() if command == "rid" else DATA / name
    args = ["rid", CONFIG, target] if command == "rid" else [command, DATA / name]
    code, output = lint(build, args)
    assert code == EXIT_ERROR, output
    assert expected in output, output


def test_every_error_is_reported_not_only_the_first(build):
    """rid-unknown-radio.json breaks the same rule twice, in two different places."""
    code, output = lint(build, ["rid", CONFIG, (DATA / "rid-unknown-radio.json").as_uri()])
    assert code == EXIT_INVALID, output
    assert "radioSettings[0]: unknown radioId" in output, output
    assert "interferenceSources[0]: unknown radioId" in output, output
    assert "2 error(s)" in output, output


def test_an_invalid_config_stops_the_rid_from_being_looked_at(build):
    code, output = lint(build, ["rid", DATA / "config-invalid.json", (DATA / "rid.json").as_uri()])
    assert code == EXIT_INVALID, output
    assert "skipping rid validation" in output, output


def test_a_missing_file_is_reported(build, tmp_path):
    code, output = lint(build, ["rid", CONFIG, (tmp_path / "nope.json").as_uri()])
    assert code == EXIT_ERROR, output
    assert "failed to open file" in output, output


def test_help_and_version_succeed(build):
    for argument in ("--help", "--version"):
        code, output = lint(build, [argument])
        assert code == EXIT_OK, output


def test_no_subcommand_is_reported(build):
    code, output = lint(build, [])
    assert code == EXIT_ERROR, output
