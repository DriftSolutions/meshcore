/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

void handle_incoming_commands() {

	shared_ptr<MQTT_Command> cmd;

	{
		AutoMutex(hMutex);
		if (config.incoming_commands.size() == 0) {
			return;
		}
		cmd = config.incoming_commands.front();
		config.incoming_commands.pop();
	}

	if (cmd->cmd == "get_self_info") {
		mqtt_send_self_info();
		return;
	}
	
	if (cmd->cmd == "get_device_info") {
		mqtt_send_device_info();
		return;
	}

	if (cmd->cmd == "get_battery_info") {
		queue_packet_battery_info();
		return;
	}	
	
	if (cmd->cmd == "get_contacts") {
		bool force = (cmd->parms.exists("force_refresh") && cmd->parms["force_refresh"].isBool() && cmd->parms["force_refresh"].getBool());
		if (force || state.users.size() == 0) {
			update_contacts(true);
		} else {
			mqtt_send_contacts();
		}
		return;		
	}
	
	if (cmd->cmd == "get_channels") {
		bool force = (cmd->parms.exists("force_refresh") && cmd->parms["force_refresh"].isBool() && cmd->parms["force_refresh"].getBool());
		if (force || state.chans.size() == 0) {
			queue_packets_get_channels();
		} else {
			mqtt_send_channels();
		}
		return;
	}
	
	if (cmd->cmd == "get_channel") {
		if (cmd->parms.exists("channel_index") && cmd->parms["channel_index"].isNum()) {
			int ind = cmd->parms["channel_index"].get_int();
			if (ind >= 0 && ind <= MESHCORE_HIGHEST_CHANNEL) {
				bool force = (cmd->parms.exists("force_refresh") && cmd->parms["force_refresh"].isBool() && cmd->parms["force_refresh"].getBool());
				if (force || state.chans.find(ind) == state.chans.end()) {
					queue_packet_get_channel_info(ind);
				} else {
					mqtt_send_channel(ind);
				}
			}
		}
		return;
	}
	
	if (cmd->cmd == "send_channel_msg") {
		if (cmd->parms.exists("channel_index") && cmd->parms["channel_index"].isNum() && cmd->parms.exists("message") && cmd->parms["message"].isStr()) {
			int channel_idx = cmd->parms["channel_index"].get_int();
			string msg = cmd->parms["message"].get_str();
			if (channel_idx >= 0 && channel_idx <= MESHCORE_HIGHEST_CHANNEL && !msg.empty()) {
				queue_packet_send_channel_msg((uint8)channel_idx, msg);
			} else {
				printf("Error in send_channel_msg: channel_index or message is empty, invalid, or not set!\n");
			}
		}
		return;
	}

	if (cmd->cmd == "send_direct_msg") {
		if (cmd->parms.exists("destination") && cmd->parms["destination"].isStr() && cmd->parms.exists("message") && cmd->parms["message"].isStr()) {
			string destination = cmd->parms["destination"].get_str();
			string msg = cmd->parms["message"].get_str();
			if (!destination.empty() && !msg.empty()) {
				if (is_valid_destination(destination)) {
					MESHCORE_TEXT_TYPES txt_type = TXT_TYPE_PLAIN;// (cmd->parms.exists("txt_type") && cmd->parms["txt_type"].isNum()) ? (MESHCORE_TEXT_TYPES)cmd->parms["txt_type"].get_int() : TXT_TYPE_PLAIN;
					queue_packet_send_direct_msg(destination, msg, 0, txt_type);
				} else {
					printf("Error in send_direct_msg: invalid destination, must be a pubkey or pubkey prefix!\n");
				}
			} else {
				printf("Error in send_direct_msg: destination or message is empty or not set!\n");
			}
		}
		return;
	}

	if (cmd->cmd == "send_status_request") {
		if (cmd->parms.exists("public_key") && cmd->parms["public_key"].isStr()) {
			string public_key = cmd->parms["public_key"].get_str();
			if (!public_key.empty()) {
				if (is_valid_pubkey(public_key)) {
					queue_packet_send_status_request(public_key);
				} else {
					printf("Error in send_status_request: invalid destination, must be a pubkey or pubkey prefix!\n");
				}
			} else {
				printf("Error in send_status_request: destination or message is empty or not set!\n");
			}
		}
		return;
	}

	if (cmd->cmd == "set_channel_config") {
		if (cmd->parms.exists("channel_index") && cmd->parms["channel_index"].isNum() && cmd->parms.exists("channel_name") && cmd->parms["channel_name"].isStr()) {
			int channel_idx = cmd->parms["channel_index"].get_int();
			string name = cmd->parms["channel_name"].get_str();
			if (channel_idx >= 0 && channel_idx <= MESHCORE_HIGHEST_CHANNEL && !name.empty()) {
				string key = (cmd->parms.exists("secret_key") && cmd->parms["secret_key"].isStr()) ? cmd->parms["secret_key"].get_str() : "";

				queue_packet_set_channel_config((uint8)channel_idx, name, key);
			} else {
				printf("Error in erase_channel: channel_index is out of range or channel_name is empty!\n");
			}
		} else {
			printf("Error in set_channel_config: invalid channel_index or channel_name!\n");
		}
		return;
	}

	if (cmd->cmd == "erase_channel") {
		if (cmd->parms.exists("channel_index") && cmd->parms["channel_index"].isNum()) {
			int ind = cmd->parms["channel_index"].get_int();
			if (ind >= 0 && ind <= MESHCORE_HIGHEST_CHANNEL) {
				queue_packet_erase_channel(ind);
			} else {
				printf("Error in erase_channel: channel_index is out of range!\n");
			}
		} else {
			printf("Error in erase_channel: invalid channel_index!\n");
		}
		return;
	}

	if (cmd->cmd == "swap_channels") {
		if (cmd->parms.exists("channel_index_1") && cmd->parms["channel_index_1"].isNum() && cmd->parms.exists("channel_index_2") && cmd->parms["channel_index_2"].isNum()) {
			int channel_index_1 = cmd->parms["channel_index_1"].get_int();
			int channel_index_2 = cmd->parms["channel_index_2"].get_int();
			if (channel_index_1 >= 0 && channel_index_1 <= MESHCORE_HIGHEST_CHANNEL && channel_index_2 >= 0 && channel_index_2 <= MESHCORE_HIGHEST_CHANNEL) {
				queue_swap_channels(channel_index_1, channel_index_2);
			} else {
				printf("Error in swap_channels: channel_index_1 and/or channel_index_2 out of range!\n");
			}
		} else {
			printf("Error in swap_channels: invalid channel_index_1 and/or channel_index_2 is invalid!\n");
		}
		return;
	}

	if (cmd->cmd == "send_channel_datagram") {
		if (cmd->parms.exists("channel_index") && cmd->parms["channel_index"].isNum() && cmd->parms.exists("data_type") && cmd->parms["data_type"].isNum() && cmd->parms.exists("data") && cmd->parms["data"].isStr()) {
			int channel_idx = cmd->parms["channel_index"].get_int();
			int data_type = cmd->parms["data_type"].get_int();
			string data = cmd->parms["data"].get_str();
			if (channel_idx >= 0 && channel_idx <= MESHCORE_HIGHEST_CHANNEL && data_type != 0 && data_type != 0xFFFF && !data.empty() || data.length() % 2 != 0 || data.length() > MESHCORE_MAX_CHAN_DATAGRAM_LENGTH * 2) {

				uint8 raw_len = (uint8)(data.length() / 2);
				uint8* raw = (uint8*)malloc(raw_len);
				if (hex2bin(data.c_str(), data.length(), raw, raw_len)) {
					queue_packet_channel_datagram(channel_idx, data_type, raw, raw_len);
				} else {
					printf("Error in send_channel_datagram: error converting hex string to binary!\n");
				}
				free(raw);
				return;
			}
		}
		printf("Error in send_channel_datagram: invalid channel_index, data_type, or data is invalid!\n");
		return;
	}

	printf("Received unknown command '%s' over MQTT.\n", cmd->cmd.c_str());
	printf("Data: %s\n", cmd->parms.write(1).c_str());
}
