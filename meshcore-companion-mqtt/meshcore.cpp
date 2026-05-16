/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

list<shared_ptr<MeshCoreCommand>> outgoing_commands;
map<uint32, shared_ptr<MeshCoreCommand>> outgoing_messages;
shared_ptr<MeshCoreCommand> current_outgoing_command;

void handleIncomingPackets();

MeshCoreCommandAcknowledged::~MeshCoreCommandAcknowledged() {}

/*
void remove_outgoing_message(uint32 tag) {
	auto x = outgoing_messages.find(tag);
	if (x != outgoing_messages.end()) {
		outgoing_messages.erase(x);
	}
}
*/

void timeout_outgoing_messages() {
	uint64 ticks = GetTickCount64();
	for (auto x = outgoing_messages.begin(); x != outgoing_messages.end();) {
		if (ticks > x->second->time_limit) {
			auto dm = dynamic_cast<MeshCoreCommandAcknowledged*>(x->second.get());
			if (dm != NULL) {
				dm->onTimeOut();
			}
			x = outgoing_messages.erase(x);
		} else {
			x++;
		}
	}
}

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
#ifdef DEBUG
			fflush(config.meshcore.log_fp);
#endif
		}
	}

	va_end(va);
}

void reset_outgoing() {
	current_outgoing_command.reset();
	outgoing_commands.clear();
	outgoing_messages.clear();
	buffer_clear(&config.sendbuf);
}

void queue_packet_app_start() {
	auto pack = make_shared<MeshCoreCommand>();
	_PACKET_APP_START p;
	pack->data.assign((char *)&p, sizeof(p));
	//pack->data.append("meshcore-companion-mqtt");
	pack->data.append("mccli");
	pack->expected_responses = { RESPONSE_CODE_SELF_INFO };
	//pack->is_critical_command = true;
	outgoing_commands.push_back(pack);
}

void queue_packet_device_query() {
	auto pack = make_shared<MeshCoreCommand>();
	pack->data.assign("\x16\x03", 2);
	pack->expected_responses = { RESPONSE_CODE_DEVICE_INFO };
	//pack->is_critical_command = true;
	outgoing_commands.push_back(pack);
}

void queue_packet_battery_info() {
	auto pack = make_shared<MeshCoreCommand>();
	pack->data.assign("\x14", 1);
	pack->expected_responses = { RESPONSE_CODE_BATTERY };
	//pack->is_critical_command = true;
	outgoing_commands.push_back(pack);
}

void queue_packet_get_channel_info(uint8 index) {
	auto pack = make_shared<MeshCoreCommand>();
	pack->data.assign(1, '\x1F');
	pack->data.append(1, (char)index);
	pack->expected_responses = { RESPONSE_CODE_CHANNEL_INFO };
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
	pack->expected_responses = { RESPONSE_CODE_CHANNEL_MSG_RECV, RESPONSE_CODE_CHANNEL_MSG_RECV_V3, RESPONSE_CODE_CONTACT_MSG_RECV, RESPONSE_CODE_CONTACT_MSG_RECV_V3, RESPONSE_CODE_NO_MORE_MSGS };
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
	pack->expected_responses = { RESPONSE_CODE_OK };
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
	pack->expected_responses = { RESPONSE_CODE_CONTACT_START };// , RESPONSE_CODE_CONTACT, RESPONSE_CODE_CONTACT_END
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

void queue_packet_send_channel_msg(uint8 channel_idx, const string& str, MESHCORE_TEXT_TYPES txt_type) {
	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x03);
	buffer_append_int<uint8>(&buf, txt_type);
	buffer_append_int<uint8>(&buf, channel_idx);
	buffer_append_int<uint32>(&buf, Get_ULE32((uint32)time(NULL)));
	buffer_append(&buf, str.c_str(), str.length());
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);
	pack->expected_responses = { RESPONSE_CODE_OK };
	pack->is_message = true;
	outgoing_commands.push_back(pack);
}

void MeshCoreCommandDirectMessage::onTimeOut() {
	if (attempt < 3) {
		auto pack = make_shared<MeshCoreCommandDirectMessage>(*this);
		pack->attempt++;
		pack->data[2] = pack->attempt;
		pack->time_limit = GetTickCount64() + 5000;
		outgoing_commands.push_front(pack);
		printf("[meshcore] Retrying direct message, retry number %u ...\n", pack->attempt);
	} else {
		printf("[meshcore] Giving up on direct message, retry limit hit...\n");
	}
}

void MeshCoreCommandStdRetry::onTimeOut() {
	if (attempt < 3) {
		auto pack = make_shared<MeshCoreCommandStdRetry>(*this);
		pack->attempt++;
		pack->time_limit = GetTickCount64() + 5000;
		outgoing_commands.push_front(pack);
		printf("[meshcore] Retrying command %s: retry number %u ...\n", GetMeshCoreCommandString(getType()).c_str(), pack->attempt);
	} else {
		printf("[meshcore] Giving up on command %s, retry limit hit...\n", GetMeshCoreCommandString(getType()).c_str());
	}
}

void queue_packet_channel_datagram(uint8 channel_idx, uint16 data_type, const uint8* data, size_t data_length) {
	if (data_length > MESHCORE_MAX_CHAN_DATAGRAM_LENGTH) {
		printf("queue_packet_channel_datagram(): datagram too big!\n");
		return;
	}

	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	/*
	buffer_append_int<uint8>(&buf, CMD_SEND_CHANNEL_DATA);
	buffer_append_int<uint16>(&buf, Get_ULE16(data_type));
	buffer_append_int<uint8>(&buf, channel_idx);
	buffer_append(&buf, (const char *)data, data_length);
	*/
	buffer_append_int<uint8>(&buf, CMD_SEND_CHANNEL_DATA);
	buffer_append_int<uint8>(&buf, channel_idx);
	buffer_append_int<uint8>(&buf, 0xFF); // flood
	buffer_append_int<uint16>(&buf, Get_ULE16(data_type));
	buffer_append(&buf, (const char*)data, data_length);
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);
	pack->expected_responses = { RESPONSE_CODE_OK };
	pack->is_message = true;
	outgoing_commands.push_back(pack);
}

bool is_valid_destination(const string& str) {
	if (str.length() != MESHCORE_PUBKEY_LEN && str.length() != MESHCORE_PUBKEY_PREFIX_LEN) {
		return false;
	}
	return (strspn(str.c_str(), "0123456789abcdef") == str.length());
}

bool is_valid_pubkey(const string& str) {
	if (str.length() != MESHCORE_PUBKEY_LEN) {
		return false;
	}
	return (strspn(str.c_str(), "0123456789abcdef") == str.length());
}

void queue_packet_send_direct_msg(const string& pubkey_or_prefix, const string& str, uint8 attempt, MESHCORE_TEXT_TYPES txt_type) {
	static const uint8 zero_prefix[MESHCORE_PUBKEY_PREFIX_LEN / 2] = { 0 };
	uint8 prefix[MESHCORE_PUBKEY_PREFIX_LEN / 2];
	if (!is_valid_destination(pubkey_or_prefix)) {
		printf("queue_packet_send_direct_msg(): Invalid destination: %s\n", pubkey_or_prefix.c_str());
		return;
	}
	if (!hex2bin(pubkey_or_prefix.c_str(), MESHCORE_PUBKEY_PREFIX_LEN, prefix, sizeof(prefix)) || !memcmp(prefix, zero_prefix, sizeof(prefix))) {
		printf("queue_packet_send_direct_msg(): Error running hex2bin on %s !\n", pubkey_or_prefix.c_str());
		return;
	}

	auto pack = (txt_type == TXT_TYPE_CLI_DATA) ? make_shared<MeshCoreCommand>() : make_shared<MeshCoreCommandDirectMessage>();
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
	buffer_free(&buf);
	
	pack->expected_responses = { RESPONSE_CODE_MSG_SENT };
	pack->is_message = true;
	outgoing_commands.push_back(pack);
}

/*
void queue_packet_send_direct_datagram(const string& pubkey, const uint8* data, size_t data_length) {
	if (data_length > MESHCORE_MAX_DIRECT_DATAGRAM_LENGTH) {
		printf("queue_packet_channel_datagram(): datagram too big!\n");
		return;
	}
	static const uint8 zero_key[MESHCORE_PUBKEY_LEN / 2] = { 0 };
	uint8 key[MESHCORE_PUBKEY_LEN / 2];
	if (!is_valid_pubkey(pubkey)) {
		printf("queue_packet_send_status_request(): Invalid pubkey: %s\n", pubkey.c_str());
		return;
	}
	if (!hex2bin(pubkey.c_str(), MESHCORE_PUBKEY_LEN, key, sizeof(key)) || !memcmp(key, zero_key, sizeof(key))) {
		printf("queue_packet_send_status_request(): Error running hex2bin on %s !\n", pubkey.c_str());
		return;
	}

	auto pack = make_shared<MeshCoreCommandStdRetry>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, CMD_BINARY_REQ);
	buffer_append(&buf, (const char*)key, sizeof(key));
	buffer_append(&buf, (const char*)data, data_length);
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);
	pack->expected_responses = { RESPONSE_CODE_MSG_SENT };
	pack->is_message = true;
	outgoing_commands.push_back(pack);
}
*/

void queue_packet_send_status_request(const string& pubkey) {
	static const uint8 zero_key[MESHCORE_PUBKEY_LEN / 2] = { 0 };
	uint8 key[MESHCORE_PUBKEY_LEN / 2];
	if (!is_valid_pubkey(pubkey)) {
		printf("queue_packet_send_status_request(): Invalid pubkey: %s\n", pubkey.c_str());
		return;
	}
	if (!hex2bin(pubkey.c_str(), MESHCORE_PUBKEY_LEN, key, sizeof(key)) || !memcmp(key, zero_key, sizeof(key))) {
		printf("queue_packet_send_status_request(): Error running hex2bin on %s !\n", pubkey.c_str());
		return;
	}

	auto pack = make_shared<MeshCoreCommandStdRetry>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x1B);
	buffer_append(&buf, (const char*)key, sizeof(key));
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	pack->expected_responses = { RESPONSE_CODE_MSG_SENT };
	outgoing_commands.push_back(pack);
}

void queue_packet_set_channel_config(uint8 channel_idx, const string& channelName, const string& secret_key) {
	if (channel_idx > MESHCORE_HIGHEST_CHANNEL || channelName.empty()) {
		printf("Error in queue_packet_set_channel_config(): invalid channel_index or empty channel_name!\n");
		return;
	}

	char name[MESHCORE_MAX_CHAN_LEN + 1] = { 0 };
	sstrcpy(name, channelName.c_str());
	if (name[0] == 0) {
		printf("Error in queue_packet_set_channel_config(): empty channel_name!\n");
		return;
	}

	string key;
	if (secret_key.empty()) {
		if (channelName == "Public" || name[0] == '#') {
			key = DeriveChannelKey(name);
			if (key.empty()) {
				// shouldn't happen
				return;
			}
			key = bin2hex((const uint8*)key.c_str(), key.length());
		} else {
			printf("Error in queue_packet_set_channel_config(): required secret_key is empty!\n");
			return;
		}
	} else {
		key = secret_key;
	}

	if (key.length() != MESHCORE_CHAN_SECRET_LEN || strspn(key.c_str(), "0123456789abcdef") != key.length()) {
		printf("Error in queue_packet_set_channel_config(): secret_key is incorrect length or not hex!\n");
		return;
	}
	uint8 keybin[MESHCORE_CHAN_SECRET_LEN/2];
	if (!hex2bin(key.c_str(), key.length(), keybin, sizeof(keybin))) {
		printf("Error in queue_packet_set_channel_config(): error decoding secret_key from hex to binary!\n");
		return;
	}

	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x20);
	buffer_append_int<uint8>(&buf, channel_idx);
	buffer_append(&buf, name, MESHCORE_MAX_CHAN_LEN);
	buffer_append(&buf, (const char *)keybin, sizeof(keybin));
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	pack->expected_responses = { RESPONSE_CODE_OK };
	outgoing_commands.push_back(pack);

	queue_packet_get_channel_info(channel_idx);
}

void queue_packet_erase_channel(uint8 channel_idx) {
	if (channel_idx > MESHCORE_HIGHEST_CHANNEL) {
		printf("Error in queue_packet_erase_channel(): invalid channel_index!\n");
		return;
	}

	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x20);
	buffer_append_int<uint8>(&buf, channel_idx);
	char name[MESHCORE_MAX_CHAN_LEN + 1] = { 0 };
	buffer_append(&buf, name, MESHCORE_MAX_CHAN_LEN);
	uint8 keybin[MESHCORE_CHAN_SECRET_LEN / 2];
	buffer_append(&buf, (const char*)keybin, sizeof(keybin));
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	pack->expected_responses = { RESPONSE_CODE_OK };
	outgoing_commands.push_back(pack);

	queue_packet_get_channel_info(channel_idx);
}

void queue_swap_channels(uint8 channel_idx_1, uint8 channel_idx_2) {
	if (channel_idx_1 > MESHCORE_HIGHEST_CHANNEL || channel_idx_2 > MESHCORE_HIGHEST_CHANNEL) {
		printf("Error in queue_swap_channels(): invalid channel_index!\n");
		return;
	}

	if (channel_idx_1 == channel_idx_2) {
		printf("Error in queue_swap_channels(): no point in swapping a channel with itself!\n");
		return;
	}

	shared_ptr<MeshCoreChannel> c1, c2;
	if (!get_channel(channel_idx_1, c1)) {
		printf("Error in queue_swap_channels(): I don't have info for channel index %u!\n", channel_idx_1);
		return;
	}

	if (!get_channel(channel_idx_2, c2)) {
		printf("Error in queue_swap_channels(): I don't have info for channel index %u!\n", channel_idx_2);
		return;
	}

	queue_packet_set_channel_config(c2->channel_index, c1->name, c1->secret);
	queue_packet_set_channel_config(c1->channel_index, c2->name, c2->secret);
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

		timeout_outgoing_messages();

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
				mesh_log("Writing command %s with %zu bytes...\n", GetMeshCoreCommandString((MESHCORE_COMMAND_CODES)cur->data[0]).c_str(), cur->data.length());
				if (config.meshcore.log_to_console) {
					PrintData(stdout, (const uint8*)cur->data.c_str(), cur->data.length());
				}
				if (config.meshcore.log_fp != NULL) {
					PrintData(config.meshcore.log_fp, (const uint8 *)cur->data.c_str(), cur->data.length());
				}
			}
		} else if (current_outgoing_command) {
			auto& cur = current_outgoing_command;
			if (GetTickCount64() > cur->time_limit) {
				//give up on this command :(
				printf("[meshcore] Outgoing command %s timed out while waiting for a reply!\n", GetMeshCoreCommandString(cur->getType()).c_str());
				current_outgoing_command.reset();
			}
		}

		if (config.sendbuf.len > 0) {
			int n = config.io_driver->Write(config.sendbuf.udata, (int)config.sendbuf.len);
			if (n > 0) {
				buffer_remove_front(&config.sendbuf, n);
			} else if (n < 0) {
				printf("[meshcore] Error writing to I/O driver!\n");
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
			printf("[meshcore] Error reading from I/O driver!\n");
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

string DeriveChannelKey(const string& channelName) {
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

	return string(key, MESHCORE_CHAN_SECRET_LEN / 2);
}

bool MeshCoreCommand::expectsAck() {
	return (dynamic_cast<MeshCoreCommandAcknowledged *>(this) != NULL);
}
