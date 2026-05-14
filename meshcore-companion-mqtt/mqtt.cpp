/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

void mqtt_log(const char* fmt, ...) {
	if (!config.mqtt.log_to_console && !config.mqtt.log_to_file) {
		return;
	}

	va_list va;
	va_start(va, fmt);

	if (config.mqtt.log_to_console) {
		char* tmp = dsl_vmprintf(fmt, va);
		printf("[mqtt] %s", tmp);
		dsl_free(tmp);
	}

	if (config.mqtt.log_to_file) {
		if (config.mqtt.log_fp == NULL) {
			if (access("./logs", 0) != 0) {
				dsl_mkdir("./logs", 0700);
			}
			string fn = "./logs/companion.mqtt.log";
			config.mqtt.log_fp = fopen(fn.c_str(), "wb");
			if (config.mqtt.log_fp != NULL) {
				printf("[mqtt] Opened %s for output...\n", fn.c_str());
			} else {
				printf("[mqtt] Error opening %s for output: %s\n", fn.c_str(), strerror(errno));
			}
		}
		if (config.mqtt.log_fp != NULL) {
			vfprintf(config.mqtt.log_fp, fmt, va);
		}
	}

	va_end(va);
}

void mosquitto_disconnect() {
	if (config.mqtt.mosq) {
		//if (config.mqtt.connected) {
			mosquitto_disconnect(config.mqtt.mosq);
		//}
		if (config.mqtt.thread_running) {
			mosquitto_loop_stop(config.mqtt.mosq, false);
		}
		mosquitto_destroy(config.mqtt.mosq);
		config.mqtt.mosq = NULL;
		config.mqtt.thread_running = config.mqtt.connected = false;
	}
}

static void on_mqtt_connect(struct mosquitto* mosq, void* userdata, int rc) {
	if (rc != 0) {
		printf("[mqtt] Error connecting to MQTT (rc=%d)\n", rc);
		return;
	}

	printf("[mqtt] Connected to MQTT...\n");

	config.mqtt.connected = true;

	string prefix_commands = config.mqtt.topic_prefix + "/command/#";
	mosquitto_subscribe(mosq, NULL, prefix_commands.c_str(), 0);
}

static void on_mqtt_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
	printf("[mqtt] Disconnected from MQTT broker (rc=%d)\n", rc);
	config.mqtt.connected = false;
}

static void on_mqtt_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg) {
	if (!msg->payload || msg->payloadlen <= 0) { return; }

	static const string prefix_commands = config.mqtt.topic_prefix + "/command/";
	/*
	static const string prefix_advertisement = config.mqtt.topic_prefix + "/advertisement";
	static const string prefix_self = config.mqtt.topic_prefix + "/self_info";
	static const string prefix_chan_info = config.mqtt.topic_prefix + "/channel_info";
	static const string prefix_chan = config.mqtt.topic_prefix + "/message/channel/";
	static const string prefix_dir = config.mqtt.topic_prefix + "/message/direct/";
	static const string prefix_contacts = config.mqtt.topic_prefix + "/contacts";
	static const string prefix_new_contact = config.mqtt.topic_prefix + "/new_contact";
	*/

	const char* topic = msg->topic;

	if (config.mqtt.log_fp || config.mqtt.log_to_console) {
		string raw((const char*)msg->payload, msg->payloadlen);
		mqtt_log("<- %s: %s\n", topic, raw.c_str());
	}

	if (!strnicmp(topic, prefix_commands.c_str(), prefix_commands.length()) && strlen(topic) > prefix_commands.length()) {
		UniValue parms;
		if (!parms.read((const char*)msg->payload, msg->payloadlen)) {
			string raw((const char*)msg->payload, msg->payloadlen);
			printf("[mqtt] Failed to parse JSON on for command %s: %s\n", topic, raw.c_str());
			return;
		}

		shared_ptr<MQTT_Command> cmd = make_shared<MQTT_Command>();
		cmd->cmd = topic + prefix_commands.length();
		cmd->parms = std::move(parms);

		AutoMutex(hMutex);
		config.incoming_commands.push(cmd);
	}
}

void mosquitto_work() {
	static time_t nextConnectAttempt = 0;

	if (config.mqtt.mosq == NULL && time(NULL) >= nextConnectAttempt) {
		char client_id[64];
		snprintf(client_id, sizeof(client_id), "meshmqtt-%d", (int)time(NULL));

		struct mosquitto* mosq = mosquitto_new(client_id, true, NULL);
		if (mosq == NULL) {
			printf("[mqtt] Failed to create mosquitto client\n");
			return;
		}

		nextConnectAttempt = time(NULL) + 30;
		mosquitto_reconnect_delay_set(mosq, 1, 30, true);
		mosquitto_connect_callback_set(mosq, on_mqtt_connect);
		mosquitto_disconnect_callback_set(mosq, on_mqtt_disconnect);
		mosquitto_message_callback_set(mosq, on_mqtt_message);

		if (!config.mqtt.username.empty()) {
			mosquitto_username_pw_set(mosq, config.mqtt.username.c_str(), config.mqtt.password.empty() ? NULL : config.mqtt.password.c_str());
		}

		printf("[mqtt] Connecting to %s:%d...\n", config.mqtt.host.c_str(), config.mqtt.port);

		int rc = mosquitto_connect_async(mosq, config.mqtt.host.c_str(), config.mqtt.port, 60);
		if (rc != MOSQ_ERR_SUCCESS) {
#ifndef WIN32
			printf("[mqtt] Failed to connect to %s:%d: %s\n", config.mqtt.host.c_str(), config.mqtt.port, mosquitto_strerror(rc));
#else
			printf("[mqtt] Failed to connect to %s:%d!\n", config.mqtt.host.c_str(), config.mqtt.port);
#endif
			mosquitto_destroy(mosq);
			return;
		}

		rc = mosquitto_loop_start(mosq);
		if (rc != MOSQ_ERR_SUCCESS) {
#ifndef WIN32
			printf("[mqtt] Failed to start network loop: %s\n", mosquitto_strerror(rc));
#else
			printf("[mqtt] Failed to start network loop!\n");
#endif
			mosquitto_destroy(mosq);
			return;
		}

		config.mqtt.thread_running = true;
		config.mqtt.mosq = mosq;
	}

	/*
	if (config.mqtt.mosq) {
		int rc = mosquitto_loop(config.mqtt.mosq, 0, 1);
		if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
#ifndef WIN32
			printf("[mqtt] MQTT loop error: %s � reconnecting in 30s\n", mosquitto_strerror(rc));
#else
			printf("[mqtt] MQTT loop error: reconnecting in 30s\n");
#endif
			mosquitto_disconnect();
			nextConnectAttempt = time(NULL) + 30;
		}
	}
	*/
}

bool mqtt_send(const string& topic, const string& s, bool retain) {
	if (config.mqtt.mosq == NULL) {
		return false;
	}

	bool ret = false;

	int rc;
	if ((rc = mosquitto_publish(config.mqtt.mosq, NULL, topic.c_str(), (int)s.size(), s.c_str(), 0, retain)) == MOSQ_ERR_SUCCESS) {
		mqtt_log("-> %s: %s\n", topic.c_str(), s.c_str());
		ret = true;
	} else {
#ifndef WIN32
		printf("[mqtt] Error sending message to MQTT: %s (%d)\n", mosquitto_strerror(rc), rc);
#else
		printf("[mqtt] Error sending message to MQTT! (error: %d)\n", rc);
#endif
		printf("[mqtt] Payload was to %s: %s\n", topic.c_str(), s.c_str());
	}
	return ret;
}

bool mqtt_send(const string& topic, const UniValue& obj, bool retain) {
	if (config.mqtt.mosq == NULL) {
		return false;
	}

	assert(obj.isObject());

	return mqtt_send(topic, obj.write(), retain);
}

bool mqtt_send_self_info() {
	if (!state.self_info.empty()) {
		return mqtt_send(config.mqtt.topic_prefix + "/self_info", state.self_info, true);
	} else {
		queue_packet_app_start();
	}
	return false;
}

bool mqtt_send_device_info() {
	if (!state.device_info.empty()) {
		return mqtt_send(config.mqtt.topic_prefix + "/device_info", state.device_info, true);
	} else {
		queue_packet_device_query();
	}
	return false;
}
