/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"

void MeshCoreUser::updateMeshCorePubKey(const string& pubkey) {
	assert(pubkey.length() == MESHCORE_PUBKEY_LEN && strspn(pubkey.c_str(), "0123456789abcdef") == pubkey.length());
	sstrcpy(_meshcore_pubkey, pubkey.c_str());
	sstrcpy(_meshcore_pubkey_prefix, pubkey.c_str());
	updateHostmask();
	int x = 1;
}

void MeshCoreUser::updateMeshCorePubKeyPrefix(const string& pubkey_prefix) {
	assert(pubkey_prefix.length() == MESHCORE_PUBKEY_PREFIX_LEN && strspn(pubkey_prefix.c_str(), "0123456789abcdef") == pubkey_prefix.length());
	sstrcpy(_meshcore_pubkey_prefix, pubkey_prefix.c_str());
	updateHostmask();
	int x = 1;
}

void MeshCoreUser::updateSeen(int hops) {
	_last_seen = time(NULL);
	if (hops != UNKNOWN_HOPS && hops != -1) {
		printf("hops: %d\n", hops);
	}
	if (hops == 0xFF) {
		// direct send
		hops = 0;
	}
	if (hops >= 0 && hops <= 64) {
		last_hops = hops;
	}
}

void MeshCoreUser::updateMeshCoreNick(const string& nick) {
	sstrcpy(_meshcore_nick, nick.c_str());
	sstrcpy(_irc_nick, get_sanitized_nick(nick).c_str());
	updateHostmask();
	int x = 1;
}

void MeshCoreUser::updateHostmask() {
	const char* host = "unknown";
	if (meshcore_pubkey[0]) {
		host = meshcore_pubkey;
	} else if (meshcore_pubkey_prefix[0]) {
		host = meshcore_pubkey_prefix;
	}
	_hostmask = mprintf("%s!meshcore@%s", irc_nick, host);
}

void MeshCoreChannel::setNameFromMeshCore(const string& name) {	
	sstrcpy(_meshcore_name, name.c_str());
	sstrcpy(_irc_name, "#");
	sstrcat(_irc_name, get_sanitized_nick(name[0] == '#' ? name.substr(1) : name).c_str());
}
