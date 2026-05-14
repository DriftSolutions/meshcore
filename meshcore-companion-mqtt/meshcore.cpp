/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

#define COMPANION_FRAME_START 0x3E
#define COMPANION_OUTGOING_FRAME_START 0x3C
#define COMPANION_FRAME_HEADER_SIZE 3
#define COMPANION_MAX_FRAME_SIZE 300

list<shared_ptr<MeshCoreCommand>> outgoing_commands;
shared_ptr<MeshCoreCommand> current_outgoing_command;

void mesh_log(const char* fmt, ...) {
	if (!config.meshcore.log_to_console && !config.meshcore.log_to_file) {
		return;
	}

	va_list va;
	va_start(va, fmt);

	if (config.meshcore.log_to_console) {
		char* tmp = dsl_vmprintf(fmt, va);
		printf("[meshcore] %s", tmp);
		dsl_free(tmp);
	}

	if (config.meshcore.log_to_file) {
		if (config.meshcore.log_fp == NULL) {
			if (access("./logs", 0) != 0) {
				dsl_mkdir("./logs", 0700);
			}
			string fn = "./logs/companion.meshcore.log";
			config.meshcore.log_fp = fopen(fn.c_str(), "wb");
			if (config.meshcore.log_fp != NULL) {
				printf("[meshcore] Opened %s for output...\n", fn.c_str());
			} else {
				printf("[meshcore] Error opening %s for output: %s\n", fn.c_str(), strerror(errno));
			}
		}
		if (config.meshcore.log_fp != NULL) {
			vfprintf(config.meshcore.log_fp, fmt, va);
		}
	}

	va_end(va);
}

void reset_outgoing() {
	current_outgoing_command.reset();
	outgoing_commands.clear();
	buffer_clear(&config.sendbuf);
}

void queue_packet_app_start() {
	auto pack = make_shared<MeshCoreCommand>();
	_PACKET_APP_START p;
	pack->data.assign((char *)&p, sizeof(p));
	//pack->data.append("meshcore-companion-mqtt");
	pack->data.append("mccli");
	pack->expected_responses = { PACKET_SELF_INFO };
	//pack->is_critical_command = true;
	outgoing_commands.push_back(pack);
}

void queue_packet_device_query() {
	auto pack = make_shared<MeshCoreCommand>();
	pack->data.assign("\x16\x03", 2);
	pack->expected_responses = { PACKET_DEVICE_INFO };
	//pack->is_critical_command = true;
	outgoing_commands.push_back(pack);
}

void queue_packet_get_channel_info(uint8 index) {
	auto pack = make_shared<MeshCoreCommand>();
	pack->data.assign(1, '\x1F');
	pack->data.append(1, (char)index);
	pack->expected_responses = { PACKET_CHANNEL_INFO };
	outgoing_commands.push_back(pack);
}

void queue_packets_get_channels() {
	for (uint8 i = 0; i <= MESHCORE_HIGHEST_CHANNEL; i++) {
		queue_packet_get_channel_info(i);
	}
}

void queue_packet_get_message() {
	auto pack = make_shared<MeshCoreCommand>();
	pack->data.assign(1, '\x0A');
	pack->expected_responses = { PACKET_CHANNEL_MSG_RECV, PACKET_CHANNEL_MSG_RECV_V3, PACKET_CONTACT_MSG_RECV, PACKET_CONTACT_MSG_RECV_V3, PACKET_NO_MORE_MSGS };
	outgoing_commands.push_back(pack);
}

void queue_packet_set_time(int64 ts) {
	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x06);
	if (ts <= 0) {
		ts = time(NULL);
	}
	buffer_append_int<uint32>(&buf, Get_ULE32((uint32)ts));
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);
	pack->expected_responses = { PACKET_OK };
	outgoing_commands.push_back(pack);
}

void queue_packet_get_contacts(uint32 last_mod = 0) {
	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x04);
	if (last_mod) {
		buffer_append_int<uint32>(&buf, Get_ULE32(last_mod));
	}
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);	
	pack->expected_responses = { PACKET_CONTACT_END };
	outgoing_commands.push_back(pack);	
}

void update_contacts(bool force_get_all) {
	if (force_get_all || state.lastContactModTime < 300) {
		queue_packet_get_contacts();
		state.lastContactsFullUpdate = state.lastContactsPartialUpdate = time(NULL);
	} else {
		queue_packet_get_contacts(state.lastContactModTime - 300);
		state.lastContactsPartialUpdate = time(NULL);
	}
}

void MeshCoreCommandChannelMessage::onError(uint8 code) {
	if (attempt < 3) {
		auto pack = make_shared<MeshCoreCommandChannelMessage>(*this);
		pack->attempt++;
		PrintData(stdout, (const uint8*)pack->data.c_str(), pack->data.length());
		outgoing_commands.push_front(pack);
		printf("Retrying channel message, retry number %u ...\n", pack->attempt);
	} else {
		printf("Giving on channel message, retry limit hit...\n");
	}
}

void queue_packet_send_channel_msg(uint8 channel_idx, const string& str, MESHCORE_TEXT_TYPES txt_type) {
	auto pack = make_shared<MeshCoreCommandChannelMessage>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x03);
	buffer_append_int<uint8>(&buf, txt_type);
	buffer_append_int<uint8>(&buf, channel_idx);
	buffer_append_int<uint32>(&buf, Get_ULE32((uint32)time(NULL)));
	buffer_append(&buf, str.c_str(), str.length());
	pack->data = buffer_as_string(&buf);
	PrintData(stdout, buf.udata, buf.len);
	buffer_free(&buf);
	pack->is_message = true;
	pack->expected_responses = { PACKET_OK };
	outgoing_commands.push_back(pack);
}

void MeshCoreCommandDirectMessage::onError(uint8 code) {
	if (attempt < 3) {
		auto pack = make_shared<MeshCoreCommandDirectMessage>(*this);
		pack->attempt++;
		pack->data[2] = pack->attempt;
		PrintData(stdout, (const uint8 *)pack->data.c_str(), pack->data.length());
		outgoing_commands.push_front(pack);
		printf("Retrying direct message, retry number %u ...\n", pack->attempt);
	} else {
		printf("Giving on direct message, retry limit hit...\n");
	}
}

bool is_valid_destination(const string& str) {
	if (str.length() != MESHCORE_PUBKEY_LEN && str.length() != MESHCORE_PUBKEY_PREFIX_LEN) {
		return false;
	}
	return (strspn(str.c_str(), "0123456789abcdef") == str.length());
}

void queue_packet_send_direct_msg(const string& pubkey_or_prefix, const string& str, uint8 attempt, MESHCORE_TEXT_TYPES txt_type) {
	static uint8 zero_prefix[MESHCORE_PUBKEY_PREFIX_LEN / 2] = { 0 };
	uint8 prefix[MESHCORE_PUBKEY_PREFIX_LEN / 2];
	if (!is_valid_destination(pubkey_or_prefix)) {
		printf("queue_packet_send_direct_msg(): Invalid destion: %s\n", pubkey_or_prefix.c_str());
		return;
	}
	if (!hex2bin(pubkey_or_prefix.c_str(), MESHCORE_PUBKEY_PREFIX_LEN, prefix, sizeof(prefix)) || !memcmp(prefix, zero_prefix, sizeof(prefix))) {
		printf("queue_packet_send_direct_msg(): Error running hex2bin on %s !\n", pubkey_or_prefix.c_str());
		return;
	}

	auto pack = make_shared<MeshCoreCommandDirectMessage>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x02);
	buffer_append_int<uint8>(&buf, txt_type);
	buffer_append_int<uint8>(&buf, attempt);
	buffer_append_int<uint32>(&buf, Get_ULE32((uint32)time(NULL)));
	buffer_append(&buf, (const char *)prefix, sizeof(prefix));

	size_t len = min(str.length(), (size_t)160);
	buffer_append(&buf, str.c_str(), len);

	pack->data = buffer_as_string(&buf);
	PrintData(stdout, buf.udata, buf.len);
	buffer_free(&buf);
	
	pack->expected_responses = { PACKET_MSG_SENT };
	pack->is_message = true;
	outgoing_commands.push_back(pack);
}

/*
CMD_SEND_TXT_MSG {
  code: byte,   // constant 2
  txt_type: byte,     // one of TXT_TYPE_*  (0 = plain)
  attempt: byte,     // values: 0..3 (attempt number)
  sender_timestamp: uint32,
  pubkey_prefix: bytes(6),     // just first 6 bytes of recipient contact's public key
  text: varchar    // remainder of frame  (max length: 160 bytes)
}
*/

string trim_nulls(const string& str) {
	size_t n = str.find((char)0, 0);
	if (n != str.npos) {
		return str.substr(0, n);
	}
	return str;
}

string trim_nulls(const char * str, size_t len) {
	return trim_nulls(string(str, len));
}

const char* GetMeshCoreErrorString(uint8 code) {
	switch (code) {
		case 0x00: return "Generic error (no specific code)";
		case 0x01: return "Invalid command";
		case 0x02: return "Invalid parameter";
		case 0x03: return "Channel not found";
		case 0x04: return "Channel already exists";
		case 0x05: return "Channel index out of range";
		case 0x06: return "Secret mismatch";
		case 0x07: return "Message too long";
		case 0x08: return "Device busy";
		case 0x09: return "Not enough storage";
		default:   return "Unknown error";
	}
}

void handleIncomingPacket(uint8* pack, uint16 packlen) {
	const uint8& packet_type = pack[0];

	if (packet_type == PACKET_LOG_DATA) {
		//ignore
		return;
	}

	if (packet_type == PACKET_ADVERTISEMENT) {
		if (packlen >= 33) {
			string pubkey = bin2hex(pack + 1, 32);
			shared_ptr<MeshCoreUser> u;
			if (get_user_by_pubkey(pubkey, u)) {
				u->last_seen = time(NULL);
			}
			UniValue obj(UniValue::VOBJ);
			obj.pushKV("public_key", pubkey);
			mqtt_send(config.mqtt.topic_prefix + "/advertisement", obj, false);
		}
		return;
	}

	if (packet_type == PACKET_CONTACT_DELETED) {
		// We don't really need to delete it from our list just because the node did, since it's more memory limited
		/*
		if (packlen >= 33) {
			string pubkey = bin2hex(pack + 1, 32);

			del_user_by_pubkey(pubkey);

			UniValue obj(UniValue::VOBJ);
			obj.pushKV("public_key", pubkey);
			mqtt_send(config.mqtt.topic_prefix + "/contact_deleted", obj, false);
		}
		*/
		return;
	}

	if (packet_type == PACKET_MESSAGES_WAITING) {
		queue_packet_get_message();
		return;
	}

	mesh_log("Received packet (%u bytes):\n", packlen);
	if (config.meshcore.log_fp != NULL) {
		PrintData(config.meshcore.log_fp, pack, packlen);
	}

	if (packet_type == PACKET_ERROR) {
		uint8 code = 0;
		if (packlen >= 2) {
			code = pack[1];
			printf("Received error reply with error %s (0x%02X) to last command!\n", GetMeshCoreErrorString(code), code);
			mesh_log("Received error reply with error %s (0x%02X) to last command!\n", GetMeshCoreErrorString(code), code);
		} else {
			printf("Received error reply to last command!\n");
			mesh_log("Received error reply to last command!\n");
		}

		if (current_outgoing_command) {
			auto& cur = current_outgoing_command;
			cur->onError(code);
			current_outgoing_command.reset();
		}

		return;
	}

	if (current_outgoing_command) {
		auto& cur = current_outgoing_command;
		if (cur->expected_responses.find(packet_type) != cur->expected_responses.end()) {
			mesh_log("Received response %02x for current command %02x\n", packet_type, cur->getType());
			current_outgoing_command.reset();
		}
	}

	if (packet_type == PACKET_SELF_INFO && packlen > sizeof(_PACKET_SELF_INFO)) {
		_PACKET_SELF_INFO* si = (_PACKET_SELF_INFO*)pack;
		string name((char*)pack + sizeof(_PACKET_SELF_INFO), packlen - sizeof(_PACKET_SELF_INFO));
		UniValue obj(UniValue::VOBJ);
		obj.pushKV("name", trim_nulls(name));
		obj.pushKV("public_key", bin2hex(si->public_key, sizeof(si->public_key)));
		obj.pushKV("advertisement_type", si->advertisement_type);
		obj.pushKV("tx_power", si->tx_power);
		obj.pushKV("max_tx_power", si->max_tx_power);
		obj.pushKV("latitude", double(Get_SLE32(si->latitude)) / 1000000.0f);
		obj.pushKV("longitude", double(Get_SLE32(si->longitude)) / 1000000.0f);
		obj.pushKV("multi_acks", si->multi_acks);
		obj.pushKV("advertisement_location_policy", si->advertisement_location_policy);
		obj.pushKV("telemetry_mode", si->telemetry_mode);
		obj.pushKV("manual_add_contacts", si->manual_add_contacts);
		obj.pushKV("radio_frequency", (double)Get_ULE32(si->radio_frequency) / 1000.0);
		obj.pushKV("radio_bandwidth", (double)Get_ULE32(si->radio_bandwidth) / 1000.0);
		obj.pushKV("radio_spreading_factor", si->radio_spreading_factor);
		obj.pushKV("radio_coding_rate", si->radio_coding_rate);
		state.self_info = obj.write();
		mqtt_send_self_info();
		return;
	}

	if (packet_type == PACKET_DEVICE_INFO && packlen >= sizeof(_PACKET_DEVICE_INFO)) {
		_PACKET_DEVICE_INFO* di = (_PACKET_DEVICE_INFO*)pack;
		UniValue obj(UniValue::VOBJ);
		obj.pushKV("firmware_version", di->firmware_version);
		obj.pushKV("max_contacts", (int)di->max_contacts_raw * 2);
		obj.pushKV("max_channels", di->max_channels);
		obj.pushKV("ble_pin", (int64)Get_ULE32(di->ble_pin));
		obj.pushKV("firmware_build", trim_nulls(di->firmware_build, sizeof(di->firmware_build)));
		obj.pushKV("model", trim_nulls(di->model, sizeof(di->model)));
		obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
		obj.pushKV("client_repeat", di->client_repeat);
		obj.pushKV("path_hash_mode", di->path_hash_mode);
		state.device_info = obj.write();
		mqtt_send_device_info();
		return;
	}

	if (packet_type == PACKET_CHANNEL_INFO && packlen >= sizeof(_PACKET_CHANNEL_INFO)) {
		_PACKET_CHANNEL_INFO* ci = (_PACKET_CHANNEL_INFO*)pack;

		add_or_update_channel(ci);
		mqtt_send_channel(ci->channel_index);

		return;
	}

	if (packet_type == PACKET_OK) {
		if (packlen >= 5) {
			uint32 value = Get_ULE32(*(uint32*)(pack + 1));
			mesh_log("Received OK reply with code %u to last command.\n", value);
		} else {
			mesh_log("Received OK reply to last command.\n");
		}
		return;
	}

	if (packet_type == PACKET_ACK) {
		_PACKET_ACK* ack = (_PACKET_ACK*)pack;
		return;
	}

	if (packet_type == PACKET_CONTACT_START) {
		printf("Receiving contacts list...\n");
		if (packlen >= 5) {
			uint32 count = Get_ULE32(*(uint32*)(pack + 1));
			printf("Receiving contacts list... (%u contacts)\n", count);
		} else {
			printf("Receiving contacts list...\n");
		}
		return;
	}

	if (packet_type == PACKET_CONTACT && packlen >= sizeof(_PACKET_CONTACT)) {
		_PACKET_CONTACT* c = (_PACKET_CONTACT*)pack;
		add_or_update_user(c);
		return;
	}

	if (packet_type == PACKET_CONTACT_END) {
		printf("End of contacts list.\n");
		if (packlen >= 5) {
			uint32 lastmod = Get_ULE32(*(uint32*)(pack + 1));
			time_t ts = (time_t)lastmod;
			struct tm tm;
			localtime_r(&ts, &tm);
#ifdef DEBUG
			printf("Last mod time: %lld -> %s", (int64)ts, ctime(&ts));
#endif			
			state.lastContactModTime = (uint32)ts;
		}

		mqtt_send_contacts();
		//state.lastContactListReceived = time(NULL);
		return;
	}

	if (packet_type == PACKET_MSG_SENT) {
		_PACKET_MSG_SENT* ms = (_PACKET_MSG_SENT*)pack;
		printf("Message send %u acknowledged.\n", Get_ULE32(ms->tag));
		return;
	}

	if (packet_type == PACKET_CURRENT_TIME) {
	}

	if (packet_type == PACKET_BATTERY) {
	}

	if (packet_type == PACKET_CONTACT_MSG_RECV && packlen >= sizeof(PACKET_CONTACT_MSG_RECV)) {
		_PACKET_CONTACT_MSG_RECV* msg = (_PACKET_CONTACT_MSG_RECV*)pack;
		UniValue obj(UniValue::VOBJ);
		string pubkey_prefix = bin2hex(msg->public_key_prefix, sizeof(msg->public_key_prefix));
		shared_ptr<MeshCoreUser> u;
		if (get_user_by_pubkey_prefix(pubkey_prefix, u)) {
			obj.pushKV("public_key", u->pubkey);
		}
		obj.pushKV("public_key_prefix", pubkey_prefix);
		obj.pushKV("txt_type", (uint8)msg->text_type);
		obj.pushKV("timestamp", (int64)Get_ULE32(msg->timestamp));
		obj.pushKV("path_length", msg->path_length);
		char* text;
		size_t header_len = sizeof(_PACKET_CONTACT_MSG_RECV);
		if (msg->text_type == 2) { // signed text			
			text = (char *)pack + sizeof(_PACKET_CONTACT_MSG_RECV);			
		} else {
			text = (char*)&msg->signature[0];
			header_len -= 4;
		}
		obj.pushKV("message", trim_nulls(text, packlen - header_len));
		//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
		mqtt_send(mprintf("%s/message/direct/%s", config.mqtt.topic_prefix.c_str(), pubkey_prefix.c_str()), obj, false);
		return;
	}

	if (packet_type == PACKET_CONTACT_MSG_RECV_V3 && packlen >= sizeof(PACKET_CONTACT_MSG_RECV_V3)) {
		_PACKET_CONTACT_MSG_RECV_V3* msg = (_PACKET_CONTACT_MSG_RECV_V3*)pack;
		UniValue obj(UniValue::VOBJ);
		string pubkey_prefix = bin2hex(msg->public_key_prefix, sizeof(msg->public_key_prefix));
		shared_ptr<MeshCoreUser> u;
		if (get_user_by_pubkey_prefix(pubkey_prefix, u)) {
			obj.pushKV("public_key", u->pubkey);
		}
		obj.pushKV("public_key_prefix", pubkey_prefix);
		obj.pushKV("txt_type", (uint8)msg->text_type);
		obj.pushKV("timestamp", (int64)Get_ULE32(msg->timestamp));
		obj.pushKV("path_length", msg->path_length);
		char* text;
		size_t header_len = sizeof(_PACKET_CONTACT_MSG_RECV_V3);
		if (msg->text_type == 2) { // signed text			
			text = (char*)pack + sizeof(_PACKET_CONTACT_MSG_RECV_V3);
		} else {
			text = (char*)&msg->signature[0];
			header_len -= 4;
		}
		obj.pushKV("message", trim_nulls(text, packlen - header_len));
		//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
		mqtt_send(mprintf("%s/message/direct/%s", config.mqtt.topic_prefix.c_str(), pubkey_prefix.c_str()), obj, false);
		return;
	}

	if (packet_type == PACKET_CHANNEL_MSG_RECV && packlen >= sizeof(PACKET_CHANNEL_MSG_RECV)) {
		_PACKET_CHANNEL_MSG_RECV* msg = (_PACKET_CHANNEL_MSG_RECV*)pack;

		UniValue obj(UniValue::VOBJ);
		obj.pushKV("channel_index", msg->channel_index);
		obj.pushKV("path_length", msg->path_length);
		obj.pushKV("txt_type", (uint8)msg->text_type);
		obj.pushKV("timestamp", (int64)Get_ULE32(msg->timestamp));

		char* text = (char*)pack + sizeof(_PACKET_CHANNEL_MSG_RECV);
		string str = trim_nulls(text, packlen - sizeof(_PACKET_CHANNEL_MSG_RECV));
		const char* p = strstr(str.c_str(), ": ");
		if (p != NULL) {
			obj.pushKV("from", str.substr(0, p - str.c_str()));
			obj.pushKV("message", p + 2);
			//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
			mqtt_send(mprintf("%s/message/channel/%u", config.mqtt.topic_prefix.c_str(), msg->channel_index), obj, false);
		} else {
			printf("Warning: unrecognized incoming channel message: %s\n", str.c_str());
		}
		return;
	}

	if (packet_type == PACKET_CHANNEL_MSG_RECV_V3 && packlen >= sizeof(PACKET_CHANNEL_MSG_RECV_V3)) {
		_PACKET_CHANNEL_MSG_RECV_V3* msg = (_PACKET_CHANNEL_MSG_RECV_V3*)pack;

		UniValue obj(UniValue::VOBJ);
		obj.pushKV("channel_index", msg->channel_index);
		obj.pushKV("path_length", msg->path_length);
		obj.pushKV("txt_type", (uint8)msg->text_type);
		obj.pushKV("timestamp", (int64)Get_ULE32(msg->timestamp));

		char* text = (char*)pack + sizeof(_PACKET_CHANNEL_MSG_RECV_V3);
		string str = trim_nulls(text, packlen - sizeof(_PACKET_CHANNEL_MSG_RECV_V3));
		const char* p = strstr(str.c_str(), ": ");
		if (p != NULL) {
			obj.pushKV("from", str.substr(0, p-str.c_str()));
			obj.pushKV("message", p + 2);
			//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
			mqtt_send(mprintf("%s/message/channel/%u", config.mqtt.topic_prefix.c_str(), msg->channel_index), obj, false);
		} else {
			printf("Warning: unrecognized incoming channel message: %s\n", str.c_str());
		}
		return;
	}

	if (packet_type == PACKET_NO_MORE_MSGS) {
		state.lastMessageCheck = time(NULL);
		return;
	}

	int x = 1;
}


void handleIncomingPackets() {
	DSL_BUFFER& recvbuf = config.recvbuf;

	while (recvbuf.len > 0) {
		if (recvbuf.udata == NULL || recvbuf.len <= 0) {
			break;
		}

		uint8* begin = (uint8*)memchr(recvbuf.udata, COMPANION_FRAME_START, recvbuf.len);
		if (begin == NULL) {
			// no frame start marker, clear the buffer
			buffer_clear(&recvbuf);
			break;
		}

		size_t offset = (begin - recvbuf.udata);
		int64 datalen = recvbuf.len - offset;
		if (datalen < COMPANION_FRAME_HEADER_SIZE) {
			//don't yet have a whole header
			break;
		}

		uint16 packlen = Get_ULE16(*(begin + 1));
		if (packlen > COMPANION_MAX_FRAME_SIZE) {
			//frame too long, probably junk data
			buffer_remove_front(&recvbuf, offset + 1);
		}

		if (datalen < packlen + COMPANION_FRAME_HEADER_SIZE) {
			//don't yet have all the data yet
			break;
		}

		handleIncomingPacket(begin + COMPANION_FRAME_HEADER_SIZE, packlen);

		buffer_remove_front(&recvbuf, COMPANION_FRAME_HEADER_SIZE + packlen);
	}
}

void meshcore_work() {
	static int64 lastDriverOpen = 0;

	if (config.io_driver->IsOpen()) {
		static int64 lastMessageCheckReq = 0;

		if (time(NULL) - state.lastMessageCheck >= 60 && time(NULL) - lastMessageCheckReq >= 30) {
			queue_packet_get_message();
			lastMessageCheckReq = time(NULL);
		}

		if (time(NULL) - lastMessageCheckReq >= 30) {
			if (time(NULL) - state.lastContactsFullUpdate >= 3600) {
				update_contacts(true);
				lastMessageCheckReq = time(NULL);
			} else if (time(NULL) - state.lastContactsPartialUpdate >= 60) {
				update_contacts(false);
				lastMessageCheckReq = time(NULL);
			}
		}

		if (current_outgoing_command.get() == NULL && outgoing_commands.size()) {
			uint64 ticks = GetTickCount64();
			bool timeToSendMessage = (state.nextMessageTime == 0 || ticks > state.nextMessageTime);
			for (auto x = outgoing_commands.begin(); x != outgoing_commands.end(); x++) {
				auto& cur = *x;
				if (cur->is_message && config.meshcore.delayBetweenMessages) {
					if (timeToSendMessage) {
						current_outgoing_command = cur;
						outgoing_commands.erase(x);			
						state.nextMessageTime = GetTickCount64() + config.meshcore.delayBetweenMessages;
						break;
#ifdef DEBUG
					} else {
						printf("Delaying send of message...\n");
#endif
					}
				} else {
					current_outgoing_command = cur;
					outgoing_commands.erase(x);
					break;
				}
			}
			if (current_outgoing_command) {
				auto& cur = current_outgoing_command;
				if (cur->data.length() > COMPANION_MAX_FRAME_SIZE) {
					current_outgoing_command.reset();
				}

				cur->time_limit = GetTickCount64() + 5000;
				buffer_append_int<uint8>(&config.sendbuf, COMPANION_OUTGOING_FRAME_START);
				uint16 tmp = Get_ULE16((uint16)cur->data.length());
				buffer_append_int<uint16>(&config.sendbuf, tmp);

				buffer_append(&config.sendbuf, cur->data.c_str(), cur->data.length());
				mesh_log("Writing command with %zu bytes...\n", cur->data.length());
				if (config.meshcore.log_fp != NULL) {
					PrintData(config.meshcore.log_fp, (const uint8 *)cur->data.c_str(), cur->data.length());
				}
			}
		} else if (current_outgoing_command) {
			auto& cur = current_outgoing_command;
			if (GetTickCount64() > cur->time_limit) {
				//give up on this command :(
				printf("Outgoing command %02x timed out while waiting for a reply!\n", cur->getType());
				current_outgoing_command.reset();
			}
		}

		if (config.sendbuf.len > 0) {
			int n = config.io_driver->Write(config.sendbuf.udata, (int)config.sendbuf.len);
			if (n > 0) {
				buffer_remove_front(&config.sendbuf, n);
			} else if (n < 0) {
				printf("Error writing to I/O driver!\n");
				config.io_driver->Close();
				return;
			}
		}

		static uint8 buf[1024];
		int n = config.io_driver->Read(buf, sizeof(buf));
		if (n > 0) {
			buffer_append(&config.recvbuf, (const char*)buf, n);
			handleIncomingPackets();
		} else if (n == 0) {
			safe_sleep(1, true);
		} else if (n < 0) {
			printf("Error reading from I/O driver!\n");
			config.io_driver->Close();
		}
	} else {
		// clear the current command and queue
		reset_outgoing();
		if (time(NULL) - lastDriverOpen >= 5) {
			lastDriverOpen = time(NULL);
			if (config.io_driver->Open(config.meshcore.device)) {
				state.reset();
				queue_packet_app_start();
				queue_packet_set_time(0);
				queue_packet_device_query();
				queue_packets_get_channels();
				update_contacts(true);
				queue_packet_get_message();
			} 
		} else {
			safe_sleep(100, true);
		}
	}
}
