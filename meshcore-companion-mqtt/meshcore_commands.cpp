/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

void queue_packet_app_start() {
	auto pack = make_shared<MeshCoreCommand>();
	_PACKET_APP_START p;
	pack->data.assign((char*)&p, sizeof(p));
	pack->data.append("meshcore-companion-mqtt");
	//pack->data.append("mccli");
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
		printf("Retrying direct message, retry number %u ...\n", pack->attempt);
	} else {
		mqtt_error("Giving up on direct message, retry limit hit...");
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
		mqtt_error("Giving up on command %s, retry limit hit...", GetMeshCoreCommandString(getType()).c_str());
	}
}

void queue_packet_channel_datagram(uint8 channel_idx, uint16 data_type, const uint8* data, size_t data_length) {
	if (data_length > MESHCORE_MAX_CHAN_DATAGRAM_LENGTH) {
		mqtt_error("queue_packet_channel_datagram(): datagram too big!");
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

void queue_packet_send_direct_msg(const string& pubkey_or_prefix, const string& str, uint8 attempt, MESHCORE_TEXT_TYPES txt_type) {
	static const uint8 zero_prefix[MESHCORE_PUBKEY_PREFIX_LEN / 2] = { 0 };
	uint8 prefix[MESHCORE_PUBKEY_PREFIX_LEN / 2];
	if (!is_valid_destination(pubkey_or_prefix)) {
		mqtt_error("queue_packet_send_direct_msg(): Invalid destination: %s", pubkey_or_prefix.c_str());
		return;
	}
	if (!hex2bin(pubkey_or_prefix.c_str(), MESHCORE_PUBKEY_PREFIX_LEN, prefix, sizeof(prefix)) || !memcmp(prefix, zero_prefix, sizeof(prefix))) {
		mqtt_error("queue_packet_send_direct_msg(): Error running hex2bin on %s !", pubkey_or_prefix.c_str());
		return;
	}

	auto pack = (txt_type == TXT_TYPE_CLI_DATA) ? make_shared<MeshCoreCommand>() : make_shared<MeshCoreCommandDirectMessage>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x02);
	buffer_append_int<uint8>(&buf, txt_type);
	buffer_append_int<uint8>(&buf, attempt);
	buffer_append_int<uint32>(&buf, Get_ULE32((uint32)time(NULL)));
	buffer_append(&buf, (const char*)prefix, sizeof(prefix));

	size_t len = min(str.length(), (size_t)MESHCORE_MAX_DIRECT_TEXT_LENGTH);
	if (str.length() > MESHCORE_MAX_DIRECT_TEXT_LENGTH) {
		printf("Warning: direct message is over MESHCORE_MAX_DIRECT_TEXT_LENGTH, truncating...\n");
		printf(" Original: %s\n", str.c_str());
		printf("Truncated: %s\n", str.substr(0, len).c_str());
	}
	buffer_append(&buf, str.c_str(), len);

	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	pack->expected_responses = { RESPONSE_CODE_MSG_SENT };
	pack->is_message = true;
	outgoing_commands.push_back(pack);
}

/**
* Escape nulls to safely send binary data as a string.
* Why this over Base64 or something else you might ask? Base64/hex/etc. all have guaranteed overhead, in the likely use cases of what people will be sending over MeshCore byte values of 0 and 1 will so rare as to result in negligible overhead.
* Take the 2 most common things people are likely to send:
*	UTF-8/ASCII strings: 0x00 is never in the middle of data, with just one optional one at the end. 0x01 is Start of Heading which is not in modern use and also isn't used in UTF-8 multibyte encodings. So zero overhead in these cases except the optional terminating null.
*	Encrypted Data: In properly encrypted data, the odds of a 0x00 or 0x01 appearing in a full-length encrypted message is less than 1 per message.
*/
string escape_nulls(const uint8* data, size_t data_length) {
	string ret;
	const uint8* p = data;
	for (size_t i = 0; i < data_length; i++, p++) {
		if (*p == 0x00) {
			ret += "\x01\x01";
		} else if (*p == 0x01) {
			ret += "\x01\x02";
		} else {
			ret += *p;
		}
	}
	return ret;
}

void unescape_nulls(uint8* data, size_t& data_length) {
	string ret;
	uint8* p = data;
	for (size_t i = 0; i < data_length; p++, i++) {
		if (*p == 0x01) {
			size_t left = data_length - i - 1;
			memmove(p, p + 1, left);
			*p = *p - 1;
			data_length--;
		}
	}
}

void queue_packet_send_direct_datagram(const string& pubkey_or_prefix, const uint8* data, size_t data_length) {
	string str = escape_nulls(data, data_length);
	if (str.length() > MESHCORE_MAX_DIRECT_TEXT_LENGTH) {
		mqtt_error("queue_packet_send_direct_datagram(): Payload is too big!");
		return;
	}

	queue_packet_send_direct_msg(pubkey_or_prefix, str, 0, TXT_TYPE_CLI_DATA);
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

void queue_packet_login(const string& pubkey_or_prefix, const string& pass) {
	string pubkey = get_pubkey_from_pubkey_or_prefix(pubkey_or_prefix);

	static const uint8 zero_key[MESHCORE_PUBKEY_LEN / 2] = { 0 };
	uint8 key[MESHCORE_PUBKEY_LEN / 2];
	if (!is_valid_pubkey(pubkey)) {
		mqtt_error("queue_packet_login(): Invalid pubkey: %s", pubkey.c_str());
		return;
	}
	if (!hex2bin(pubkey.c_str(), MESHCORE_PUBKEY_LEN, key, sizeof(key)) || !memcmp(key, zero_key, sizeof(key))) {
		mqtt_error("queue_packet_login(): Error running hex2bin on %s !", pubkey.c_str());
		return;
	}

	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x1A);
	buffer_append(&buf, (const char*)key, sizeof(key));
	buffer_append(&buf, pass.c_str(), pass.length());
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	pack->expected_responses = { RESPONSE_CODE_MSG_SENT };
	outgoing_commands.push_back(pack);
}

void queue_packet_send_status_request(const string& pubkey_or_prefix) {
	string pubkey = get_pubkey_from_pubkey_or_prefix(pubkey_or_prefix);

	static const uint8 zero_key[MESHCORE_PUBKEY_LEN / 2] = { 0 };
	uint8 key[MESHCORE_PUBKEY_LEN / 2];
	if (!is_valid_pubkey(pubkey)) {
		mqtt_error("queue_packet_send_status_request(): Invalid pubkey: %s", pubkey.c_str());
		return;
	}
	if (!hex2bin(pubkey.c_str(), MESHCORE_PUBKEY_LEN, key, sizeof(key)) || !memcmp(key, zero_key, sizeof(key))) {
		mqtt_error("queue_packet_send_status_request(): Error running hex2bin on %s !", pubkey.c_str());
		return;
	}

	auto pack = make_shared<MeshCoreCommandStatusRequest>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x1B);
	buffer_append(&buf, (const char*)key, sizeof(key));
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	sstrcpy(pack->pubkey_prefix, pubkey.c_str());
	pack->expected_responses = { RESPONSE_CODE_MSG_SENT };
	outgoing_commands.push_back(pack);
}

void queue_packet_set_channel_config(uint8 channel_idx, const string& channelName, const string& secret_key) {
	if (channel_idx > MESHCORE_HIGHEST_CHANNEL || channelName.empty()) {
		mqtt_error("Error in queue_packet_set_channel_config(): invalid channel_index or empty channel_name!");
		return;
	}

	char name[MESHCORE_MAX_CHAN_LEN + 1] = { 0 };
	sstrcpy(name, channelName.c_str());
	if (name[0] == 0) {
		mqtt_error("Error in queue_packet_set_channel_config(): empty channel_name!");
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
			mqtt_error("Error in queue_packet_set_channel_config(): required secret_key is empty!");
			return;
		}
	} else {
		key = secret_key;
	}

	if (key.length() != MESHCORE_CHAN_SECRET_LEN || strspn(key.c_str(), "0123456789abcdef") != key.length()) {
		mqtt_error("Error in queue_packet_set_channel_config(): secret_key is incorrect length or not hex!");
		return;
	}
	uint8 keybin[MESHCORE_CHAN_SECRET_LEN / 2];
	if (!hex2bin(key.c_str(), key.length(), keybin, sizeof(keybin))) {
		mqtt_error("Error in queue_packet_set_channel_config(): error decoding secret_key from hex to binary!");
		return;
	}

	auto pack = make_shared<MeshCoreCommand>();
	DSL_BUFFER buf;
	buffer_init(&buf);
	buffer_append_int<uint8>(&buf, 0x20);
	buffer_append_int<uint8>(&buf, channel_idx);
	buffer_append(&buf, name, MESHCORE_MAX_CHAN_LEN);
	buffer_append(&buf, (const char*)keybin, sizeof(keybin));
	pack->data = buffer_as_string(&buf);
	buffer_free(&buf);

	pack->expected_responses = { RESPONSE_CODE_OK };
	outgoing_commands.push_back(pack);

	queue_packet_get_channel_info(channel_idx);
}

void queue_packet_erase_channel(uint8 channel_idx) {
	if (channel_idx > MESHCORE_HIGHEST_CHANNEL) {
		mqtt_error("Error in queue_packet_erase_channel(): invalid channel_index!");
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
		mqtt_error("Error in queue_swap_channels(): invalid channel_index!");
		return;
	}

	if (channel_idx_1 == channel_idx_2) {
		mqtt_error("Error in queue_swap_channels(): no point in swapping a channel with itself!");
		return;
	}

	shared_ptr<MeshCoreChannel> c1, c2;
	if (!get_channel(channel_idx_1, c1)) {
		mqtt_error("Error in queue_swap_channels(): I don't have info for channel index %u!", channel_idx_1);
		return;
	}

	if (!get_channel(channel_idx_2, c2)) {
		mqtt_error("Error in queue_swap_channels(): I don't have info for channel index %u!", channel_idx_2);
		return;
	}

	queue_packet_set_channel_config(c2->channel_index, c1->name, c1->secret);
	queue_packet_set_channel_config(c1->channel_index, c2->name, c2->secret);
}
