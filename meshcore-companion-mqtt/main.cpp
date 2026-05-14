/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

CONFIG config;
MESHCORE_STATE state;
DSL_Mutex hMutex;

bool LoadConfig() {
	if (access("meshcore-companion-mqtt.conf", 0) != 0) {
		printf("Warning: No meshcore-companion-mqtt.conf found, using defaults...\n");
		return true;
	}

	ConfigSection root;
	if (!root.LoadFromFile("meshcore-companion-mqtt.conf")) {
		printf("Error parsing meshcore-irc.conf!\n");
		return false;
	}

	auto base = root.GetSection("MeshCore");
	if (base != NULL) {
		auto v = base->GetValue("Device");
		if (v) config.meshcore.device = v->AsString();
	}

	auto mqtt = root.GetSection("MQTT");
	if (mqtt != NULL) {
		auto v = mqtt->GetValue("Host");
		if (v) config.mqtt.host = v->AsString();
		v = mqtt->GetValue("Port");
		if (v) config.mqtt.port = (uint16)v->AsInt();
		v = mqtt->GetValue("Username");
		if (v) config.mqtt.username = v->AsString();
		v = mqtt->GetValue("Password");
		if (v) config.mqtt.password = v->AsString();
		v = mqtt->GetValue("TopicPrefix");
		if (v) config.mqtt.topic_prefix = v->AsString();
		v = mqtt->GetValue("LogToConsole");
		if (v) config.mqtt.log_to_console = v->AsBool();
		v = mqtt->GetValue("LogToFile");
		if (v) config.mqtt.log_to_file = v->AsBool();
	}

	return true;
}

void do_shutdown() {
	state.chans.clear();
	state.users.clear();

	mosquitto_disconnect();

	if (config.mqtt.log_fp != NULL) {
		fclose(config.mqtt.log_fp);
		config.mqtt.log_fp = NULL;
	}

	if (config.io_driver) {
		printf("Closing I/O driver...\n");
		config.io_driver.reset();
	}

	mosquitto_lib_cleanup();

	if (config.recvbuf.data) {
		buffer_free(&config.recvbuf);
	}
	if (config.sendbuf.data) {
		buffer_free(&config.sendbuf);
	}

	printf("Goodbye.\n");
}

#if defined(WIN32)
/**
 * Handles Ctrl+C, hitting the Close button, etc., on Windows
 */
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType) {
	//dwCtrlType == CTRL_CLOSE_EVENT ||
	if (dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
		if (!config.shutdown_now) {
			if (dwCtrlType != CTRL_CLOSE_EVENT) {
				printf("Caught Ctrl+C (or similar), setting shutdown_now=true\n");
			}
			config.shutdown_now = true;
		}
		return TRUE;
	}
	return TRUE;
}
#else
/**
 * Catches Ctrl+C, etc. on Linux/Unix
 */
static void catch_sigint(int signo) {
	if (!config.shutdown_now) {
		printf("Caught SIGINT, setting shutdown_now=true\n");
		config.shutdown_now = true;
	}
}
#endif

int main(int argc, const char* argv[]) {
	printf("Starting up meshcore-companion-mqtt v" MESHCORE_COMPANION_MQTT_VERSION " ...\n\n");
	dsl_init();
	atexit(dsl_cleanup);

	if (!LoadConfig()) {
		exit(1);
	}

	config.io_driver = make_shared<IO_Driver_Serial>();
	/*
	if (!config.io_driver->Open(config.device)) {
		exit(1);
	}
	*/

	if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
		printf("Error initializing libmosquitto!\n");
		exit(1);
	}
	atexit(do_shutdown);

	buffer_init(&config.recvbuf);
	buffer_init(&config.sendbuf);

#if defined(WIN32)
	SetConsoleTitle("meshcore-companion-mqtt/" PLATFORM "");
	SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);
	SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
	struct sigaction sa_old;
	struct sigaction sa_new;

	// set up signal handling
	sa_new.sa_handler = catch_sigint;
	sigemptyset(&sa_new.sa_mask);
	sa_new.sa_flags = 0;
	sigaction(SIGINT, &sa_new, &sa_old);
#endif

	int64 lastContactsMaintenance = time(NULL);

	while (!config.shutdown_now) {
		io_work();
		handle_incoming_commands();
		mosquitto_work();

		if (time(NULL) - lastContactsMaintenance >= 3600) {
			// just do this hourly
			clear_old_contacts();
			lastContactsMaintenance = time(NULL);
		}
	}

	exit(0);
}
