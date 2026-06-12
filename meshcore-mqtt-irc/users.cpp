/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"

map<MeshCoreUser *, shared_ptr<MeshCoreUser>> users;

bool MeshCoreUser::shouldExpireUser() {
	return (last_seen < time(NULL) - config.irc.expire_users_after);
}

bool MeshCoreUser::isOurNode() {
	return (this == config.self_user.get());
}

bool get_user_by_meshcore_name(const string& name, shared_ptr<MeshCoreUser>& u) {
	if (!name.empty()) {
		for (auto& x : users) {
			if (!stricmp(x.second->meshcore_nick, name.c_str())) {
				u = x.second;
				return true;
			}
		}
	}
	return false;
}

bool get_user_by_irc_nick(const string& irc_nick, shared_ptr<MeshCoreUser>& u) {
	if (!irc_nick.empty()) {
		for (auto& x : users) {
			if (!stricmp(x.second->irc_nick, irc_nick.c_str())) {
				u = x.second;
				return true;
			}
		}
	}
	return false;
}

bool get_user_by_hostmask(const string& hostmask, shared_ptr<MeshCoreUser>& u) {
	if (!hostmask.empty()) {
		for (auto& x : users) {
			if (!stricmp(x.second->hostmask.c_str(), hostmask.c_str())) {
				u = x.second;
				return true;
			}
		}
	}
	return false;
}

bool get_user_by_pubkey(const string& pubkey, shared_ptr<MeshCoreUser>& u) {
	if (!pubkey.empty()) {
		for (auto& x : users) {
			if (!stricmp(x.second->meshcore_pubkey, pubkey.c_str())) {
				u = x.second;
				return true;
			}
		}
	}
	return false;
}

bool get_user_by_pubkey_prefix(const string& pubkey_prefix, shared_ptr<MeshCoreUser>& u) {
	if (!pubkey_prefix.empty()) {
		for (auto& x : users) {
			if (!stricmp(x.second->meshcore_pubkey_prefix, pubkey_prefix.c_str())) {
				u = x.second;
				return true;
			}
		}
	}
	return false;
}

bool add_user(const string& name, const string& pubkey, const string& pubkey_prefix, uint8 flags, int32 hops, shared_ptr<MeshCoreUser>* out) {
	if (name.empty()) { return false; }

	shared_ptr<MeshCoreUser> u;
	bool was_found_by_name = false;
	if (pubkey.empty() || !get_user_by_pubkey(pubkey, u)) {
		if (pubkey_prefix.empty() || !get_user_by_pubkey_prefix(pubkey_prefix, u)) {
			if (get_user_by_meshcore_name(name, u)) {
				was_found_by_name = true;
			}
		}
	}

	if (u) {
		if (out != NULL) {
			*out = u;
		}
		u->meshcore_flags = flags;
		u->updateSeen(hops);
		if (strcmp(name.c_str(), u->meshcore_nick)) {
			// if their nickname has changed, update it
			if (!was_found_by_name) {
				// only send nick change notices if the user was found by pubkey so we know it's the same person
				string new_nick = get_sanitized_nick(name);
				if (strcmp(u->irc_nick, new_nick.c_str())) { // the MeshCore nick could change without actually changing the IRC nick
					SendNickChangeNoticesFor(u.get(), new_nick);
				}
			}
			u->updateMeshCoreNick(name);
		}
		if (!pubkey.empty() && stricmp(pubkey.c_str(), u->meshcore_pubkey)) {
			// if their pubkey has changed, update it (shouldn't happen unless it's filling in someone from a channel message we don't already know, or if someone is using the same nick on 2+ nodes)
			u->updateMeshCorePubKey(pubkey);
			/*
			if (!stricmp(pubkey.c_str(), config.self_user->meshcore_pubkey)) {
				config.self_user = u;
			}
			*/
		} else if (pubkey.empty() && !pubkey_prefix.empty() && u->meshcore_pubkey[0] == 0 && stricmp(pubkey_prefix.c_str(), u->meshcore_pubkey_prefix)) {
			// If we don't have their full pubkey, but only have the prefix and it has changed
			u->updateMeshCorePubKeyPrefix(pubkey_prefix);
		}

		return true;
	}

	u = make_shared<MeshCoreUser>();
	u->updateMeshCoreNick(name);
	if (!pubkey.empty()) {
		u->updateMeshCorePubKey(pubkey);
		/*
		if (!stricmp(pubkey.c_str(), config.self_user->meshcore_pubkey)) {
			config.self_user = u;
		}
		*/
	} else if (!pubkey_prefix.empty()) {
		u->updateMeshCorePubKeyPrefix(pubkey_prefix);
	}

	u->meshcore_flags = flags;
	u->updateSeen(hops);

	users[u.get()] = u;
	if (out != NULL) {
		*out = u;
	}
	//printf("[irc] Added user %s -> %s (%s)\n", u->meshcore_nick, u->irc_nick, u->hostmask.c_str());

	return true;
}

bool MeshCoreUser::hadRecentAction() {
	return (last_action_time > time(NULL) - config.irc.idle_users_after);
}

void MeshCoreUser::onAction() {
	_last_action_time = _last_seen = time(NULL);

	if (is_away) {
		_is_away = false;

		if (isOurNode()) {
			vector<string> parms = {
				":" + config.irc.server_hostname,
				RPL_UNAWAY,
				config.self_user->irc_nick,
				"You are no longer marked as being away"
			};
			SendLineToAllAuthenticatedClients(parms);
		}
	}
}

void MeshCoreUser::markAway() {
	if (!is_away) {
		_is_away = true;

		if (isOurNode()) {
			vector<string> parms = {
				":" + config.irc.server_hostname,
				RPL_NOWAWAY,
				config.self_user->irc_nick,
				"You have been marked as being away due to inactivity"
			};
			SendLineToAllAuthenticatedClients(parms);
		}
	}
}
