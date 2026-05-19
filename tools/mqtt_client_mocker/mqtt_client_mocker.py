import json
import time
import paho.mqtt.client as mqtt

BROKER = "127.0.0.1"
PORT = 1883

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

def on_message(client, userdata, msg):
    print("RX:", msg.topic, msg.payload.decode())

    if msg.topic.endswith("/command"):
        response = {
            "status": "ok",
            "message": "Mock command accepted"
        }

        response_topic = msg.topic.replace("/command", "/response")
        client.publish(response_topic, json.dumps(response), qos=1)

client.on_message = on_message
client.connect(BROKER, PORT)
client.subscribe("project/device/+/command", qos=1)

client.loop_start()

while True:
    event = {
        "type": "heartbeat",
        "timestamp": time.time(),
        "source": "mock-broker-side"
    }

    client.publish("project/system/heartbeat", json.dumps(event), qos=1)
    time.sleep(5)
