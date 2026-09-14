import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from helpers.binaries import ROOT, Build, BuildError
from helpers.httpmock import HttpMock
from helpers.mqttbroker import Broker
from helpers.rid import write_rid
from helpers.server import ScgServer


def pytest_addoption(parser):
    parser.addoption(
        "--build-dir", action="store", default=None,
        help="the meson build directory that holds scgserver and scglint",
    )


def pytest_configure(config):
    if not config.getoption("--build-dir"):
        raise pytest.UsageError(
            "--build-dir is required, give it the meson build directory, "
            "for example --build-dir=build-msvc"
        )


@pytest.fixture(scope="session")
def repo():
    return ROOT


@pytest.fixture(scope="session")
def build(request):
    try:
        return Build(request.config.getoption("--build-dir"))
    except BuildError as e:
        pytest.exit(str(e), returncode=2)


@pytest.fixture(scope="session")
def broker():
    with Broker() as b:
        yield b


@pytest.fixture
def http_mock():
    """Starts http servers that answer badly, and closes them at the end of the test."""
    started = []

    def make(**options):
        mock = HttpMock(**options).start()
        started.append(mock)
        return mock

    yield make
    for mock in started:
        mock.stop()


@pytest.fixture
def rid_path(tmp_path):
    """A rid short enough that recording it costs nothing."""
    return write_rid(tmp_path / "rid.json", duration_s=0.5)


@pytest.fixture
def rid_duration():
    return 0.5


@pytest.fixture
def server(build, broker, tmp_path):
    """Starts scgserver processes and stops them at the end of the test."""
    started = []

    def make(**options):
        instance = ScgServer(build, broker, tmp_path, **options).start()
        started.append(instance)
        return instance

    yield make
    for instance in started:
        instance.stop()


@pytest.fixture
def scgserver(server):
    return server()
