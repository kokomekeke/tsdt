#include <mosquitto.h>

#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
volatile std::sig_atomic_t g_running = 1;

void handle_signal(int)
{
    g_running = 0;
}

void on_message(struct mosquitto *, void *, const struct mosquitto_message *msg)
{
    std::printf("Received: %.*s\n", msg->payloadlen, static_cast<const char *>(msg->payload));
}
}

int main()
{
    std::signal(SIGINT, handle_signal);

    mosquitto_lib_init();

    mosquitto *mosq = mosquitto_new("subscriber1", true, nullptr);
    if (!mosq) {
        std::fprintf(stderr, "Failed to create Mosquitto client.\n");
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    mosquitto_message_callback_set(mosq, on_message);

    if (mosquitto_connect(mosq, "localhost", 1883, 60) != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "Failed to connect to localhost:1883.\n");
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "Failed to start Mosquitto network loop.\n");
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    const int subscribe_result = mosquitto_subscribe(mosq, nullptr, "test/topic", 0);
    if (subscribe_result != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "Subscribe failed: %s\n", mosquitto_strerror(subscribe_result));
        mosquitto_loop_stop(mosq, true);
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return EXIT_FAILURE;
    }

    std::cout << "Connected to localhost:1883\n";
    std::cout << "Subscribed to test/topic\n";
    std::cout << "Press Ctrl+C to exit.\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    mosquitto_loop_stop(mosq, true);
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return EXIT_SUCCESS;
}