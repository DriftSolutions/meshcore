/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

bool get_contact_by_pubkey(const string& pubkey, shared_ptr<MeshCoreContact>& u) {
	auto x = state.contacts.find(pubkey);
	if (x != state.contacts.end()) {
		u = x->second;
		return true;
	}
	return false;
}

bool get_contact_by_pubkey_prefix(const string& pubkey_prefix, shared_ptr<MeshCoreContact>& out) {
	for (auto& u : state.contacts) {
		if (!stricmp(u.second->pubkey_prefix, pubkey_prefix.c_str())) {
			out = u.second;
			return true;
		}
	}
	return false;
}

string get_pubkey_from_pubkey_or_prefix(const string& pubkey_or_prefix) {
	if (pubkey_or_prefix.length() == MESHCORE_PUBKEY_LEN) {
		return pubkey_or_prefix;
	}

	shared_ptr<MeshCoreContact> u;
	if (u) {
		return u->pubkey;
	}

	return "";
}

void del_contact_by_pubkey(const string& pubkey) {
	auto x = state.contacts.find(pubkey);
	if (x != state.contacts.end()) {
		state.contacts.erase(x);
	}
}

void add_or_update_contact(_PACKET_CONTACT* c) {
	UniValue obj(UniValue::VOBJ);
	string pubkey = bin2hex(c->public_key, sizeof(c->public_key));

	char name[MESHCORE_MAX_NICK_LEN + 1] = { 0 };
	sstrcpy(name, trim_nulls(c->adv_name).c_str());
	if (!IsValidUTF8(name)) {
		char* p = (char *)FirstInvalidUTF8(name);
		while (p != NULL && name[0]) {
			*p = '_';
			p = (char*)FirstInvalidUTF8(name);
		}
		if (name[0] == 0) {
			printf("Ignoring user %s - nothing left after invalid UTF-8\n", trim_nulls(c->adv_name).c_str());
			return;
		}
	}

	shared_ptr<MeshCoreContact> u;
	bool is_new = false;
	if (!get_contact_by_pubkey(pubkey, u)) {
		u = make_shared<MeshCoreContact>();
		u->updateMeshCorePubKey(pubkey);
		is_new = true;
	}

	sstrcpy(u->name, name);
	u->type = c->type;
	u->latitude = double(Get_SLE32(c->adv_lat)) / 1000000.0f;
	u->longitude = double(Get_SLE32(c->adv_lon)) / 1000000.0f;
	u->flags = c->flags;
	u->last_hops = c->out_path_len;
	u->last_seen = max(u->last_seen, (int64)Get_ULE32(c->lastmod));
	u->last_seen = max(u->last_seen, (int64)Get_ULE32(c->last_advert));

	if (is_new) {
		if (config.meshcore.maxContactsListSize) {
			while (state.contacts.size() >= config.meshcore.maxContactsListSize) {
				// delete the oldest seen user
				contactMap::iterator toDel = state.contacts.end();
				for (auto x = state.contacts.begin(); x != state.contacts.end(); x++) {
					if (toDel == state.contacts.end() || x->second->last_seen < toDel->second->last_seen) {
						toDel = x;
					}
				}
				if (toDel != state.contacts.end()) {
					state.contacts.erase(toDel);
				}
			}
		}

		state.contacts[pubkey] = u;

		if (state.haveSentContactsList) {
			mqtt_send_new_contact(u);
		}
	}

	//obj.pushKV("version", trim_nulls(di->version, sizeof(di->version)));
	//mqtt_send(mprintf("%s/channel_info/%u", config.mqtt.topic_prefix.c_str(), ci->channel_index), obj, true);
	int x = 1;
}

void clear_old_contacts() {
	int64 ts = time(NULL) - config.meshcore.expireUnseenContacts;
	for (auto x = state.contacts.begin(); x != state.contacts.end(); ) {
		if (x->second->last_seen < ts) {
			x = state.contacts.erase(x);
		} else {
			x++;
		}
	}
}

void MeshCoreContact::ToUniValue(UniValue& obj) {
	obj.clear();
	obj.setObject();
	obj.pushKV("type", type);
	obj.pushKV("public_key", pubkey);
	obj.pushKV("name", name);
	obj.pushKV("latitude", latitude);
	obj.pushKV("longitude", longitude);
	obj.pushKV("flags", flags);
	obj.pushKV("out_path_len", last_hops);
	obj.pushKV("last_seen", last_seen);
}

bool mqtt_send_contacts() {
	UniValue root(UniValue::VOBJ);
	for (auto& u : state.contacts) {
		UniValue obj(UniValue::VOBJ);
		u.second->ToUniValue(obj);
		root.pushKV(u.first, obj);
	}
	if (state.contacts.size() && mqtt_send(config.mqtt.topic_prefix + "/contacts", root, true)) {
		state.haveSentContactsList = true;
		return true;
	}
	return false;
}

bool mqtt_send_new_contact(shared_ptr<MeshCoreContact>& u) {
	UniValue obj(UniValue::VOBJ);
	u->ToUniValue(obj);
	return mqtt_send(config.mqtt.topic_prefix + "/new_contact", obj, false);
}
