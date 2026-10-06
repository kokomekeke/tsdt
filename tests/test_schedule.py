"""The two rules that decide whether a test can be set up at the time it asks for.

A playback holds the pipeline from start_ahead before its time until the scenario runs
out, and only one pipeline runs at a time, so two tests may not overlap. The tests are
prepared one at a time and not during a playback, so the whole queue of preparations has
to finish before the earliest playback opens its window.

start_ahead is thirty seconds here. The times in a command are whole seconds, so a wide
window keeps the two rules apart from the rounding.
"""

import pytest

START_AHEAD = 30.0


@pytest.fixture
def sched(server):
    return server(start_ahead_s=START_AHEAD)


def test_two_tests_far_apart_are_both_accepted(sched, tsd_path):
    url = tsd_path.as_uri()

    mark = sched.setup(url, test_id="first", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: first")

    mark = sched.setup(url, test_id="second", seconds_ahead=1200)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: second")


def test_a_test_inside_the_window_of_another_is_rejected(sched, tsd_path):
    url = tsd_path.as_uri()

    mark = sched.setup(url, test_id="first", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: first")

    # ten seconds later, well inside the thirty the first one holds before its time
    mark = sched.setup(url, test_id="second", seconds_ahead=610)
    message, _ = sched.expect_error(mark)
    assert "overlaps test" in message, message
    assert '"first"' in message, message


def test_the_overlap_is_symmetrical(sched, tsd_path):
    """A test that comes before an already scheduled one is caught the same way."""
    url = tsd_path.as_uri()

    mark = sched.setup(url, test_id="later", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: later")

    mark = sched.setup(url, test_id="earlier", seconds_ahead=590)
    message, _ = sched.expect_error(mark)
    assert "overlaps test" in message, message


def test_a_test_too_soon_to_prepare_is_rejected(sched, tsd_path):
    """Ten seconds away, and the pipeline needs thirty before that to be free."""
    mark = sched.setup(tsd_path.as_uri(), test_id="toosoon", seconds_ahead=10)
    message, _ = sched.expect_error(mark)
    assert "cannot be prepared in time" in message, message


def test_a_test_far_enough_out_is_prepared(sched, tsd_path):
    mark = sched.setup(tsd_path.as_uri(), test_id="intime", seconds_ahead=600)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("preparation started: intime")
    assert sched.wait_for_log("preparation finished: intime", timeout=60.0)


def test_the_queue_of_preparations_is_counted(server, tmp_path):
    """a gnss recording of sixty seconds is estimated at sixty seconds of work

    alone a test needs more than ninety seconds, sixty to prepare and thirty of
    start_ahead. with another one queued it needs more than a hundred and fifty. the two
    commands are sent without a wait, so the first one is still pending when the second
    one is checked
    """
    from helpers.rid import write_gnss_rid
    from helpers.tsd import wrap_rid

    rid = write_gnss_rid(tmp_path / "long.json", duration_s=60.0, bandwidth_hz=48000)
    url = wrap_rid(rid, duration_s=60.0).as_uri()

    alone = server(start_ahead_s=START_AHEAD, config_changes=with_ephemeris)
    mark = alone.setup(url, test_id="alone", seconds_ahead=120)
    alone.expect_no_error(mark, timeout=2.0)
    assert alone.wait_for_log("test scheduled: alone")

    # a directory of its own, it would restore the test of the first server otherwise
    def separate(config):
        with_ephemeris(config)
        config["recording_directory"] = str(tmp_path / "crowded").replace("\\", "/")

    crowded = server(start_ahead_s=START_AHEAD, config_changes=separate)
    crowded.setup(url, test_id="waiting", seconds_ahead=3600)
    mark = crowded.setup(url, test_id="crowded", seconds_ahead=120)

    message, _ = crowded.expect_error(mark)
    assert "cannot be prepared in time" in message, message
    assert crowded.wait_for_log("test scheduled: waiting")


def test_jammers_alone_are_not_counted_as_work(server, tsd_path):
    """a test without jobs only needs start_ahead"""
    sched = server(start_ahead_s=START_AHEAD)
    mark = sched.setup(tsd_path.as_uri(), test_id="quick", seconds_ahead=40)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: quick")


def with_ephemeris(config):
    config["gnss_ephemeris_file"] = "ephemeris/none.rnx"
