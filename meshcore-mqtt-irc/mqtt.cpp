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

void MQTT_IRC_Client::onSend(const string& topic, const char* payload, int payloadlen) {
	string raw((const char*)payload, payloadlen);
	printLog(mprintf("-> %s: %s\n", topic.c_str(), raw.c_str()));
}
void MQTT_IRC_Client::onRecv(const string& topic, const char* payload, int payloadlen) {
	string raw((const char*)payload, payloadlen);
	printLog(mprintf("<- %s: %s\n", topic.c_str(), raw.c_str()));
}

void MQTT_IRC_Client::onContact(const string& nick, const string& pubkey, int type, int hops, const UniValue& payload) {
	uint8 flags = (payload.exists("flags") && payload["flags"].isNum()) ? (uint8)payload["flags"].get_int() : 0;
	add_user(nick, pubkey, "", flags, hops);
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
		if (add_user(nick, pubkey, "", 0, 0, &config.self_user)) {
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

void MQTT_IRC_Client::onError(const string& errmsg, const UniValue& payload) {
	printf("[mqtt] Received error message: %s\n", errmsg.c_str());

	SendServerNoticeToAllAuthenticatedClients(mprintf("Received error message from MQTT: %s\n", errmsg.c_str()));
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

void MQTT_IRC_Client::onChannelMessage(int channel_idx, const string& from, const string& text, MESHCORE_TEXT_TYPES txt_type, int hops, const UniValue& payload) {
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
		add_user(from, "", "", 0, hops);
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

void MQTT_IRC_Client::onDirectMessage(const string& pubkey_prefix, const string& text, MESHCORE_TEXT_TYPES txt_type, int hops, const UniValue& payload) {
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

void MQTT_IRC_Client::onDirectData(const string& pubkey_prefix, uint8* data, size_t data_len, int hops, const UniValue& payload) {
	if (config.self_user.get() == NULL) {
		return;
	}

	shared_ptr<MeshCoreUser> user;
	if (!get_user_by_pubkey_prefix(pubkey_prefix, user)) {
		printf("[mqtt] Could not find user with pubkey_prefix %s\n", pubkey_prefix.c_str());
		return;
	}

	printf("[mqtt] [DM] Datagram from %s: %s\n", user->irc_nick, bin2hex(data, data_len).c_str());

	vector<string> parms = {
		":" + user->hostmask,
		"PRIVMSG",
		config.self_user->irc_nick,
		mprintf("Received datagram: %s\n", bin2hex(data, data_len).c_str())
	};
	if (!SendLineToAllAuthenticatedClients(parms)) {
		// no clients are online and connected
		AddOfflineMessage(parms);
	}

	user->updateSeen(hops);
	user->onAction();
}

void MQTT_IRC_Client::onDirectMessageOnMQTT(const string& destination, const string& text, MESHCORE_TEXT_TYPES txt_type) {
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
			(txt_type == TXT_TYPE_NOTICE) ? "NOTICE" : "PRIVMSG",
			user->irc_nick,
			line
		};
		// for some reason mIRC thinks you are messaging yourself instead of the other user???
		SendLineToAllAuthenticatedClients(parms);
	}

	config.self_user->onAction();
}

string FormatMinutes(int64 secs) {
	bool is_neg = (secs < 0);
	if (is_neg) {
		secs *= -1;
	}
	int64 months = 0;
	int64 days = secs / 86400;
	if (days) {
		time_t ts1 = time(NULL);
		time_t ts2 = ts1 + secs;
		struct tm tm1, tm2;
		localtime_r(&ts1, &tm1);
		localtime_r(&ts2, &tm2);
		if (tm1.tm_mon < tm2.tm_mon) {
			months = (tm2.tm_mon - tm1.tm_mon);
			tm2 = tm1;
			tm2.tm_mon += (int)months;
			//tm2.tm_min = tm2.tm_hour = tm2.tm_sec = 0;
			//tm2.tm_mday = tm1.tm_mday;
			time_t ts3 = mktime(&tm2);
			if (ts3 > ts1) {
				int64 mdays = (ts3 - ts1) / 86400;
				if (mdays <= days) {
					days -= mdays;
					secs -= mdays * 86400;
				} else {
					months = 0;
				}
			} else {
				months = 0;
			}
		} else if (tm1.tm_mon > tm2.tm_mon) {
			int x = 1;
			//months = (tm1.tm_mon - tm2.tm_mon);
			//int64 mdays = (ts2 - ts1) / 86400;
			//days -= mdays;
			//secs -= mdays * 86400;
		}

		secs -= days * 86400;
	}
	int64 hours = secs / 3600;
	if (hours) {
		secs -= hours * 3600;
	}
	int64 mins = secs / 60;
	if (mins) {
		secs -= mins * 60;
	}
	if (secs >= 30) {
		mins++;
	}

	stringstream sstr;
	if (is_neg) {
		sstr << "-";
	}
	if (months) {
		sstr << months << "mon ";
	}
	if (days) {
		sstr << days << "d ";
	}
	if (hours) {
		sstr << hours << "h ";
	}
	sstr << mins << "m";
	return sstr.str();
}

void MQTT_IRC_Client::onStatusResponse(const string& pubkey_prefix, const UniValue& payload) {
	shared_ptr<MeshCoreUser> user;
	if (!get_user_by_pubkey_prefix(pubkey_prefix, user)) {
		printf("[mqtt] Could not find user with pubkey_prefix %s\n", pubkey_prefix.c_str());
		return;
	}

	string msg = "\x01PONG Status response received from " + pubkey_prefix;

	if (payload.exists("batt_milli_volts") && payload["batt_milli_volts"].isNum()) {
		double volts = double(payload["batt_milli_volts"].get_int()) / 1000.0f;
		msg += mprintf(" [Battery: %.02fv]", volts);
	}
	if (payload.exists("total_up_time_secs") && payload["total_up_time_secs"].isNum()) {
		int64 uptime = int64(payload["total_up_time_secs"].get_int64());
		msg += mprintf(" [Uptime: %s]", FormatMinutes(uptime).c_str());
	}

	msg += "\x01";

	vector<string> parms = {
		":" + user->hostmask,
		"PRIVMSG",
		config.self_user->irc_nick,
		msg
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
