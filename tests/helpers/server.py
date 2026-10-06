"""Runs scgserver against the broker in the test process.

Each instance writes its own config, so the recordings land in the temporary directory
of the test and never in the repository, and gets command and error topics of its own,
so nothing a previous test left behind can be mistaken for its own answer.
"""

import datetime
import itertools
import json
import time

from .binaries import ROOT

TEMPLATE = ROOT / "tests" / "data" / "config.json"

_counter = itertools.count()


def utc_string(seconds_ahead):
    """The time format the server reads, whole seconds and a Z."""
    now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    return (now + datetime.timedelta(seconds=seconds_ahead)).strftime("%Y-%m-%dT%H:%M:%SZ")


def setup_command(scenario, test_id, seconds_ahead):
    return json.dumps({
        "command": "TEST_SETUP",
        "extras": {
            "scenario_url": scenario,
            "start_time": utc_string(seconds_ahead),
            "test_id": test_id,
        },
    })


def cancel_command(test_id):
    return json.dumps({
        "command": "TEST_CANCEL",
        "extras": {"test_id": test_id},
    })


def bare_command(name):
    """A command with no arguments. extras is a field of every one of them."""
    return json.dumps({
        "command": name,
        "extras": {},
    })


def int_stop_command():
    return bare_command("INT_STOP")


class ScgServer:

    def __init__(self, build, broker, work_dir, fetch_timeout_s=2.0,
                 start_ahead_s=2.0, status_period_s=60.0, config_changes=None):
        self.build = build
        self.broker = broker
        self.work_dir = work_dir
        index = next(_counter)
        self.command_topic = f"cmd/test{index}"
        self.error_log_topic = f"tm/test{index}"
        self.test_log_topic = f"tm/test{index}/test"

        config = json.loads(TEMPLATE.read_text(encoding="utf-8"))
        config["recording_directory"] = str(work_dir / "recordings").replace("\\", "/")
        config["scenario_fetch_timeout_s"] = fetch_timeout_s
        config["start_ahead_s"] = start_ahead_s
        config["status_update_period_s"] = status_period_s
        config["mqtt"]["broker_host"] = broker.host
        config["mqtt"]["broker_port"] = broker.port
        config["mqtt"]["command_topic"] = self.command_topic
        config["mqtt"]["signal_log_topic"] = self.error_log_topic
        config["mqtt"]["test_log_topic"] = self.test_log_topic
        config["mqtt"]["error_log_topic"] = self.error_log_topic
        if config_changes:
            config_changes(config)

        self.config_path = work_dir / f"config{index}.json"
        self.config_path.write_text(json.dumps(config, indent=4), encoding="utf-8")
        self.log_path = work_dir / f"scgserver{index}.log"
        self.log_file = None
        self.process = None

    def start(self, timeout=30.0):
        self.log_file = open(self.log_path, "w", encoding="utf-8", errors="replace")
        # scgserver opens scgserver.log in the working directory, so it runs in the
        # directory of the test instead of the one the repository sits in
        self.process = self.build.popen(
            "scgserver", [self.config_path], stdout=self.log_file, cwd=self.work_dir
        )
        if not self.wait_for_log("subscribed to", timeout):
            raise RuntimeError(f"scgserver did not subscribe:\n{self.log}")
        return self

    def stop(self):
        if self.process is not None:
            self.process.kill()
            self.process.wait(timeout=30)
            self.process = None
        if self.log_file is not None:
            self.log_file.close()
            self.log_file = None

    @property
    def log(self):
        if self.log_file is not None:
            self.log_file.flush()
        if not self.log_path.is_file():
            return ""
        return self.log_path.read_text(encoding="utf-8", errors="replace")

    def wait_for_log(self, text, timeout=10.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if text in self.log:
                return True
            if self.process is not None and self.process.poll() is not None:
                return text in self.log
            time.sleep(0.05)
        return False

    def send(self, payload):
        """Publishes a raw command payload. Returns the moment it was sent."""
        mark = self.broker.mark()
        self.broker.publish(self.command_topic, payload)
        return mark

    def setup(self, scenario, test_id="t0", seconds_ahead=3600):
        return self.send(setup_command(scenario, test_id, seconds_ahead))

    def cancel(self, test_id):
        return self.send(cancel_command(test_id))

    def int_stop(self):
        return self.send(int_stop_command())

    def bare(self, name):
        return self.send(bare_command(name))

    def error(self, mark, timeout=6.0):
        return self.broker.wait_for_error(mark, self.error_log_topic, timeout)

    def expect_error(self, mark, timeout=6.0):
        answer = self.broker.wait_for_error(mark, self.error_log_topic, timeout)
        assert answer is not None, f"no error came back in {timeout} s\n{self.log}"
        return answer

    def expect_no_error(self, mark, timeout=2.0):
        answer = self.broker.wait_for_error(mark, self.error_log_topic, timeout)
        assert answer is None, f"unexpected error: {answer[0]}"
