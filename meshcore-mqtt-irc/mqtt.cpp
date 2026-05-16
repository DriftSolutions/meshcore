/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"

#ifdef DEBUG
#define MQTT_REQUEST_RETRY_TIME 5
#else
#define MQTT_REQUEST_RETRY_TIME 30
#endif

void accept_irc_clients() {
	if (config.waitingForInitialState) {
		int64 ts = time(NULL) - 3600;
		if (config.lastReceivedChannels >= ts && chans.size() && config.lastReceivedContacts >= ts && config.self_user) {
			config.waitingForInitialState = false;
			printf("[irc] We are now accepting IRC clients...\n");
			ev->EnableRecv(config.irc.lSock);
		}
	}
}

void MQTT_IRC_Client::printLog(const string& str) {
	if (!config.mqtt.log_to_console && !config.mqtt.log_to_file) {
		return;
	}

	if (config.mqtt.log_to_console) {
		printf("[mqtt] %s\n", str.c_str());
	}

	if (config.mqtt.log_to_file) {
		if (config.mqtt.log_fp == NULL) {
			if (access("./logs", 0) != 0) {
				dsl_mkdir("./logs", 0700);
			}
			string fn = "./logs/mqtt.log";
			config.mqtt.log_fp = fopen(fn.c_str(), "wb");
			if (config.mqtt.log_fp != NULL) {
				printf("[mqtt] Opened %s for output...\n", fn.c_str());
			} else {
				printf("[mqtt] Error opening %s for output: %s\n", fn.c_str(), strerror(errno));
			}
		}
		if (config.mqtt.log_fp != NULL) {
			fprintf(config.mqtt.log_fp, "%s\n", str.c_str());
#ifdef DEBUG
			fflush(config.mqtt.log_fp);
#endif
		}
	}
}

void MQTT_IRC_Client::onSend(const string& topic, const string& payload) {
	printLog(mprintf("-> %s: %s\n", topic.c_str(), payload.c_str()));
}
void MQTT_IRC_Client::onRecv(const string& topic, const char* payload, int payloadlen) {
	string raw((const char*)payload, payloadlen);
	printLog(mprintf("<- %s: %s\n", topic.c_str(), raw.c_str()));
}

void MQTT_IRC_Client::onContact(const string& nick, const string& pubkey, int type, int hops, const UniValue& payload) {
	add_user(nick, pubkey, "", hops);
}

void MQTT_IRC_Client::onContactsComplete(const UniValue& payload) {
	printf("[mqtt] Received updated contacts list...\n");
	config.lastReceivedContacts = time(NULL);
	accept_irc_clients();
}

void MQTT_IRC_Client::onAdvertisement(const string& pubkey, const UniValue& payload) {
	shared_ptr<MeshCoreUser> u;
	if (get_user_by_pubkey(pubkey, u)) {
		//int hops = (e.exists("out_path_len") && e["out_path_len"].isNum()) ? e["out_path_len"].get_int() : UNKNOWN_HOPS;
		u->updateSeen(UNKNOWN_HOPS);
	}
}

void MQTT_IRC_Client::onSelfInfo(const string& nick, const string& pubkey, const UniValue& payload) {
	if (config.self_user.get() == NULL || stricmp(config.self_user->meshcore_nick, nick.c_str()) || stricmp(config.self_user->meshcore_pubkey, pubkey.c_str())) {
		if (add_user(nick, pubkey, "", 0, &config.self_user)) {
			printf("[mqtt] My node name: %s -> %s\n", config.self_user->meshcore_nick, config.self_user->irc_nick);
			printf("[mqtt] My node public key: %s\n", config.self_user->meshcore_pubkey);
		} else {
			printf("[mqtt] Error adding my self user!\n");
		}
		//config.self_user->hostmask = mprintf("%s!meshcore@%s", config.self_user->irc_nick, config.self_user->meshcore_pubkey);
	}

	config.lastReceivedSelfInfo = time(NULL);
	accept_irc_clients();
}

// is_private = true if the channel encryption key isn't the standard derived one
void MQTT_IRC_Client::onChanInfo(int channel_idx, const string& channelName, bool is_private, const UniValue& payload) {
	add_channel(channel_idx, channelName, is_private);
}

void MQTT_IRC_Client::onChanInfoComplete() {
	if (chans.size()) {
		printf("[mqtt] Received updated channel list...\n");
		config.lastReceivedChannels = time(NULL);
		accept_irc_clients();
	}
}

void split_incoming_into_lines(const string& line, vector<string>& lines) {
	lines.clear();
	char* tmp = strdup(line.c_str());
	char* p2 = NULL;
	char* p = strtok_r(tmp, "\r\n", &p2);
	while (p != NULL) {
		char* tmp2 = strdup(p);
		strtrim(tmp2);
		if (tmp2[0]) {
			lines.push_back(tmp2);
		}
		free(tmp2);
		p = strtok_r(NULL, "\r\n", &p2);
	}
	free(tmp);
}

void MQTT_IRC_Client::onChannelMessage(int channel_idx, const string& from, const string& text, int txt_type, int hops, const UniValue& payload) {
	if (config.self_user.get() == NULL) {
		return;
	}

	shared_ptr<MeshCoreChannel> chan;
	if (!get_channel_by_meshcore_index(channel_idx, chan)) {
		printf("[mqtt] Error getting channel for channel_index = %d\n", channel_idx);
		return;
	}

	shared_ptr<MeshCoreUser> user;
	if (!get_user_by_meshcore_name(from, user)) {
		add_user(from, "", "", hops);
		if (!get_user_by_meshcore_name(from, user)) {
			printf("[mqtt] Error getting user record for %s", from.c_str());
			return;
		}
	}

	printf("[mqtt] [%s] <%s> %s\n", chan->irc_name, user->irc_nick, text.c_str());

	user->updateSeen(hops);

	vector<string> lines;
	split_incoming_into_lines(text, lines);
	for (auto& line : lines) {
		chan->onReceiveMessage(user, line.c_str(), txt_type);
	}
}

void MQTT_IRC_Client::onChannelData(int channel_idx, uint16 data_type, uint8* data, size_t data_len, int hops, const UniValue& payload) {
	if (config.self_user.get() == NULL) {
		return;
	}

	shared_ptr<MeshCoreChannel> chan;
	if (!get_channel_by_meshcore_index(channel_idx, chan)) {
		printf("[mqtt] Error getting channel for channel_index = %d\n", channel_idx);
		return;
	}

	printf("[mqtt] [%s] Datagram: %s\n", chan->irc_name, bin2hex(data, data_len).c_str());

	vector<string> parms = {
		":" + config.irc.server_hostname,
		"PRIVMSG",
		chan->irc_name,
		mprintf("Received datagram of type %u: %s\n", data_type, bin2hex(data, data_len).c_str())
	};
	if (!SendLineToAllAuthenticatedClients(parms)) {
		// no clients are online and connected
		AddOfflineMessage(parms);
	}
}

void MQTT_IRC_Client::onDirectMessage(const string& pubkey_prefix, const string& text, int txt_type, int hops, const UniValue& payload) {
	if (config.self_user.get() == NULL) {
		return;
	}

	shared_ptr<MeshCoreUser> user;
	if (!get_user_by_pubkey_prefix(pubkey_prefix, user)) {
		printf("[mqtt] Could not find user with pubkey_prefix %s\n", pubkey_prefix.c_str());
		return;
	}

	printf("[mqtt] [DM] <%s> %s\n", user->irc_nick, text.c_str());

	vector<string> lines;
	split_incoming_into_lines(text, lines);
	for (auto& line : lines) {
		vector<string> parms = {
			":" + user->hostmask,
			(txt_type == 3) ? "NOTICE" : "PRIVMSG",
			config.self_user->irc_nick,
			line
		};
		if (!SendLineToAllAuthenticatedClients(parms)) {
			AddOfflineMessage(parms);
		}
	}

	user->updateSeen(hops);
	user->onAction();
}

void MQTT_IRC_Client::onDirectMessageOnMQTT(const string& destination, const string& text, int txt_type) {
	if (config.self_user.get() == NULL) {
		return;
	}

	shared_ptr<MeshCoreUser> user;
	if (destination.length() >= MESHCORE_PUBKEY_LEN) {
		if (!get_user_by_pubkey(destination, user)) {
			printf("[mqtt] Could not find user with pubkey_prefix %s\n", destination.c_str());
			return;
		}
	} else {
		if (!get_user_by_pubkey_prefix(destination, user)) {
			printf("[mqtt] Could not find user with pubkey_prefix %s\n", destination.c_str());
			return;
		}
	}

	printf("[mqtt] [DM] <%s> %s\n", user->irc_nick, text.c_str());

	vector<string> lines;
	split_incoming_into_lines(text, lines);
	for (auto& line : lines) {
		vector<string> parms = {
			":" + config.self_user->hostmask,
			(txt_type == 3) ? "NOTICE" : "PRIVMSG",
			user->irc_nick,
			line
		};
		// for some reason mIRC thinks you are messaging yourself instead of the other user???
		SendLineToAllAuthenticatedClients(parms);
	}

	config.self_user->onAction();
}

void MQTT_IRC_Client::onStatusResponse(const string& pubkey_prefix, const string& status_data) {
	shared_ptr<MeshCoreUser> user;
	if (!get_user_by_pubkey_prefix(pubkey_prefix, user)) {
		printf("[mqtt] Could not find user with pubkey_prefix %s\n", pubkey_prefix.c_str());
		return;
	}

	vector<string> parms = {
		":" + user->hostmask,
		"PRIVMSG",
		config.self_user->irc_nick,
		"\x01PONG Status response received from " + pubkey_prefix + "\x01"
	};
	SendLineToAllAuthenticatedClients(parms);
}

void mosquitto_loop() {
	if (config.mqtt.client == NULL) {
		return;
	}

	static int64 lastSelfReq = 0;
	static int64 lastContactsReq = 0;
	static int64 lastChannelReq = 0;

	config.mqtt.client->Work();

	if (config.mqtt.client->connected) {
		if (time(NULL) - config.lastReceivedSelfInfo >= 3600 && time(NULL) - lastSelfReq >= MQTT_REQUEST_RETRY_TIME) {
			printf("[mqtt] Sending request for our node information...\n");
			config.mqtt.client->RequestSelfInfo();
			lastSelfReq = time(NULL);
		}
		if (time(NULL) - config.lastReceivedContacts >= 3600 && time(NULL) - lastContactsReq >= MQTT_REQUEST_RETRY_TIME) {
			printf("[mqtt] Sending request for contacts...\n");
			config.mqtt.client->RequestContacts();
			lastContactsReq = time(NULL);
		}
		if (time(NULL) - config.lastReceivedChannels >= 3600 && time(NULL) - lastChannelReq >= MQTT_REQUEST_RETRY_TIME) {
			printf("[mqtt] Sending request for channels...\n");
			config.mqtt.client->RequestChannels();
			lastChannelReq = time(NULL);
		}
	}
}
