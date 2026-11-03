"""a tsd cut into parts, each one played by a pipeline of its own

rids closer than start_ahead are merged into one part, a longer gap starts a new one. the
two rids here configure the radio differently, which is only allowed across such a gap.
start_ahead is two seconds, the steps are

    0.0  first rid, 0.5 s
    0.5  quiet, 2.5 s
    3.0  second rid, 0.5 s
    3.5  quiet, 0.5 s
    4.0  end
"""

import datetime
import json
import re
import time

from helpers.rid import write_rid
from helpers.tsd import write_tsd

START_AHEAD_S = 2.0
END_S = 4.0


def two_parts(tmp_path):
    first = write_rid(tmp_path / "first.json", duration_s=0.5, bandwidth_hz=192000)
    second = write_rid(tmp_path / "second.json", duration_s=0.5, bandwidth_hz=96000)
    tsd = write_tsd(
        tmp_path / "tsd.json",
        [(0.5, first.as_uri()), (2.5, None), (0.5, second.as_uri()), (0.5, None)],
    )
    return first, second, tsd


def start_time(tmp_path, test_id):
    content = json.loads((tmp_path / "recordings" / test_id / "test.json").read_text(encoding="utf-8"))
    return datetime.datetime.fromisoformat(content["command"]["start_time"])


def statuses(instance, test_id):
    messages = [json.loads(p) for _, _, p in instance.broker.since(0.0, instance.test_log_topic)]
    return [m for m in messages if m["test_id"] == test_id]


def wait_until(condition, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.05)
    return False


def test_the_parts_play_one_after_the_other(server, tmp_path):
    _, _, tsd = two_parts(tmp_path)
    instance = server(start_ahead_s=START_AHEAD_S)
    mark = instance.setup(tsd.as_uri(), test_id="parts", seconds_ahead=5)
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("playback finished: parts", timeout=30.0), instance.log

    log = instance.log
    order = [
        'part 0 of test "parts" started',
        'part 0 of test "parts" finished',
        'part 1 of test "parts" started',
        'part 1 of test "parts" finished',
        "playback finished: parts",
    ]
    positions = [log.find(line) for line in order]
    assert -1 not in positions and positions == sorted(positions), log

    directory = tmp_path / "recordings" / "parts"
    assert sorted(p.name for p in directory.iterdir()) == [
        "rid_000.json", "rid_002.json", "scenario_000.json", "scenario_001.json", "test.json", "tsd.json",
    ]


def test_the_status_times_follow_the_tsd(server, tmp_path):
    """running at the start of the tsd and completed at its end, whatever the pipelines do"""
    _, _, tsd = two_parts(tmp_path)
    instance = server(start_ahead_s=START_AHEAD_S)
    instance.setup(tsd.as_uri(), test_id="timed", seconds_ahead=5)
    assert instance.wait_for_log("playback finished: timed", timeout=30.0), instance.log
    assert wait_until(lambda: statuses(instance, "timed")[-1:] and statuses(instance, "timed")[-1]["test_status"] == "TEST_COMPLETED")

    start = start_time(tmp_path, "timed")
    times = {m["test_status"]: datetime.datetime.fromisoformat(m["timestamp"]) for m in statuses(instance, "timed")}
    assert times["TEST_RUNNING"] == start, times
    assert times["TEST_COMPLETED"] == start + datetime.timedelta(seconds=END_S), times


def test_a_summary_is_logged_on_setup(server, tmp_path):
    first, second, tsd = two_parts(tmp_path)
    instance = server(start_ahead_s=START_AHEAD_S)
    mark = instance.setup(tsd.as_uri(), test_id="summed", seconds_ahead=600)
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("test scheduled: summed"), instance.log

    log = instance.log
    head = 'test "summed" set up from TSD "test"'
    assert head in log, log
    lines = log[log.index(head):].splitlines()[1:7]
    entries = [re.match(r"\s+(\S+)\s\s(.*)$", line).groups() for line in lines]

    start = start_time(tmp_path, "summed")
    offsets = [(datetime.datetime.fromisoformat(t) - start).total_seconds() for t, _ in entries]
    labels = [label for _, label in entries]
    assert labels == [
        "test start",
        f'transmission: "{first.as_uri()}"',
        "gap",
        f'transmission: "{second.as_uri()}"',
        "gap",
        "test end",
    ], lines
    assert offsets == [0.0, 0.0, 0.5, 3.0, 3.5, END_S], lines


def test_a_summary_lists_every_merged_rid(server, tmp_path):
    rid = write_rid(tmp_path / "rid.json", duration_s=0.5)
    tsd = write_tsd(tmp_path / "tsd.json", [(1.0, None), (0.5, rid.as_uri()), (0.5, rid.as_uri())])
    instance = server(start_ahead_s=START_AHEAD_S)
    instance.setup(tsd.as_uri(), test_id="merged", seconds_ahead=600)
    assert instance.wait_for_log("test scheduled: merged"), instance.log

    log = instance.log
    head = 'test "merged" set up from TSD "test"'
    lines = log[log.index(head):].splitlines()[1:5]
    labels = [re.match(r"\s+\S+\s\s(.*)$", line).group(1) for line in lines]
    assert labels == [
        "test start",
        "gap",
        f'transmission: "{rid.as_uri()}", "{rid.as_uri()}"',
        "test end",
    ], lines
