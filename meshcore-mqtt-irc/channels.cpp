/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"

map<int, shared_ptr<MeshCoreChannel>> chans;

void add_channel(int idx, const string& meshcore_name, bool is_private) {
	if (idx < 0 || idx > MESHCORE_HIGHEST_CHANNEL) {
		return;
	}

	shared_ptr<MeshCoreChannel> c;
	if (get_channel_by_meshcore_index(idx, c)) {
		// we already have it

		if (!strcmp(c->meshcore_name, meshcore_name.c_str())) {
			// no change
			return;
		}
		if (!strcmp(c->irc_name, get_channel_name_from_meshcore(meshcore_name).c_str())) {
			// no change
			return;
		}

		SendPartNoticesFor(config.self_user.get(), c->irc_name);

		if (meshcore_name.empty()) {
			printf("[irc] Left channel %d: %s\n", idx, c->meshcore_name);
			auto x = chans.find(idx);
			if (x != chans.end()) {
				chans.erase(x);
			}
		} else {
			printf("[irc] Channel %d changed: %s -> %s\n", idx, c->meshcore_name, meshcore_name.c_str());
		}

		//c->partUserFromChannel(config.self_user, true, true);
	}

	if (meshcore_name.empty()) {
		return;
	}

	c = make_shared<MeshCoreChannel>();
	c->meshcore_index = idx;
	c->setNameFromMeshCore(meshcore_name);
	c->is_private = is_private;
	c->topic.topic = GetChannelSetting(c->irc_name, "topic", "");
	c->topic.set_by = GetChannelSetting(c->irc_name, "topic_set_by", "");
	c->topic.timestamp = max((int64)0, (int64)atoi64(GetChannelSetting(c->irc_name, "topic_ts", "").c_str()));
	chans[idx] = c;

	printf("[irc] Added channel %d: %s -> %s\n", idx, c->meshcore_name, c->irc_name);

	if (!config.waitingForInitialState) {
		c->addUserToChannel(config.self_user);
	}
}

bool get_channel_by_meshcore_index(int idx, shared_ptr<MeshCoreChannel>& chan) {
	auto x = chans.find(idx);
	if (x != chans.end()) {
		chan = x->second;
		return true;
	}
	return false;
}

bool get_channel_by_irc_name(const string& channel_name, shared_ptr<MeshCoreChannel>& chan) {
	for (auto& c : chans) {
		if (!stricmp(channel_name.c_str(), c.second->irc_name)) {
			chan = c.second;
			return true;
		}
	}
	return false;
}

void MeshCoreChannel::addUserToChannel(shared_ptr<MeshCoreUser>& u) {
	if (!isUserInChannel(u)) {
		auto urec = make_shared<MeshCoreChannelUser>(u);
		users.push_back(urec);
		SendJoinNoticesFor(u.get(), irc_name);
		if (u->isFavorite()) {
			SendUserModeNoticesFor(irc_name, "+o", u->irc_nick);
		}
	}
}

void MeshCoreChannel::partUserFromChannel(shared_ptr<MeshCoreUser>& user, bool send_part_notices, bool remove_from_user_list) {
	for (auto x = users.begin(); x != users.end(); x++) {
		auto u = *x;
		if (u->user.get() == user.get()) {
			if (send_part_notices) {
				SendPartNoticesFor(u->user.get(), irc_name);
			}
			if (remove_from_user_list) {
				users.erase(x);
			}
			break;
		}
	}
}

void MeshCoreChannel::updateTopic(const string& stopic, const string& set_by, int64 ts) {
	topic.topic = stopic;
	topic.set_by = set_by;
	topic.timestamp = (ts > 0) ? ts : time(NULL);

	SetChannelSetting(irc_name, "topic", stopic);
	SetChannelSetting(irc_name, "topic_set_by", set_by);
	SetChannelSetting(irc_name, "topic_ts", mprintf("%lld", topic.timestamp).c_str());
}

void MeshCoreChannel::handleIdleUsers() {
	for (auto x = users.begin(); x != users.end();) {
		auto u = *x;

		if (u->user->isOurNode()) {
			x++;
			continue;
		}

		if (u->shouldPartUser()) {
			partUserFromChannel(u->user, true, false);
			x = users.erase(x);
		} else if (u->has_voice && config.irc.auto_voice_idle_users && !u->hasSpokenRecently()) {
			SendUserModeNoticesFor(irc_name, "-v", u->user->irc_nick);
			u->has_voice = false;
			x++;
		} else {
			x++;
		}
	}
}

void MeshCoreChannel::sendPostJoinNotices(Client* c) {
	sendTopic(c);
	/*
	if (config.irc.auto_voice_idle_users && hasSpokenRecently(config.self_user->irc_nick)) {
		SendUserModeNoticesFor(irc_name, "+v", config.self_user->irc_nick);
	}
	*/

	sendNamesTo(c);
	SendUserModeNoticesFor(irc_name, "+q", config.self_user->irc_nick);
}

void MeshCoreChannel::sendTopic(Client* c) {
	if (topic.topic.empty()) {
		vector<string> parms = {
			irc_name,
			"No topic is set"
		};
		c->SendServerReply(RPL_NOTOPIC, { parms });
	} else {
		vector<string> parms = {
			irc_name,
			topic.topic
		};
		c->SendServerReply(RPL_TOPIC, { parms });

		parms = {
			irc_name,
			topic.set_by,
			mprintf("%lld", topic.timestamp)
		};
		c->SendServerReply(RPL_TOPICWHOTIME, { parms });
	}
}

void MeshCoreChannel::sendNamesTo(Client* c) {
	string line;
	for (auto& u : users) {
		if (!line.empty()) {
			line += " ";
		}
		if (u->user->isOurNode()) {
			line += "~";
		} else if (u->user->isFavorite()) {
			line += "@";
		} else if (u->has_voice) {
			line += "+";
		}
		line += u->user->irc_nick;

		if (line.length() > 140) {
			vector<string> parms = {
				irc_name,
				line
			};
			c->SendServerReply(RPL_NAMREPLY, parms);
			line.clear();
		}
	}

	if (line.length()) {
		vector<string> parms = {
			irc_name,
			line
		};
		c->SendServerReply(RPL_NAMREPLY, parms);
	}

	vector<string> parms = {
		irc_name,
		"End of /NAMES list"
	};
	c->SendServerReply(RPL_ENDOFNAMES, parms);
}

bool MeshCoreChannel::hasSpokenRecently(const string& irc_nick) {
	shared_ptr<MeshCoreChannelUser> u;
	if (getUserInChannel(irc_nick, u)) {
		return u->hasSpokenRecently();
	}
	return false;
}

bool MeshCoreChannelUser::hasSpokenRecently() {
	return (last_message_time > time(NULL) - config.irc.idle_users_after);
}

bool MeshCoreChannelUser::shouldPartUser() {
	return (last_message_time < time(NULL) - config.irc.part_users_after);
}

void MeshCoreChannel::onReceiveMessage(shared_ptr<MeshCoreUser>& user, const char* text, MESHCORE_TEXT_TYPES txt_type) {
	if (!isUserInChannel(user)) {
		addUserToChannel(user);
	}

	shared_ptr<MeshCoreChannelUser> uc;
	if (!getUserInChannel(user, uc)) {
		printf("[mqtt] Error getting user channel state for %s in %s", user->meshcore_nick, irc_name);
		return;
	}

	if (config.irc.auto_voice_idle_users && !uc->has_voice && !uc->user->isOurNode()) {
		// send MODE notice
		SendUserModeNoticesFor(irc_name, "+v", user->irc_nick);
		uc->has_voice = true;
	}

	vector<string> parms = {
		":" + user->hostmask,
		(txt_type == TXT_TYPE_NOTICE) ? "NOTICE" : "PRIVMSG",
		irc_name,
		text
	};
	if (!SendLineToAllAuthenticatedClients(parms)) {
		// no clients are online and connected
		AddOfflineMessage(parms);
	}

	user->onAction();
	uc->last_message_time = time(NULL);
}
