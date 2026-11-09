"""What the server does with commands that are wrong in one way or another."""

import json
import time

import pytest

from helpers.server import setup_command, utc_string
from helpers.tsd import write_tsd


def statuses(instance, mark, test_id):
    messages = [json.loads(p) for _, _, p in instance.broker.since(mark, instance.test_log_topic)]
    return [m["test_status"] for m in messages if m["test_id"] == test_id]


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False


def test_a_payload_that_is_not_json_is_reported(scgserver):
    mark = scgserver.send("this is not json")
    message, _ = scgserver.expect_error(mark)
    assert "bad payload" in message, message


def test_an_unknown_command_is_reported(scgserver):
    mark = scgserver.send(json.dumps({"command": "Nope", "extras": {}}))
    message, _ = scgserver.expect_error(mark)
    assert "bad payload" in message, message


def test_a_missing_field_is_reported(scgserver, tsd_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {"scenario_url": tsd_path.as_uri(), "start_time": utc_string(3600)},
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "bad payload" in message, message


def test_a_time_in_the_past_is_reported(scgserver, tsd_path):
    mark = scgserver.setup(tsd_path.as_uri(), test_id="past", seconds_ahead=-3600)
    message, _ = scgserver.expect_error(mark)
    assert "starting time is in the past" in message, message


def test_a_time_without_an_offset_is_reported(scgserver, tsd_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {
            "scenario_url": tsd_path.as_uri(),
            "start_time": utc_string(3600).rstrip("Z"),
            "test_id": "nooffset",
        },
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "no utc offset" in message, message


def test_a_time_with_a_fraction_of_a_second_is_accepted(scgserver, tsd_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {
            "scenario_url": tsd_path.as_uri(),
            "start_time": utc_string(3600).replace("Z", ".000Z"),
            "test_id": "fraction",
        },
    })
    mark = scgserver.send(payload)
    scgserver.expect_no_error(mark)
    assert scgserver.wait_for_log("test scheduled: fraction")


def test_a_fraction_without_digits_is_reported(scgserver, tsd_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {
            "scenario_url": tsd_path.as_uri(),
            "start_time": utc_string(3600).replace("Z", ".Z"),
            "test_id": "nodigits",
        },
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "could not parse starting time" in message, message


def test_a_time_that_does_not_parse_is_reported(scgserver, tsd_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {"scenario_url": tsd_path.as_uri(), "start_time": "tomorrow", "test_id": "badtime"},
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "could not parse starting time" in message, message


@pytest.mark.parametrize("test_id", ["", ".", "..", "a/b", "a:b", "a*b"])
def test_a_test_id_that_is_not_a_directory_name_is_reported(scgserver, tsd_path, test_id):
    mark = scgserver.setup(tsd_path.as_uri(), test_id=test_id, seconds_ahead=3600)
    message, _ = scgserver.expect_error(mark)
    assert "cannot be used as a directory name" in message, message


def test_a_duplicate_test_id_is_reported(scgserver, tsd_path):
    url = tsd_path.as_uri()
    mark = scgserver.setup(url, test_id="twice", seconds_ahead=3600)
    scgserver.expect_no_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, "twice")[-1:] == ["TEST_READY"]), statuses(scgserver, mark, "twice")

    mark = scgserver.setup(url, test_id="twice", seconds_ahead=7200)
    message, _ = scgserver.expect_error(mark)
    assert "test id already scheduled" in message, message
    assert statuses(scgserver, mark, "twice") == [], statuses(scgserver, mark, "twice")


def test_a_refused_setup_is_reported_as_failed(scgserver, tsd_path):
    """the tms moves the session to failed on TEST_FAILED, also when it was never scheduled"""
    mark = scgserver.setup(tsd_path.as_uri(), test_id="refused", seconds_ahead=-3600)
    scgserver.expect_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, "refused") == ["TEST_FAILED"]), statuses(scgserver, mark, "refused")


def test_a_setup_whose_tsd_cannot_be_read_is_reported_as_failed(scgserver, tmp_path):
    mark = scgserver.setup((tmp_path / "missing.json").as_uri(), test_id="unread", seconds_ahead=3600)
    scgserver.expect_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, "unread") == ["TEST_FAILED"]), statuses(scgserver, mark, "unread")


def test_a_setup_whose_tsd_is_not_a_tsd_is_reported_as_failed(scgserver, tmp_path):
    other = tmp_path / "other.json"
    other.write_text('{ "hello": 1 }', encoding="utf-8")
    mark = scgserver.setup(other.as_uri(), test_id="nottsd")
    scgserver.expect_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, "nottsd") == ["TEST_FAILED"]), statuses(scgserver, mark, "nottsd")


@pytest.mark.parametrize("name, steps", [
    ("nosteps", []),
    ("negative", [(-1.0, None)]),
])
def test_a_setup_whose_tsd_is_invalid_is_reported_as_failed(scgserver, tmp_path, name, steps):
    tsd = write_tsd(tmp_path / "invalid-tsd.json", steps)
    mark = scgserver.setup(tsd.as_uri(), test_id=name)
    scgserver.expect_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, name) == ["TEST_FAILED"]), statuses(scgserver, mark, name)


def test_a_setup_whose_step_is_shorter_than_its_rid_is_reported_as_failed(scgserver, tmp_path, rid_path):
    tsd = write_tsd(tmp_path / "short-tsd.json", [(0.1, rid_path.as_uri())])
    mark = scgserver.setup(tsd.as_uri(), test_id="short")
    scgserver.expect_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, "short") == ["TEST_FAILED"]), statuses(scgserver, mark, "short")


@pytest.mark.parametrize("name", [
    "not-json.json",
    "rid-missing-channels.json",
    "rid-unknown-radio.json",
    "rid-negative-duration.json",
    "rid-channel-out-of-range.json",
    "rid-source-without-settings.json",
    "rid-gain-update-without-settings.json",
    "rid-gain-update-channel-out-of-range.json",
    "rid-gain-update-same-channel-twice.json",
    "rid-gain-update-zero-step.json",
    "rid-gain-update-past-end.json",
    "rid-gain-update-time-does-not-advance.json",
])
def test_a_setup_whose_rid_is_invalid_is_reported_as_failed(scgserver, repo, tmp_path, name):
    """the step is longer than any of the rids, so only the rid itself is wrong"""
    rid = repo / "tests" / "data" / name
    tsd = write_tsd(tmp_path / "tsd.json", [(5.0, rid.as_uri())])
    mark = scgserver.setup(tsd.as_uri(), test_id="badrid")
    scgserver.expect_error(mark)
    assert wait_until(lambda: statuses(scgserver, mark, "badrid") == ["TEST_FAILED"]), statuses(scgserver, mark, "badrid")


def test_starting_an_unknown_test_is_reported(scgserver):
    mark = scgserver.test_start("nosuchtest")
    message, _ = scgserver.expect_error(mark)
    assert "unknown test id" in message, message


def test_a_scheduled_test_can_be_started(scgserver, tsd_path):
    mark = scgserver.setup(tsd_path.as_uri(), test_id="authorized", seconds_ahead=3600)
    scgserver.expect_no_error(mark)
    assert scgserver.wait_for_log("test scheduled: authorized")

    mark = scgserver.test_start("authorized")
    scgserver.expect_no_error(mark)
    assert scgserver.wait_for_log("test authorized: authorized")


# read like any other command and then dropped. they are part of the protocol and do
# nothing here yet, so the server warns instead of refusing them
IGNORED = ["INT_START", "TEST_UPDATE", "TEST_END"]


@pytest.mark.parametrize("name", IGNORED)
def test_a_command_that_does_nothing_yet_is_accepted(scgserver, name):
    mark = scgserver.bare(name)
    scgserver.expect_no_error(mark)


@pytest.mark.parametrize("name", IGNORED)
def test_a_command_that_does_nothing_yet_is_warned_about(scgserver, name):
    scgserver.bare(name)
    assert scgserver.wait_for_log(f"as {name}, which does nothing here yet"), scgserver.log


def test_cancelling_an_unknown_test_is_reported(scgserver):
    mark = scgserver.cancel("nosuchtest")
    message, _ = scgserver.expect_error(mark)
    assert "unknown test id" in message, message


def test_a_scheduled_test_can_be_cancelled(scgserver, tsd_path):
    mark = scgserver.setup(tsd_path.as_uri(), test_id="cancelme", seconds_ahead=3600)
    scgserver.expect_no_error(mark)
    assert scgserver.wait_for_log("test scheduled: cancelme")

    mark = scgserver.cancel("cancelme")
    scgserver.expect_no_error(mark)
    assert scgserver.wait_for_log("test cancelled: cancelme")


def test_a_cancelled_test_id_can_be_used_again(scgserver, tsd_path):
    url = tsd_path.as_uri()
    scgserver.setup(url, test_id="again", seconds_ahead=3600)
    assert scgserver.wait_for_log("test scheduled: again")
    scgserver.cancel("again")
    assert scgserver.wait_for_log("test cancelled: again")

    mark = scgserver.setup(url, test_id="again", seconds_ahead=3600)
    scgserver.expect_no_error(mark)
    assert scgserver.log.count("test scheduled: again") == 2
