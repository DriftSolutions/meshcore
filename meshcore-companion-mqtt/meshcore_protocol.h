/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MESHCORE_PROTOCOL_H
#define MESHCORE_PROTOCOL_H

// Command codes
enum MESHCORE_COMMAND_CODES : uint8 {
	CMD_APP_START                   = 0x01,
	CMD_SEND_TXT_MSG                = 0x02,
	CMD_SEND_CHANNEL_TXT_MSG        = 0x03,
	CMD_GET_CONTACTS                = 0x04,
	CMD_GET_DEVICE_TIME             = 0x05,
	CMD_SET_DEVICE_TIME             = 0x06,
	CMD_SEND_SELF_ADVERT            = 0x07,
	CMD_SET_ADVERT_NAME             = 0x08,
	CMD_ADD_UPDATE_CONTACT          = 0x09,
	CMD_SYNC_NEXT_MESSAGE           = 0x0A,
	CMD_SET_RADIO_PARAMS            = 0x0B,
	CMD_SET_RADIO_TX_POWER          = 0x0C,
	CMD_RESET_PATH                  = 0x0D,
	CMD_SET_ADVERT_LATLON           = 0x0E,
	CMD_REMOVE_CONTACT              = 0x0F,
	CMD_SHARE_CONTACT               = 0x10,
	CMD_EXPORT_CONTACT              = 0x11,
	CMD_IMPORT_CONTACT              = 0x12,
	CMD_REBOOT                      = 0x13,
	CMD_GET_BATT_AND_STORAGE        = 0x14,
	CMD_SET_TUNING_PARAMS           = 0x15,
	CMD_DEVICE_QEURY                = 0x16,
	CMD_EXPORT_PRIVATE_KEY          = 0x17,
	CMD_IMPORT_PRIVATE_KEY          = 0x18,
	CMD_SEND_RAW_DATA               = 0x19,
	CMD_SEND_LOGIN                  = 0x1A,
	CMD_SEND_STATUS_REQ             = 0x1B,
	CMD_HAS_CONNECTION              = 0x1C,
	CMD_LOGOUT                      = 0x1D,  // 'Disconnect'
	CMD_GET_CONTACT_BY_KEY          = 0x1E,
	CMD_GET_CHANNEL                 = 0x1F,
	CMD_SET_CHANNEL                 = 0x20,
	CMD_SIGN_START                  = 0x21,
	CMD_SIGN_DATA                   = 0x22,
	CMD_SIGN_FINISH                 = 0x23,
	CMD_SEND_TRACE_PATH             = 0x24,
	CMD_SET_DEVICE_PIN              = 0x25,
	CMD_SET_OTHER_PARAMS            = 0x26,
	CMD_SEND_TELEMETRY_REQ          = 0x27,  // can deprecate this
	CMD_GET_CUSTOM_VARS             = 0x28,
	CMD_SET_CUSTOM_VAR              = 0x29,
	CMD_GET_ADVERT_PATH             = 0x2A,
	CMD_GET_TUNING_PARAMS           = 0x2B,
	// NOTE: CMD range 0x2C..0x31 parked, potentially for WiFi operations
	CMD_BINARY_REQ                  = 0x32,
	CMD_FACTORY_RESET               = 0x33,
	CMD_PATH_DISCOVERY              = 0x34,
	CMD_SET_FLOOD_SCOPE             = 0x36,
	CMD_SEND_CONTROL_DATA           = 0x37,
	CMD_SEND_ANON_REQ               = 0x39,
	CMD_SET_AUTOADD_CONFIG          = 0x3A,
	CMD_GET_AUTOADD_CONFIG          = 0x3B,
	CMD_GET_ALLOWED_REPEAT_FREQ     = 0x3C,
	CMD_GET_STATS                   = 0x38,
	CMD_SET_PATH_HASH_MODE          = 0x3D,
	CMD_SEND_CHANNEL_DATA           = 0x3E,
	CMD_SET_DEFAULT_FLOOD_SCOPE     = 0x3F,
	CMD_GET_DEFAULT_FLOOD_SCOPE     = 0x40,
	CMD_UNKNOWN                     = 0xFF
};

// Response codes - https://docs.meshcore.io/companion_protocol/#response-parsing
enum MESHCORE_RESPONSE_CODES : uint8 {
	RESPONSE_CODE_OK                   = 0x00,
	RESPONSE_CODE_ERROR                = 0x01,
	RESPONSE_CODE_CONTACT_START        = 0x02,
	RESPONSE_CODE_CONTACT              = 0x03,
	RESPONSE_CODE_CONTACT_END          = 0x04,
	RESPONSE_CODE_SELF_INFO            = 0x05,
	RESPONSE_CODE_MSG_SENT             = 0x06,
	RESPONSE_CODE_CONTACT_MSG_RECV     = 0x07,
	RESPONSE_CODE_CHANNEL_MSG_RECV     = 0x08,
	RESPONSE_CODE_CURRENT_TIME         = 0x09,
	RESPONSE_CODE_NO_MORE_MSGS         = 0x0A,
	RESPONSE_CODE_BATTERY              = 0x0C,
	RESPONSE_CODE_DEVICE_INFO          = 0x0D,
	RESPONSE_CODE_CONTACT_MSG_RECV_V3  = 0x10,
	RESPONSE_CODE_CHANNEL_MSG_RECV_V3  = 0x11,
	RESPONSE_CODE_CHANNEL_INFO         = 0x12,
	RESPONSE_CODE_CHANNEL_DATA_RECV    = 0x1B,
	RESPONSE_CODE_ADVERTISEMENT        = 0x80,
	RESPONSE_CODE_ACK                  = 0x82,
	RESPONSE_CODE_MESSAGES_WAITING     = 0x83,
	RESPONSE_CODE_STATUS_RESPONSE      = 0x87,
	RESPONSE_CODE_LOG_DATA             = 0x88,
	PUSH_CODE_NEW_ADVERT               = 0x8A,
	RESPONSE_CODE_CONTACT_DELETED      = 0x8F,
};

enum MESHCORE_TEXT_TYPES : uint8 {
	TXT_TYPE_PLAIN = 0, // a plain text message
	TXT_TYPE_CLI_DATA = 1, // a CLI command
	TXT_TYPE_SIGNED_PLAIN = 2 // plain text, signed by sender
};

#pragma pack(1)

struct _PACKET_APP_START {
	MESHCORE_COMMAND_CODES packet_type = CMD_APP_START;
	uint8 app_ver = 0x03;
	uint8 reserved[6] = { 0 };
};

struct _PACKET_SELF_INFO {
/*
Byte 0: 0x05
Byte 1: Advertisement Type
Byte 2: TX Power
Byte 3: Max TX Power
Bytes 4-35: Public Key (32 bytes, hex)
Bytes 36-39: Advertisement Latitude (32-bit little-endian, divided by 1e6)
Bytes 40-43: Advertisement Longitude (32-bit little-endian, divided by 1e6)
Byte 44: Multi ACKs
Byte 45: Advertisement Location Policy
Byte 46: Telemetry Mode (bitfield)
Byte 47: Manual Add Contacts (bool)
Bytes 48-51: Radio Frequency (32-bit little-endian, divided by 1000.0)
Bytes 52-55: Radio Bandwidth (32-bit little-endian, divided by 1000.0)
Byte 56: Radio Spreading Factor
Byte 57: Radio Coding Rate
Bytes 58+: Device Name (UTF-8, variable length, no null terminator required)
*/
	MESHCORE_RESPONSE_CODES packet_type; // RESPONSE_CODE_SELF_INFO;
	uint8 advertisement_type;
	uint8 tx_power;
	uint8 max_tx_power;
	uint8 public_key[32];
	int32 latitude; // 32-bit little-endian, divided by 1e6
	int32 longitude; // 32-bit little-endian, divided by 1e6
	uint8 multi_acks;
	uint8 advertisement_location_policy;
	uint8 telemetry_mode;
	uint8 manual_add_contacts; // bool
	uint32 radio_frequency; // 32-bit little-endian, divided by 1000.0
	uint32 radio_bandwidth; // 32-bit little-endian, divided by 1000.0
	uint8 radio_spreading_factor;
	uint8 radio_coding_rate;
	//Bytes 58 + : Device Name(UTF - 8, variable length, no null terminator required)
};

struct _PACKET_DEVICE_INFO {
/*
	Byte 0: 0x0D
	Byte 1: Firmware Version (uint8)
	Bytes 2+: Variable length based on firmware version

	For firmware version >= 3:
	Byte 2: Max Contacts Raw (uint8, actual = value * 2)
	Byte 3: Max Channels (uint8)
	Bytes 4-7: BLE PIN (32-bit little-endian)
	Bytes 8-19: Firmware Build (12 bytes, UTF-8, null-padded)
	Bytes 20-59: Model (40 bytes, UTF-8, null-padded)
	Bytes 60-79: Version (20 bytes, UTF-8, null-padded)
	Byte 80: Client repeat enabled/preferred (firmware v9+)
	Byte 81: Path hash mode (firmware v10+)
*/
	MESHCORE_RESPONSE_CODES packet_type;       // RESPONSE_CODE_DEVICE_INFO
	uint8  firmware_version;
	// firmware version >= 3:
	uint8  max_contacts_raw;  // actual = value * 2
	uint8  max_channels;
	uint32 ble_pin;
	char   firmware_build[12];
	char   model[40];
	char   version[20];
	uint8  client_repeat;     // firmware v9+
	uint8  path_hash_mode;    // firmware v10+
};

struct _PACKET_CHANNEL_INFO {
	/*
		Byte 0 : 0x12
		Byte 1 : Channel Index
		Bytes 2 - 33 : Channel Name(32 bytes, null - terminated)
		Bytes 34 - 49 : Secret(16 bytes)
	*/
	MESHCORE_RESPONSE_CODES packet_type;      // RESPONSE_CODE_CHANNEL_INFO
	uint8 channel_index;
	char  channel_name[32];
	uint8 secret[16];
};

struct _PACKET_BATTERY {
	/*
Byte 0: 0x0C
Bytes 1-2: Battery Voltage (16-bit little-endian, millivolts)
Bytes 3-6: Used Storage (32-bit little-endian, KB)
Bytes 7-10: Total Storage (32-bit little-endian, KB)
	*/
	MESHCORE_RESPONSE_CODES packet_type;      // RESPONSE_CODE_BATTERY
	uint16 battery_voltage;  // millivolts
	uint32 used_storage;     // KB
	uint32 total_storage;    // KB
};

struct _PACKET_MSG_SENT {
	/*
Byte 0: 0x06
Byte 1: Route Flag (0 = direct, 1 = flood)
Bytes 2-5: Tag / Expected ACK (4 bytes, little-endian)
Bytes 6-9: Suggested Timeout (32-bit little-endian, milliseconds)
	*/
	MESHCORE_RESPONSE_CODES packet_type;        // RESPONSE_CODE_MSG_SENT
	uint8  route_flag;         // 0 = direct, 1 = flood
	uint32 tag;                // expected ACK
	uint32 suggested_timeout;  // milliseconds
};

struct _PACKET_ACK {
	/*
Byte 0: 0x82
Bytes 1-4: ACK Code (4 bytes, hex)
Bytes 5+: CRC/other data?
	*/
	MESHCORE_RESPONSE_CODES packet_type;  // RESPONSE_CODE_ACK
	uint8 ack_code[4];
};

struct _PACKET_CONTACT_MSG_RECV {
	/*
Byte 0: 0x07 (packet type)
Bytes 1-6: Public Key Prefix (6 bytes, hex)
Byte 7: Path Length
Byte 8: Text Type
Bytes 9-12: Timestamp (32-bit little-endian)
Bytes 13-16: Signature (4 bytes, only if txt_type == 2)
Bytes 17+: Message Text (UTF-8)
	*/
	MESHCORE_RESPONSE_CODES packet_type;         // RESPONSE_CODE_CONTACT_MSG_RECV
	uint8  public_key_prefix[6];
	uint8  path_length;
	MESHCORE_TEXT_TYPES  text_type;
	uint32 timestamp;
	uint8  signature[4];        // only present if text_type == 2
	// Bytes 17+: Message Text (UTF-8, variable length)
};

struct _PACKET_CONTACT_MSG_RECV_V3 {
	/*
Byte 0: 0x10 (packet type)
Byte 1: SNR (signed byte, multiplied by 4)
Bytes 2-3: Reserved
Bytes 4-9: Public Key Prefix (6 bytes, hex)
Byte 10: Path Length
Byte 11: Text Type
Bytes 12-15: Timestamp (32-bit little-endian)
Bytes 16-19: Signature (4 bytes, only if txt_type == 2)
Bytes 20+: Message Text (UTF-8)
	*/
	MESHCORE_RESPONSE_CODES packet_type;         // RESPONSE_CODE_CONTACT_MSG_RECV_V3
	int8   snr;                 // multiplied by 4
	uint8  reserved[2];
	uint8  public_key_prefix[6];
	uint8  path_length;
	MESHCORE_TEXT_TYPES  text_type;
	uint32 timestamp;
	uint8  signature[4];        // only present if text_type == 2
	// Bytes 20+: Message Text (UTF-8, variable length)
};

struct _PACKET_CHANNEL_MSG_RECV {
	/*
Byte 0: 0x08 (packet type)
Byte 1: Channel Index (0-7)
Byte 2: Path Length
Byte 3: Text Type
Bytes 4-7: Timestamp (32-bit little-endian)
Bytes 8+: Message Text (UTF-8)
	*/
	MESHCORE_RESPONSE_CODES packet_type;    // RESPONSE_CODE_CHANNEL_MSG_RECV
	uint8  channel_index;  // 0-7
	uint8  path_length;
	uint8  text_type;
	uint32 timestamp;
	// Bytes 8+: Message Text (UTF-8, variable length)
};

struct _PACKET_CHANNEL_MSG_RECV_V3 {
	/*
Byte 0: 0x11 (packet type)
Byte 1: SNR (signed byte, multiplied by 4)
Bytes 2-3: Reserved
Byte 4: Channel Index (0-7)
Byte 5: Path Length
Byte 6: Text Type
Bytes 7-10: Timestamp (32-bit little-endian)
Bytes 11+: Message Text (UTF-8)
	*/
	MESHCORE_RESPONSE_CODES packet_type;    // RESPONSE_CODE_CHANNEL_MSG_RECV_V3
	int8   snr;            // multiplied by 4
	uint8  reserved[2];
	uint8  channel_index;  // 0-7
	uint8  path_length;
	uint8  text_type;
	uint32 timestamp;
	// Bytes 11+: Message Text (UTF-8, variable length)
};

struct _PACKET_CHANNEL_DATA_RECV {
	MESHCORE_RESPONSE_CODES packet_type;    // RESPONSE_CODE_CHANNEL_DATA_RECV
	int8   snr;            // multiplied by 4
	uint8  reserved[2];
	uint8  channel_index;  // 0-7
	uint8  path_length;
	uint16  data_type;
	// Data, variable length
};

enum CONTACT_TYPE : uint8 {
	ADV_TYPE_NONE = 0,
	ADV_TYPE_CHAT = 1,
	ADV_TYPE_REPEATER = 2,
	ADV_TYPE_ROOM = 3
};

struct _PACKET_CONTACT {
	MESHCORE_RESPONSE_CODES packet_type;   // RESPONSE_CODE_CONTACT
	uint8        public_key[32];
	CONTACT_TYPE type;          // one of ADV_TYPE_*
	uint8        flags;
	int8         out_path_len;
	uint8        out_path[64];
	char         adv_name[32];  // null-terminated
	uint32       last_advert;
	int32        adv_lat;       // advertised latitude * 1E6
	int32        adv_lon;       // advertised longitude * 1E6
	uint32       lastmod;
};

struct _PACKET_STATUS_RESPONSE {
	MESHCORE_RESPONSE_CODES packet_type;   // 0x87
	uint8 reserved;
	uint8 public_key_prefix[6];
	//status_data : bytes     // remainder of frame
};

struct _PACKET_STATUS_RESPONSE_REPEATER {
	uint16 batt_milli_volts;
	uint16 curr_tx_queue_len;
	int16  noise_floor;
	int16  last_rssi;
	uint32 n_packets_recv;
	uint32 n_packets_sent;
	uint32 total_air_time_secs;
	uint32 total_up_time_secs;
	uint32 n_sent_flood;
	uint32 n_sent_direct;
	uint32 n_recv_flood;
	uint32 n_recv_direct;
	uint16 err_events;
	int16  last_snr;
	uint16 n_direct_dups;
	uint16 n_flood_dups;
};

struct _PACKET_ADD_UPDATE_CONTACT {
	/*
code: byte,   // constant 9
public_key : bytes(32),
type : byte,   // one of ADV_TYPE_*
flags : byte,
out_path_len : signed - byte,
out_path : bytes(64),
adv_name : chars(32),    // null terminated
last_advert : uint32
(optional) adv_lat: int32,    // advertised latitude * 1E6
(optional)adv_lon: int32,    // advertised longitude * 1E6
*/
};

#pragma pack()

#endif // MESHCORE_PROTOCOL_H
