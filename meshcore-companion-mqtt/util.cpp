/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

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

string GetMeshCoreCommandString(MESHCORE_COMMAND_CODES cmd) {
	switch (cmd) {
		case CMD_APP_START:               return "CMD_APP_START";
		case CMD_SEND_TXT_MSG:            return "CMD_SEND_TXT_MSG";
		case CMD_SEND_CHANNEL_TXT_MSG:    return "CMD_SEND_CHANNEL_TXT_MSG";
		case CMD_GET_CONTACTS:            return "CMD_GET_CONTACTS";
		case CMD_GET_DEVICE_TIME:         return "CMD_GET_DEVICE_TIME";
		case CMD_SET_DEVICE_TIME:         return "CMD_SET_DEVICE_TIME";
		case CMD_SEND_SELF_ADVERT:        return "CMD_SEND_SELF_ADVERT";
		case CMD_SET_ADVERT_NAME:         return "CMD_SET_ADVERT_NAME";
		case CMD_ADD_UPDATE_CONTACT:      return "CMD_ADD_UPDATE_CONTACT";
		case CMD_SYNC_NEXT_MESSAGE:       return "CMD_SYNC_NEXT_MESSAGE";
		case CMD_SET_RADIO_PARAMS:        return "CMD_SET_RADIO_PARAMS";
		case CMD_SET_RADIO_TX_POWER:      return "CMD_SET_RADIO_TX_POWER";
		case CMD_RESET_PATH:              return "CMD_RESET_PATH";
		case CMD_SET_ADVERT_LATLON:       return "CMD_SET_ADVERT_LATLON";
		case CMD_REMOVE_CONTACT:          return "CMD_REMOVE_CONTACT";
		case CMD_SHARE_CONTACT:           return "CMD_SHARE_CONTACT";
		case CMD_EXPORT_CONTACT:          return "CMD_EXPORT_CONTACT";
		case CMD_IMPORT_CONTACT:          return "CMD_IMPORT_CONTACT";
		case CMD_REBOOT:                  return "CMD_REBOOT";
		case CMD_GET_BATT_AND_STORAGE:    return "CMD_GET_BATT_AND_STORAGE";
		case CMD_SET_TUNING_PARAMS:       return "CMD_SET_TUNING_PARAMS";
		case CMD_DEVICE_QEURY:            return "CMD_DEVICE_QEURY";
		case CMD_EXPORT_PRIVATE_KEY:      return "CMD_EXPORT_PRIVATE_KEY";
		case CMD_IMPORT_PRIVATE_KEY:      return "CMD_IMPORT_PRIVATE_KEY";
		case CMD_SEND_RAW_DATA:           return "CMD_SEND_RAW_DATA";
		case CMD_SEND_LOGIN:              return "CMD_SEND_LOGIN";
		case CMD_SEND_STATUS_REQ:         return "CMD_SEND_STATUS_REQ";
		case CMD_HAS_CONNECTION:          return "CMD_HAS_CONNECTION";
		case CMD_LOGOUT:                  return "CMD_LOGOUT";
		case CMD_GET_CONTACT_BY_KEY:      return "CMD_GET_CONTACT_BY_KEY";
		case CMD_GET_CHANNEL:             return "CMD_GET_CHANNEL";
		case CMD_SET_CHANNEL:             return "CMD_SET_CHANNEL";
		case CMD_SIGN_START:              return "CMD_SIGN_START";
		case CMD_SIGN_DATA:               return "CMD_SIGN_DATA";
		case CMD_SIGN_FINISH:             return "CMD_SIGN_FINISH";
		case CMD_SEND_TRACE_PATH:         return "CMD_SEND_TRACE_PATH";
		case CMD_SET_DEVICE_PIN:          return "CMD_SET_DEVICE_PIN";
		case CMD_SET_OTHER_PARAMS:        return "CMD_SET_OTHER_PARAMS";
		case CMD_SEND_TELEMETRY_REQ:      return "CMD_SEND_TELEMETRY_REQ";
		case CMD_GET_CUSTOM_VARS:         return "CMD_GET_CUSTOM_VARS";
		case CMD_SET_CUSTOM_VAR:          return "CMD_SET_CUSTOM_VAR";
		case CMD_GET_ADVERT_PATH:         return "CMD_GET_ADVERT_PATH";
		case CMD_GET_TUNING_PARAMS:       return "CMD_GET_TUNING_PARAMS";
		case CMD_BINARY_REQ:              return "CMD_BINARY_REQ";
		case CMD_FACTORY_RESET:           return "CMD_FACTORY_RESET";
		case CMD_PATH_DISCOVERY:          return "CMD_PATH_DISCOVERY";
		case CMD_SET_FLOOD_SCOPE:         return "CMD_SET_FLOOD_SCOPE";
		case CMD_SEND_CONTROL_DATA:       return "CMD_SEND_CONTROL_DATA";
		case CMD_GET_STATS:               return "CMD_GET_STATS";
		case CMD_SEND_ANON_REQ:           return "CMD_SEND_ANON_REQ";
		case CMD_SET_AUTOADD_CONFIG:      return "CMD_SET_AUTOADD_CONFIG";
		case CMD_GET_AUTOADD_CONFIG:      return "CMD_GET_AUTOADD_CONFIG";
		case CMD_GET_ALLOWED_REPEAT_FREQ: return "CMD_GET_ALLOWED_REPEAT_FREQ";
		case CMD_SET_PATH_HASH_MODE:      return "CMD_SET_PATH_HASH_MODE";
		case CMD_SEND_CHANNEL_DATA:       return "CMD_SEND_CHANNEL_DATA";
		case CMD_SET_DEFAULT_FLOOD_SCOPE: return "CMD_SET_DEFAULT_FLOOD_SCOPE";
		case CMD_GET_DEFAULT_FLOOD_SCOPE: return "CMD_GET_DEFAULT_FLOOD_SCOPE";
		default: {
			char buf[32];
			snprintf(buf, sizeof(buf), "UNKNOWN (0x%02X)", (uint8)cmd);
			return buf;
		}
	}
}

string GetMeshCoreResponseString(MESHCORE_RESPONSE_CODES resp) {
	switch (resp) {
		case RESPONSE_CODE_OK:                   return "RESPONSE_CODE_OK";
		case RESPONSE_CODE_ERROR:                return "RESPONSE_CODE_ERROR";
		case RESPONSE_CODE_CONTACT_START:        return "RESPONSE_CODE_CONTACT_START";
		case RESPONSE_CODE_CONTACT:              return "RESPONSE_CODE_CONTACT";
		case RESPONSE_CODE_CONTACT_END:          return "RESPONSE_CODE_CONTACT_END";
		case RESPONSE_CODE_SELF_INFO:            return "RESPONSE_CODE_SELF_INFO";
		case RESPONSE_CODE_MSG_SENT:             return "RESPONSE_CODE_MSG_SENT";
		case RESPONSE_CODE_CONTACT_MSG_RECV:     return "RESPONSE_CODE_CONTACT_MSG_RECV";
		case RESPONSE_CODE_CHANNEL_MSG_RECV:     return "RESPONSE_CODE_CHANNEL_MSG_RECV";
		case RESPONSE_CODE_CURRENT_TIME:         return "RESPONSE_CODE_CURRENT_TIME";
		case RESPONSE_CODE_NO_MORE_MSGS:         return "RESPONSE_CODE_NO_MORE_MSGS";
		case RESPONSE_CODE_BATTERY:              return "RESPONSE_CODE_BATTERY";
		case RESPONSE_CODE_DEVICE_INFO:          return "RESPONSE_CODE_DEVICE_INFO";
		case RESPONSE_CODE_CONTACT_MSG_RECV_V3:  return "RESPONSE_CODE_CONTACT_MSG_RECV_V3";
		case RESPONSE_CODE_CHANNEL_MSG_RECV_V3:  return "RESPONSE_CODE_CHANNEL_MSG_RECV_V3";
		case RESPONSE_CODE_CHANNEL_INFO:         return "RESPONSE_CODE_CHANNEL_INFO";
		case RESPONSE_CODE_ADVERTISEMENT:        return "RESPONSE_CODE_ADVERTISEMENT";
		case RESPONSE_CODE_ACK:                  return "RESPONSE_CODE_ACK";
		case RESPONSE_CODE_MESSAGES_WAITING:     return "RESPONSE_CODE_MESSAGES_WAITING";
		case RESPONSE_CODE_STATUS_RESPONSE:      return "RESPONSE_CODE_STATUS_RESPONSE";
		case RESPONSE_CODE_LOG_DATA:             return "RESPONSE_CODE_LOG_DATA";
		case PUSH_CODE_NEW_ADVERT:               return "PUSH_CODE_NEW_ADVERT";
		case RESPONSE_CODE_CONTACT_DELETED:      return "RESPONSE_CODE_CONTACT_DELETED";
		default: {
			char buf[32];
			snprintf(buf, sizeof(buf), "UNKNOWN (0x%02X)", (uint8)resp);
			return buf;
		}
	}
}

string trim_nulls(const string& str) {
	size_t n = str.find((char)0, 0);
	if (n != str.npos) {
		return str.substr(0, n);
	}
	return str;
}

string trim_nulls(const char* str, size_t len) {
	return trim_nulls(string(str, len));
}
