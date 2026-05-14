/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

void add_or_update_channel(_PACKET_CHANNEL_INFO* ci) {
	if (ci->channel_index < 0 || ci->channel_index > MESHCORE_HIGHEST_CHANNEL) {
		return;
	}

	shared_ptr<MeshCoreChannel> c;
	auto x = state.chans.find(ci->channel_index);
	if (x == state.chans.end()) {
		c = make_shared<MeshCoreChannel>();
		c->channel_index = ci->channel_index;
		state.chans[c->channel_index] = c;
	}

	sstrcpy(c->name, trim_nulls(ci->channel_name, sizeof(ci->channel_name)).c_str());
	bin2hex(ci->secret, sizeof(ci->secret), c->secret, sizeof(c->secret));
}

void MeshCoreChannel::ToUniValue(UniValue& obj) {
	obj.clear();
	obj.setObject();
	
	obj.pushKV("channel_index", channel_index);
	obj.pushKV("name", name);
	obj.pushKV("secret", secret);
}

bool _mqtt_send_channel(shared_ptr<MeshCoreChannel>& c) {
	UniValue obj(UniValue::VOBJ);
	c->ToUniValue(obj);
	return mqtt_send(mprintf("%s/channel_info/%u", config.mqtt.topic_prefix.c_str(), c->channel_index), obj, true);
}

bool mqtt_send_channels() {
	bool had_error = false;
	for (auto& u : state.chans) {
		if (!_mqtt_send_channel(u.second)) {
			had_error = true;
		}
	}
	return (!had_error && state.chans.size() != 0);
}

bool mqtt_send_channel(int idx) {
	auto x = state.chans.find(idx);
	if (x != state.chans.end()) {
		return _mqtt_send_channel(x->second);
	}
	return false;
}
