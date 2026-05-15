/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"
//#include <sodium.h>

string get_sanitized_nick(const string& str) {
	char* nick = strdup(str.c_str());

	char* p = nick;
	while (*p) {
		if ( *p == '-' && nick != p && *(p+1) != 0) {
			//allow -, but not as the first or last digit
			p++;
		} else if (!isalnum((unsigned char)*p)) {
			*p++ = '_';
		} else {
			p++;
		}
	}

	strtrim(nick);
	string ret = nick;
	free(nick);

	return ret;
}

string get_channel_name_from_meshcore(const string& name) {
	char irc_name[IRC_MAX_CHAN_LEN + 1];
	sstrcpy(irc_name, "#");
	sstrcat(irc_name, get_sanitized_nick(name[0] == '#' ? name.substr(1) : name).c_str());
	return irc_name;
}

int64 parse_duration_str(const string& str) {
	if (str.length() == 0) {
		return -1;
	}

	int64 ret = atoi64(str.c_str());
	if (str[str.length() - 1] == 'h') {
		ret *= 3600;
	} else if (str[str.length() - 1] == 'd') {
		ret *= 86400;
	} else if (str[str.length() - 1] == 'w') {
		ret *= (86400 * 7);
	}
	return ret;
}
