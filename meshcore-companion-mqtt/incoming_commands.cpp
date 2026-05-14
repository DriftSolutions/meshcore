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
		if (cmd->parms.exists("channel") && cmd->parms["channel"].isNum()) {
			int ind = cmd->parms["channel"].get_int();
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
		if (cmd->parms.exists("channel") && cmd->parms["channel"].isNum() && cmd->parms.exists("message") && cmd->parms["message"].isStr()) {
			int channel_idx = cmd->parms["channel"].get_int();
			string msg = cmd->parms["message"].get_str();
			if (channel_idx >= 0 && channel_idx <= MESHCORE_HIGHEST_CHANNEL && !msg.empty()) {
				queue_packet_send_channel_msg(channel_idx, msg);
			} else {
				printf("Error in send_direct_msg: channel or message is empty, invalid, or not set!\n");
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
					MESHCORE_TEXT_TYPES txt_type = (cmd->parms.exists("txt_type") && cmd->parms["txt_type"].isNum()) ? (MESHCORE_TEXT_TYPES)cmd->parms["txt_type"].get_int() : TXT_TYPE_PLAIN;
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

	printf("Received unknown command '%s' over MQTT.\n", cmd->cmd.c_str());
	printf("Data: %s\n", cmd->parms.write(1).c_str());
}
