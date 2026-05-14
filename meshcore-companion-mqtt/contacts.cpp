/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

bool get_user_by_pubkey(const string& pubkey, shared_ptr<MeshCoreUser>& u) {
	auto x = state.users.find(pubkey);
	if (x != state.users.end()) {
		u = x->second;
		return true;
	}
	return false;
}

bool get_user_by_pubkey_prefix(const string& pubkey_prefix, shared_ptr<MeshCoreUser>& out) {
	for (auto& u : state.users) {
		if (!stricmp(u.second->pubkey_prefix, pubkey_prefix.c_str())) {
			out = u.second;
			return true;
		}
	}
	return false;
}

void del_user_by_pubkey(const string& pubkey) {
	auto x = state.users.find(pubkey);
	if (x != state.users.end()) {
		state.users.erase(x);
	}
}

void add_or_update_user(_PACKET_CONTACT* c) {
	UniValue obj(UniValue::VOBJ);
	string pubkey = bin2hex(c->public_key, sizeof(c->public_key));

	shared_ptr<MeshCoreUser> u;
	bool is_new = false;
	if (!get_user_by_pubkey(pubkey, u)) {
		u = make_shared<MeshCoreUser>();
		u->updateMeshCorePubKey(pubkey);
		is_new = true;
	}

	sstrcpy(u->name, trim_nulls(c->adv_name).c_str());
	u->type = c->type;
	u->latitude = double(Get_SLE32(c->adv_lat)) / 1000000.0f;
	u->longitude = double(Get_SLE32(c->adv_lon)) / 1000000.0f;
	u->flags = c->flags;
	u->last_hops = c->out_path_len;
	u->last_seen = max(u->last_seen, (int64)Get_ULE32(c->lastmod));
	u->last_seen = max(u->last_seen, (int64)Get_ULE32(c->last_advert));

	if (is_new) {
		if (config.meshcore.maxContactsListSize) {
			while (state.users.size() >= config.meshcore.maxContactsListSize) {
				// delete the oldest seen user
				userMap::iterator toDel = state.users.end();
				for (auto x = state.users.begin(); x != state.users.end(); x++) {
					if (toDel == state.users.end() || x->second->last_seen < toDel->second->last_seen) {
						toDel = x;
					}
				}
				if (toDel != state.users.end()) {
					state.users.erase(toDel);
				}
			}
		}

		state.users[pubkey] = u;

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
	for (auto x = state.users.begin(); x != state.users.end(); ) {
		if (x->second->last_seen < ts) {
			x = state.users.erase(x);
		} else {
			x++;
		}
	}
}

void MeshCoreUser::ToUniValue(UniValue& obj) {
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
	for (auto& u : state.users) {
		UniValue obj(UniValue::VOBJ);
		u.second->ToUniValue(obj);
		root.pushKV(u.first, obj);
	}
	if (state.users.size() && mqtt_send(config.mqtt.topic_prefix + "/contacts", root, true)) {
		state.haveSentContactsList = true;
		return true;
	}
	return false;
}

bool mqtt_send_new_contact(shared_ptr<MeshCoreUser>& u) {
	UniValue obj(UniValue::VOBJ);
	u->ToUniValue(obj);
	return mqtt_send(config.mqtt.topic_prefix + "/new_contact", obj, false);
}
