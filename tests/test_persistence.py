"""the tests that are not over are saved and restored when the server starts again

a second server started in the same directory stands in for the restart
"""

import json
import time

from helpers.rid import write_gnss_rid

DURATION_S = 0.5


def with_ephemeris(config):
    config["gnss_ephemeris_file"] = "ephemeris/none.rnx"


def saved(tmp_path, test_id):
    path = tmp_path / "recordings" / test_id / "test.json"
    return json.loads(path.read_text(encoding="utf-8"))


def statuses(server, mark, test_id):
    messages = [json.loads(p) for _, _, p in server.broker.since(mark, server.test_log_topic)]
    return [m["status"] for m in messages if m["test_id"] == test_id]


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False


def test_a_scheduled_test_is_saved(server, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    first = server(config_changes=with_ephemeris)
    first.setup(rid.as_uri(), test_id="kept", seconds_ahead=600)
    assert first.wait_for_log("preparation finished: kept", timeout=30.0), first.log

    directory = tmp_path / "recordings" / "kept"
    assert (directory / "rid.json").read_text(encoding="utf-8") == rid.read_text(encoding="utf-8")
    content = saved(tmp_path, "kept")
    assert content["status"] == "TEST_READY"
    assert content["command"]["test_id"] == "kept"
    assert content["command"]["scenario_url"] == rid.as_uri()


def test_a_prepared_test_is_restored_without_preparing_it_again(server, broker, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    first = server(config_changes=with_ephemeris)
    first.setup(rid.as_uri(), test_id="kept", seconds_ahead=600)
    assert first.wait_for_log("preparation finished: kept", timeout=30.0), first.log
    first.stop()

    mark = broker.mark()
    second = server(config_changes=with_ephemeris)
    assert second.wait_for_log("test restored: kept"), second.log
    assert wait_until(lambda: statuses(second, mark, "kept") == ["TEST_READY"]), statuses(second, mark, "kept")
    assert "preparation started: kept" not in second.log
    assert (tmp_path / "recordings" / "kept" / "gnss_000.wav").is_file()


def test_a_test_that_was_not_prepared_is_prepared_after_the_restart(server, broker, tmp_path):
    rid = write_gnss_rid(tmp_path / "gnss.json", duration_s=DURATION_S)
    first = server(config_changes=with_ephemeris)
    first.setup(rid.as_uri(), test_id="unfinished", seconds_ahead=600)
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


def test_a_test_missed_while_the_server_was_down_fails(server, broker, tmp_path, rid_path):
    first = server(start_ahead_s=2.0)
    first.setup(rid_path.as_uri(), test_id="missed", seconds_ahead=5)
    assert first.wait_for_log("preparation finished: missed", timeout=30.0), first.log
    first.stop()

    time.sleep(6.0)

    mark = broker.mark()
    second = server(start_ahead_s=2.0)
    message, _ = second.expect_error(mark)
    assert "missed its starting time" in message, message
    assert '"missed"' in message, message
    assert wait_until(lambda: statuses(second, mark, "missed") == ["TEST_FAILED"])
    assert not (tmp_path / "recordings" / "missed" / "test.json").exists()


def test_a_finished_test_is_not_saved(server, tmp_path, rid_path):
    first = server(start_ahead_s=2.0)
    first.setup(rid_path.as_uri(), test_id="finished", seconds_ahead=5)
    assert first.wait_for_log("playback finished: finished", timeout=30.0), first.log

    directory = tmp_path / "recordings" / "finished"
    assert wait_until(lambda: not (directory / "test.json").exists())
    assert (directory / "rid.json").is_file()


def test_an_unreadable_saved_test_is_dropped(server, broker, tmp_path):
    directory = tmp_path / "recordings" / "broken"
    directory.mkdir(parents=True)
    (directory / "test.json").write_text("not json", encoding="utf-8")

    mark = broker.mark()
    second = server()
    message, _ = second.expect_error(mark)
    assert '"broken"' in message, message
    assert wait_until(lambda: statuses(second, mark, "broken") == ["TEST_FAILED"])
    assert not (directory / "test.json").exists()
