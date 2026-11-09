"""the gain updates of a rid, set on the radios while the test plays

the radios are mocked, so the radio monitor has no device to set the gain on and only
logs each update it would set. the tests read those lines back from the server log
"""

import json
import re
import time

from helpers.rid import make_rid
from helpers.tsd import write_tsd

GAIN_LINE = re.compile(r"radio (\w+) ch(\d+): no device, gain (\S+)dB, (\S+)ms late")


def write_rid_with_gain(path, duration_s, gains_db, step_interval_s, values):
    """values is a list of (t, gain) pairs on channel 0 of r0."""
    rid = make_rid(duration_s=duration_s)
    rid["radio_settings"][0]["gains_db"] = gains_db
    rid["gain_updates"] = [
        {
            "radio_id": "r0",
            "channel_id": 0,
            "step_interval_s": step_interval_s,
            "values": [{"t": t, "gain": g} for t, g in values],
        }
    ]
    path.write_text(json.dumps(rid, indent=4), encoding="utf-8")
    return path


def gains_set(instance):
    """(radio, channel, gain, late_ms) of every gain update in the log, in order."""
    return [
        (m.group(1), int(m.group(2)), float(m.group(3)), float(m.group(4)))
        for m in GAIN_LINE.finditer(instance.log)
    ]


def play(server, tsd, test_id):
    instance = server(start_ahead_s=2.0)
    instance.setup(tsd.as_uri(), test_id=test_id, seconds_ahead=4)
    instance.test_start(test_id)
    assert instance.wait_for_log(f"playback started: {test_id}", timeout=30.0), instance.log
    return instance


def test_a_ramp_is_set_every_step_interval(server, tmp_path):
    rid = write_rid_with_gain(tmp_path / "ramp.json", 2.0, [10.0, 10.0], 0.25, [(0.5, 10.0), (1.5, 20.0)])
    tsd = write_tsd(tmp_path / "ramp-tsd.json", [(2.0, rid.as_uri())])
    instance = play(server, tsd, "ramp")
    assert instance.wait_for_log("playback finished: ramp", timeout=30.0), instance.log

    updates = gains_set(instance)
    assert [(r, c, g) for r, c, g, _ in updates] == [
        ("r0", 0, 10.0),
        ("r0", 0, 12.5),
        ("r0", 0, 15.0),
        ("r0", 0, 17.5),
        ("r0", 0, 20.0),
    ], instance.log


def test_an_update_is_set_on_time(server, tmp_path):
    """the monitor sets an update at most 5 ms late on an idle machine"""
    rid = write_rid_with_gain(tmp_path / "ontime.json", 2.0, [10.0, 10.0], 0.25, [(0.5, 10.0), (1.5, 20.0)])
    tsd = write_tsd(tmp_path / "ontime-tsd.json", [(2.0, rid.as_uri())])
    instance = play(server, tsd, "ontime")
    assert instance.wait_for_log("playback finished: ontime", timeout=30.0), instance.log

    updates = gains_set(instance)
    assert len(updates) == 5, instance.log
    for radio, channel, gain, late_ms in updates:
        assert 0.0 <= late_ms < 100.0, f"radio {radio} ch{channel} gain {gain} was set {late_ms} ms late"


def test_the_next_step_sets_the_gain_of_its_radio_settings_again(server, tmp_path):
    """the two steps are end to end, so they play in one part"""
    ramp = write_rid_with_gain(tmp_path / "ramp.json", 1.0, [10.0, 10.0], 0.25, [(0.0, 10.0), (0.5, 20.0)])
    plain = tmp_path / "plain.json"
    plain.write_text(json.dumps(make_rid(duration_s=1.0), indent=4), encoding="utf-8")
    tsd = write_tsd(tmp_path / "two-tsd.json", [(1.0, ramp.as_uri()), (1.0, plain.as_uri())])
    instance = play(server, tsd, "two")
    assert instance.wait_for_log("playback finished: two", timeout=30.0), instance.log
    assert "part 1 of test" not in instance.log, instance.log

    updates = gains_set(instance)
    assert [(r, c, g) for r, c, g, _ in updates] == [
        ("r0", 0, 10.0),
        ("r0", 0, 15.0),
        ("r0", 0, 20.0),
        ("r0", 0, 10.0),
    ], instance.log


def test_a_stop_drops_the_updates_that_are_left(server, tmp_path):
    rid = write_rid_with_gain(tmp_path / "long.json", 20.0, [10.0, 10.0], 1.0, [(0.0, 10.0), (20.0, 30.0)])
    tsd = write_tsd(tmp_path / "long-tsd.json", [(20.0, rid.as_uri())])
    instance = play(server, tsd, "stopped")
    assert instance.wait_for_log("radio r0 ch0: no device, gain 11dB", timeout=10.0), instance.log

    instance.int_stop()
    assert instance.wait_for_log("playback stopped: stopped"), instance.log
    count = len(gains_set(instance))
    time.sleep(2.5)
    assert len(gains_set(instance)) == count, instance.log
