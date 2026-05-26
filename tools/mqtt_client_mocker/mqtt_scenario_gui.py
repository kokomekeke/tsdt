import json
import time
import uuid
from pathlib import Path

import streamlit as st
import paho.mqtt.client as mqtt
from jsonschema import validate, ValidationError


SCHEMA_PATH = Path("schemas/radio_interference.schema.json")


def load_schema():
    with open(SCHEMA_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def publish_mqtt(host, port, topic, payload):
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(host, port, keepalive=60)
    client.publish(topic, json.dumps(payload), qos=1)
    client.disconnect()


st.set_page_config(page_title="MQTT Scenario Mocker", layout="wide")

st.title("MQTT Scenario Mocker")
st.write("APP STARTED")

schema = load_schema()

st.write("BEFORE SIDEBAR")
with st.sidebar:
    st.header("MQTT settings")
    broker_host = st.text_input("Broker host", "127.0.0.1")
    broker_port = st.number_input(
        "Broker port",
        min_value=1,
        max_value=65535,
        value=1883,
        step=1,
    )
    topic = st.text_input("Publish topic", "project/device/mock-client/command")
st.write("AFTER SIDEBAR")

st.write("BEFORE GLOBAL SETTINGS")
st.header("Global settings")

center_frequency_hz = st.number_input(
    "center_frequency_hz",
    min_value=100.0,
    max_value=6_000_000_000.0,
    value=1_575_420_000.0,
    step=1_000.0,
)
st.write("AFTER GLOBAL SETTINGS")

st.header("Interference sources")

num_sources = st.number_input(
    "Number of interference sources",
    min_value=1,
    max_value=20,
    value=1,
    step=1,
)

st.write("BEFORE SOURCES")
sources = []

for i in range(num_sources):
    with st.expander(f"Interference source #{i + 1}", expanded=True):
        col1, col2 = st.columns(2)

        with col1:
            source_type = st.selectbox(
                "type",
                [
                    "cw_jammer",
                    "noise_jammer",
                    "chirp_jammer",
                    "gnss_spoofer",
                ],
                key=f"type_{i}",
            )

            power_dbm = st.number_input(
                "power_dbm",
                min_value=-150.0,
                max_value=60.0,
                value=-40.0,
                step=1.0,
                key=f"power_{i}",
            )

            start_time_s = st.number_input(
                "start_time_s",
                min_value=0.0,
                value=0.0,
                step=0.1,
                key=f"start_{i}",
            )

            end_time_s = st.number_input(
                "end_time_s",
                min_value=0.0,
                value=10.0,
                step=0.1,
                key=f"end_{i}",
            )

        with col2:
            frequency_offset_hz = st.number_input(
                "frequency_offset_hz",
                min_value=-100_000_000.0,
                max_value=100_000_000.0,
                value=0.0,
                step=1_000.0,
                key=f"freq_offset_{i}",
            )

            bandwidth_hz = st.number_input(
                "bandwidth_hz",
                min_value=1.0,
                max_value=100_000_000.0,
                value=1_000_000.0,
                step=1_000.0,
                key=f"bandwidth_{i}",
            )

            modulation = st.selectbox(
                "modulation",
                [
                    "none",
                    "bpsk",
                    "qpsk",
                    "noise",
                    "gnss",
                ],
                key=f"modulation_{i}",
            )

        source = {
            "type": source_type,
            "power_dbm": power_dbm,
            "start_time_s": start_time_s,
            "end_time_s": end_time_s,
            "frequency_offset_hz": frequency_offset_hz,
            "bandwidth_hz": bandwidth_hz,
            "modulation": modulation,
        }

        if source_type == "chirp_jammer":
            st.subheader("Chirp settings")

            chirp_col1, chirp_col2, chirp_col3 = st.columns(3)

            with chirp_col1:
                start_offset_hz = st.number_input(
                    "chirp.start_offset_hz",
                    value=-500_000.0,
                    step=1_000.0,
                    key=f"chirp_start_{i}",
                )

            with chirp_col2:
                end_offset_hz = st.number_input(
                    "chirp.end_offset_hz",
                    value=500_000.0,
                    step=1_000.0,
                    key=f"chirp_end_{i}",
                )

            with chirp_col3:
                sweep_time_s = st.number_input(
                    "chirp.sweep_time_s",
                    min_value=0.0001,
                    value=1.0,
                    step=0.1,
                    key=f"chirp_sweep_{i}",
                )

            source["chirp"] = {
                "start_offset_hz": start_offset_hz,
                "end_offset_hz": end_offset_hz,
                "sweep_time_s": sweep_time_s,
            }

        if source_type == "gnss_spoofer":
            st.subheader("Spoofing settings")

            spoof_col1, spoof_col2 = st.columns(2)

            with spoof_col1:
                constellation = st.selectbox(
                    "spoofing.constellation",
                    [
                        "GPS",
                        "GLONASS",
                        "GALILEO",
                        "BEIDOU",
                    ],
                    key=f"constellation_{i}",
                )

                prn_text = st.text_input(
                    "spoofing.prn_list comma separated",
                    "1,2,3,4",
                    key=f"prn_{i}",
                )

            with spoof_col2:
                pos_x = st.number_input("position_offset_m[0]", value=0.0, key=f"pos_x_{i}")
                pos_y = st.number_input("position_offset_m[1]", value=0.0, key=f"pos_y_{i}")
                pos_z = st.number_input("position_offset_m[2]", value=0.0, key=f"pos_z_{i}")

            prn_list = [
                int(x.strip())
                for x in prn_text.split(",")
                if x.strip()
            ]

            source["spoofing"] = {
                "constellation": constellation,
                "prn_list": prn_list,
                "position_offset_m": [pos_x, pos_y, pos_z],
            }

        sources.append(source)
st.write("AFTER SOURCES")

scenario = {
    "global_settings": {
        "center_frequency_hz": center_frequency_hz
    },
    "interference_sources": sources,
}

command_envelope = {
    "command_id": str(uuid.uuid4()),
    "timestamp": time.time(),
    "command": "load_interference_description",
    "payload": scenario,
}

st.header("Generated scenario JSON")
st.json(scenario)

try:
    validate(instance=scenario, schema=schema)
    st.success("Scenario is valid according to schema.")
    valid = True
except ValidationError as e:
    st.error(f"Schema validation error: {e.message}")
    st.write("Path:", list(e.path))
    valid = False

col_a, col_b = st.columns(2)

with col_a:
    if st.button("Publish scenario to MQTT", disabled=not valid):
        publish_mqtt(
            broker_host,
            int(broker_port),
            topic,
            command_envelope,
        )
        st.success(f"Published to topic: {topic}")

with col_b:
    st.download_button(
        "Download scenario JSON",
        data=json.dumps(scenario, indent=2),
        file_name="scenario.json",
        mime="application/json",
    )