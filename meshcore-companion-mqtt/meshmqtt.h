/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#define DSL_STATIC
#include <drift/dsl.h>
#include <list>
#include <queue>
#include <assert.h>

#include <univalue.h>

#define UNKNOWN_HOPS INT_MIN
#define MESHCORE_HIGHEST_CHANNEL 39

// MeshCore uses 32 bytes and must be NULL terminated, so 31 max characters
#define MESHCORE_MAX_NICK_LEN 31
#define MESHCORE_MAX_CHAN_LEN 32
#define MESHCORE_PUBKEY_LEN 64
#define MESHCORE_PUBKEY_PREFIX_LEN 12
#define MESHCORE_SECRET_LEN 64

#include "meshcore_protocol.h"

#define LIBMOSQUITTO_STATIC
#include <mosquitto.h>

#define MESHCORE_COMPANION_MQTT_VERSION "0.0.1"

struct ci_less {
	struct nocase_compare {
		bool operator() (const unsigned char& c1, const unsigned char& c2) const {
			return tolower(c1) < tolower(c2);
		}
	};
	bool operator() (const std::string& s1, const std::string& s2) const {
		return std::lexicographical_compare
		(s1.begin(), s1.end(),   // source range
			s2.begin(), s2.end(),   // dest range
			nocase_compare());  // comparison
	}
};

class IO_Driver {
protected:
	string _name;
public:
	virtual ~IO_Driver() { }

	const string& name = _name;

	virtual bool Open(const string& device) = 0;
	virtual bool IsOpen() = 0;
	virtual void Close() = 0;

	virtual int Read(uint8* buf, int buflen) = 0;
	virtual int Write(const uint8* buf, int buflen) = 0;
};

class IO_Driver_Serial : public IO_Driver {
private:
#ifdef WIN32
	HANDLE hPort = INVALID_HANDLE_VALUE;
#else
	int fd = -1;
#endif

public:
	IO_Driver_Serial() {
		_name = "Serial";
	}
	virtual ~IO_Driver_Serial() { Close(); }

	bool Open(const string& device);
	bool IsOpen();
	void Close();

	int Read(uint8* buf, int buflen);
	int Write(const uint8* buf, int buflen);
};

class MeshCoreUser {
private:
	char _pubkey[MESHCORE_PUBKEY_LEN + 1] = { 0 };
	char _pubkey_prefix[MESHCORE_PUBKEY_PREFIX_LEN + 1] = { 0 };

	int64 _last_seen = time(NULL);
public:
	CONTACT_TYPE type = ADV_TYPE_NONE;

	char name[MESHCORE_MAX_NICK_LEN + 1];
	const char* const pubkey = _pubkey;
	const char* const pubkey_prefix = _pubkey_prefix;

	const int64 time_added = time(NULL);
	int64 last_seen = 0;
	int last_hops = UNKNOWN_HOPS;

	uint8 flags = 0;
	double latitude = 0.0f;
	double longitude = 0.0f;

	void updateMeshCorePubKey(const string& pubkey) {
		sstrcpy(_pubkey, pubkey.c_str());
		sstrcpy(_pubkey_prefix, pubkey.c_str());
	}
	void updateMeshCorePubKeyPrefix(const string& pubkey_prefix) {
		sstrcpy(_pubkey_prefix, pubkey_prefix.c_str());
	}

	void ToUniValue(UniValue& obj);
};
typedef map<string, shared_ptr<MeshCoreUser>> userMap;

class MeshCoreChannel {
private:

public:
	int channel_index = -1;
	char name[MESHCORE_MAX_CHAN_LEN + 1];
	char secret[MESHCORE_SECRET_LEN + 1];

	void ToUniValue(UniValue& obj);
};
//extern map<int, shared_ptr<MeshCoreChannel>> chans;

extern DSL_Mutex hMutex;
class MQTT_Command {
public:
	string cmd;
	UniValue parms;
};

class MeshCoreCommand {
public:
	string data;

	uint8 getType() {
		if (data.size()) {
			return (uint8)data[0];
		}
		assert(0);
		return 0xFF;
	}

	set<uint8> expected_responses;
	bool is_message = false;
	uint64 time_limit = 0;

	virtual void onError(uint8 code) {} // called when an ERROR response is received

	//bool is_critical_command = false;
};

class MeshCoreCommandChannelMessage : public MeshCoreCommand {
private:
	uint8 attempt = 0;

	virtual void onError(uint8 code);
};

class MeshCoreCommandDirectMessage : public MeshCoreCommand {
private:
	uint8 attempt = 0;

	virtual void onError(uint8 code);
};

extern list<shared_ptr<MeshCoreCommand>> outgoing_commands;
extern shared_ptr<MeshCoreCommand> current_outgoing_command;

class CONFIG {
public:
	bool shutdown_now = false;

	shared_ptr<IO_Driver> io_driver;
	DSL_BUFFER recvbuf = { 0 }; // I/O receive buffer
	DSL_BUFFER sendbuf = { 0 }; // I/O send buffer	

	queue<shared_ptr<MQTT_Command>> incoming_commands;

	struct {
#ifdef WIN32
		string device = "COM3:";
#else
		string device = "/dev/ttyUSB0";
#endif

		int64 expireUnseenContacts = 86400 * 3; // remove them after 3 days without being seen
		size_t maxContactsListSize = 500;
		uint64 delayBetweenMessages = 2000;

#ifdef DEBUG
		bool log_to_file = true;
#else
		bool log_to_file = false;
#endif
		FILE* log_fp = NULL;
		bool log_to_console = false;
	} meshcore;

	struct {
		string host = "127.0.0.1";
		uint16 port = 1883;
		string username;
		string password;
		string topic_prefix = "meshmqtt";

		mosquitto* mosq = NULL;
		bool connected = false;
		//bool connecting = false;
		bool thread_running = false;

#ifdef DEBUG
		bool log_to_file = true;
#else
		bool log_to_file = false;
#endif
		FILE* log_fp = NULL;
		bool log_to_console = false;
	} mqtt;
};
extern CONFIG config;

struct MESHCORE_STATE {
	int64 lastMessageCheck = 0;

	int64 lastContactsFullUpdate = 0;
	int64 lastContactsPartialUpdate = 0;
	//int64 lastContactsListReceived = 0;
	uint32 lastContactModTime = 0;
	bool haveSentContactsList = false;

	uint64 nextMessageTime = 0;

	string self_info;
	string device_info;

	userMap users;
	map<int, shared_ptr<MeshCoreChannel>> chans;

	void reset() {
		lastContactsPartialUpdate = lastContactsFullUpdate = 0;
		lastContactModTime = 0;
		haveSentContactsList = false;

		self_info.clear();
		device_info.clear();

		users.clear();
		chans.clear();
	}
};
extern MESHCORE_STATE state;

void mosquitto_disconnect();

void io_work();
void handle_incoming_commands();
void mosquitto_work();
bool mqtt_send(const string& topic, const string& s, bool retain);
bool mqtt_send(const string& topic, const UniValue& obj, bool retain);
bool mqtt_send_self_info();
bool mqtt_send_device_info();
//bool send_meshcore_msg_chan(int idx, const char* str, int txt_type);
//bool send_meshcore_msg_dm(const char* nick, const char* pubkey, const char* str, int txt_type);

string trim_nulls(const string& str);
string trim_nulls(const char* str, size_t len);

void add_or_update_channel(_PACKET_CHANNEL_INFO* ci);
bool mqtt_send_channels();
bool mqtt_send_channel(int idx);

void add_or_update_user(_PACKET_CONTACT * c);
bool get_user_by_pubkey(const string& pubkey, shared_ptr<MeshCoreUser>& u);
bool get_user_by_pubkey_prefix(const string& pubkey_prefix, shared_ptr<MeshCoreUser>& u);
void del_user_by_pubkey(const string& pubkey);
void clear_old_contacts();
bool mqtt_send_contacts();
bool mqtt_send_new_contact(shared_ptr<MeshCoreUser>& u);

void queue_packet_app_start();
void queue_packet_device_query();
void queue_packet_get_channel_info(uint8 index);
void queue_packets_get_channels();
void queue_packet_send_channel_msg(uint8 channel_idx, const string& str, MESHCORE_TEXT_TYPES txt_type = TXT_TYPE_PLAIN);
void queue_packet_send_direct_msg(const string& pubkey_or_prefix, const string& str, uint8 attempt, MESHCORE_TEXT_TYPES txt_type = TXT_TYPE_PLAIN);
bool is_valid_destination(const string& destination);
void update_contacts(bool force_get_all);

#pragma once
