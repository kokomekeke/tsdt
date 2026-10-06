"""The line the mqtt client writes for every message that crosses the connection.

Both programs turn it on, so the log of a server holds one line per message in each
direction, with the topic, the quality of service, the retain flag and the payload. A
payload longer than 128 bytes is named by its size instead, which keeps a line readable
when a whole rid error goes out on the error topic.

Nothing else logs a payload any more. The server says what a command turned out to be
and leaves the bytes to these lines.
"""

import re

# a setup command carries two urls and a time, so it is well past the cap. a command with
# no arguments is around forty bytes and comes through whole
LONG_PAYLOAD = re.compile(r"mqtt in \[[^\]]+\] qos \d+ retain \d+: \[\d+ bytes\]")


def test_an_incoming_message_is_logged(scgserver):
    mark = scgserver.int_stop()
    scgserver.expect_error(mark)
    assert "mqtt in [" in scgserver.log, scgserver.log


def test_an_outgoing_message_is_logged(scgserver):
    mark = scgserver.int_stop()
    scgserver.expect_error(mark)
    assert "mqtt out [" in scgserver.log, scgserver.log


def test_a_short_payload_is_logged_whole(scgserver):
    """the json the test suite writes keeps a space after each colon"""
    mark = scgserver.int_stop()
    scgserver.expect_error(mark)
    assert '"command": "INT_STOP"' in scgserver.log, scgserver.log


def test_a_long_payload_is_named_by_its_size(scgserver, tsd_path):
    mark = scgserver.setup(tsd_path.as_uri(), test_id="traced", seconds_ahead=3600)
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.wait_for_log("test scheduled: traced")
    assert LONG_PAYLOAD.search(scgserver.log), scgserver.log


def test_the_topic_and_the_quality_of_service_are_in_the_line(scgserver):
    """the server publishes at qos 2 and reads the command topic at the qos it was sent"""
    mark = scgserver.int_stop()
    scgserver.expect_error(mark)
    assert re.search(
        r"mqtt out \[" + re.escape(scgserver.error_log_topic) + r"\] qos 2 retain 0: ",
        scgserver.log,
    ), scgserver.log


def test_the_command_is_logged_by_name_without_its_payload(scgserver, tsd_path):
    """what replaced the per command lines that used to print the fields"""
    mark = scgserver.setup(tsd_path.as_uri(), test_id="named", seconds_ahead=3600)
    scgserver.expect_no_error(mark, timeout=2.0)
    assert scgserver.wait_for_log("as TEST_SETUP"), scgserver.log
    assert "scenario_url=" not in scgserver.log, scgserver.log
