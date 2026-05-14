/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define __MESHCORE_MQTT_CLIENT_INTERNAL__
#include "meshcoremqttclient.h"

/*
#ifdef DEBUG
#define MQTT_REQUEST_RETRY_TIME 5
#else
#define MQTT_REQUEST_RETRY_TIME 30
#endif
*/

#define NOTICE_PREFIX u8"˃"
#define NOTICE_PREFIX_LEN 2

MeshCore_MQTT_Client::MeshCore_MQTT_Client(const string& phost, uint16 pport, const string& pusername, const string& ppasspord, const string& ptopic_prefix) {
	host = phost;
	port = pport;
	username = pusername;
	password = ppasspord;
	topic_prefix = ptopic_prefix;

	do {
		uniq_id = dsl_get_random<int64>();
	} while (uniq_id == 0);

	if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
		Log("Error initializing libmosquitto!");
	}
}

MeshCore_MQTT_Client::~MeshCore_MQTT_Client() {
	Disconnect();
	mosquitto_lib_cleanup();
}

void MeshCore_MQTT_Client::Disconnect() {
	if (mosq) {
		if (connected) {
			mosquitto_disconnect(mosq);
		}
		mosquitto_destroy(mosq);
		mosq = NULL;
		_connected = false;
	}
}

bool MeshCore_MQTT_Client::Send(const string& topic, const string& s) {
	if (mosq == NULL) {
		return false;
	}

	bool ret = false;

	int rc;
	if ((rc = mosquitto_publish(mosq, NULL, topic.c_str(), (int)s.size(), s.c_str(), 0, false)) == MOSQ_ERR_SUCCESS) {
		onSend(topic, s);
		ret = true;
	} else {
#ifndef WIN32
		Log("Error sending message to MQTT: %s (%d)", mosquitto_strerror(rc), rc);
#else
		Log("Error sending message to MQTT! (error: %d)", rc);
#endif
		Log("Payload was to %s: %s", topic.c_str(), s.c_str());
	}
	return ret;
}

bool MeshCore_MQTT_Client::Send(const string& topic, const UniValue& obj) {
	if (mosq == NULL) {
		return false;
	}

	assert(obj.isObject());

	return Send(topic, obj.write());
}

void MeshCore_MQTT_Client::printLog(const string& str) {
	puts(str.c_str());
}

void MeshCore_MQTT_Client::Log(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);

	char* tmp = dsl_vmprintf(fmt, va);
	printLog(tmp);
	dsl_free(tmp);

	va_end(va);
}

// ── MQTT callbacks ────────────────────────────────────────────────────────

void on_mqtt_connect(struct mosquitto* mosq, void* userdata, int rc) {
	MeshCore_MQTT_Client* c = (MeshCore_MQTT_Client*)userdata;

	if (rc != 0) {
		c->Log("Error connecting to MQTT (rc=%d)", rc);
		return;
	}

	c->cbConnected();
}

void MeshCore_MQTT_Client::cbConnected() {
	Log("Connected to MQTT...");

	_connected = true;
	mosquitto_subscribe(mosq, NULL, mprintf("%s/#", topic_prefix.c_str()).c_str(), 0);
}

void on_mqtt_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
	MeshCore_MQTT_Client* c = (MeshCore_MQTT_Client*)userdata;
	c->cbDisconnected(rc);
}

void MeshCore_MQTT_Client::cbDisconnected(int rc) {
	Log("Disconnected from MQTT broker (rc=%d)", rc);
	_connected = false;
}

void MeshCore_MQTT_Client::_handle_contact(const UniValue& e) {
	if (e.isObject() && e.exists("type") && e["type"].isNum() && e.exists("public_key") && e["public_key"].isStr() && e.exists("name") && e["name"].isStr()) {
		string pubkey = e["public_key"].get_str();
		string nick = e["name"].get_str();
		if (!nick.empty() && !pubkey.empty()) {
			int type = e["type"].get_int();
			int hops = (e.exists("out_path_len") && e["out_path_len"].isNum()) ? e["out_path_len"].get_int() : UNKNOWN_HOPS;
			onContact(nick, pubkey, type, hops, e);
		}
	}
}

void MeshCore_MQTT_Client::_handle_advertisement(const UniValue& e) {
	if (e.exists("public_key") && e["public_key"].isStr()) {
		string pubkey = e["public_key"].get_str();
		if (!pubkey.empty()) {
			onAdvertisement(pubkey, e);
		}
	}
}

void MeshCore_MQTT_Client::_handle_self_info(const UniValue& payload) {
	string nick = payload.exists("name") && payload["name"].isStr() ? payload["name"].get_str() : "";
	string pubkey = payload.exists("public_key") && payload["public_key"].isStr() ? payload["public_key"].get_str() : "";
	if (!nick.empty() && !pubkey.empty()) {
		sstrcpy(my_name, nick.c_str());
		sstrcpy(my_pubkey, pubkey.c_str());
		sstrcpy(my_pubkey_prefix, pubkey.c_str());
		onSelfInfo(nick, pubkey, payload);
	}
}

void MeshCore_MQTT_Client::_handle_chan_info(const UniValue& payload) {
	int idx = (payload.exists("channel_index") && payload["channel_index"].isNum()) ? payload["channel_index"].get_int() : -1;
	if (idx < 0 || idx > MESHCORE_HIGHEST_CHANNEL) {
		return;
	}
	last_chan_info_seen[idx] = GetTickCount64();

	if (payload.exists("name") && payload["name"].isStr()) {
		string name = payload["name"].get_str();
		if (!name.empty()) {
			bool is_private = false; // is this channel one with custom channel key (not the Public channel or standard derived key)

			string key = (payload.exists("secret") && payload["secret"].isStr()) ? payload["secret"].get_str() : "";
			if (key.length() == 32) {

				uint8 bin[16];
				hex2bin(key.c_str(), key.length(), bin, sizeof(bin));

				string derived = DeriveChannelKey(name);
				if (memcmp(bin, derived.c_str(), sizeof(bin))) {
					is_private = true;
				}
			}

			onChanInfo(idx, name, is_private, payload);
		}
	}

	if (idx == MESHCORE_HIGHEST_CHANNEL) {
		if (allChansSeenRecently()) {
			onChanInfoComplete();
		}
	}
}

void MeshCore_MQTT_Client::cbChannelMessage(int channel_idx, const string& from, const char * text, int txt_type, int hops, const UniValue& payload) {
	if (txt_type != 0 && txt_type != 3) {
		return;
	}
	if (channel_idx < 0 || channel_idx > MESHCORE_HIGHEST_CHANNEL || from.empty() || text[0] == 0) {
		return;
	}

	if (!strnicmp(text, NOTICE_PREFIX, NOTICE_PREFIX_LEN)) {
		// is a NOTICE
		txt_type = 3;
		text += NOTICE_PREFIX_LEN;
	}

	onChannelMessage(channel_idx, from, text, txt_type, hops, payload);
}

void MeshCore_MQTT_Client::cbDirectMessage(const string& pubkey_prefix, const char* text, int txt_type, int hops, const UniValue& payload) {
	if (txt_type != 0 && txt_type != 3) {
		return;
	}
	/*
	if (!stricmp(pubkey_prefix, config.self_user->meshcore_pubkey_prefix)) {
		//ignore messages from ourself
		return;
	}
	*/

	if (!strnicmp(text, NOTICE_PREFIX, NOTICE_PREFIX_LEN)) {
		// is a NOTICE
		txt_type = 3;
		text += NOTICE_PREFIX_LEN;
	}

	onDirectMessage(pubkey_prefix, text, txt_type, hops, payload);
}

void on_mqtt_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg) {
	if (!msg->payload || msg->payloadlen <= 0) { return; }

	MeshCore_MQTT_Client* c = (MeshCore_MQTT_Client*)userdata;
	const char* topic = msg->topic;
	c->cbRecvMQTT(topic, msg->payload, msg->payloadlen);
}

void MeshCore_MQTT_Client::cbRecvMQTT(const char * topic, const void * raw_payload, int payloadlen) {

	onRecv(topic, (const char*)raw_payload, payloadlen);

	static const string prefix_advertisement = topic_prefix + "/advertisement";
	static const string prefix_self = topic_prefix + "/self_info";
	static const string prefix_chan_info = topic_prefix + "/channel_info/";
	static const string prefix_chan = topic_prefix + "/message/channel/";
	static const string prefix_dir = topic_prefix + "/message/direct/";
	static const string prefix_contacts = topic_prefix + "/contacts";
	static const string prefix_new_contact = topic_prefix + "/new_contact";
	static const string prefix_my_own_chan_messages = topic_prefix + "/command/send_chan_msg";
	static const string prefix_my_own_dm = topic_prefix + "/command/send_direct_msg";

	UniValue payload;
	if (!payload.read((const char*)raw_payload, payloadlen)) {
		//string raw((const char*)msg->payload, msg->payloadlen);
		//printf("[mqtt] Failed to parse JSON on topic %s: %s\n", topic, raw.c_str());
		return;
	}

	if (topic == prefix_my_own_chan_messages) {
		int64 msg_uniq_id = payload.exists("lib_uniq_id") && payload["lib_uniq_id"].isNum() ? payload["lib_uniq_id"].get_int64() : 0;
		if (msg_uniq_id && msg_uniq_id == uniq_id) {
			// ignore messages from this instance
			return;
		}

		int channel_idx = payload.exists("channel_index") && payload["channel_index"].isNum() ? payload["channel_index"].get_int() : -1;
		int txt_type = payload.exists("txt_type") && payload["txt_type"].isNum() ? payload["txt_type"].get_int() : 0;
		string text = payload.exists("message") && payload["message"].isStr() ? payload["message"].get_str() : "";

		UniValue obj(UniValue::VOBJ);
		cbChannelMessage(channel_idx, my_name, text.c_str(), txt_type, 0, obj);
		return;
	}

	if (topic == prefix_my_own_dm) {
		int64 msg_uniq_id = payload.exists("lib_uniq_id") && payload["lib_uniq_id"].isNum() ? payload["lib_uniq_id"].get_int64() : 0;
		if (msg_uniq_id && msg_uniq_id == uniq_id) {
			// ignore messages from this instance
			return;
		}

		string destination = payload.exists("destination") && payload["destination"].isStr() ? payload["destination"].get_str() : "";
		int txt_type = payload.exists("txt_type") && payload["txt_type"].isNum() ? payload["txt_type"].get_int() : 0;
		string text = payload.exists("message") && payload["message"].isStr() ? payload["message"].get_str() : "";
		if (destination.empty() || text.empty()) { return; }

		if (my_pubkey_prefix[0]) {
			UniValue obj(UniValue::VOBJ);
			onDirectMessage(my_pubkey_prefix, text, txt_type, 0, obj);
		}

		onDirectMessageOnMQTT(destination, text, txt_type);
		return;
	}

#ifdef DEBUG_MQTT
	if (topic != prefix_contacts && topic != prefix_chan_info) {
		printf("Payload only: %s\n", payload.write().c_str());
	}
#endif

	if (!strnicmp(topic, prefix_chan_info.c_str(), prefix_chan_info.length())) {
		_handle_chan_info(payload);
		return;
	}

	if (topic == prefix_new_contact) {
		_handle_contact(payload);
		return;
	}

	if (topic == prefix_self) {
		_handle_self_info(payload);
		return;
	}

	if (topic == prefix_contacts) {
		for (const auto& k : payload.getKeys()) {
			_handle_contact(payload[k]);
		}
		onContactsComplete(payload);
		return;
	}

	if (topic == prefix_advertisement) {
		_handle_advertisement(payload);
		return;
	}

	if (!strnicmp(topic, prefix_chan.c_str(), prefix_chan.length())) {
		string from = payload.exists("from") && payload["from"].isStr() ? payload["from"].get_str() : "";
		string msg_text = payload.exists("message") && payload["message"].isStr() ? payload["message"].get_str() : "";

		int channel_idx = payload.exists("channel_index") && payload["channel_index"].isNum() ? payload["channel_index"].get_int() : 0;
		int txt_type = payload.exists("txt_type") && payload["txt_type"].isNum() ? payload["txt_type"].get_int() : 0;
		int hops = payload.exists("path_len") && payload["path_len"].isNum() ? payload["path_len"].get_int() : UNKNOWN_HOPS;

		cbChannelMessage(channel_idx, from, msg_text.c_str(), txt_type, hops, payload);
		return;
	}

	if (!strnicmp(topic, prefix_dir.c_str(), prefix_dir.length())) {
		string from = payload.exists("public_key_prefix") && payload["public_key_prefix"].isStr() ? payload["public_key_prefix"].get_str() : "";
		string text = payload.exists("message") && payload["message"].isStr() ? payload["message"].get_str() : "";
		if (from.empty() || text.empty()) { return; }

		int txt_type = payload.exists("txt_type") && payload["txt_type"].isNum() ? payload["txt_type"].get_int() : 0;
		int hops = payload.exists("path_len") && payload["path_len"].isNum() ? payload["path_len"].get_int() : UNKNOWN_HOPS;

		cbDirectMessage(from, text.c_str(), txt_type, hops, payload);
		return;
	}

	int x = 1;
}

bool MeshCore_MQTT_Client::RequestContacts() {
	UniValue payload(UniValue::VOBJ);
	string topic = topic_prefix + "/command/get_contacts";
	return Send(topic, payload);
}

bool MeshCore_MQTT_Client::RequestChannels() {
	UniValue payload(UniValue::VOBJ);
	string topic = string(topic_prefix) + "/command/get_channels";
	return Send(topic, payload);
}

bool MeshCore_MQTT_Client::RequestSelfInfo() {
	UniValue payload(UniValue::VOBJ);
	string topic = string(topic_prefix) + "/command/get_self_info";
	return Send(topic, payload);
}

void MeshCore_MQTT_Client::Work() {

	if (mosq == NULL && time(NULL) >= nextConnectAttempt) {
		char client_id[64];
		snprintf(client_id, sizeof(client_id), "meshirc-%d", (int)time(NULL));

		mosq = mosquitto_new(client_id, true, this);
		if (!mosq) {
			Log("[Failed to create mosquitto client");
			nextConnectAttempt = time(NULL) + 30;
		} else {
			mosquitto_connect_callback_set(mosq, on_mqtt_connect);
			mosquitto_disconnect_callback_set(mosq, on_mqtt_disconnect);
			mosquitto_message_callback_set(mosq, on_mqtt_message);

			if (!username.empty()) {
				mosquitto_username_pw_set(mosq, username.c_str(), password.empty() ? NULL : password.c_str());
			}

			Log("Connecting to %s:%d...", host.c_str(), port);
			int rc = mosquitto_connect(mosq, host.c_str(), port, 60);
			if (rc != MOSQ_ERR_SUCCESS) {
#ifndef WIN32
				Log("Failed to connect to %s:%d: %s", host.c_str(), port, mosquitto_strerror(rc));
#else
				Log("Failed to connect to %s:%d!", host.c_str(), port);
#endif
				Disconnect();
				nextConnectAttempt = time(NULL) + 30;
			}
		}
	}

	if (mosq) {
		int rc = mosquitto_loop(mosq, 0, 1);
		if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
#ifndef WIN32
			Log("MQTT loop error: %s — reconnecting in 30s", mosquitto_strerror(rc));
#else
			Log(" MQTT loop error: reconnecting in 30s");
#endif
			Disconnect();
			nextConnectAttempt = time(NULL) + 30;
		}
	}
}

void _get_split_into_lines(const string& str, vector<string>& lines, size_t len) {
	vector<char*> words;
	char* tmp = strdup(str.c_str());
	strtrim(tmp);
	char* p2 = NULL;
	char* p = strtok_r(tmp, " \t", &p2);
	while (p != NULL) {
		if (p[0]) {
			words.push_back(p);
		}
		p = strtok_r(NULL, " \t", &p2);
	}

	string line;
	for (size_t i = 0; i < words.size(); i++) {
		if (line.empty()) {
			line = words[i];
		} else {
			if (line.length() + 1 + strlen(words[i]) > len) {
				lines.push_back(line);
				line = words[i];
			} else {
				line += " ";
				line += words[i];
			}
		}
	}
	if (!line.empty()) {
		lines.push_back(line);
	}

	free(tmp);
}

void split_msg_for_chan(const string& sender_nick, const string& str, vector<string>& lines) {
	size_t len = 133 - NOTICE_PREFIX_LEN;
	if (!sender_nick.empty()) {
		len -= (sender_nick.length() + 2);
	} else {
		// use a default of 16 if we don't know the bot's name
		len -= 16;
	}
	_get_split_into_lines(str, lines, len);
}

bool MeshCore_MQTT_Client::SendChannelMsg(int idx, const string& str, int txt_type) {
	if (!connected || str.empty() || idx < 0 || idx > MESHCORE_HIGHEST_CHANNEL) {
		return false;
	}

	string topic = string(topic_prefix) + "/command/send_channel_msg";

	UniValue payload(UniValue::VOBJ);
	payload.pushKV("channel_index", idx);
	payload.pushKV("txt_type", txt_type);
	payload.pushKV("lib_uniq_id", uniq_id);

	vector<string> lines;
	split_msg_for_chan(my_name, str, lines);

	bool ret = false;

	for (auto& line : lines) {
		if (txt_type == 3) {
			line.insert(0, (const char*)NOTICE_PREFIX);
		}
		payload.pushKV("message", line);

		Log("Sending channel message to channel %d: %s", idx, line.c_str());

		if (Send(topic, payload)) {
			ret = true;
		}
	}
	return ret;
}

void split_msg_for_privmsg(const char* str, vector<string>& lines) {
	_get_split_into_lines(str, lines, 133 - NOTICE_PREFIX_LEN);
}

bool MeshCore_MQTT_Client::SendDirectMsg(const string& pubkey, const string& str, int txt_type) {
	if (!connected || pubkey.empty() || str.empty()) {
		return false;
	}

	string topic = string(topic_prefix) + "/command/send_direct_msg";

	UniValue payload(UniValue::VOBJ);
	payload.pushKV("destination", pubkey);
	payload.pushKV("txt_type", txt_type);
	payload.pushKV("lib_uniq_id", uniq_id);

	vector<string> lines;
	split_msg_for_privmsg(str.c_str(), lines);

	bool ret = false;

	for (auto& line : lines) {
		if (txt_type == 3) {
			line.insert(0, (const char*)NOTICE_PREFIX);
		}

		payload.pushKV("message", line);

		Log("Sending direct message to %s: %s", pubkey.c_str(), line.c_str());

		if (Send(topic, payload)) {
			ret = true;
		}
	}

	return ret;
}

string MeshCore_MQTT_Client::DeriveChannelKey(const string& channelName) {
	if (channelName == "Public") {
		return "\x8b\x33\x87\xe9\xc5\xcd\xea\x6a\xc9\xe5\xed\xba\xa1\x15\xcd\x72";
	}

	char* tmp = strdup(channelName.c_str());
	strtrim(tmp);
	string input = (tmp[0] == '#') ? tmp : string("#") + tmp;
	free(tmp);

	char key[33] = { 0 };
	if (!hashdata("sha256", (const uint8*)input.c_str(), input.length(), key, sizeof(key), true)) {
		return "";
	}

	return string(key, 16);
}
