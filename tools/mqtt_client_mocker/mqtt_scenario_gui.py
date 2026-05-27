import json
import time
import uuid
from datetime import datetime, timezone
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
    client.connect(host, int(port), keepalive=60)
    client.publish(topic, json.dumps(payload), qos=1)
    client.disconnect()


st.set_page_config(page_title="MQTT Scenario Mocker", layout="wide")
st.title("MQTT Scenario Mocker")

try:
    schema = load_schema()
    st.success("Schema loaded OK")
except Exception as e:
    st.error(f"Schema load error: {e}")
    st.stop()


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

    topic = st.text_input(
        "Publish topic",
        "project/device/mock-client/command",
    )


st.header("Metadata")

col1, col2 = st.columns(2)

with col1:
    version = st.text_input("version", "1.0")
    scenario_name = st.text_input("scenario_name", "test_scenario")
    author = st.text_input("author", "mock_gui")

with col2:
    description = st.text_area("description", "Generated from MQTT Scenario Mocker")
    created_utc = st.text_input(
        "created_utc",
        datetime.now(timezone.utc).isoformat(),
    )


st.header("Global settings")

duration_s = st.number_input(
    "duration_s",
    min_value=0.001,
    value=10.0,
    step=0.1,
)


st.header("Radios")

num_radios = st.number_input(
    "Number of radios",
    min_value=1,
    max_value=4,
    value=1,
    step=1,
)

radios = []

for i in range(num_radios):
    with st.expander(f"Radio #{i + 1}", expanded=True):
        col1, col2, col3 = st.columns(3)

        with col1:
            radio_id = st.text_input(
                "radio_id",
                f"radio_{i + 1}",
                key=f"radio_id_{i}",
            )

            radio_type = st.selectbox(
                "type",
                ["transmitter", "receiver"],
                key=f"radio_type_{i}",
            )

        with col2:
            center_frequency_hz = st.number_input(
                "center_frequency_hz",
                min_value=100.0,
                max_value=6_000_000_000.0,
                value=1_575_420_000.0,
                step=1_000.0,
                key=f"radio_cf_{i}",
            )

            sample_rate_hz = st.number_input(
                "sample_rate_hz",
                min_value=1_000.0,
                max_value=50_000_000.0,
                value=10_000_000.0,
                step=1_000.0,
                key=f"radio_sr_{i}",
            )

        with col3:
            gain_db = st.number_input(
                "gain_db",
                min_value=-100.0,
                max_value=100.0,
                value=0.0,
                step=1.0,
                key=f"radio_gain_{i}",
            )

        radios.append(
            {
                "radio_id": radio_id,
                "type": radio_type,
                "center_frequency_hz": center_frequency_hz,
                "sample_rate_hz": sample_rate_hz,
                "gain_db": gain_db,
            }
        )


st.header("Interference sources")

radio_ids = [r["radio_id"] for r in radios]

num_sources = st.number_input(
    "Number of interference sources",
    min_value=1,
    max_value=20,
    value=1,
    step=1,
)

interference_sources = []

for i in range(num_sources):
    with st.expander(f"Interference source #{i + 1}", expanded=True):
        col1, col2, col3 = st.columns(3)

        with col1:
            source_radio_id = st.selectbox(
                "radio_id",
                radio_ids,
                key=f"source_radio_id_{i}",
            )

            source_type = st.selectbox(
                "type",
                [
                    "cw_jammer",
                    "noise_jammer",
                    "chirp_jammer",
                    "gnss_spoofer",
                ],
                key=f"source_type_{i}",
            )

            power_dbm = st.number_input(
                "power_dbm",
                min_value=-150.0,
                max_value=60.0,
                value=-40.0,
                step=1.0,
                key=f"power_dbm_{i}",
            )

        with col2:
            start_time_s = st.number_input(
                "start_time_s",
                min_value=0.0,
                value=0.0,
                step=0.1,
                key=f"start_time_{i}",
            )

            end_time_s = st.number_input(
                "end_time_s",
                min_value=0.0,
                value=10.0,
                step=0.1,
                key=f"end_time_{i}",
            )

            source_center_frequency_hz = st.number_input(
                "center_frequency_hz",
                min_value=-100_000_000.0,
                max_value=100_000_000.0,
                value=0.0,
                step=1_000.0,
                key=f"source_cf_{i}",
            )

        with col3:
            modulation = st.selectbox(
                "modulation",
                ["none", "fm", "am"],
                key=f"modulation_{i}",
            )

        source = {
            "radio_id": source_radio_id,
            "type": source_type,
            "power_dbm": power_dbm,
            "start_time_s": start_time_s,
            "end_time_s": end_time_s,
            "center_frequency_hz": source_center_frequency_hz,
            "modulation": modulation,
        }

        parameters = {}

        if source_type in ["noise_jammer", "chirp_jammer"]:
            st.subheader("Jammer parameters")

            parameters["span_hz"] = st.number_input(
                "parameters.span_hz",
                min_value=1.0,
                max_value=56_000_000.0,
                value=1_000_000.0,
                step=1_000.0,
                key=f"span_hz_{i}",
            )

        if source_type == "chirp_jammer":
            parameters["sweep_time_s"] = st.number_input(
                "parameters.sweep_time_s",
                min_value=0.0001,
                value=1.0,
                step=0.1,
                key=f"sweep_time_s_{i}",
            )

        if source_type == "gnss_spoofer":
            st.subheader("GNSS spoofing parameters")

            col_a, col_b = st.columns(2)

            with col_a:
                parameters["constellation"] = st.selectbox(
                    "parameters.constellation",
                    ["GPS", "GLONASS", "GALILEO", "BEIDOU"],
                    key=f"constellation_{i}",
                )

                prn_text = st.text_input(
                    "parameters.prn_list",
                    "1,2,3,4",
                    key=f"prn_list_{i}",
                )

                parameters["prn_list"] = [
                    int(x.strip())
                    for x in prn_text.split(",")
                    if x.strip()
                ]

            with col_b:
                trajectory_count = st.number_input(
                    "Trajectory points",
                    min_value=1,
                    max_value=20,
                    value=1,
                    step=1,
                    key=f"trajectory_count_{i}",
                )

            trajectory = []

            for j in range(trajectory_count):
                st.markdown(f"Trajectory point #{j + 1}")

                t_col, x_col, y_col, z_col = st.columns(4)

                with t_col:
                    t = st.number_input(
                        "t",
                        min_value=0.0,
                        value=float(j),
                        step=0.1,
                        key=f"traj_t_{i}_{j}",
                    )

                with x_col:
                    x = st.number_input(
                        "x [m]",
                        value=0.0,
                        step=1.0,
                        key=f"traj_x_{i}_{j}",
                    )

                with y_col:
                    y = st.number_input(
                        "y [m]",
                        value=0.0,
                        step=1.0,
                        key=f"traj_y_{i}_{j}",
                    )

                with z_col:
                    z = st.number_input(
                        "z [m]",
                        value=0.0,
                        step=1.0,
                        key=f"traj_z_{i}_{j}",
                    )

                trajectory.append(
                    {
                        "t": t,
                        "position_m": [x, y, z],
                    }
                )

            parameters["trajectory"] = trajectory

        if parameters:
            source["parameters"] = parameters

        interference_sources.append(source)


scenario = {
    "metadata": {
        "version": version,
        "scenario_name": scenario_name,
        "description": description,
        "created_utc": created_utc,
        "author": author,
    },
    "global_settings": {
        "duration_s": duration_s,
    },
    "radios": radios,
    "interference_sources": interference_sources,
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