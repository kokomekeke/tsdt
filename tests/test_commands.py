"""What the server does with commands that are wrong in one way or another."""

import json

import pytest

from helpers.server import setup_command, utc_string


def test_a_payload_that_is_not_json_is_reported(scgserver):
    mark = scgserver.send("this is not json")
    message, _ = scgserver.expect_error(mark)
    assert "bad payload" in message, message


def test_an_unknown_command_is_reported(scgserver):
    mark = scgserver.send(json.dumps({"command": "Nope", "extras": {}}))
    message, _ = scgserver.expect_error(mark)
    assert "bad payload" in message, message


def test_a_missing_field_is_reported(scgserver, rid_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {"scenario_url": rid_path.as_uri(), "start_time": utc_string(3600)},
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "bad payload" in message, message


def test_a_time_in_the_past_is_reported(scgserver, rid_path):
    mark = scgserver.setup(rid_path.as_uri(), test_id="past", seconds_ahead=-3600)
    message, _ = scgserver.expect_error(mark)
    assert "starting time is in the past" in message, message


def test_a_time_without_an_offset_is_reported(scgserver, rid_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {
            "scenario_url": rid_path.as_uri(),
            "start_time": utc_string(3600).rstrip("Z"),
            "test_id": "nooffset",
        },
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "no utc offset" in message, message


def test_a_time_that_does_not_parse_is_reported(scgserver, rid_path):
    payload = json.dumps({
        "command": "TEST_SETUP",
        "extras": {"scenario_url": rid_path.as_uri(), "start_time": "tomorrow", "test_id": "badtime"},
    })
    mark = scgserver.send(payload)
    message, _ = scgserver.expect_error(mark)
    assert "could not parse starting time" in message, message


@pytest.mark.parametrize("test_id", ["", ".", "..", "a/b", "a:b", "a*b"])
def test_a_test_id_that_is_not_a_directory_name_is_reported(scgserver, rid_path, test_id):
    mark = scgserver.setup(rid_path.as_uri(), test_id=test_id, seconds_ahead=3600)
    message, _ = scgserver.expect_error(mark)
    assert "cannot be used as a directory name" in message, message


def test_a_duplicate_test_id_is_reported(scgserver, rid_path):
    url = rid_path.as_uri()
    mark = scgserver.setup(url, test_id="twice", seconds_ahead=3600)
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.wait_for_log("test scheduled: twice")

    mark = scgserver.setup(url, test_id="twice", seconds_ahead=7200)
    message, _ = scgserver.expect_error(mark)
    assert "test id already scheduled" in message, message


def test_cancelling_an_unknown_test_is_reported(scgserver):
    mark = scgserver.cancel("nosuchtest")
    message, _ = scgserver.expect_error(mark)
    assert "unknown test id" in message, message


def test_a_scheduled_test_can_be_cancelled(scgserver, rid_path):
    mark = scgserver.setup(rid_path.as_uri(), test_id="cancelme", seconds_ahead=3600)
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.wait_for_log("test scheduled: cancelme")

    mark = scgserver.cancel("cancelme")
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.wait_for_log("test cancelled: cancelme")


def test_a_cancelled_test_id_can_be_used_again(scgserver, rid_path):
    url = rid_path.as_uri()
    scgserver.setup(url, test_id="again", seconds_ahead=3600)
    assert scgserver.wait_for_log("test scheduled: again")
    scgserver.cancel("again")
    assert scgserver.wait_for_log("test cancelled: again")

    mark = scgserver.setup(url, test_id="again", seconds_ahead=3600)
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.log.count("test scheduled: again") == 2
