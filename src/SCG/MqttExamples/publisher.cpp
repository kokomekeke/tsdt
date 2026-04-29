#include <mosquitto.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
    volatile std::sig_atomic_t g_running = 1;

    void handle_signal(int)
    {
        g_running = 0;
    }
}

int main()
{
	std::signal(SIGINT, handle_signal);

	mosquitto_lib_init();

	mosquitto *mosq = mosquitto_new("publisher1", true, nullptr);
	if (!mosq) {
		std::fprintf(stderr, "Failed to create Mosquitto client.\n");
		mosquitto_lib_cleanup();
		return EXIT_FAILURE;
	}

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

	std::cout << "Connected to localhost:1883\n";
	std::cout << "Type a line and press Enter to publish it to test/topic.\n";
	std::cout << "Press Ctrl+C to exit.\n";

	std::string message;
	while (g_running) {
		std::cout << "> " << std::flush;
		if (!std::getline(std::cin, message)) {
			break;
		}

		const int publish_result = mosquitto_publish(
			mosq,
			nullptr,
			"test/topic",
			static_cast<int>(message.size()),
			message.c_str(),
			0,
			false);

		if (publish_result != MOSQ_ERR_SUCCESS) {
			std::fprintf(stderr, "Publish failed: %s\n", mosquitto_strerror(publish_result));
			continue;
		}
	}

	mosquitto_loop_stop(mosq, true);
	mosquitto_disconnect(mosq);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	return EXIT_SUCCESS;
}
