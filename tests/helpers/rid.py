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


# ---------------------------------------------------------------------------------------
# Rids that move the radios about, for config-two-radios.json
#
# r0 drives one channel there and r1 drives two. A source on channel 1 is therefore fine
# on r1 and out of range on r0, which is what makes a lookup that reads the wrong row of
# radio_settings visible instead of silently agreeing.
# ---------------------------------------------------------------------------------------

PERMUTED_RADIOS = {
    "r0": {
        "radio_id": "r0",
        "channels": 1,
        "bandwidth_hz": 192000,
        "center_frequencies_hz": [1575420000],
        "gains_db": [10.0],
    },
    "r1": {
        "radio_id": "r1",
        "channels": 2,
        "bandwidth_hz": 192000,
        "center_frequencies_hz": [1575420000, 1227600000],
        "gains_db": [10.0, 10.0],
    },
}

PERMUTED_SOURCES = {
    "r0": {
        "type": "cw_jammer",
        "radio_id": "r0",
        "channel_id": 0,
        "center_frequency_hz": 1575420000,
        "power_dbm": -10.0,
        "start_time_s": 0.0,
        "end_time_s": 0.5,
        "parameters": {},
    },
    "r1": {
        # the second channel, which only r1 has
        "type": "cw_jammer",
        "radio_id": "r1",
        "channel_id": 1,
        "center_frequency_hz": 1227600000,
        "power_dbm": -10.0,
        "start_time_s": 0.0,
        "end_time_s": 0.5,
        "parameters": {},
    },
}


def make_permuted_rid(order, duration_s=0.5):
    """A rid naming the given radios, in the given order, one source on each."""
    return {
        "metadata": {
            "version": "1.0.0",
            "scenario_name": "permuted",
            "created_utc": "2026-01-01T00:00:00Z",
            "author": "tests",
            "description": "radio_settings in the order " + ", ".join(order),
        },
        "global_settings": {
            "duration_s": duration_s,
        },
        "radio_settings": [PERMUTED_RADIOS[r] for r in order],
        "interference_sources": [PERMUTED_SOURCES[r] for r in order],
    }


# ---------------------------------------------------------------------------------------
# A rid with a gnss spoofer on it
#
# 192 kHz is narrower than a real L1 signal needs, and is chosen to keep the recording
# small. The band still holds the center, so the engine keeps L1 rather than filtering it
# away; what comes out is spectrally cut, which is fine for asking whether the pipeline
# runs at all.
# ---------------------------------------------------------------------------------------

def make_gnss_rid(duration_s=0.5, radio_id="r0", bandwidth_hz=192000):
    return {
        "metadata": {
            "version": "1.0.0",
            "scenario_name": "gnss",
            "created_utc": "2026-01-01T00:00:00Z",
            "author": "tests",
            "description": "one gnss spoofer, written by the test suite",
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
                "type": "gnss_spoofer",
                "radio_id": radio_id,
                "channel_id": 0,
                "center_frequency_hz": 1575420000,
                "power_dbm": -10.0,
                "start_time_s": 0.0,
                "end_time_s": duration_s,
                "parameters": {
                    "constellation": "GPS",
                    "pnr_list": [1, 2, 3],
                    "trajectory": [
                        {"t": 0.0, "longitude": 19.015773, "latitude": 47.492721, "altitude": 120.0},
                        {"t": duration_s, "longitude": 19.015800, "latitude": 47.492740, "altitude": 120.0},
                    ],
                },
            }
        ],
    }


def write_gnss_rid(path, duration_s=0.5, radio_id="r0", bandwidth_hz=192000):
    rid = make_gnss_rid(duration_s, radio_id, bandwidth_hz)
    path.write_text(json.dumps(rid, indent=4), encoding="utf-8")
    return path
