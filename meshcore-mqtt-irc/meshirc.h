/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ENABLE_SQLITE
#define ENABLE_SQLITE
#endif

#include "../libmeshcoremqttclient/meshcoremqttclient.h"

#define MQTT_IRC_VERSION "0.0.1"

// IRC limits vary. The original spec was 9 digits, later versions said servers can support longer nicknames if they feel like it. mIRC limits to 30 digits so that seems like a good way to go.
#define IRC_MAX_NICK_LEN 30
// IRC allows more than 32 digits for channel lengths, but we're limited to the MeshCore limit
#define IRC_MAX_CHAN_LEN 32

#include "irc_numerics.h"

extern DSL_Sockets socks;
extern DSL_Sockets_Events * ev;

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

class Client;

class MeshCoreUser {
private:
	char _meshcore_nick[MESHCORE_MAX_NICK_LEN+1]; 
	char _meshcore_pubkey[MESHCORE_PUBKEY_LEN+1] = { 0 };
	char _meshcore_pubkey_prefix[MESHCORE_PUBKEY_PREFIX_LEN+1] = { 0 };

	char _irc_nick[IRC_MAX_NICK_LEN+1];
	string _hostmask;
	void updateHostmask();

	int64 _last_seen = time(NULL);
	int64 _last_action_time = 0;
	bool _is_away = false;
public:
	const char* const meshcore_nick = _meshcore_nick;
	const char* const meshcore_pubkey = _meshcore_pubkey;
	const char* const meshcore_pubkey_prefix = _meshcore_pubkey_prefix;
	uint8 meshcore_flags = 0;

	const char* const irc_nick = _irc_nick;
	/**
	* Full IRC hostmask, which is: irc_nick + "!meshcore@" + meshcore_pubkey
	* If the full pubkey is unavailable, will fallback to pubkey_prefix and if we don't have that then "unknown"
	*/
	const string& hostmask = _hostmask;

	const bool& is_away = _is_away;

	const int64 time_added = time(NULL);
	const int64& last_seen = _last_seen;
	int last_hops = UNKNOWN_HOPS;
	void updateSeen(int hops);
	const int64& last_action_time = _last_action_time; // last time the user has actually done something like message in a channel or DM

	void updateMeshCoreNick(const string& nick);
	void updateMeshCorePubKey(const string& pubkey);
	void updateMeshCorePubKeyPrefix(const string& pubkey_prefix);

	bool isOurNode();
	bool hadRecentAction();
	void onAction();
	bool shouldExpireUser();
	void markAway();
};
extern map<MeshCoreUser*, shared_ptr<MeshCoreUser>> users;

class MeshCoreChannelUser {
public:
	shared_ptr<MeshCoreUser> user;
	bool has_voice = false;
	int64 last_message_time = 0;

	MeshCoreChannelUser(shared_ptr<MeshCoreUser>& puser) {
		user = puser;
	}

	bool hasSpokenRecently();
	bool shouldPartUser();
};

class MeshCoreChannel {
private:
	char _meshcore_name[MESHCORE_MAX_CHAN_LEN+1];
	char _irc_name[IRC_MAX_CHAN_LEN+1]; // IRC allows more than 32 digits, but we're limited to the MC limit

public:
	int meshcore_index = -1;
	const char* const meshcore_name = _meshcore_name;
	bool is_private = false;
	const char* const irc_name = _irc_name;

	list<shared_ptr<MeshCoreChannelUser>> users;

	bool isUserInChannel(const string& irc_nick) {
		for (auto& x : users) {
			if (!stricmp(x->user->irc_nick, irc_nick.c_str())) {
				return true;
			}
		}
		return false;
	}
	bool getUserInChannel(const string& irc_nick, shared_ptr<MeshCoreChannelUser>& u) {
		for (auto& x : users) {
			if (!stricmp(x->user->irc_nick, irc_nick.c_str())) {
				u = x;
				return true;
			}
		}
		return false;
	}

	bool isUserInChannel(const shared_ptr<MeshCoreUser>& user) {
		for (auto& x : users) {
			if (x->user.get() == user.get()) {
				return true;
			}
		}
		return false;
	}
	bool getUserInChannel(const shared_ptr<MeshCoreUser>& user, shared_ptr<MeshCoreChannelUser>& u) {
		for (auto& x : users) {
			if (x->user.get() == user.get()) {
				u = x;
				return true;
			}
		}
		return false;
	}


	void addUserToChannel(shared_ptr<MeshCoreUser>& user);
	void partUserFromChannel(shared_ptr<MeshCoreUser>& user, bool send_part_notices, bool remove_from_user_list);
	void onReceiveMessage(shared_ptr<MeshCoreUser>& user, const char* text, MESHCORE_TEXT_TYPES txt_type);

	void setNameFromMeshCore(const string& name);
	void sendNamesTo(Client* c);
	void sendPostJoinNotices(Client* c);

	string getChannelMode() {
		string ret = "+nt";
		if (is_private) {
			ret += "s";
		}
		return ret;
	}


	void handleIdleUsers();
	bool hasSpokenRecently(const string& irc_nick);
};
extern map<int, shared_ptr<MeshCoreChannel>> chans;

enum CLIENT_STATE {
	CS_DROP_AFTER_SENT, // done, but finish sending the send buffer before disconnecting
	CS_DROP, // done, client disconnected
	CS_AUTH, // authenticating (NICK/USER)
	CS_CONNECTED
};

class Client {
private:
	void handleIncoming();
	bool handleIncomingAuthenticating(char* cmd, char* parms[], int maxparms);
	bool handleIncomingConnected(char* cmd, char* parms[], int maxparms);
	void handleIncomingAlways(char* cmd, char* parms[], int maxparms);

	void welcomeUser();

	void sendErrorNeedMoreParams(const string& cmd);
	//void sendServerPrefixed(const vector<string>& parms);

	CLIENT_STATE _state = CS_AUTH;
	void sendLine(const string& line);
	FILE* log_fp = NULL;
public:
	uint64 id = 0;
	const CLIENT_STATE& state = _state;
	DSL_SOCKET_LIBEVENT* sock = NULL;
	shared_ptr<MeshCoreUser> user;
	int64 timeConnected = 0;
	int64 lastRecv = 0;

	DSL_BUFFER sendbuf = { 0 };
	DSL_BUFFER recvbuf = { 0 };
	void onRecv();

	bool auth_had_nick = false;
	bool auth_had_user = false;

	void log(const char* fmt, ...);

	Client(DSL_SOCKET_LIBEVENT* psock, shared_ptr<MeshCoreUser>& puser) {
		sock = psock;
		user = puser;

		socks.SetNonBlocking(sock->sock);
		socks.SetKeepAlive(sock->sock);
		ev->EnableRecv(sock);

		timeConnected = lastRecv = time(NULL);
		buffer_init(&sendbuf);
		buffer_init(&recvbuf);
	}

	~Client() {
		if (log_fp != NULL) {
			fclose(log_fp);
			log_fp = NULL;
		}
		if (sock != NULL) {
			ev->Remove(sock, true);
			sock = NULL;
		}

		buffer_free(&sendbuf);
		buffer_free(&recvbuf);
	}

	void SendLine(const vector<string>& parms);
	void SendServerReply(const string& numeric, const vector<string>& parms, const string& to = ""); // if 'to' is empty, use our node's IRC nick
	void SendServerNotice(const string& msg, const string& to = "");
	void SendPing();
	void SendAwayNoticeFor(shared_ptr<MeshCoreUser>& u) {
		SendServerReply(RPL_AWAY, { u->irc_nick, "Is away due to inactivity" });
	}
	//void SendAwayNoticeFor(shared_ptr<MeshCoreUser>& u);

	void SetClientDropped(bool finish_sending) {
		_state = finish_sending ? CS_DROP_AFTER_SENT : CS_DROP;
		if (sock != NULL) {
			ev->DisableRecv(sock);
			if (!finish_sending) {
				ev->DisableWrite(sock);
			}
		}
	}
};
extern list<shared_ptr<Client>> clients;
//extern DB_SQLite * db;

bool SendLineToAllAuthenticatedClients(const vector<string>& parms);
bool SendServerNoticeToAllAuthenticatedClients(const string& str);


class MQTT_IRC_Client : public MeshCore_MQTT_Client {
public:
	MQTT_IRC_Client(const string& phost, uint16 pport, const string& pusername, const string& ppasspord, const string& ptopic_prefix) : MeshCore_MQTT_Client(phost, pport, pusername, ppasspord, ptopic_prefix) {
	}

	void printLog(const string& str); // the default implementation is puts()
	void onSend(const string& topic, const char * payload, int payloadlen);
	void onRecv(const string& topic, const char * payload, int payloadlen);
	void onError(const string& errmsg, const UniValue& payload);
	void onContact(const string& adv_name, const string& pubkey, int type, int hops, const UniValue& payload);
	void onContactsComplete(const UniValue& payload);
	void onAdvertisement(const string& pubkey, const UniValue& payload);
	void onSelfInfo(const string& adv_name, const string& pubkey, const UniValue& payload);
	void onChanInfo(int channel_idx, const string& channelName, bool is_private, const UniValue& payload);
	void onChanInfoComplete();
	void onChannelMessage(int channel_idx, const string& from, const string& text, MESHCORE_TEXT_TYPES txt_type, int hops, const UniValue& payload);
	void onChannelData(int channel_idx, uint16 data_type, uint8* data, size_t data_len, int hops, const UniValue& payload);
	void onDirectMessage(const string& pubkey_prefix, const string& text, MESHCORE_TEXT_TYPES txt_type, int hops, const UniValue& payload);
	void onDirectMessageOnMQTT(const string& destination, const string& text, MESHCORE_TEXT_TYPES txt_type);
	void onDirectData(const string& pubkey_prefix, uint8* data, size_t data_len, int hops, const UniValue& payload);
	void onStatusResponse(const string& pubkey_prefix, const UniValue& payload);
};

class CONFIG {
public:
	bool shutdown_now = false;

	shared_ptr<MeshCoreUser> self_user;

	bool waitingForInitialState = true;
	int64 lastReceivedChannels = 0;
	int64 lastReceivedContacts = 0;
	int64 lastReceivedSelfInfo = 0;

	vector<DSL_SOCKET_LIBEVENT*> timers;

	struct {
		string server_hostname = "virtual.meshcore.irc";
		string network_name = "MeshCore IRC Server";
		string bind_ip = "0.0.0.0";
		uint16 listen_port = 6667;
		DSL_SOCKET_LIBEVENT* lSock = NULL;

		bool allow_nick_change = false; // not actually implemented
		bool auto_voice_idle_users = true; // if users have messages within the last 'idle_users_after' seconds, give them +v in channels so people know they were there recently

		int64 idle_users_after = 7200; // if a nick hasn't messaged in 2 hours, consider them idle.
		int64 part_users_after = 86400; // if a nick hasn't messaged in 24 hours, remove them from the channel.		
		int64 expire_users_after = 86400 * 3; // if a nick hasn't been seen (messages, adverts, etc.) in 3 days, remove them.

#ifdef DEBUG
		bool log_to_file = true;
#else
		bool log_to_file = false;
#endif
		bool log_to_console = false;
	} irc;

	struct {
		string host = "127.0.0.1";
		uint16 port = 1883;
		string username;
		string password;
		string topic_prefix = "meshmqtt";

		MQTT_IRC_Client * client = NULL;
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

bool db_init();
void db_quit();
bool AddOfflineMessage(const vector<string>& parms);
void SendOfflineMessages(Client * cli);

string get_sanitized_nick(const string& str);
string get_channel_name_from_meshcore(const string& name);
int64 parse_duration_str(const string& str);
//string get_channel_key_from_name(const string& channel_name);

void mosquitto_disconnect();
void mosquitto_loop();

void add_channel(int channel_idx, const string& meshcore_name, bool is_private);
bool get_channel_by_meshcore_index(int channel_idx, shared_ptr<MeshCoreChannel>& chan);
bool get_channel_by_irc_name(const string& channel_name, shared_ptr<MeshCoreChannel>& chan);

#define MESHCORE_CONTACT_FLAG_FAVORITE 0x01

bool get_user_by_meshcore_name(const string& name, shared_ptr<MeshCoreUser>& u);
bool get_user_by_irc_nick(const string& irc_nick, shared_ptr<MeshCoreUser>& u);
bool get_user_by_hostmask(const string& hostmask, shared_ptr<MeshCoreUser>& u);
bool get_user_by_pubkey(const string& pubkey, shared_ptr<MeshCoreUser>& u);
bool get_user_by_pubkey_prefix(const string& pubkey_prefix, shared_ptr<MeshCoreUser>& u);

bool add_user(const string& name, const string& pubkey, const string& pubkey_prefix, uint8 flags, int32 hops, shared_ptr<MeshCoreUser> * out = NULL);
//void remove_user(shared_ptr<MeshCoreUser> u);
//void remove_old_users();

void SendJoinNoticesFor(MeshCoreUser* c, const string& chan);
void SendPartNoticesFor(MeshCoreUser* c, const string& chan);
void SendQuitNoticesFor(MeshCoreUser* c, const string& reason);
void SendNickChangeNoticesFor(MeshCoreUser* c, const string& new_nick);
void SendUserModeNoticesFor(const string& chan, const string& mode, const string& irc_nick);

