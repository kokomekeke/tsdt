"""Every way reading a scenario can go, through the server and through scglint."""

import json
import time

import pytest

from helpers.binaries import ROOT
from helpers.httpmock import free_port

CONFIG = ROOT / "tests" / "data" / "config.json"

# name, options for the http mock, the name asked for, the text the answer must carry.
# a text of None means the read worked and the rid came through
SERVED = [
    ("file_is_there", dict(mode="serve"), "rid.json", None),
    ("file_is_missing", dict(mode="serve"), "nope.json", "404 Not Found"),
    ("answers_nothing", dict(mode="stall", seconds=30.0), "rid.json", "timed out reading the response"),
    ("answers_slowly", dict(mode="slow", chunk=16, delay=1.0), "rid.json", "timed out reading the response"),
    ("says_500", dict(mode="status", code=500), "rid.json", "500 Internal Server Error"),
    ("body_too_large", dict(mode="big"), "rid.json", "larger than the limit"),
    ("redirects", dict(mode="redirect"), "rid.json", "302 Found"),
]

# name, how the location is written, the text the answer must carry
LOCAL = [
    ("host_is_a_name", "name_host", "has to be an ip address"),
    ("bare_path", "bare", 'has to begin with "file://" or "http://"'),
    ("relative_file_url", "relative", "empty host and an absolute path"),
    ("unknown_scheme", "ftp", "scheme of a location has to be http or file"),
    ("file_url", "file", None),
    ("file_url_missing", "file_missing", "failed to open file"),
]


def closed_port_url():
    return f"http://127.0.0.1:{free_port()}/rid.json"


def config_with_timeout(tmp_path, seconds):
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    config["scenario_fetch_timeout_s"] = seconds
    path = tmp_path / f"config-{seconds}.json"
    path.write_text(json.dumps(config, indent=4), encoding="utf-8")
    return path


def local_url(kind, rid_path):
    if kind == "closed_port":
        return f"http://127.0.0.1:{free_port()}/rid.json"
    if kind == "name_host":
        return "http://localhost:8000/rid.json"
    if kind == "bare":
        return str(rid_path)
    if kind == "relative":
        return "file://assets/rid/test-rid-1.json"
    if kind == "ftp":
        return "ftp://127.0.0.1/rid.json"
    if kind == "file":
        return rid_path.as_uri()
    if kind == "file_missing":
        return (rid_path.parent / "nope.json").as_uri()
    raise ValueError(kind)


@pytest.mark.parametrize("name,options,asked,expected", SERVED, ids=[c[0] for c in SERVED])
def test_over_mqtt_served(scgserver, http_mock, rid_path, name, options, asked, expected):
    mock = http_mock(directory=rid_path.parent, **options)
    mark = scgserver.setup(mock.url(asked), test_id=name)

    if expected is None:
        scgserver.expect_no_error(mark, timeout=2.0)
        assert scgserver.wait_for_log(f"test scheduled: {name}")
        return

    message, seconds = scgserver.expect_error(mark)
    assert expected in message, message
    if "timed out" in expected:
        assert 1.5 < seconds < 4.0, f"the timeout took {seconds:.2f} s"


@pytest.mark.parametrize("name,kind,expected", LOCAL, ids=[c[0] for c in LOCAL])
def test_over_mqtt_local(scgserver, rid_path, name, kind, expected):
    mark = scgserver.setup(local_url(kind, rid_path), test_id=name)

    if expected is None:
        scgserver.expect_no_error(mark, timeout=2.0)
        assert scgserver.wait_for_log(f"test scheduled: {name}")
        return

    message, _ = scgserver.expect_error(mark)
    assert expected in message, message


@pytest.mark.parametrize("name,options,asked,expected", SERVED, ids=[c[0] for c in SERVED])
def test_through_lint_served(build, http_mock, rid_path, name, options, asked, expected):
    mock = http_mock(directory=rid_path.parent, **options)
    result = build.run("scglint", ["rid", CONFIG, mock.url(asked)])
    output = result.stdout + result.stderr

    if expected is None:
        assert result.returncode == 0, output
        return
    assert result.returncode == 2, output
    assert expected in output, output


@pytest.mark.parametrize("name,kind,expected", LOCAL, ids=[c[0] for c in LOCAL])
def test_through_lint_local(build, rid_path, name, kind, expected):
    result = build.run("scglint", ["rid", CONFIG, local_url(kind, rid_path)])
    output = result.stdout + result.stderr

    if expected is None:
        assert result.returncode == 0, output
        return
    assert result.returncode == 2, output
    assert expected in output, output


# a machine refuses a connection to a closed port of its own accord, and on windows that
# answer takes about two seconds to arrive. the timeout is set well away from it in both
# directions here, so which of the two ends the attempt is decided rather than raced


def test_a_closed_port_is_refused(server, rid_path):
    """Six seconds to wait, and the refusal arrives long before that."""
    instance = server(fetch_timeout_s=6.0)
    mark = instance.setup(closed_port_url(), test_id="refused")
    message, seconds = instance.expect_error(mark, timeout=12.0)
    assert "the connection was refused" in message, message
    assert seconds < 6.0, f"the refusal took {seconds:.2f} s"


def test_a_connect_can_run_out_of_time(server, rid_path):
    """A fifth of a second to wait, and the refusal is nowhere near that quick."""
    instance = server(fetch_timeout_s=0.2)
    mark = instance.setup(closed_port_url(), test_id="tooslow")
    message, _ = instance.expect_error(mark)
    assert "timed out connecting to" in message, message


def test_through_lint_a_closed_port_is_refused(build, tmp_path):
    result = build.run("scglint", ["rid", config_with_timeout(tmp_path, 6.0), closed_port_url()])
    output = result.stdout + result.stderr
    assert result.returncode == 2, output
    assert "the connection was refused" in output, output


def test_through_lint_a_connect_can_run_out_of_time(build, tmp_path):
    start = time.monotonic()
    result = build.run("scglint", ["rid", config_with_timeout(tmp_path, 0.2), closed_port_url()])
    took = time.monotonic() - start
    output = result.stdout + result.stderr

    assert result.returncode == 2, output
    assert "timed out connecting to" in output, output
    assert took < 10.0, f"the call took {took:.2f} s"
