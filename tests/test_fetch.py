"""Every way reading a tsd can go, through the server and through scglint.

A TEST_SETUP names a tsd, and the tsd names the rid of each of its steps. The tables
below are about reading the tsd itself, so the step of every tsd here points at a
local file and only the tsd travels over http. The cases at the end are about the
second read, the one that goes after a step.
"""

import json
import socket
import time

import pytest

from helpers.binaries import ROOT
from helpers.httpmock import free_port
from helpers.tsd import write_tsd

CONFIG = ROOT / "tests" / "data" / "config.json"

# name, options for the http mock, the name asked for, the text the answer must carry.
# a text of None means the read worked and the tsd came through
SERVED = [
    ("file_is_there", dict(mode="serve"), "tsd.json", None),
    ("file_is_missing", dict(mode="serve"), "nope.json", "404 Not Found"),
    ("answers_nothing", dict(mode="stall", seconds=30.0), "tsd.json", "timed out reading the response"),
    ("answers_slowly", dict(mode="slow", chunk=16, delay=1.0), "tsd.json", "timed out reading the response"),
    ("says_500", dict(mode="status", code=500), "tsd.json", "500 Internal Server Error"),
    ("body_too_large", dict(mode="big"), "tsd.json", "larger than the limit"),
    ("redirects", dict(mode="redirect"), "tsd.json", "302 Found"),
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
    return f"http://127.0.0.1:{free_port()}/tsd.json"


def config_with_timeout(tmp_path, seconds):
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    config["scenario_fetch_timeout_s"] = seconds
    path = tmp_path / f"config-{seconds}.json"
    path.write_text(json.dumps(config, indent=4), encoding="utf-8")
    return path


def local_url(kind, tsd_path):
    if kind == "closed_port":
        return f"http://127.0.0.1:{free_port()}/tsd.json"
    if kind == "name_host":
        return "http://localhost:8000/tsd.json"
    if kind == "bare":
        return str(tsd_path)
    if kind == "relative":
        return "file://assets/rid/test-rid-1.json"
    if kind == "ftp":
        return "ftp://127.0.0.1/tsd.json"
    if kind == "file":
        return tsd_path.as_uri()
    if kind == "file_missing":
        return (tsd_path.parent / "nope.json").as_uri()
    raise ValueError(kind)


@pytest.mark.parametrize("name,options,asked,expected", SERVED, ids=[c[0] for c in SERVED])
def test_over_mqtt_served(scgserver, http_mock, tsd_path, name, options, asked, expected):
    mock = http_mock(directory=tsd_path.parent, **options)
    mark = scgserver.setup(mock.url(asked), test_id=name)

    if expected is None:
        scgserver.expect_no_error(mark)
        assert scgserver.wait_for_log(f"test scheduled: {name}")
        return

    message, seconds = scgserver.expect_error(mark)
    assert expected in message, message
    if "timed out" in expected:
        assert 1.5 < seconds < 4.0, f"the timeout took {seconds:.2f} s"


@pytest.mark.parametrize("name,kind,expected", LOCAL, ids=[c[0] for c in LOCAL])
def test_over_mqtt_local(scgserver, tsd_path, name, kind, expected):
    mark = scgserver.setup(local_url(kind, tsd_path), test_id=name)

    if expected is None:
        scgserver.expect_no_error(mark)
        assert scgserver.wait_for_log(f"test scheduled: {name}")
        return

    message, _ = scgserver.expect_error(mark)
    assert expected in message, message


@pytest.mark.parametrize("name,options,asked,expected", SERVED, ids=[c[0] for c in SERVED])
def test_through_lint_served(build, http_mock, tsd_path, name, options, asked, expected):
    mock = http_mock(directory=tsd_path.parent, **options)
    result = build.run("scglint", ["tsd", CONFIG, mock.url(asked)])
    output = result.stdout + result.stderr

    if expected is None:
        assert result.returncode == 0, output
        return
    assert result.returncode == 2, output
    assert expected in output, output


@pytest.mark.parametrize("name,kind,expected", LOCAL, ids=[c[0] for c in LOCAL])
def test_through_lint_local(build, tsd_path, name, kind, expected):
    result = build.run("scglint", ["tsd", CONFIG, local_url(kind, tsd_path)])
    output = result.stdout + result.stderr

    if expected is None:
        assert result.returncode == 0, output
        return
    assert result.returncode == 2, output
    assert expected in output, output


# a machine refuses a connection to a closed port of its own accord, and on windows that
# answer takes about two seconds to arrive. the refusal tests set the timeout well above it
#
# the timeout tests connect to a listener whose accept queue is full. linux drops the syn
# then, so the connect hangs until the timeout. windows refuses it, which again takes about
# two seconds, well above the timeout


@pytest.fixture
def full_backlog_url():
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(0)
    filler = socket.create_connection(listener.getsockname(), timeout=2.0)
    host, port = listener.getsockname()
    yield f"http://{host}:{port}/tsd.json"
    filler.close()
    listener.close()


def test_a_closed_port_is_refused(server, tsd_path):
    """Six seconds to wait, and the refusal arrives long before that."""
    instance = server(fetch_timeout_s=6.0)
    mark = instance.setup(closed_port_url(), test_id="refused")
    message, seconds = instance.expect_error(mark, timeout=12.0)
    assert "the connection was refused" in message, message
    assert seconds < 6.0, f"the refusal took {seconds:.2f} s"


def test_a_connect_can_run_out_of_time(server, full_backlog_url):
    """A fifth of a second to wait, and the listener never answers."""
    instance = server(fetch_timeout_s=0.2)
    mark = instance.setup(full_backlog_url, test_id="tooslow")
    message, _ = instance.expect_error(mark)
    assert "timed out connecting to" in message, message


def test_through_lint_a_closed_port_is_refused(build, tmp_path):
    result = build.run("scglint", ["tsd", config_with_timeout(tmp_path, 6.0), closed_port_url()])
    output = result.stdout + result.stderr
    assert result.returncode == 2, output
    assert "the connection was refused" in output, output


def test_through_lint_a_connect_can_run_out_of_time(build, tmp_path, full_backlog_url):
    start = time.monotonic()
    result = build.run("scglint", ["tsd", config_with_timeout(tmp_path, 0.2), full_backlog_url])
    took = time.monotonic() - start
    output = result.stdout + result.stderr

    assert result.returncode == 2, output
    assert "timed out connecting to" in output, output
    assert took < 10.0, f"the call took {took:.2f} s"


# the second read, the one a step asks for. the tsd itself comes through every time
# here, so what is left is how the failure is reported and which step it names


def test_a_step_naming_a_file_that_is_not_there_is_reported(scgserver, tmp_path):
    tsd = write_tsd(tmp_path / "gap-tsd.json", [(0.5, (tmp_path / "nope.json").as_uri())])
    mark = scgserver.setup(tsd.as_uri(), test_id="nofile")
    message, _ = scgserver.expect_error(mark)
    assert "step[0]" in message, message
    assert "failed to open file" in message, message


def test_a_step_naming_something_that_is_not_a_rid_is_reported(scgserver, tmp_path):
    other = tmp_path / "other.json"
    other.write_text('{ "hello": 1 }', encoding="utf-8")
    tsd = write_tsd(tmp_path / "other-tsd.json", [(0.5, other.as_uri())])
    mark = scgserver.setup(tsd.as_uri(), test_id="notarid")
    message, _ = scgserver.expect_error(mark)
    assert "step[0]" in message, message
    assert "failed to parse RID" in message, message


def test_a_step_served_badly_is_reported(scgserver, http_mock, rid_path, tmp_path):
    mock = http_mock(directory=rid_path.parent, mode="status", code=500)
    tsd = write_tsd(tmp_path / "served-tsd.json", [(0.5, mock.url("rid.json"))])
    mark = scgserver.setup(tsd.as_uri(), test_id="badserve")
    message, _ = scgserver.expect_error(mark)
    assert "step[0]" in message, message
    assert "500 Internal Server Error" in message, message


def test_the_second_step_is_the_one_named(scgserver, rid_path, tmp_path):
    """the first step is fine, so the index in the message is not always zero"""
    tsd = write_tsd(
        tmp_path / "late-tsd.json",
        [(0.5, rid_path.as_uri()), (0.5, (tmp_path / "nope.json").as_uri())],
    )
    mark = scgserver.setup(tsd.as_uri(), test_id="latestep")
    message, _ = scgserver.expect_error(mark)
    assert "step[1]" in message, message


def test_a_url_two_steps_name_is_read_once(scgserver, http_mock, rid_path, tmp_path):
    mock = http_mock(directory=rid_path.parent, mode="serve")
    url = mock.url("rid.json")
    tsd = write_tsd(tmp_path / "twice-tsd.json", [(0.5, url), (0.5, url)])
    mark = scgserver.setup(tsd.as_uri(), test_id="readonce")
    scgserver.expect_no_error(mark)
    assert scgserver.wait_for_log("test scheduled: readonce")
    assert scgserver.log.count("reading the RID of step") == 1, scgserver.log


def test_a_document_that_is_not_a_tsd_is_reported(scgserver, tmp_path):
    other = tmp_path / "other.json"
    other.write_text('{ "hello": 1 }', encoding="utf-8")
    mark = scgserver.setup(other.as_uri(), test_id="nottsd")
    message, _ = scgserver.expect_error(mark)
    assert "failed to parse TSD" in message, message


def test_a_rid_in_the_place_of_a_tsd_is_reported(scgserver, rid_path):
    """they share no key, so a rid cannot pass for a tsd of one step"""
    mark = scgserver.setup(rid_path.as_uri(), test_id="ridastsd")
    message, _ = scgserver.expect_error(mark)
    assert "failed to parse TSD" in message, message
