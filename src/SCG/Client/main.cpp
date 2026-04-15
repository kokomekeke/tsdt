#include <mosquitto.h>
#include <stdio.h>
#include <string.h>

void on_message(struct mosquitto *mosq, void *userdata,
                const struct mosquitto_message *msg)
{
    printf("Received: %s\n", (char*)msg->payload);
}

int main()
{
    mosquitto_lib_init();

    struct mosquitto *mosq = mosquitto_new("client1", true, NULL);

    mosquitto_message_callback_set(mosq, on_message);

    mosquitto_connect(mosq, "localhost", 1883, 60);

    mosquitto_subscribe(mosq, NULL, "test/topic", 0);

    while (1) {
        mosquitto_loop(mosq, -1, 1); // blocking
    }

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}