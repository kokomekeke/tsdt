"""Builds tsd files for the tests.

A TEST_SETUP names a tsd, and every step of the tsd names the rid it plays by url. A
test that only cares about one rid wraps it in a tsd of a single step, which is what
wrap_rid does.

A step has to be at least as long as the rid it plays, so a step written here is given
the length of its rid unless the test asks for another one. interference is a required
key and may be null, and a step with null there stays quiet for its length.
"""

import json


def make_tsd(steps, name="test", description="written by the test suite"):
    """steps is a list of (duration_seconds, rid url or None) pairs."""
    return {
        "name": name,
        "description": description,
        "steps": [
            {"duration_seconds": duration, "interference": url}
            for duration, url in steps
        ],
    }


def write_tsd(path, steps, **kwargs):
    path.write_text(json.dumps(make_tsd(steps, **kwargs), indent=4), encoding="utf-8")
    return path


def wrap_rid(rid_path, duration_s=0.5, **kwargs):
    """A tsd of one step next to the rid, playing it."""
    path = rid_path.parent / (rid_path.stem + "-tsd.json")
    return write_tsd(path, [(duration_s, rid_path.as_uri())], **kwargs)
