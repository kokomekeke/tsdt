"""INT_STOP, the command that ends whatever is being transmitted.

It takes no arguments and picks its target itself, which is the running test. A cancel
names a test and removes everything it left behind; a stop keeps the directory and only
ends the test, reported as TEST_FAILED because the status list has nothing better.

Only a transmission can be stopped. A test that is merely scheduled, or one whose
recordings are still being made, is reported back rather than touched.
"""

import datetime
import json
import time

from helpers.rid import write_gnss_rid, write_rid
from helpers.tsd import write_tsd

# long enough that the test is still transmitting when the stop arrives. the radios are
# mocked, so the playback runs in real time without any hardware
PLAYING_S = 20.0


def with_ephemeris(config):
    config["gnss_ephemeris_file"] = "ephemeris/none.rnx"


def playing(server, tmp_path, test_id="running"):
    """Starts a server and leaves it transmitting a test of that id."""
    rid = write_rid(tmp_path / "long.json", duration_s=PLAYING_S)
    tsd = write_tsd(tmp_path / "long-tsd.json", [(PLAYING_S, rid.as_uri())])
    instance = server(start_ahead_s=2.0)
    instance.setup(tsd.as_uri(), test_id=test_id, seconds_ahead=4)
    assert instance.wait_for_log(f"playback started: {test_id}", timeout=30.0), instance.log
    assert wait_until(lambda: "TEST_RUNNING" in statuses(instance, test_id), timeout=10.0), statuses(instance, test_id)
    return instance


def statuses(instance, test_id="running"):
    """Every status this server published for the test, in order."""
    messages = [json.loads(p) for _, _, p in instance.broker.since(0.0, instance.test_log_topic)]
    return [m["test_status"] for m in messages if m["test_id"] == test_id]


def signal_types(instance):
    """Every signal status this server published, in order. An error carries no type."""
    messages = [json.loads(p) for _, _, p in instance.broker.since(0.0, instance.error_log_topic)]
    return [m["type"] for m in messages if "type" in m]


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False


def test_a_stop_with_nothing_running_is_reported(scgserver):
    mark = scgserver.int_stop()
    message, _ = scgserver.expect_error(mark)
    assert "no test is running" in message, message


def test_a_stop_with_a_test_only_scheduled_is_reported(scgserver, tsd_path):
    mark = scgserver.setup(tsd_path.as_uri(), test_id="later", seconds_ahead=3600)
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.wait_for_log("test scheduled: later")

    mark = scgserver.int_stop()
    message, _ = scgserver.expect_error(mark)
    assert "no test is running" in message, message


def test_a_stop_during_preparation_is_reported(server, tmp_path):
    """a preparation is not a transmission, so it is left alone"""
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=60.0, bandwidth_hz=48000)
    tsd = write_tsd(tmp_path / "gnss-tsd.json", [(60.0, rid.as_uri())])
    instance = server(start_ahead_s=2.0, config_changes=with_ephemeris)
    instance.setup(tsd.as_uri(), test_id="preparing", seconds_ahead=120)
    assert instance.wait_for_log("preparation started: preparing", timeout=30.0), instance.log

    mark = instance.int_stop()
    message, _ = instance.expect_error(mark)
    assert "is being prepared" in message, message
    assert '"preparing"' in message, message


def test_a_stop_ends_the_transmission(server, tmp_path):
    instance = playing(server, tmp_path)
    mark = instance.int_stop()
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("playback stopped: running"), instance.log
    assert "playback finished: running" not in instance.log, instance.log


def test_a_stopped_test_is_reported_as_failed(server, tmp_path):
    instance = playing(server, tmp_path)
    instance.int_stop()
    assert instance.wait_for_log("playback stopped: running"), instance.log

    assert wait_until(lambda: statuses(instance)[-1:] == ["TEST_FAILED"]), statuses(instance)
    assert statuses(instance) == [
        "TEST_INIT",
        "TEST_PREPARE",
        "TEST_READY",
        "TEST_RUNNING",
        "TEST_FAILED",
    ], statuses(instance)


def test_the_running_status_is_not_before_the_start_time(server, tmp_path):
    """TEST_RUNNING is published when the first samples go to the radio, which the sink
    holds back until the time of the test. the timestamp is that moment"""
    instance = playing(server, tmp_path)

    directory = tmp_path / "recordings" / "running"
    content = json.loads((directory / "test.json").read_text(encoding="utf-8"))
    start = datetime.datetime.fromisoformat(content["command"]["start_time"])

    messages = [json.loads(p) for _, _, p in instance.broker.since(0.0, instance.test_log_topic)]
    running = [m for m in messages if m["test_id"] == "running" and m["test_status"] == "TEST_RUNNING"]
    assert len(running) == 1, messages
    assert datetime.datetime.fromisoformat(running[0]["timestamp"]) >= start, (running, start)


def test_a_stop_during_the_hold_fails_the_test_without_transmitting(server, tmp_path):
    """the pipeline starts start_ahead before the time of the test. a stop before that
    time ends the test before anything went to the radio"""
    rid = write_rid(tmp_path / "long.json", duration_s=PLAYING_S)
    tsd = write_tsd(tmp_path / "long-tsd.json", [(PLAYING_S, rid.as_uri())])
    instance = server(start_ahead_s=10.0)
    instance.setup(tsd.as_uri(), test_id="held", seconds_ahead=15)
    assert instance.wait_for_log("playback started: held", timeout=30.0), instance.log

    mark = instance.int_stop()
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("playback stopped: held"), instance.log

    assert wait_until(lambda: statuses(instance, "held")[-1:] == ["TEST_FAILED"]), statuses(instance, "held")
    assert statuses(instance, "held") == [
        "TEST_INIT",
        "TEST_PREPARE",
        "TEST_READY",
        "TEST_FAILED",
    ], statuses(instance, "held")
    assert "jamming" not in signal_types(instance), signal_types(instance)


def test_a_stopped_test_keeps_its_directory(server, tmp_path):
    """the documents it was built from stay, test.json holds the failed status"""
    instance = playing(server, tmp_path)
    instance.int_stop()
    assert instance.wait_for_log("playback stopped: running"), instance.log

    directory = tmp_path / "recordings" / "running"
    assert sorted(p.name for p in directory.iterdir()) == [
        "rid.json", "rid_000.json", "scenario.json", "test.json", "tsd.json",
    ]
    content = json.loads((directory / "test.json").read_text(encoding="utf-8"))
    assert content["status"] == "TEST_FAILED"


def test_the_signal_status_goes_back_to_none(server, tmp_path):
    """the status is only published when it changes, so the jamming has to arrive first

    the pipeline starts start_ahead before the time of the test and holds its output
    until that moment, so the first event becomes active a couple of seconds after the
    playback is logged as started
    """
    instance = playing(server, tmp_path)
    assert wait_until(lambda: "jamming" in signal_types(instance), timeout=15.0), \
        signal_types(instance)

    instance.int_stop()
    assert instance.wait_for_log("playback stopped: running"), instance.log
    assert wait_until(lambda: signal_types(instance)[-1:] == ["none"]), signal_types(instance)


def test_a_stopped_test_id_can_be_used_again(server, tmp_path):
    """the id is free as soon as the stop is handled, not on the next tick"""
    instance = playing(server, tmp_path)
    instance.int_stop()
    assert instance.wait_for_log("playback stopped: running"), instance.log

    rid = write_rid(tmp_path / "again.json", duration_s=0.5)
    tsd = write_tsd(tmp_path / "again-tsd.json", [(0.5, rid.as_uri())])
    mark = instance.setup(tsd.as_uri(), test_id="running", seconds_ahead=3600)
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.log.count("test scheduled: running") == 2, instance.log


def test_a_second_stop_is_reported(server, tmp_path):
    instance = playing(server, tmp_path)
    instance.int_stop()
    assert instance.wait_for_log("playback stopped: running"), instance.log

    mark = instance.int_stop()
    message, _ = instance.expect_error(mark)
    assert "no test is running" in message, message
