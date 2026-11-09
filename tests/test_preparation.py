"""the directory a test is prepared into, and the playback from it

the suite sets mock_gnss, so a recording is silence of the right length and rate and
the engine is never asked for anything. that is what makes the checks on the file
cheap and what lets them run without an ephemeris. with prerecord_gnss off nothing is
recorded, and a mocked spoofer plays as a Zero signal instead
"""

import json
import time

import pytest

from helpers.rid import write_gnss_rid
from helpers.tsd import wrap_rid
from helpers.wavio import read_iq, rms

BANDWIDTH_HZ = 192000
DURATION_S = 0.5


def with_ephemeris(config):
    config["gnss_ephemeris_file"] = "ephemeris/none.rnx"


def live(config):
    with_ephemeris(config)
    config["prerecord_gnss"] = False


def test_a_spoofer_is_recorded_into_the_directory_of_its_test(server, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S, bandwidth_hz=BANDWIDTH_HZ)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    sched = server(config_changes=with_ephemeris)
    mark = sched.setup(tsd.as_uri(), test_id="spoofed", seconds_ahead=600)
    sched.expect_no_error(mark)
    assert sched.wait_for_log("preparation finished: spoofed", timeout=30.0), sched.log

    directory = tmp_path / "recordings" / "spoofed"
    assert sorted(p.name for p in directory.iterdir()) == [
        "gnss_000_0000.wav", "rid_000.json", "test.json", "tsd.json",
    ]

    sampleRate, channels = read_iq(directory / "gnss_000_0000.wav")
    assert sampleRate == BANDWIDTH_HZ
    assert len(channels) == 1
    assert len(channels[0]) == int(DURATION_S * BANDWIDTH_HZ)
    assert rms(channels[0]) == 0.0


def test_jammers_alone_leave_no_recording(server, tmp_path, tsd_path):
    sched = server()
    sched.setup(tsd_path.as_uri(), test_id="jammed", seconds_ahead=600)
    assert sched.wait_for_log("preparation finished: jammed", timeout=30.0), sched.log

    directory = tmp_path / "recordings" / "jammed"
    assert sorted(p.name for p in directory.iterdir()) == [
        "rid_000.json", "test.json", "tsd.json",
    ]


def test_a_cancel_removes_the_directory(server, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S, bandwidth_hz=BANDWIDTH_HZ)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    sched = server(config_changes=with_ephemeris)
    sched.setup(tsd.as_uri(), test_id="dropped", seconds_ahead=600)
    assert sched.wait_for_log("preparation finished: dropped", timeout=30.0), sched.log

    directory = tmp_path / "recordings" / "dropped"
    assert directory.is_dir()

    mark = sched.cancel("dropped")
    sched.expect_no_error(mark)
    assert sched.wait_for_log("test cancelled: dropped")
    assert not directory.exists()


def test_a_spoofer_needs_an_ephemeris(server, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S, bandwidth_hz=BANDWIDTH_HZ)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    sched = server()
    mark = sched.setup(tsd.as_uri(), test_id="noephemeris", seconds_ahead=600)
    message, _ = sched.expect_error(mark)
    assert "gnss_ephemeris_file" in message, message


@pytest.mark.parametrize("config_changes", [with_ephemeris, live], ids=["prerecorded", "live"])
def test_a_spoofed_test_runs_through_every_status(server, tmp_path, config_changes):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S, bandwidth_hz=BANDWIDTH_HZ)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    sched = server(start_ahead_s=2.0, config_changes=config_changes)
    mark = sched.setup(tsd.as_uri(), test_id="played", seconds_ahead=6)
    sched.test_start("played")
    sched.expect_no_error(mark)
    assert sched.wait_for_log("playback finished: played", timeout=30.0), sched.log

    def statuses():
        return [json.loads(p) for _, _, p in sched.broker.since(mark, sched.test_log_topic)]

    def signals():
        messages = [json.loads(p) for _, _, p in sched.broker.since(mark, sched.error_log_topic)]
        return [m["type"] for m in messages if "type" in m]

    assert wait_until(lambda: [s["test_status"] for s in statuses()][-1:] == ["TEST_COMPLETED"]), statuses()
    assert [s["test_status"] for s in statuses()] == [
        "TEST_INIT",
        "TEST_PREPARE",
        "TEST_READY",
        "TEST_RUNNING",
        "TEST_COMPLETED",
    ], statuses()
    assert all(s["test_id"] == "played" for s in statuses()), statuses()

    assert wait_until(lambda: signals()[-1:] == ["none"]), signals()
    assert "spoofing" in signals(), signals()
    assert wait_until(lambda: not (tmp_path / "recordings" / "played").exists())


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False
