# MQTT CLIENT MOCKER
Létrehozok egy python mqtt klienst, ami tudja küldeni és fogadni az üzeneteket amik a dokumentációban le lettek fixálva.
Ezzel könnyebb lesz később tesztelni a scenario generatorban létrehozott mqtt kliens működését.

Paho-mqtt-t használok pythonban.  
# deps
~~~bash
pip install streamlit
pip install paho-mqtt
pip install jsonschema
~~~

## Run mosquitto broker
~~~bash
cd mosquitto-broker
docker compose up
~~~

## Run mqtt mocker
~~~bash
cd mqtt_client_mocker
streamlit run mqtt_scenario_gui.py --server.headless true --browser.gatherUsageStats false
~~~



