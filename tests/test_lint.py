"""scglint against files written for this and nothing else.

Every file under tests/data is here to make the linter say one particular thing, and the
name says which. The files that ship with the repository are checked in test_assets.py,
which is a different question: that one asks whether the data is good, this one asks
whether the program can tell.
"""

import json

import pytest

from helpers.binaries import ROOT
from helpers.rid import make_permuted_rid, make_rid
from helpers.tsd import write_tsd

DATA = ROOT / "tests" / "data"
CONFIG = DATA / "config.json"
# two radios of different channel counts, for the permutation test below
CONFIG_TWO_RADIOS = DATA / "config-two-radios.json"

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
    ("rid-source-without-settings.json", "interferenceSources[0]: radio r1 has no radio_settings entry"),
]

UNREADABLE = [
    ("config", "config-missing-key.json", "failed to parse config file"),
    ("rid", "rid-missing-channels.json", "radio_settings"),
    ("rid", "not-json.json", "failed to parse rid file"),
]


# every way a rid may name the radios of config-two-radios.json: both, both the other way
# round, and each on its own. the verdict is the same for all of them
RADIO_ORDERS = [("r0", "r1"), ("r1", "r0"), ("r0",), ("r1",)]


@pytest.mark.parametrize("order", RADIO_ORDERS, ids=["+".join(o) for o in RADIO_ORDERS])
def test_the_order_of_radio_settings_does_not_change_the_verdict(build, tmp_path, order):
    """A rid is free to name the radios in any order, and to leave some of them out.

    Whoever reads a source's radio has to look it up in the rid's own radio_settings.
    Reaching for the config's radio array instead happens to agree when a rid lists every
    radio in the same order, and stops agreeing here.
    """
    path = tmp_path / ("rid-" + "-".join(order) + ".json")
    path.write_text(json.dumps(make_permuted_rid(order), indent=4), encoding="utf-8")

    code, output = lint(build, ["rid", CONFIG_TWO_RADIOS, path.as_uri()])
    assert code == EXIT_OK, output


def test_a_rid_that_configures_only_the_second_radio_is_accepted(build):
    """The channel count was looked up in the config's radio array rather than the rid's.

    They are different arrays, and they only line up when the rid lists every radio the
    config has, in the same order. This file lists one of two, so the lookup ran off the
    end of radio_settings.
    """
    code, output = lint(build, ["rid", CONFIG, (DATA / "rid-second-radio-only.json").as_uri()])
    assert code == EXIT_OK, output


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


# the tsd subcommand reads a tsd, then the rid of every step, merges them and checks the
# result. the step urls have to be absolute, so these are written into tmp_path


def write_rid_file(path, **kwargs):
    path.write_text(json.dumps(make_rid(**kwargs), indent=4), encoding="utf-8")
    return path


def test_a_valid_tsd_is_accepted(build, tmp_path):
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_OK, output
    assert "1 step(s), 2 interference source(s), 1 s in all" in output, output


def test_the_steps_are_laid_end_to_end(build, tmp_path):
    """three steps of a second each, and the sources of the last one sit in the third"""
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    tsd = write_tsd(
        tmp_path / "tsd.json",
        [(1.0, rid.as_uri()), (1.0, rid.as_uri()), (1.0, rid.as_uri())],
    )
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_OK, output
    assert "3 step(s), 6 interference source(s), 3 s in all" in output, output


def test_a_step_without_an_interference_only_adds_time(build, tmp_path):
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri()), (2.0, None)])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_OK, output
    assert "2 step(s), 2 interference source(s), 3 s in all" in output, output


def test_a_step_longer_than_its_rid_is_accepted(build, tmp_path):
    """the rid plays at the start of the step and the rest of it is quiet"""
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    tsd = write_tsd(tmp_path / "tsd.json", [(4.0, rid.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_OK, output
    assert "4 s in all" in output, output


def test_a_step_shorter_than_its_rid_is_reported(build, tmp_path):
    rid = write_rid_file(tmp_path / "rid.json", duration_s=2.0)
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "step[0]" in output, output
    assert "the step is 1 s long and the RID it plays is 2 s long" in output, output


def test_steps_that_configure_the_radios_differently_are_reported(build, tmp_path):
    """one tuning per radio holds for the whole playback, so the rids have to agree"""
    first = write_rid_file(tmp_path / "first.json", duration_s=1.0, bandwidth_hz=192000)
    second = write_rid_file(tmp_path / "second.json", duration_s=1.0, bandwidth_hz=96000)
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, first.as_uri()), (1.0, second.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "step[1] does not configure the radios the way step[0] does" in output, output
    assert "bandwidth_hz" in output, output


def test_steps_naming_different_radios_are_reported(build, tmp_path):
    """not even a subset is allowed, every rid names the same radios in the same order"""
    both = tmp_path / "both.json"
    both.write_text(json.dumps(make_permuted_rid(("r0", "r1")), indent=4), encoding="utf-8")
    one = tmp_path / "one.json"
    one.write_text(json.dumps(make_permuted_rid(("r0",)), indent=4), encoding="utf-8")
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, both.as_uri()), (1.0, one.as_uri())])
    code, output = lint(build, ["tsd", CONFIG_TWO_RADIOS, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "it configures 1 radios instead of 2" in output, output


def test_steps_of_different_versions_are_reported(build, tmp_path):
    rid = make_rid(duration_s=1.0)
    first = tmp_path / "first.json"
    first.write_text(json.dumps(rid, indent=4), encoding="utf-8")
    rid["metadata"]["version"] = "2.0.0"
    second = tmp_path / "second.json"
    second.write_text(json.dumps(rid, indent=4), encoding="utf-8")
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, first.as_uri()), (1.0, second.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert 'step[1] is version "2.0.0"' in output, output


def test_a_tsd_of_quiet_steps_alone_is_reported(build, tmp_path):
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, None), (1.0, None)])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "no step of the TSD names an interference" in output, output


def test_a_tsd_without_steps_is_reported(build, tmp_path):
    tsd = write_tsd(tmp_path / "tsd.json", [])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "the TSD has no steps" in output, output


def test_a_step_with_a_duration_of_zero_is_reported(build, tmp_path):
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    tsd = write_tsd(tmp_path / "tsd.json", [(0.0, rid.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "step[0]: duration_seconds must be positive" in output, output


def test_a_step_whose_rid_breaks_a_rule_is_reported(build, tmp_path):
    """the rid rules run on each step on its own, which is what names the step"""
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0, radio_id="nosuchradio")
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri())])
    code, output = lint(build, ["tsd", CONFIG, tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "step[0]" in output, output
    assert "unknown radioId: nosuchradio" in output, output


def test_a_key_the_tsd_does_not_know_is_ignored(build, tmp_path):
    """a tsd may carry fields that are none of our business"""
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    content = json.loads(
        write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri())]).read_text(encoding="utf-8")
    )
    content["image"] = "https://example.invalid/picture.png"
    content["whatever"] = {"nested": [1, 2, 3]}
    content["steps"][0]["also_this"] = True
    path = tmp_path / "extra.json"
    path.write_text(json.dumps(content, indent=4), encoding="utf-8")

    code, output = lint(build, ["tsd", CONFIG, path.as_uri()])
    assert code == EXIT_OK, output


def test_a_step_without_an_interference_key_is_reported(build, tmp_path):
    """interference is required, null is how a step says it has none"""
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    content = json.loads(
        write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri())]).read_text(encoding="utf-8")
    )
    del content["steps"][0]["interference"]
    path = tmp_path / "nokey.json"
    path.write_text(json.dumps(content, indent=4), encoding="utf-8")

    code, output = lint(build, ["tsd", CONFIG, path.as_uri()])
    assert code == EXIT_ERROR, output
    assert "interference" in output, output


def test_a_document_that_is_not_a_tsd_is_reported(build):
    code, output = lint(build, ["tsd", CONFIG, (DATA / "not-json.json").as_uri()])
    assert code == EXIT_ERROR, output
    assert "failed to parse TSD" in output, output


def test_an_invalid_config_stops_the_tsd_from_being_looked_at(build, tmp_path):
    rid = write_rid_file(tmp_path / "rid.json", duration_s=1.0)
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, rid.as_uri())])
    code, output = lint(build, ["tsd", DATA / "config-invalid.json", tsd.as_uri()])
    assert code == EXIT_INVALID, output
    assert "skipping tsd validation" in output, output


def test_help_and_version_succeed(build):
    for argument in ("--help", "--version"):
        code, output = lint(build, [argument])
        assert code == EXIT_OK, output


def test_no_subcommand_is_reported(build):
    code, output = lint(build, [])
    assert code == EXIT_ERROR, output
