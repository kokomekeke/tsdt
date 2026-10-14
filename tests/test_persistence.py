"""every test is saved with its last status, the ones that are not over are restored
when the server starts again

a second server started in the same directory stands in for the restart
"""

import json
import time

import pytest

from helpers.rid import write_gnss_rid
from helpers.server import utc_string
from helpers.tsd import wrap_rid

DURATION_S = 0.5


def with_ephemeris(config):
    config["gnss_ephemeris_file"] = "ephemeris/none.rnx"


def write_saved(tmp_path, test_id, rid, seconds_ahead, status="TEST_INIT", saved_id=None):
    """the files the server leaves for a test, written by hand

    only these two are read back. the server also leaves the tsd and the rid of every
    step beside them, as a record of what the test was built from, and the url in the
    command is never read again, which is why a rid stands in for a tsd here
    """
    directory = tmp_path / "recordings" / test_id
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "rid.json").write_text(rid.read_text(encoding="utf-8"), encoding="utf-8")
    content = {
        "command": {
            "scenario_url": rid.as_uri(),
            "start_time": utc_string(seconds_ahead),
            "test_id": saved_id or test_id,
        },
        "status": status,
    }
    (directory / "test.json").write_text(json.dumps(content), encoding="utf-8")
    return directory


def saved(tmp_path, test_id):
    path = tmp_path / "recordings" / test_id / "test.json"
    return json.loads(path.read_text(encoding="utf-8"))


def statuses(server, mark, test_id):
    messages = [json.loads(p) for _, _, p in server.broker.since(mark, server.test_log_topic)]
    return [m["test_status"] for m in messages if m["test_id"] == test_id]


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False


def test_a_scheduled_test_is_saved(server, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    first = server(config_changes=with_ephemeris)
    first.setup(tsd.as_uri(), test_id="kept", seconds_ahead=600)
    assert first.wait_for_log("preparation finished: kept", timeout=30.0), first.log

    directory = tmp_path / "recordings" / "kept"
    # the tsd and the rid of the step are kept as they were read, rid.json holds the
    # merged rid the test plays
    assert (directory / "tsd.json").read_text(encoding="utf-8") == tsd.read_text(encoding="utf-8")
    assert (directory / "rid_000.json").read_text(encoding="utf-8") == rid.read_text(encoding="utf-8")
    merged = json.loads((directory / "rid.json").read_text(encoding="utf-8"))
    assert merged["metadata"]["scenario_name"] == "test"
    assert merged["global_settings"]["duration_s"] == DURATION_S

    content = saved(tmp_path, "kept")
    assert content["status"] == "TEST_READY"
    assert content["command"]["test_id"] == "kept"
    assert content["command"]["scenario_url"] == tsd.as_uri()


def test_a_prepared_test_is_restored_without_preparing_it_again(server, broker, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    first = server(config_changes=with_ephemeris)
    first.setup(tsd.as_uri(), test_id="kept", seconds_ahead=600)
    assert first.wait_for_log("preparation finished: kept", timeout=30.0), first.log
    first.stop()

    mark = broker.mark()
    second = server(config_changes=with_ephemeris)
    assert second.wait_for_log("test restored: kept"), second.log
    assert wait_until(lambda: statuses(second, mark, "kept") == ["TEST_READY"]), statuses(second, mark, "kept")
    assert "preparation started: kept" not in second.log
    assert (tmp_path / "recordings" / "kept" / "gnss_0000.wav").is_file()


def test_a_test_that_was_not_prepared_is_prepared_after_the_restart(server, broker, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    first = server(config_changes=with_ephemeris)
    first.setup(tsd.as_uri(), test_id="unfinished", seconds_ahead=600)
    assert first.wait_for_log("preparation finished: unfinished", timeout=30.0), first.log
    first.stop()

    # as if the server went down while it was preparing
    path = tmp_path / "recordings" / "unfinished" / "test.json"
    content = saved(tmp_path, "unfinished")
    content["status"] = "TEST_PREPARE"
    path.write_text(json.dumps(content), encoding="utf-8")

    mark = broker.mark()
    second = server(config_changes=with_ephemeris)
    assert second.wait_for_log("test restored: unfinished"), second.log
    assert second.wait_for_log("preparation finished: unfinished", timeout=30.0), second.log
    assert wait_until(lambda: statuses(second, mark, "unfinished")[-1:] == ["TEST_READY"])
    assert statuses(second, mark, "unfinished") == ["TEST_INIT", "TEST_PREPARE", "TEST_READY"]


def test_a_test_missed_while_the_server_was_down_fails(server, broker, tmp_path, tsd_path):
    first = server(start_ahead_s=2.0)
    first.setup(tsd_path.as_uri(), test_id="missed", seconds_ahead=5)
    assert first.wait_for_log("preparation finished: missed", timeout=30.0), first.log
    first.stop()

    time.sleep(6.0)

    mark = broker.mark()
    second = server(start_ahead_s=2.0)
    message, _ = second.expect_error(mark)
    assert "missed its starting time" in message, message
    assert '"missed"' in message, message
    assert wait_until(lambda: statuses(second, mark, "missed") == ["TEST_FAILED"])
    assert saved(tmp_path, "missed")["status"] == "TEST_FAILED"


def test_a_finished_test_is_saved_as_completed(server, tmp_path, tsd_path):
    first = server(start_ahead_s=2.0)
    first.setup(tsd_path.as_uri(), test_id="finished", seconds_ahead=5)
    assert first.wait_for_log("playback finished: finished", timeout=30.0), first.log

    assert wait_until(lambda: saved(tmp_path, "finished")["status"] == "TEST_COMPLETED")
    assert (tmp_path / "recordings" / "finished" / "rid.json").is_file()


@pytest.mark.parametrize("status", ["TEST_COMPLETED", "TEST_FAILED"])
def test_a_test_that_is_over_is_not_restored(server, broker, tmp_path, rid_path, status):
    write_saved(tmp_path, "over", rid_path, seconds_ahead=600, status=status)

    mark = broker.mark()
    restarted = server()
    assert restarted.wait_for_log("server running"), restarted.log
    restarted.expect_no_error(mark)
    assert statuses(restarted, mark, "over") == []
    assert "test restored" not in restarted.log
    assert saved(tmp_path, "over")["status"] == status


def test_a_directory_without_test_json_is_skipped(server, broker, tmp_path, rid_path):
    directory = write_saved(tmp_path, "plain", rid_path, seconds_ahead=600)
    (directory / "test.json").unlink()

    mark = broker.mark()
    restarted = server()
    assert restarted.wait_for_log("server running"), restarted.log
    restarted.expect_no_error(mark)
    assert statuses(restarted, mark, "plain") == []
    assert "test restored" not in restarted.log


def test_an_unreadable_saved_test_is_dropped(server, broker, tmp_path):
    directory = tmp_path / "recordings" / "broken"
    directory.mkdir(parents=True)
    (directory / "test.json").write_text("not json", encoding="utf-8")

    mark = broker.mark()
    second = server()
    message, _ = second.expect_error(mark)
    assert '"broken"' in message, message
    assert wait_until(lambda: statuses(second, mark, "broken") == ["TEST_FAILED"])
    assert (directory / "test.json").read_text(encoding="utf-8") == "not json"


def test_a_running_test_is_played_again(server, broker, tmp_path, rid_path):
    """the server went down during start_ahead, before the start time"""
    write_saved(tmp_path, "interrupted", rid_path, seconds_ahead=600, status="TEST_RUNNING")

    mark = broker.mark()
    restarted = server()
    assert restarted.wait_for_log("test restored: interrupted"), restarted.log
    assert wait_until(lambda: statuses(restarted, mark, "interrupted") == ["TEST_READY"])
    assert "preparation started: interrupted" not in restarted.log
    assert saved(tmp_path, "interrupted")["status"] == "TEST_READY"


def test_the_later_of_two_overlapping_tests_is_dropped(server, broker, tmp_path, rid_path):
    """restored in the order they start, so the earlier one is kept"""
    write_saved(tmp_path, "later", rid_path, seconds_ahead=601, status="TEST_READY")
    write_saved(tmp_path, "earlier", rid_path, seconds_ahead=600, status="TEST_READY")

    mark = broker.mark()
    restarted = server()
    message, _ = restarted.expect_error(mark)
    assert '"later"' in message, message
    assert "overlaps test" in message and '"earlier"' in message, message
    assert wait_until(lambda: statuses(restarted, mark, "later") == ["TEST_FAILED"])
    assert statuses(restarted, mark, "earlier") == ["TEST_READY"]
    assert saved(tmp_path, "later")["status"] == "TEST_FAILED"
    assert saved(tmp_path, "earlier")["status"] == "TEST_READY"


def test_a_test_that_can_no_longer_be_prepared_in_time_is_dropped(server, broker, tmp_path):
    """sixty seconds of gnss recording do not fit into the forty left before the start"""
    rid = write_gnss_rid(tmp_path / "long.json", duration_s=60.0, bandwidth_hz=48000)
    write_saved(tmp_path, "late", rid, seconds_ahead=40)

    mark = broker.mark()
    restarted = server(config_changes=with_ephemeris)
    message, _ = restarted.expect_error(mark)
    assert '"late"' in message and "cannot be prepared in time" in message, message
    assert wait_until(lambda: statuses(restarted, mark, "late") == ["TEST_FAILED"])


def test_a_test_the_new_config_rejects_is_dropped(server, broker, tmp_path):
    """a spoofer saved under a config with an ephemeris, restored under one without"""
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    write_saved(tmp_path, "spoofed", rid, seconds_ahead=600, status="TEST_READY")

    mark = broker.mark()
    restarted = server()
    message, _ = restarted.expect_error(mark)
    assert '"spoofed"' in message and "gnss_ephemeris_file" in message, message
    assert wait_until(lambda: statuses(restarted, mark, "spoofed") == ["TEST_FAILED"])
    assert saved(tmp_path, "spoofed")["status"] == "TEST_FAILED"


def test_a_test_without_its_rid_is_dropped(server, broker, tmp_path, rid_path):
    directory = write_saved(tmp_path, "norid", rid_path, seconds_ahead=600)
    (directory / "rid.json").unlink()

    mark = broker.mark()
    restarted = server()
    message, _ = restarted.expect_error(mark)
    assert '"norid"' in message and "rid.json" in message, message
    assert wait_until(lambda: statuses(restarted, mark, "norid") == ["TEST_FAILED"])


def test_a_test_in_the_wrong_directory_is_dropped(server, broker, tmp_path, rid_path):
    write_saved(tmp_path, "moved", rid_path, seconds_ahead=600, saved_id="original")

    mark = broker.mark()
    restarted = server()
    message, _ = restarted.expect_error(mark)
    assert '"moved"' in message and '"original"' in message, message
    assert wait_until(lambda: statuses(restarted, mark, "moved") == ["TEST_FAILED"])
    assert "test restored" not in restarted.log


def test_the_state_is_saved_on_every_change(server, tmp_path):
    """the status in test.json follows the test to its end"""
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    tsd = wrap_rid(rid, duration_s=DURATION_S)
    sched = server(start_ahead_s=2.0, config_changes=with_ephemeris)
    sched.setup(tsd.as_uri(), test_id="followed", seconds_ahead=6)
    assert sched.wait_for_log("preparation finished: followed", timeout=30.0), sched.log
    assert saved(tmp_path, "followed")["status"] == "TEST_READY"

    running = []

    def status_changed():
        try:
            running.append(saved(tmp_path, "followed")["status"])
        except FileNotFoundError:
            pass
        return "TEST_RUNNING" in running

    assert sched.wait_for_log("playback started: followed", timeout=30.0), sched.log
    assert wait_until(status_changed, timeout=2.0), running

    assert sched.wait_for_log("playback finished: followed", timeout=30.0), sched.log
    assert wait_until(lambda: saved(tmp_path, "followed")["status"] == "TEST_COMPLETED")
