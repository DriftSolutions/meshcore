/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

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

class DuplicateRecord {
public:
	uint64 ticks = time(NULL);
	string str;

	DuplicateRecord(const string& pstr) {
		str = pstr;
	}
};

bool is_duplicate(uint8* pack, uint16 packlen, bool dont_add_record = false) {
	static list<DuplicateRecord> dupes;

	string str((const char*)pack, packlen);
	
	uint64 ts_expires = GetTickCount64() - 10000;
	for (auto x = dupes.begin(); x != dupes.end();) {
		if (x->ticks < ts_expires) {
			x = dupes.erase(x);
		} else if (x->str == str) {
			return true;
		} else {
			x++;
		}
	}

	if (!dont_add_record) {
		dupes.push_back(DuplicateRecord(str));
	}

	return false;
}

void handle_incoming_packet_push_notifications(uint8* pack, uint16 packlen) {
	const MESHCORE_RESPONSE_CODES& packet_type = (MESHCORE_RESPONSE_CODES)pack[0];

	if (packet_type == RESPONSE_CODE_LOG_DATA || packet_type == PUSH_CODE_NEW_ADVERT) {
		//ignore
		return;
	}

	if (packet_type == RESPONSE_CODE_ADVERTISEMENT) {
		if (packlen >= 33) {
			string pubkey = bin2hex(pack + 1, 32);
			shared_ptr<MeshCoreContact> u;
			if (get_contact_by_pubkey(pubkey, u)) {
				u->last_seen = time(NULL);
			}
			UniValue obj(UniValue::VOBJ);
			obj.pushKV("public_key", pubkey);
			mqtt_send(config.mqtt.topic_prefix + "/advertisement", obj, false);
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_CONTACT && packlen >= sizeof(_PACKET_CONTACT)) {
		_PACKET_CONTACT* c = (_PACKET_CONTACT*)pack;
		add_or_update_contact(c);
		return;
	}

	if (packet_type == RESPONSE_CODE_CONTACT_END) {
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
		return;
	}

	if (packet_type == RESPONSE_CODE_CONTACT_DELETED) {
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

	if (packet_type == RESPONSE_CODE_MESSAGES_WAITING) {
		queue_packet_get_message();
		return;
	}

	if (packet_type == RESPONSE_CODE_ACK) {
		_PACKET_ACK* ack = (_PACKET_ACK*)pack;
		if (packlen >= sizeof(_PACKET_ACK)) {
			uint32 tag = Get_ULE32(*(uint32*)ack->ack_code);
			printf("Message with tag %u acknowledged.\n", tag);

			auto x = outgoing_messages.find(tag);
			if (x != outgoing_messages.end()) {
				auto dm = dynamic_cast<MeshCoreCommandAcknowledged*>(x->second.get());
				if (dm != NULL) {
					dm->onAck();
				}
				outgoing_messages.erase(x);
			} else {
				int x = 1;
			}
		} else {
			printf("Message acknowledged with unknown tag...\n");
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_STATUS_RESPONSE && packlen >= sizeof(_PACKET_STATUS_RESPONSE)) {
		_PACKET_STATUS_RESPONSE* s = (_PACKET_STATUS_RESPONSE*)pack;

		string pubkey_prefix = bin2hex(s->public_key_prefix, sizeof(s->public_key_prefix));

		for (auto x = outgoing_messages.begin(); x != outgoing_messages.end(); x++) {
			auto req = dynamic_cast<MeshCoreCommandStatusRequest*>(x->second.get());
			if (req != NULL) {
				if (!stricmp(pubkey_prefix.c_str(), req->pubkey_prefix)) {
					// Received, remove from retry queue
					outgoing_messages.erase(x);
					break;
				}
			}
		}

		UniValue obj(UniValue::VOBJ);

		shared_ptr<MeshCoreContact> u;
		if (get_contact_by_pubkey_prefix(pubkey_prefix, u)) {
			obj.pushKV("public_key", u->pubkey);
		}
		obj.pushKV("public_key_prefix", pubkey_prefix);

		if (packlen >= sizeof(_PACKET_STATUS_RESPONSE_REPEATER)) {
			_PACKET_STATUS_RESPONSE_REPEATER * r = (_PACKET_STATUS_RESPONSE_REPEATER*)(pack + sizeof(_PACKET_STATUS_RESPONSE));

			obj.pushKV("batt_milli_volts", Get_ULE16(r->batt_milli_volts));
			obj.pushKV("curr_tx_queue_len", Get_ULE16(r->curr_tx_queue_len));
			obj.pushKV("noise_floor", (int)Get_SLE16(r->noise_floor));
			obj.pushKV("last_rssi", (int)Get_SLE16(r->last_rssi));
			obj.pushKV("n_packets_recv", (int64)Get_ULE32(r->n_packets_recv));
			obj.pushKV("n_packets_sent", (int64)Get_ULE32(r->n_packets_sent));
			obj.pushKV("total_air_time_secs", (int64)Get_ULE32(r->total_air_time_secs));
			obj.pushKV("total_up_time_secs", (int64)Get_ULE32(r->total_up_time_secs));
			obj.pushKV("n_sent_flood", (int64)Get_ULE32(r->n_sent_flood));
			obj.pushKV("n_sent_direct", (int64)Get_ULE32(r->n_sent_direct));
			obj.pushKV("n_recv_flood", (int64)Get_ULE32(r->n_recv_flood));
			obj.pushKV("n_recv_direct", (int64)Get_ULE32(r->n_recv_direct));
			obj.pushKV("err_events", Get_ULE16(r->err_events));
			obj.pushKV("last_snr", (int)Get_SLE16(r->last_snr));
			obj.pushKV("n_direct_dups", Get_ULE16(r->n_direct_dups));
			obj.pushKV("n_flood_dups", Get_ULE16(r->n_flood_dups));
		} else if (packlen > sizeof(_PACKET_STATUS_RESPONSE)) {
			const uint8* data = pack + sizeof(_PACKET_STATUS_RESPONSE);
			size_t len = packlen - sizeof(_PACKET_STATUS_RESPONSE);
			obj.pushKV("status_data", bin2hex(data, len));

			if (!config.meshcore.log_to_console) {
				printf("Status data:\n");
				PrintData(stdout, data, len);
			}

			mesh_log("Status data:\n");
			if (config.meshcore.log_fp != NULL) {
				PrintData(config.meshcore.log_fp, data, len);
			}
		}

		mqtt_send(config.mqtt.topic_prefix + "/status_response/" + pubkey_prefix, obj, false);
		return;
	}

	int x = 1;
}

void handleDirectMessage(_PACKET_CONTACT_MSG_RECV_COMMON * msg, size_t msglen) {
	UniValue obj(UniValue::VOBJ);
	string pubkey_prefix = bin2hex(msg->public_key_prefix, sizeof(msg->public_key_prefix));
	shared_ptr<MeshCoreContact> u;
	if (get_contact_by_pubkey_prefix(pubkey_prefix, u)) {
		obj.pushKV("public_key", u->pubkey);
	}
	obj.pushKV("public_key_prefix", pubkey_prefix);
	obj.pushKV("path_length", msg->path_length);
	obj.pushKV("timestamp", (int64)Get_ULE32(msg->timestamp));

	if (msg->text_type != TXT_TYPE_CLI_DATA) {
		obj.pushKV("txt_type", (uint8)msg->text_type);
		char* text = (char*)msg + sizeof(_PACKET_CONTACT_MSG_RECV_COMMON);
		size_t header_len = sizeof(_PACKET_CONTACT_MSG_RECV_COMMON);
		if (msg->text_type == 2) { // signed text			
			text += 4;
			header_len += 4;
		}
		obj.pushKV("message", trim_nulls(text, msglen - header_len));
		mqtt_send(mprintf("%s/direct/message/%s", config.mqtt.topic_prefix.c_str(), pubkey_prefix.c_str()), obj, false);
	} else {
		uint8* raw_data = (uint8*)msg + sizeof(_PACKET_CONTACT_MSG_RECV_COMMON);
		size_t header_len = sizeof(_PACKET_CONTACT_MSG_RECV_COMMON);
		size_t datalen = msglen - header_len;

		uint8* renulled_raw_data = (uint8 *)malloc(datalen);
		memcpy(renulled_raw_data, raw_data, datalen);
		size_t renulled_datalen = datalen;
		unescape_nulls(renulled_raw_data, renulled_datalen);

		obj.pushKV("data", bin2hex(raw_data, datalen));
		obj.pushKV("unescaped_data", bin2hex(renulled_raw_data, renulled_datalen));
		mqtt_send(mprintf("%s/direct/data/%s", config.mqtt.topic_prefix.c_str(), pubkey_prefix.c_str()), obj, false);
	}
}

void handle_incoming_packet(uint8* pack, uint16 packlen) {
	const MESHCORE_RESPONSE_CODES& packet_type = (MESHCORE_RESPONSE_CODES)pack[0];
	mesh_log("Received packet (%s, %u bytes):\n", GetMeshCoreResponseString(packet_type).c_str(), packlen);
	if (config.meshcore.log_to_console) {
		PrintData(stdout, pack, packlen);
	}
	if (config.meshcore.log_fp != NULL) {
		PrintData(config.meshcore.log_fp, pack, packlen);
	}

	if (packet_type >= 0x80 || packet_type == RESPONSE_CODE_CONTACT || packet_type == RESPONSE_CODE_CONTACT_END) {
		handle_incoming_packet_push_notifications(pack, packlen);
		return;
	}

	// In theory, we should only be here if a command was just issued
	//assert(current_outgoing_command.get() != NULL);
	shared_ptr<MeshCoreCommand> current_command = current_outgoing_command;
	current_outgoing_command.reset();

	if (current_command) {
		bool is_expected = (current_command->expected_responses.find(packet_type) != current_command->expected_responses.end());
		if (is_expected) {
			current_command->onRecvExpected();
			mesh_log("Received response %s for current command %s\n", GetMeshCoreResponseString(packet_type).c_str(), GetMeshCoreCommandString(current_command->getType()).c_str());
		} else {
			mqtt_error("Received unexpected %s for current command %s", GetMeshCoreResponseString(packet_type).c_str(), GetMeshCoreCommandString(current_command->getType()).c_str());
			current_command.reset();
		}
	}

	if (packet_type == RESPONSE_CODE_OK) {
		if (packlen >= 5) {
			uint32 value = Get_ULE32(*(uint32*)(pack + 1));
			mesh_log("Received OK reply with code %u to last command.\n", value);
		} else {
			mesh_log("Received OK reply to last command.\n");
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_ERROR) {
		uint8 code = 0;
		if (packlen >= 2) {
			code = pack[1];
			mqtt_error("Received error reply with error %s (0x%02X) to last command!", GetMeshCoreErrorString(code), code);
		} else {
			mqtt_error("Received error reply to last command!");
		}

		if (current_command) {
			current_command->onError(code);
		}

		return;
	}

	if (packet_type == RESPONSE_CODE_MSG_SENT) {
		if (packlen >= sizeof(_PACKET_MSG_SENT)) {
			_PACKET_MSG_SENT* ms = (_PACKET_MSG_SENT*)pack;
			uint32 tag = Get_ULE32(ms->tag);
			uint64 timeo = Get_ULE32(ms->suggested_timeout);
			printf("Message sent with tag %u. (suggested timeout: %llu, flood: %s)\n", tag, timeo, (ms->route_flag == 1) ? "yes" : "no");

			if (current_command) {
				auto dm = dynamic_cast<MeshCoreCommandAcknowledged*>(current_command.get());
				if (dm != NULL) {
					dm->expected_tag = tag;
					dm->time_limit = GetTickCount64() + get_timeout_for_command(dm->getType(), timeo);
					outgoing_messages[tag] = current_command;
					current_command.reset();
				}
			}
		} else {
			printf("Message sent with unknown tag...\n");
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_SELF_INFO && packlen > sizeof(_PACKET_SELF_INFO)) {
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

	if (packet_type == RESPONSE_CODE_DEVICE_INFO && packlen >= sizeof(_PACKET_DEVICE_INFO)) {
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

	if (packet_type == RESPONSE_CODE_CHANNEL_INFO && packlen >= sizeof(_PACKET_CHANNEL_INFO)) {
		_PACKET_CHANNEL_INFO* ci = (_PACKET_CHANNEL_INFO*)pack;

		add_or_update_channel(ci);
		mqtt_send_channel(ci->channel_index);

		return;
	}

	if (packet_type == RESPONSE_CODE_CONTACT_START) {
		if (packlen >= 5) {
			uint32 count = Get_ULE32(*(uint32*)(pack + 1));
			printf("Receiving contacts list... (%u contacts)\n", count);
		} else {
			printf("Receiving contacts list...\n");
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_CURRENT_TIME) {
	}

	if (packet_type == RESPONSE_CODE_BATTERY) {
		_PACKET_BATTERY* bs = (_PACKET_BATTERY*)pack;

		uint16 millivolts = Get_ULE16(bs->battery_voltage);
		double volts = double(millivolts) / 1000.0f;
		uint32 used_storage = Get_ULE32(bs->used_storage);
		uint32 total_storage = Get_ULE32(bs->total_storage);

		printf("[meshcore] Battery: %.02fv. Storage: %u of %u kB used.\n", volts, used_storage, total_storage);

		UniValue obj(UniValue::VOBJ);
		obj.pushKV("millivolts", millivolts);
		obj.pushKV("volts", volts);
		obj.pushKV("used_storage", (int64)used_storage);
		obj.pushKV("total_storage", (int64)total_storage);
		mqtt_send(config.mqtt.topic_prefix + "/battery_info", obj, false);
		return;
	}

	if (packet_type == RESPONSE_CODE_CONTACT_MSG_RECV && packlen >= sizeof(_PACKET_CONTACT_MSG_RECV)) {
		if (is_duplicate(pack, packlen)) {
			return;
		}

		_PACKET_CONTACT_MSG_RECV* msg = (_PACKET_CONTACT_MSG_RECV*)pack;
		handleDirectMessage(&msg->com, packlen - ((uint8 *)&msg->com - pack));
		return;
	}

	if (packet_type == RESPONSE_CODE_CONTACT_MSG_RECV_V3 && packlen >= sizeof(_PACKET_CONTACT_MSG_RECV_V3)) {
		if (is_duplicate(pack, packlen)) {
			return;
		}

		_PACKET_CONTACT_MSG_RECV_V3* msg = (_PACKET_CONTACT_MSG_RECV_V3*)pack;
		handleDirectMessage(&msg->com, packlen - ((uint8*)&msg->com - pack));
		/*
		UniValue obj(UniValue::VOBJ);
		string pubkey_prefix = bin2hex(msg->public_key_prefix, sizeof(msg->public_key_prefix));
		shared_ptr<MeshCoreContact> u;
		if (get_contact_by_pubkey_prefix(pubkey_prefix, u)) {
			obj.pushKV("public_key", u->pubkey);
		}
		obj.pushKV("public_key_prefix", pubkey_prefix);

		if (msg->text_type != TXT_TYPE_CLI_DATA) {
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
			mqtt_send(mprintf("%s/direct/message/%s", config.mqtt.topic_prefix.c_str(), pubkey_prefix.c_str()), obj, false);
		} else {
			obj.pushKV("timestamp", (int64)Get_ULE32(msg->timestamp));
			obj.pushKV("path_length", msg->path_length);
			uint8* raw_data = (uint8*)&msg->signature[0];
			size_t header_len = sizeof(_PACKET_CONTACT_MSG_RECV_V3) - 4;
			size_t datalen = packlen - header_len;
			obj.pushKV("data", bin2hex(raw_data, datalen));
			//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
			mqtt_send(mprintf("%s/direct/data/%s", config.mqtt.topic_prefix.c_str(), pubkey_prefix.c_str()), obj, false);
		}
		*/
		return;
	}

	if (packet_type == RESPONSE_CODE_CHANNEL_MSG_RECV && packlen >= sizeof(_PACKET_CHANNEL_MSG_RECV)) {
		if (is_duplicate(pack, packlen)) {
			return;
		}

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
			mqtt_send(mprintf("%s/channel/message/%u", config.mqtt.topic_prefix.c_str(), msg->channel_index), obj, false);
		} else {
			printf("Warning: unrecognized incoming channel message: %s\n", str.c_str());
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_CHANNEL_MSG_RECV_V3 && packlen >= sizeof(_PACKET_CHANNEL_MSG_RECV_V3)) {
		if (is_duplicate(pack, packlen)) {
			return;
		}

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
			obj.pushKV("from", str.substr(0, p - str.c_str()));
			obj.pushKV("message", p + 2);
			//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
			mqtt_send(mprintf("%s/channel/message/%u", config.mqtt.topic_prefix.c_str(), msg->channel_index), obj, false);
		} else {
			printf("Warning: unrecognized incoming channel message: %s\n", str.c_str());
		}
		return;
	}

	if (packet_type == RESPONSE_CODE_CHANNEL_DATA_RECV && packlen > sizeof(_PACKET_CHANNEL_DATA_RECV)) {
		if (is_duplicate(pack, packlen)) {
			return;
		}

		_PACKET_CHANNEL_DATA_RECV* msg = (_PACKET_CHANNEL_DATA_RECV*)pack;

		uint8* data = (uint8*)pack + sizeof(_PACKET_CHANNEL_DATA_RECV);
		size_t len = packlen - sizeof(_PACKET_CHANNEL_DATA_RECV);
		string str = bin2hex(data, len);

		UniValue obj(UniValue::VOBJ);
		obj.pushKV("channel_index", msg->channel_index);
		obj.pushKV("path_length", msg->path_length);
		obj.pushKV("data_type", Get_ULE16(msg->data_type));
		obj.pushKV("data", str);

		mqtt_send(mprintf("%s/channel/data/%u", config.mqtt.topic_prefix.c_str(), msg->channel_index), obj, false);

		return;
	}

	if (packet_type == RESPONSE_CODE_NO_MORE_MSGS) {
		state.lastNoMoreMessages = time(NULL);
		return;
	}

	return;
}

void handle_incoming_packets() {
	DSL_BUFFER& recvbuf = state.recvbuf;

	while (recvbuf.len > 0) {
		if (recvbuf.udata == NULL || recvbuf.len <= 0) {
			break;
		}

		uint8* begin = (uint8*)memchr(recvbuf.udata, COMPANION_INCOMING_FRAME_START, recvbuf.len);
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

		handle_incoming_packet(begin + COMPANION_FRAME_HEADER_SIZE, packlen);

		buffer_remove_front(&recvbuf, COMPANION_FRAME_HEADER_SIZE + packlen);
	}
}
