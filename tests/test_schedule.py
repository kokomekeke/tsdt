"""The two rules that decide whether a test can be set up at the time it asks for.

A playback holds the pipeline from start_ahead before its time until the scenario runs
out, and only one pipeline runs at a time, so two tests may not overlap. The tests are
prepared one at a time in the order they start and not during a playback, so a
preparation waits for every playback it would run into, and each one has to end before
the playback of its own test opens its window. A new test is refused when its own
preparation or that of another test would end too late.

start_ahead is thirty seconds here. The times in a command are whole seconds, so a wide
window keeps the two rules apart from the rounding.
"""

import pytest

from helpers.rid import write_gnss_rid, write_rid
from helpers.tsd import wrap_rid, write_tsd

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


def test_a_test_that_starts_first_is_prepared_first(server, tmp_path):
    """a gnss recording of sixty seconds is estimated at sixty seconds of work

    the test far out is prepared after the one that starts first and its playback, so the
    one that starts first only waits for the far one if that is already being prepared
    """
    rid = write_gnss_rid(tmp_path / "long.json", duration_s=60.0, bandwidth_hz=48000)
    url = wrap_rid(rid, duration_s=60.0).as_uri()

    sched = server(start_ahead_s=START_AHEAD, config_changes=with_ephemeris)
    sched.setup(url, test_id="far", seconds_ahead=3600)
    mark = sched.setup(url, test_id="first", seconds_ahead=200)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: far")
    assert sched.wait_for_log("test scheduled: first")


def test_a_test_is_scheduled_while_another_plays(server, tmp_path, tsd_path):
    instance = playing(server, tmp_path)
    mark = instance.setup(tsd_path.as_uri(), test_id="next", seconds_ahead=60)
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("test scheduled: next")


def test_a_preparation_waits_for_the_playback_in_front_of_it(server, tmp_path, tsd_path):
    """the gnss preparation would not end before the window of the ready test opens, so
    it waits until that test has played"""
    rid = write_gnss_rid(tmp_path / "long.json", duration_s=60.0, bandwidth_hz=48000)
    url = wrap_rid(rid, duration_s=60.0).as_uri()

    instance = server(start_ahead_s=2.0, config_changes=with_ephemeris)
    instance.setup(tsd_path.as_uri(), test_id="soon", seconds_ahead=10)
    instance.test_start("soon")
    assert instance.wait_for_log("preparation finished: soon", timeout=5.0), instance.log

    mark = instance.setup(url, test_id="after", seconds_ahead=200)
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("test scheduled: after")
    assert not instance.wait_for_log("preparation started: after", timeout=3.0), instance.log

    assert instance.wait_for_log("playback finished: soon", timeout=30.0), instance.log
    assert instance.wait_for_log("preparation started: after", timeout=5.0), instance.log
    log = instance.log
    assert log.index("playback finished: soon") < log.index("preparation started: after"), log


def test_a_test_that_delays_another_preparation_is_rejected(server, tmp_path):
    """while a test plays nothing is prepared. the gnss preparation of later would run
    after the playback, sixty seconds, and end well before its window. the window of
    earlier opens in the middle of it, so it would have to wait until earlier has played
    and end too late"""
    rid = write_gnss_rid(tmp_path / "long.json", duration_s=60.0, bandwidth_hz=48000)
    url = wrap_rid(rid, duration_s=60.0).as_uri()
    jammer = write_rid(tmp_path / "jammer.json", duration_s=0.5)
    jammer_url = wrap_rid(jammer, duration_s=0.5).as_uri()

    instance = playing(server, tmp_path, config_changes=with_ephemeris)
    mark = instance.setup(url, test_id="later", seconds_ahead=110)
    instance.expect_no_error(mark, timeout=2.0)
    assert instance.wait_for_log("test scheduled: later")

    mark = instance.setup(jammer_url, test_id="earlier", seconds_ahead=60)
    message, _ = instance.expect_error(mark)
    assert "would delay the preparation of test" in message, message
    assert '"earlier"' in message and '"later"' in message, message


def playing(server, tmp_path, config_changes=None):
    """a server with a start_ahead of two seconds that plays a test of twenty"""
    rid = write_rid(tmp_path / "running.json", duration_s=20.0)
    tsd = write_tsd(tmp_path / "running-tsd.json", [(20.0, rid.as_uri())])
    instance = server(start_ahead_s=2.0, config_changes=config_changes)
    instance.setup(tsd.as_uri(), test_id="running", seconds_ahead=4)
    instance.test_start("running")
    assert instance.wait_for_log("playback started: running", timeout=30.0), instance.log
    return instance


def test_jammers_alone_are_not_counted_as_work(server, tsd_path):
    """a test without jobs only needs start_ahead"""
    sched = server(start_ahead_s=START_AHEAD)
    mark = sched.setup(tsd_path.as_uri(), test_id="quick", seconds_ahead=40)
    sched.expect_no_error(mark, timeout=2.0)
    assert sched.wait_for_log("test scheduled: quick")


def with_ephemeris(config):
    config["gnss_ephemeris_file"] = "ephemeris/none.rnx"
