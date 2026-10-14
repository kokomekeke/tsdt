# TOOLS

The programs here are not built with the project, they are the things used to run it
and check it. One of them lives on the share rather than in the repository.

## rsg-harness

The conformance harness for the SISI interface, a binary with the json schemas it checks
against. It is on the share:

~~~
largeshare/temp/sisi/rsg-harness
~~~

It used to be committed under `assets/rsg-harness` and was removed from the history, so a
clone does not have it.

## mosquitto broker

A broker in a container, for running `scgserver` and `scgcommander` against something.

~~~bash
cd mosquitto-broker
docker compose up
~~~

Anonymous, on 1883, no TLS. `mosquitto_pub -t test/topic -m "hello world"` is enough to
see that it is up.

## mqtt client mocker

`mqtt_client_mocker` is a python client that sends and reads the messages of the
protocol, with a streamlit interface. It was written to try the message flow without the
server. [Its own README](mqtt_client_mocker/README.md) has the details.

## linux build container

`docker` holds the Dockerfile and the compose file for a container that builds the
project on linux, for when the host does not have the dependencies.
