"""The two rules that decide whether a test can be set up at the time it asks for.

A playback holds the pipeline from start_ahead before its time until the scenario runs
out, and only one pipeline runs at a time, so two tests may not overlap. The recordings
run on that same pipeline beforehand, so the whole queue of them has to be finished
before the earliest playback opens its window.

start_ahead is thirty seconds here. The times in a command are whole seconds, so a wide
window keeps the two rules apart from the rounding.
"""

import pytest

START_AHEAD = 30.0


@pytest.fixture
def sched(server):
    return server(start_ahead_s=START_AHEAD)


def test_two_tests_far_apart_are_both_accepted(sched, rid_path):
    url = rid_path.as_uri()

    mark = sched.setup(url, test_id="first", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: first")

    mark = sched.setup(url, test_id="second", seconds_ahead=1200)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: second")


def test_a_test_inside_the_window_of_another_is_rejected(sched, rid_path):
    url = rid_path.as_uri()

    mark = sched.setup(url, test_id="first", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: first")

    # ten seconds later, well inside the thirty the first one holds before its time
    mark = sched.setup(url, test_id="second", seconds_ahead=610)
    message, _ = sched.expect_error(mark)
    assert "overlaps test" in message, message
    assert '"first"' in message, message


def test_the_overlap_is_symmetrical(sched, rid_path):
    """A test that comes before an already scheduled one is caught the same way."""
    url = rid_path.as_uri()

    mark = sched.setup(url, test_id="later", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: later")

    mark = sched.setup(url, test_id="earlier", seconds_ahead=590)
    message, _ = sched.expect_error(mark)
    assert "overlaps test" in message, message


def test_a_test_too_soon_to_record_is_rejected(sched, rid_path):
    """Ten seconds away, and the pipeline needs thirty before that to be free."""
    mark = sched.setup(rid_path.as_uri(), test_id="toosoon", seconds_ahead=10)
    message, _ = sched.expect_error(mark)
    assert "cannot be recorded in time" in message, message


def test_a_test_far_enough_out_is_recorded(sched, rid_path):
    mark = sched.setup(rid_path.as_uri(), test_id="intime", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("recording started: intime")
    assert sched.wait_for_log("recording finished: intime", timeout=60.0)


def test_the_queue_of_recordings_is_counted(server, tmp_path):
    """A time that is far enough out on its own stops being far enough with one waiting.

    The scenario is sixty seconds, so a test on its own has to be more than ninety
    seconds away, sixty to record and thirty to hold the pipeline. With another one still
    being recorded the queue is a hundred and twenty and the same test needs more than a
    hundred and fifty. A hundred and twenty is in between, so it is accepted alone and
    rejected while the other one is under way.

    A recording that is over is not queued work any more, so the two commands go out one
    after the other with no wait between them. HandleCommands takes everything that has
    arrived in one pass, so the first one is still waiting its turn when the second is
    looked at. The scenario is narrow, a recording of it is a few tens of megabytes.
    """
    from helpers.rid import write_rid

    url = write_rid(tmp_path / "long.json", duration_s=60.0, bandwidth_hz=48000).as_uri()

    alone = server(start_ahead_s=START_AHEAD)
    mark = alone.setup(url, test_id="alone", seconds_ahead=120)
    alone.expect_no_error(mark, timeout=2.0)
    assert alone.wait_for_log("test scheduled: alone")

    crowded = server(start_ahead_s=START_AHEAD)
    crowded.setup(url, test_id="waiting", seconds_ahead=3600)
    mark = crowded.setup(url, test_id="crowded", seconds_ahead=120)

    message, _ = crowded.expect_error(mark)
    assert "cannot be recorded in time" in message, message
    assert crowded.wait_for_log("test scheduled: waiting")
