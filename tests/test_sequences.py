"""Longer runs of commands and events against one server, checked from end to end.

Every test here sends a series of commands that mixes playbacks, refusals, cancels, stops
and restarts, waits until the last test is over and then checks everything the server
published: the statuses of every test and their order across tests, the errors, the
signal status and what is left in the recording directory.

The radios are mocked, so a playback takes as long as its tsd. start_ahead is two seconds
everywhere. A test holds the pipeline from two seconds before its time until its end, so
the times below keep that much room between tests.
"""

import datetime
import json
import time

from helpers.rid import write_rid
from helpers.tsd import write_tsd

START_AHEAD_S = 2.0
PLAYED = ["TEST_INIT", "TEST_PREPARE", "TEST_READY", "TEST_RUNNING", "TEST_COMPLETED"]


def published(instance, topic):
    return [json.loads(p) for _, _, p in instance.broker.since(0.0, topic)]


def timeline(instance):
    """(test id, status) of every status the server published, in the order they came."""
    return [(m["test_id"], m["test_status"]) for m in published(instance, instance.test_log_topic)]


def statuses(instance, test_id):
    return [s for t, s in timeline(instance) if t == test_id]


def status_times(instance, test_id):
    return {
        m["test_status"]: datetime.datetime.fromisoformat(m["timestamp"])
        for m in published(instance, instance.test_log_topic)
        if m["test_id"] == test_id
    }


def errors(instance):
    """(session id, text) of every log message the server published, in order, without the
    answers to the barriers"""
    return [
        (m.get("session_id"), m["message"])
        for m in published(instance, instance.error_log_topic)
        if "message" in m and not str(m.get("session_id")).startswith("barrier-")
    ]


def signal_changes(instance):
    """The signal statuses in order, with repeats of the same status left out."""
    types = [m["type"] for m in published(instance, instance.error_log_topic) if "type" in m]
    return [t for i, t in enumerate(types) if i == 0 or types[i - 1] != t]


def start_time(tmp_path, test_id):
    content = json.loads((tmp_path / "recordings" / test_id / "test.json").read_text(encoding="utf-8"))
    return datetime.datetime.fromisoformat(content["command"]["start_time"])


def recordings(tmp_path):
    directory = tmp_path / "recordings"
    if not directory.is_dir():
        return []
    return sorted(p.name for p in directory.iterdir())


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False


def one_rid_tsd(tmp_path, name, duration_s):
    rid = write_rid(tmp_path / f"{name}.json", duration_s=duration_s)
    return write_tsd(tmp_path / f"{name}-tsd.json", [(duration_s, rid.as_uri())])


def test_tests_play_in_turn_around_a_refusal_a_cancel_and_a_missing_start(server, tmp_path):
    """a and b play, e overlaps b, c is cancelled, and d is started, cancelled and set up
    again without a TEST_START

        +6   a, 1 s
        +11  b, 1 s
        +12  e, refused
        +16  c, cancelled right after its setup
        +21  d, fails at its time
    """
    url = one_rid_tsd(tmp_path, "short", 1.0).as_uri()
    instance = server(start_ahead_s=START_AHEAD_S)

    instance.setup(url, test_id="a", seconds_ahead=6)
    instance.setup(url, test_id="b", seconds_ahead=11)
    instance.setup(url, test_id="e", seconds_ahead=12)
    instance.setup(url, test_id="c", seconds_ahead=16)
    instance.setup(url, test_id="d", seconds_ahead=21)
    instance.test_start("a")
    instance.test_start("b")
    instance.cancel("c")
    instance.test_start("d")
    instance.cancel("d")
    instance.setup(url, test_id="d", seconds_ahead=21)

    assert wait_until(lambda: statuses(instance, "d")[-1:] == ["TEST_FAILED"], timeout=40.0), timeline(instance)

    assert statuses(instance, "a") == PLAYED, timeline(instance)
    assert statuses(instance, "b") == PLAYED, timeline(instance)
    assert statuses(instance, "e") == ["TEST_FAILED"], timeline(instance)
    c = statuses(instance, "c")
    assert c[0] == "TEST_INIT" and c[-1] == "TEST_FAILED" and "TEST_RUNNING" not in c, c
    d = statuses(instance, "d")
    assert d.count("TEST_INIT") == 2 and "TEST_RUNNING" not in d, d
    assert d[-4:] == ["TEST_INIT", "TEST_PREPARE", "TEST_READY", "TEST_FAILED"], d

    order = timeline(instance)
    assert order.index(("a", "TEST_COMPLETED")) < order.index(("b", "TEST_RUNNING")), order
    assert order.index(("b", "TEST_COMPLETED")) < len(order) - 1 - order[::-1].index(("d", "TEST_FAILED")), order

    texts = errors(instance)
    assert [t for t, _ in texts] == ["e", "d"], texts
    assert "overlaps test" in texts[0][1], texts
    assert "was not authorized by its starting time" in texts[1][1], texts

    assert signal_changes(instance) == ["none", "jamming", "none", "jamming", "none"], signal_changes(instance)
    assert wait_until(lambda: recordings(tmp_path) == []), recordings(tmp_path)


def test_a_stop_ends_one_test_and_the_next_ones_still_play(server, tmp_path):
    """a is stopped while it transmits, b plays after it, and a is set up again and plays

        +5   a, 6 s, stopped about 1 s in
        +14  b, 1 s
        +20  a again, 1 s
    """
    long_url = one_rid_tsd(tmp_path, "long", 6.0).as_uri()
    short_url = one_rid_tsd(tmp_path, "short", 1.0).as_uri()
    instance = server(start_ahead_s=START_AHEAD_S)
    begin = time.monotonic()

    instance.setup(long_url, test_id="a", seconds_ahead=5)
    instance.setup(short_url, test_id="b", seconds_ahead=14)
    instance.test_start("a")
    instance.test_start("b")

    assert wait_until(lambda: "TEST_RUNNING" in statuses(instance, "a"), timeout=15.0), timeline(instance)
    time.sleep(1.0)
    mark = instance.int_stop()
    instance.expect_no_error(mark)
    assert instance.wait_for_log("playback stopped: a"), instance.log

    mark = instance.setup(short_url, test_id="a", seconds_ahead=round(20 - (time.monotonic() - begin)))
    instance.expect_no_error(mark)
    instance.test_start("a")

    assert wait_until(lambda: statuses(instance, "a")[-1:] == ["TEST_COMPLETED"] and statuses(instance, "a").count("TEST_INIT") == 2, timeout=40.0), timeline(instance)

    assert statuses(instance, "a") == PLAYED[:4] + ["TEST_FAILED"] + PLAYED, timeline(instance)
    assert statuses(instance, "b") == PLAYED, timeline(instance)
    order = timeline(instance)
    assert order.index(("a", "TEST_FAILED")) < order.index(("b", "TEST_RUNNING")), order
    assert order.index(("b", "TEST_COMPLETED")) < len(order) - 1 - order[::-1].index(("a", "TEST_RUNNING")), order

    assert errors(instance) == [], errors(instance)
    assert signal_changes(instance) == ["none", "jamming", "none", "jamming", "none", "jamming", "none"], signal_changes(instance)
    assert wait_until(lambda: recordings(tmp_path) == []), recordings(tmp_path)


def test_two_tests_of_several_parts_play_back_to_back(server, tmp_path):
    """x has three parts and y two. y gets its TEST_START only once its pipeline holds

        x at +5:   0.0 rid, 0.5 quiet 2.5, 3.0 rid, 3.5 quiet 2.5, 6.0 rid, 6.5 end
        y at +15:  0.0 rid, 0.5 quiet 2.5, 3.0 rid, 3.5 end
    """
    rid = write_rid(tmp_path / "rid.json", duration_s=0.5)
    u = rid.as_uri()
    x_tsd = write_tsd(tmp_path / "x.json", [(0.5, u), (2.5, None), (0.5, u), (2.5, None), (0.5, u)])
    y_tsd = write_tsd(tmp_path / "y.json", [(0.5, u), (2.5, None), (0.5, u)])
    instance = server(start_ahead_s=START_AHEAD_S)

    instance.setup(x_tsd.as_uri(), test_id="x", seconds_ahead=5)
    instance.setup(y_tsd.as_uri(), test_id="y", seconds_ahead=15)
    instance.test_start("x")
    assert instance.wait_for_log("test scheduled: y"), instance.log
    x_start = start_time(tmp_path, "x")
    y_start = start_time(tmp_path, "y")

    assert instance.wait_for_log("playback started: y", timeout=30.0), instance.log
    mark = instance.test_start("y")
    instance.expect_no_error(mark)
    assert wait_until(lambda: statuses(instance, "y")[-1:] == ["TEST_COMPLETED"], timeout=20.0), timeline(instance)

    log = instance.log
    order = [
        'part 0 of test "x" started',
        'part 0 of test "x" finished',
        'part 1 of test "x" started',
        'part 1 of test "x" finished',
        'part 2 of test "x" started',
        'part 2 of test "x" finished',
        "playback finished: x",
        "playback started: y",
        'part 0 of test "y" started',
        'part 0 of test "y" finished',
        'part 1 of test "y" started',
        'part 1 of test "y" finished',
        "playback finished: y",
    ]
    positions = [log.find(line) for line in order]
    assert -1 not in positions and positions == sorted(positions), log

    assert statuses(instance, "x") == PLAYED, timeline(instance)
    assert statuses(instance, "y") == PLAYED, timeline(instance)
    x_times = status_times(instance, "x")
    y_times = status_times(instance, "y")
    assert x_times["TEST_RUNNING"] == x_start, x_times
    assert x_times["TEST_COMPLETED"] == x_start + datetime.timedelta(seconds=6.5), x_times
    assert y_times["TEST_RUNNING"] == y_start, y_times
    assert y_times["TEST_COMPLETED"] == y_start + datetime.timedelta(seconds=3.5), y_times

    assert errors(instance) == [], errors(instance)
    assert signal_changes(instance) == ["none"] + ["jamming", "none"] * 5, signal_changes(instance)
    assert wait_until(lambda: recordings(tmp_path) == []), recordings(tmp_path)


def test_a_restart_between_two_tests_keeps_the_second_one(server, tmp_path):
    """a plays on the first server, which is killed before b. the second server restores b
    as prepared and authorized and plays it without preparing it again

        +5   a, 1 s
        +30  b, 1 s
    """
    url = one_rid_tsd(tmp_path, "short", 1.0).as_uri()
    first = server(start_ahead_s=START_AHEAD_S)
    first.setup(url, test_id="a", seconds_ahead=5)
    first.setup(url, test_id="b", seconds_ahead=30)
    first.test_start("a")
    first.test_start("b")
    assert wait_until(lambda: statuses(first, "a")[-1:] == ["TEST_COMPLETED"], timeout=20.0), timeline(first)
    assert wait_until(lambda: statuses(first, "b") == ["TEST_INIT", "TEST_PREPARE", "TEST_READY"]), timeline(first)
    assert wait_until(lambda: recordings(tmp_path) == ["b"]), recordings(tmp_path)
    first.stop()

    second = server(start_ahead_s=START_AHEAD_S)
    assert second.wait_for_log("test restored: b"), second.log
    assert wait_until(lambda: statuses(second, "b")[-1:] == ["TEST_COMPLETED"], timeout=40.0), timeline(second)

    assert statuses(first, "a") == PLAYED, timeline(first)
    assert timeline(second) == [("b", "TEST_READY"), ("b", "TEST_RUNNING"), ("b", "TEST_COMPLETED")], timeline(second)
    assert "preparation started: b" not in second.log, second.log
    assert errors(first) == [] and errors(second) == [], (errors(first), errors(second))
    assert wait_until(lambda: recordings(tmp_path) == []), recordings(tmp_path)


def test_a_burst_of_commands_is_handled_in_order(server, tmp_path):
    """the commands go out back to back, so most of them land in one batch. the cancel
    frees the id of x, the second x is a new test and plays"""
    url = one_rid_tsd(tmp_path, "short", 1.0).as_uri()
    instance = server(start_ahead_s=START_AHEAD_S)

    instance.setup(url, test_id="x", seconds_ahead=8)
    instance.setup(url, test_id="y", seconds_ahead=9)
    instance.test_start("x")
    instance.cancel("x")
    instance.test_start("x")
    instance.setup(url, test_id="x", seconds_ahead=8)
    instance.test_start("x")

    assert wait_until(lambda: statuses(instance, "x")[-1:] == ["TEST_COMPLETED"], timeout=30.0), timeline(instance)

    x = statuses(instance, "x")
    assert x.count("TEST_INIT") == 2, x
    second = len(x) - 1 - x[::-1].index("TEST_INIT")
    assert x[second - 1] == "TEST_FAILED" and "TEST_RUNNING" not in x[:second], x
    assert x[second:] == PLAYED, x
    assert statuses(instance, "y") == ["TEST_FAILED"], timeline(instance)

    texts = errors(instance)
    assert [t for t, _ in texts] == ["y", "x"], texts
    assert "overlaps test" in texts[0][1], texts
    assert "unknown test id" in texts[1][1], texts
    assert wait_until(lambda: recordings(tmp_path) == []), recordings(tmp_path)
