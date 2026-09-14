"""Builds rid files for the tests.

The files under assets/rid are ten seconds long at 1.92 megasamples a second, and a
recording of one is three hundred megabytes. The tests write their own instead, short
and narrow, which is what keeps the recordings small. The shape follows
assets/rid/test-rid-sisi-stage-1.json and matches the radios in tests/data/config.json,
one card with two channels.

bandwidth_hz becomes the sample rate, so it decides the size of the recording. The
jammers stay inside the band it leaves.
"""

import json


def make_rid(duration_s=0.5, radio_id="r0", bandwidth_hz=192000):
    return {
        "metadata": {
            "version": "1.0.0",
            "scenario_name": "test",
            "created_utc": "2026-01-01T00:00:00Z",
            "author": "tests",
            "description": "written by the test suite",
        },
        "global_settings": {
            "duration_s": duration_s,
        },
        "radio_settings": [
            {
                "radio_id": radio_id,
                "channels": 2,
                "bandwidth_hz": bandwidth_hz,
                "center_frequencies_hz": [1575420000, 1227600000],
                "gains_db": [10.0, 10.0],
            }
        ],
        "interference_sources": [
            {
                "type": "cw_jammer",
                "radio_id": radio_id,
                "channel_id": 0,
                "center_frequency_hz": 1575420000,
                "power_dbm": -10.0,
                "start_time_s": 0.0,
                "end_time_s": duration_s,
                "parameters": {},
            },
            {
                "type": "noise_jammer",
                "radio_id": radio_id,
                "channel_id": 1,
                "center_frequency_hz": 1227600000 + bandwidth_hz // 8,
                "power_dbm": -12.0,
                "start_time_s": 0.0,
                "end_time_s": duration_s,
                "parameters": {"span_hz": bandwidth_hz // 8},
            },
        ],
    }


def write_rid(path, duration_s=0.5, radio_id="r0", bandwidth_hz=192000):
    rid = make_rid(duration_s, radio_id, bandwidth_hz)
    path.write_text(json.dumps(rid, indent=4), encoding="utf-8")
    return path
