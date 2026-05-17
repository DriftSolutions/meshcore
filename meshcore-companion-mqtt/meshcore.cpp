/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

list<shared_ptr<MeshCoreCommand>> outgoing_commands;
map<uint32, shared_ptr<MeshCoreCommand>> outgoing_messages;
shared_ptr<MeshCoreCommand> current_outgoing_command;

void handle_incoming_packets();

MeshCoreCommandAcknowledged::~MeshCoreCommandAcknowledged() {}

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

inline void handle_outgoing_commands() {
	// Time out the current going command if one is in progress and we've waited too long for a reply.
	if (current_outgoing_command) {
		auto& cur = current_outgoing_command;
		if (GetTickCount64() > cur->time_limit) {
			//give up on this command :(
			printf("[meshcore] Outgoing command %s timed out while waiting for a reply!\n", GetMeshCoreCommandString(cur->getType()).c_str());
			current_outgoing_command.reset();
		}
	}

	// Process the next outgoing command if there is one.
	if (current_outgoing_command.get() == NULL && outgoing_commands.size()) {
		uint64 ticks = GetTickCount64();
		bool timeToSendMessage = (state.nextMessageTime == 0 || ticks > state.nextMessageTime);

		for (auto x = outgoing_commands.begin(); x != outgoing_commands.end(); x++) {
			auto& cur = *x;
			if (cur->is_message && config.meshcore.delayBetweenMessages) {
				// Only send text messages once every delayBetweenMessages milliseconds to help prevent flooding.
				if (timeToSendMessage) {
					current_outgoing_command = cur;
					outgoing_commands.erase(x);
					state.nextMessageTime = GetTickCount64() + config.meshcore.delayBetweenMessages;
					break;
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
				// Command data is too large, drop it.
				current_outgoing_command.reset();
			}

			cur->time_limit = GetTickCount64() + 5000;
			buffer_append_int<uint8>(&state.sendbuf, COMPANION_OUTGOING_FRAME_START);
			uint16 tmp = Get_ULE16((uint16)cur->data.length());
			buffer_append_int<uint16>(&state.sendbuf, tmp);
			buffer_append(&state.sendbuf, cur->data.c_str(), cur->data.length());

			mesh_log("Writing command %s with %zu bytes...\n", GetMeshCoreCommandString((MESHCORE_COMMAND_CODES)cur->data[0]).c_str(), cur->data.length());
			if (config.meshcore.log_to_console) {
				PrintData(stdout, (const uint8*)cur->data.c_str(), cur->data.length());
			}
			if (config.meshcore.log_fp != NULL) {
				PrintData(config.meshcore.log_fp, (const uint8*)cur->data.c_str(), cur->data.length());
			}
		}
	}
}

void meshcore_close() {
	if (config.io_driver->IsOpen()) {
		config.io_driver->Close();
	}

	current_outgoing_command.reset();
	outgoing_commands.clear();
	outgoing_messages.clear();
	state.reset();
}

void meshcore_work() {
	static int64 lastDriverOpen = 0;

	if (config.io_driver->IsOpen()) {
		static int64 lastMessageCheckReq = 0;
		static int64 lastContactsUpdateReq = 0;

		// Check for messages if we haven't received a RESPONSE_CODE_NO_MORE_MSGS in the last minute, just in case.
		if (time(NULL) - state.lastNoMoreMessages >= 60 && time(NULL) - lastMessageCheckReq >= 30) {
			queue_packet_get_message();
			lastMessageCheckReq = time(NULL);
		}

		// Get a list of changed contacts every 30 seconds, or the full list hourly.
		if (time(NULL) - lastContactsUpdateReq >= 30) {
			if (time(NULL) - state.lastContactsFullUpdate >= 3600) {
				update_contacts(true);
				lastContactsUpdateReq = time(NULL);
			} else if (time(NULL) - state.lastContactsPartialUpdate >= 60) {
				update_contacts(false);
				lastContactsUpdateReq = time(NULL);
			}
		}

		// Retry or time out messages that get ACKS (direct messages, etc.)
		timeout_outgoing_messages();
		handle_outgoing_commands();

		// Send any queued data to the node
		if (state.sendbuf.len > 0) {
			int n = config.io_driver->Write(state.sendbuf.udata, (int)state.sendbuf.len);
			if (n > 0) {
				buffer_remove_front(&state.sendbuf, n);
			} else if (n < 0) {
				printf("[meshcore] Error writing to I/O driver!\n");
				meshcore_close();
				return;
			}
		}

		// Read data from the node
		static uint8 buf[1024];
		int n = config.io_driver->Read(buf, sizeof(buf));
		if (n > 0) {
			buffer_append(&state.recvbuf, (const char*)buf, n);
			handle_incoming_packets();
		} else if (n == 0) {
			safe_sleep(1, true);
		} else if (n < 0) {
			printf("[meshcore] Error reading from I/O driver!\n");
			meshcore_close();
		}
	} else {
		if (time(NULL) - lastDriverOpen >= 5) {
			lastDriverOpen = time(NULL);
			if (config.io_driver->Open(config.meshcore.device)) {
				queue_packet_app_start();
				queue_packet_set_time();
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

bool MeshCoreCommand::expectsAck() {
	return (dynamic_cast<MeshCoreCommandAcknowledged *>(this) != NULL);
}
