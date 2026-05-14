/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#define DSL_STATIC
#ifndef ENABLE_LIBEVENT
#define ENABLE_LIBEVENT
#endif
#include <drift/dsl.h>
#include <list>
#include <assert.h>

#include <univalue.h>

#define UNKNOWN_HOPS INT_MIN
#define DIRECT_HOP 0xFF
#define MESHCORE_HIGHEST_CHANNEL 39

// MeshCore uses 32 bytes and must be NULL terminated, so 31 max characters
#define MESHCORE_MAX_NICK_LEN 31
#define MESHCORE_MAX_CHAN_LEN 32
#define MESHCORE_PUBKEY_LEN 64
#define MESHCORE_PUBKEY_PREFIX_LEN 12

#ifdef __MESHCORE_MQTT_CLIENT_INTERNAL__
#define LIBMOSQUITTO_STATIC
#include <mosquitto.h>
#else
#define mosquitto void
#endif

class MeshCore_MQTT_Client {
private:
	mosquitto* mosq = NULL;

	int64 uniq_id = 0;
	bool _connected = false;
	string topic_prefix;
	uint64 last_chan_info_seen[MESHCORE_HIGHEST_CHANNEL + 1] = { 0 };
	bool allChansSeenRecently() {
		uint64 now = GetTickCount64();
		for (size_t i = 0; i <= MESHCORE_HIGHEST_CHANNEL; i++) {
			if (now - last_chan_info_seen[i] > 10000) {
				return false;
			}
		}
		return true;
	}

	int64 nextConnectAttempt = 0;

	string host, username, password;
	uint16 port;

	char my_name[MESHCORE_MAX_NICK_LEN + 1];
	char my_pubkey[MESHCORE_PUBKEY_LEN + 1] = { 0 };
	char my_pubkey_prefix[MESHCORE_PUBKEY_PREFIX_LEN + 1] = { 0 };

	void _handle_contact(const UniValue& payload);
	void _handle_advertisement(const UniValue& payload);
	void _handle_self_info(const UniValue& payload);
	void _handle_chan_info(const UniValue& payload);
public:
	MeshCore_MQTT_Client(const string& phost, uint16 pport = 1883, const string& pusername = "", const string& ppasspord = "", const string& ptopic_prefix = "meshcore");

	~MeshCore_MQTT_Client();

	const bool& connected = _connected;
	void setConnected(bool conn) {
		_connected = conn;
	}

	bool Connect();
	void Disconnect();
	void Work();
	bool Send(const string& topic, const string& payload);
	bool Send(const string& topic, const UniValue& payload);


	bool SendChannelMsg(int channel_idx, const string& str, int txt_type = 0);
	bool SendDirectMsg(const string& pubkey, const string& str, int txt_type = 0);

	bool RequestContacts();
	bool RequestChannels();
	bool RequestSelfInfo();
	void Log(const char* fmt, ...);

	/**
	* Derive a MeshCore #channel key from the name. (first 16 bytes of an SHA-256 on the name)
	* If you pass the string 'Public', then the default key for the Public channel will be returned. Otherwise a # is prepended to the channelName if it doesn't already start with a #.
	*/
	string DeriveChannelKey(const string& channelName);

	// End-user callbacks for you to implement:

	virtual void printLog(const string& str); // the default implementation is puts()
	virtual void onSend(const string& topic, const string& payload) {}
	virtual void onRecv(const string& topic, const char* payload, int payloadlen) {}
	virtual void onContact(const string& adv_name, const string& pubkey, int type, int hops, const UniValue& payload) {}
	virtual void onContactsComplete(const UniValue& payload) {} // will have normally been preceded by one or more onContact() calls
	virtual void onAdvertisement(const string& pubkey, const UniValue& payload) {}
	virtual void onSelfInfo(const string& adv_name, const string& pubkey, const UniValue& payload) {}
	virtual void onChanInfo(int channel_idx, const string& channelName, bool is_private, const UniValue& payload) {} // is_private = true if the channel encryption key isn't the standard derived one
	virtual void onChanInfoComplete() {}
	virtual void onChannelMessage(int channel_idx, const string& from, const string& text, int txt_type, int hops, const UniValue& payload) {}
	virtual void onDirectMessage(const string& pubkey_prefix, const string& text, int txt_type, int hops, const UniValue& payload) {}
	virtual void onDirectMessageOnMQTT(const string& destination, const string& text, int txt_type) {} // this is called when DMs are seen on MQTT from other instances connected to MQTT

	// Internal callbacks
	void cbConnected();
	void cbDisconnected(int rc);
	void cbChannelMessage(int channel_idx, const string& from, const char* text, int txt_type, int hops, const UniValue& payload);
	void cbDirectMessage(const string& pubkey_prefix, const char* text, int txt_type, int hops, const UniValue& payload);
	void cbRecvMQTT(const char* topic, const void* payload, int payloadlen);
};
