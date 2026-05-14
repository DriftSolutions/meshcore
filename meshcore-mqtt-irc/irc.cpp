/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"

void SendJoinNoticesFor(MeshCoreUser* u, const string& chan) {
	vector<string> parms = {
		":" + u->hostmask,
		"JOIN",
		chan,
	};
	SendLineToAllAuthenticatedClients(parms);
}

void SendPartNoticesFor(MeshCoreUser* u, const string& chan) {
	vector<string> parms = {
		":" + u->hostmask,
		"PART",
		chan,
	};
	SendLineToAllAuthenticatedClients(parms);
}

void SendQuitNoticesFor(MeshCoreUser* u, const string& reason) {
	vector<string> parms = {
		":" + u->hostmask,
		"QUIT",
		reason,
	};
	SendLineToAllAuthenticatedClients(parms);
}

void SendNickChangeNoticesFor(MeshCoreUser* u, const string& new_nick) {
	vector<string> parms = {
		":" + u->hostmask,
		"NICK",
		new_nick,
	};
	SendLineToAllAuthenticatedClients(parms);
}

void SendUserModeNoticesFor(const string& chan, const string& mode, const string& irc_nick) {
	vector<string> parms = {
		":" + config.irc.server_hostname,
		"MODE",
		chan,
		mode,
		irc_nick
	};
	SendLineToAllAuthenticatedClients(parms);
}

void Client::onRecv() {
	char buf[4096];
	int n = socks.Recv(sock->sock, buf, sizeof(buf));
	if (n > 0) {
		if (state >= CS_AUTH) {
			lastRecv = time(NULL);
			buffer_append(&recvbuf, buf, n);
			handleIncoming();
		}
	} else if (n == 0) {
		printf("Client from %s:%d closed connection\n", sock->sock->remote_ip, sock->sock->remote_port);
		SetClientDropped(false);
	} else {
		printf("[irc] Error sending to client from %s:%d: %s\n", sock->sock->remote_ip, sock->sock->remote_port, socks.GetLastErrorString(sock->sock));
		SetClientDropped(false);
	}
}

void Client::log(const char* fmt, ...) {
	if (!config.irc.log_to_console && !config.irc.log_to_file) {
		return;
	}

	va_list va;
	va_start(va, fmt);

	if (config.irc.log_to_console) {
		char* tmp = dsl_vmprintf(fmt, va);
		printf("[irc] %s\n", tmp);
		dsl_free(tmp);
	}

	if (config.irc.log_to_file) {
		if (log_fp == NULL) {
			if (access("./logs", 0) != 0) {
				dsl_mkdir("./logs", 0700);
			}
			string fn = mprintf("./logs/irc-%s-%u.log", str_replace(sock->sock->remote_ip, ".", "_").c_str(), sock->sock->remote_port);
			log_fp = fopen(fn.c_str(), "wb");
			if (log_fp != NULL) {
				printf("[irc] Opened %s for output...\n", fn.c_str());
			} else {
				printf("[irc] Error opening %s for output: %s\n", fn.c_str(), strerror(errno));
			}
		}
		if (log_fp != NULL) {
			vfprintf(log_fp, fmt, va);
		}
	}

	va_end(va);
}

int parse_irc_line(char* buf, char** cmd, char** parms, int maxparms) {
	*cmd = buf;
	memset(parms, 0, sizeof(char*) * maxparms);

	char* p = strchr(buf, ' ');
	if (p) {
		*p++ = 0;
	} else {
		// no parms
		return 0;
	}

	int pcount = 0;
	while (pcount < maxparms) {
		if (*p == ':') { // multi-word param, must be last in the line
			parms[pcount] = p + 1;
			pcount++;
			break;
		}
		
		char* q = strchr(p, ' ');
		if (q == NULL) {
			// no more params
			parms[pcount] = p;
			pcount++;
			break;
		}

		*q = 0;
		parms[pcount++] = p;
		p = q + 1;
	}

	return pcount;
}

#define MAX_IRC_LINE_LENGTH 512

void Client::handleIncoming() {
	while (recvbuf.len > 0 && state >= CS_AUTH) {
		char buf[MAX_IRC_LINE_LENGTH + 2] = { 0 };
		int i;
		for (i = 0; i < recvbuf.len; i++) {
			if (recvbuf.data[i] == '\n') {
				if (i >= MAX_IRC_LINE_LENGTH) {
					printf("[irc] Received too long of a line from %s:%d, dropping...\n", sock->sock->remote_ip, sock->sock->remote_port);
					SetClientDropped(true);
					return;
				}

				i++;

				strlcpy(buf, recvbuf.data, i);
				strtrim(buf, "\r\n", TRIM_RIGHT);
				buffer_remove_front(&recvbuf, i);
				break;
			}
		}
		if (i == recvbuf.len) { break; } // no line terminator found
		if (buf[0] == 0) { continue; } // empty line

		log("<- %s\n", buf);
		/*
		if (config.ircnets[netno].log_fp) {
			char durbuf[32];
			fprintf(config.ircnets[netno].log_fp, "%s < %s\r\n", ircbot_cycles_ts(durbuf, sizeof(durbuf)), buf);
			fflush(config.ircnets[netno].log_fp);
		}
		*/

		/*
		if (!strnicmp(buf, ":ERROR ", 7) || !strnicmp(buf, "ERROR ", 6)) {
			ib_printf(_("[irc-%d] %s\n"), netno, buf);
			break;
		}
		*/
		char* cmd = NULL, *parms[32];
		int nparms = parse_irc_line(buf, &cmd, (char**)&parms, 32);
		if (nparms < 0) { continue; }
		int x = 1;

		if (state == CS_AUTH && handleIncomingAuthenticating(cmd, parms, nparms)) {
			continue;
		} else if (state == CS_CONNECTED && handleIncomingConnected(cmd, parms, nparms)) {
			continue;
		}

		handleIncomingAlways(cmd, parms, nparms);
	}
}

void Client::sendErrorNeedMoreParams(const string& cmd) {
	vector<string> parms = {
		cmd,
		"Not enough parameters"
	};
	SendServerReply(ERR_NEEDMOREPARAMS, parms);
}

void Client::SendServerNotice(const string& msg, const string& to) {
	SendServerReply("NOTICE", { msg }, to);
}

void Client::SendServerReply(const string& numeric, const vector<string>& parms, const string& to) {
	/*
	assert(parms.size() != 0);
	if (parms.size() == 0) {
		return;
	}
	*/

	//:server_name numeric client_nick otherparms

	string line = ":" + config.irc.server_hostname + " " + numeric + " " + (to.empty() ? config.self_user->irc_nick : to);
	for (size_t i = 0; i < parms.size(); i++) {
		line += " ";
		if (i == parms.size() - 1 && strchr(parms[i].c_str(), ' ') != NULL) { //multi-word last parm
			line += ":";
		} else if (strchr(parms[i].c_str(), ' ') != NULL) {
			// has space in parameter, but isn't last parm
			assert(false);
		}
		line += parms[i];
	}

	line += "\r\n";
	sendLine(line);
}

void SendLineToAllAuthenticatedClients(const vector<string>& parms) {
	for (auto& c : clients) {
		if (c->state == CS_CONNECTED) {
			c->SendLine(parms);
		}
	}
}

void Client::SendLine(const vector<string>& parms) {
	assert(parms.size() != 0);
	if (parms.size() == 0) {
		return;
	}

	string line;
	for (size_t i = 0; i < parms.size(); i++) {
		if (i > 0) {
			line += " ";
		}
		if (i == parms.size() - 1 && strchr(parms[i].c_str(), ' ') != NULL) { //multi-word last parm
			line += ":";
		} else if (strchr(parms[i].c_str(), ' ') != NULL) {
			// has space in parameter, but isn't last parm
			assert(false);
		}
		line += parms[i];
	}

	line += "\r\n";
	sendLine(line);
}

void Client::sendLine(const string& line) {
	if (state >= CS_AUTH) {
		log("-> %s", line.c_str());
		buffer_append(&sendbuf, line.c_str(), line.length());
		ev->EnableWrite(sock);
	}
}

void Client::SendPing() {
	size_t val = dsl_get_random<size_t>() & 0xFFFFFFFF;
	string str = mprintf("PING :%zu\r\n", val);
	sendLine(str);
}

bool Client::handleIncomingAuthenticating(char* cmd, char* parms[], int nparms) {
	if (!stricmp(cmd, "NICK")) {
		if (nparms > 0) {
			// we ignore the parm and use the nick set on the node
			auth_had_nick = true;
			if (auth_had_nick && auth_had_user) {
				welcomeUser();
			}
		} else {
			SendServerReply(ERR_NONICKNAMEGIVEN, { "No nickname given" });
		}
		return true;
	}

	if (!stricmp(cmd, "USER")) {
		if (nparms == 4) {
			// we ignore the parm and use the nick set on the node
			auth_had_user = true;
			if (auth_had_nick && auth_had_user) {
				welcomeUser();
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "CAP")) {
		//if (nparms == 1 && 
		//SendServerReply(ERR_UNKNOWNCOMMAND, { cmd, "Unknown command" });
		return true;
	}

	return false;
}

string pad_right(const string& str, char c, size_t pad_to) {
	string ret = str;
	while (ret.length() < pad_to) {
		ret += c;
	}
	return ret;
}

bool Client::handleIncomingConnected(char* cmd, char* parms[], int nparms) {
	if (!stricmp(cmd, "JOIN")) {
		if (nparms > 0) {
			StrTokenizer st(parms[0], ',');
			shared_ptr<MeshCoreChannel> c;
			for (size_t i = 1; i <= st.NumTok(); i++) {
				string chan = st.stdGetSingleTok(i);
				if (get_channel_by_irc_name(chan, c)) {
					vector<string> parms = {
						":" + user->hostmask,
						"JOIN",
						c->irc_name,
					};
					SendLine(parms);

					c->sendPostJoinNotices(this);
				} else {
					vector<string> parms = {
						chan,
						"No such channel"
					};
					SendServerReply(ERR_NOSUCHCHANNEL, parms);
				}
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "PART")) {
		if (nparms > 0) {
			StrTokenizer st(parms[0], ',');
			shared_ptr<MeshCoreChannel> c;
			for (size_t i = 1; i <= st.NumTok(); i++) {
				string chan = st.stdGetSingleTok(i);
				if (get_channel_by_irc_name(chan, c)) {
					vector<string> parms = {
						chan,
						"You cannot PART channels, you have to edit your channel config in MeshCore"
					};
					SendServerReply(ERR_CHANOPRIVSNEEDED, parms);
				} else {
					vector<string> parms = {
						chan,
						"No such channel"
					};
					SendServerReply(ERR_NOSUCHCHANNEL, parms);
				}
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "NAMES")) {
		if (nparms > 0) {
			StrTokenizer st(parms[0], ',');
			shared_ptr<MeshCoreChannel> c;
			for (size_t i = 1; i <= st.NumTok(); i++) {
				string chan = st.stdGetSingleTok(i);
				if (get_channel_by_irc_name(chan, c)) {
					c->sendNamesTo(this);
				} else {
					printf("[irc] Got NAMES request for unknown channel %s\n", chan.c_str());
					SendServerReply(ERR_NOSUCHCHANNEL, { chan, "No such channel" });
				}
			}
		} else {
			//send all channels
			for (auto& c : chans) {
				c.second->sendNamesTo(this);
			}
		}
		return true;
	}

	if (!stricmp(cmd, "WHOIS")) {
		if (nparms > 0) {
			StrTokenizer st(parms[0], ',');
			shared_ptr<MeshCoreUser> u;
			for (size_t i = 1; i <= st.NumTok(); i++) {
				string irc_nick = st.stdGetSingleTok(i);
				if (get_user_by_irc_nick(irc_nick, u)) {
					vector<string> parms = {
						u->irc_nick,
						"meshcore",
						u->meshcore_pubkey,
						"*",
						u->meshcore_nick
					};
					SendServerReply(RPL_WHOISUSER, { parms });

					parms = {
						u->irc_nick
					};
					if (u->last_hops >= 0) {
						parms.push_back(mprintf("is %u hops away", u->last_hops));
					} else {
						parms.push_back(mprintf("is an unknown number of hops away"));
					}
					SendServerReply(RPL_WHOISOPERATOR, parms);

					parms = {
						u->irc_nick,
						mprintf("%lld", time(NULL) - ((u->last_action_time > 0) ? u->last_action_time : u->last_seen)),
						mprintf("%lld", u->time_added),
						"seconds idle, signon time"
					};
					SendServerReply(RPL_WHOISIDLE, { parms });

					shared_ptr<MeshCoreChannelUser> uc;
					string channels;
					for (auto& chan : chans) {
						if (chan.second->getUserInChannel(u, uc)) {
							if (!channels.empty()) {
								channels += " ";
							}
							if (uc->has_voice) {
								channels += "+";
							}
							channels += chan.second->irc_name;
						}
					}
					if (!channels.empty()) {
						parms = {
							u->irc_nick,
							channels,
						};
						SendServerReply(RPL_WHOISCHANNELS, { parms });
					}

					if (u->is_away) {
						SendAwayNoticeFor(u);
					}

					parms = {
						u->irc_nick,
						"End of /WHOIS list",
					};
					SendServerReply(RPL_ENDOFWHOIS, { parms });
				} else {
					printf("[irc] Could not find user with IRC nick %s\n", irc_nick.c_str());
					SendServerReply(ERR_NOSUCHNICK, { irc_nick, "No such nick" });
				}
			}
		} else {
			//send all channels
			for (auto& c : chans) {
				c.second->sendNamesTo(this);
			}
		}
		return true;
	}

	if (!stricmp(cmd, "MODE")) {
		if (nparms > 0) {
			// MODE target
			if (parms[0][0] == '#') {
				shared_ptr<MeshCoreChannel> c;
				if (get_channel_by_irc_name(parms[0], c)) {
					if (nparms > 1) {
						//trying to set the mode
						vector<string> parms = {
							c->irc_name,
							"You're not channel operator"
						};
						SendServerReply(ERR_CHANOPRIVSNEEDED, parms);
					} else {
						//just wanting to know what the mode is
						vector<string> parms = {
							c->irc_name,
							c->getChannelMode(),
						};
						SendServerReply(RPL_CHANNELMODEIS, { parms });
					}
				} else {
					printf("[irc] Got MODE request for unknown channel %s\n", parms[0]);
					SendServerReply(ERR_NOSUCHCHANNEL, { parms[0], "No such channel" });
				}
			} else if (!stricmp(parms[0], config.self_user->irc_nick)) {
				vector<string> parms = {
					//u->irc_nick,
					"+",
				};
				SendServerReply(RPL_UMODEIS, { parms });
			} else {
				SendServerReply(ERR_USERSDONTMATCH, { "Cant change mode for other users" });
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "TOPIC")) {
		if (nparms > 0) {
			shared_ptr<MeshCoreChannel> c;
			if (get_channel_by_irc_name(parms[0], c)) {
				if (nparms > 1) {
					//trying to set the topic
					vector<string> parms = {
						c->irc_name,
						"You're not channel operator"
					};
					SendServerReply(ERR_CHANOPRIVSNEEDED, parms);
				} else {
					//just wanting to know what the topic is
					vector<string> parms = {
						c->irc_name,
						"No topic is set"
					};
					SendServerReply(RPL_NOTOPIC, { parms });
				}
			} else {
				printf("[irc] Got TOPIC request for unknown channel %s\n", parms[0]);
				SendServerReply(ERR_NOSUCHCHANNEL, { parms[0], "No such channel" });
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "PRIVMSG") || !stricmp(cmd, "NOTICE")) {
		if (nparms > 1) {
			int txt_type = stricmp(cmd, "NOTICE") ? 0 : 3;
			shared_ptr<MeshCoreUser> u;
			shared_ptr<MeshCoreChannel> c;

			StrTokenizer st(parms[0], ',');
			for (size_t i = 1; i <= st.NumTok(); i++) {
				string dest = st.stdGetSingleTok(i);
				if (dest[0] == '#') {
					if (get_channel_by_irc_name(dest, c)) {
						user->onAction();
						config.mqtt.client->SendChannelMsg(c->meshcore_index, parms[1], txt_type);
					} else {
						printf("[irc] Tried to send %s to unknown channel %s\n", cmd, dest.c_str());
						SendServerReply(ERR_NOSUCHCHANNEL, { dest, "No such channel" });
						//ERR_CANNOTSENDTOCHAN
					}
				} else {
					bool is_hostmask = (dest.find('!') != dest.npos || dest.find('@') != dest.npos);
					if (is_hostmask ? get_user_by_hostmask(dest, u) : get_user_by_irc_nick(dest, u)) {
						if (u->meshcore_pubkey[0]) {
							user->onAction();
							config.mqtt.client->SendDirectMsg(u->meshcore_pubkey, parms[1], txt_type);
						} else {
							SendServerReply(ERR_NOSUCHNICK, { dest, "No pubkey for this user" });
						}
					} else {
						printf("[irc] Tried to send %s to unknown user %s\n", cmd, dest.c_str());
						SendServerReply(ERR_NOSUCHNICK, { dest, "No such nick" });
					}
				}
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "USERS")) {
		SendServerReply(RPL_USERSSTART, { mprintf("%s   %s   Pubkey", pad_right("IRC Nick", ' ', 30).c_str(), pad_right("MeshCore Username", ' ', 31).c_str()) });
		for (auto& u : users) {
			SendServerReply(RPL_USERS, { mprintf("%s   %s   %s", pad_right(u.second->irc_nick, ' ', 30).c_str(), pad_right(u.second->meshcore_nick, ' ', 31).c_str(), u.second->meshcore_pubkey) });
		}
		SendServerReply(RPL_ENDOFUSERS, { "End of users" });
		return true;
	}

	if (!stricmp(cmd, "ISON")) {
		if (nparms > 0) {
			vector<string> nicks;
			shared_ptr<MeshCoreUser> u;
			for (int i = 0; i < nparms; i++) {
				if (get_user_by_irc_nick(parms[i], u)) {
					nicks.push_back(u->irc_nick);
				}
			}
			SendServerReply(RPL_ISON, nicks);
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "USERHOST")) {
		if (nparms > 0) {
			shared_ptr<MeshCoreUser> u;
			string str;
			for (int i = 0; i < nparms; i++) {
				if (get_user_by_irc_nick(parms[i], u)) {
					if (!str.empty()) {
						str += " ";
					}
					str += mprintf("%s=%cmeshcore@%s", u->irc_nick, u->is_away ? '-' : '+', u->meshcore_pubkey);
				}
			}
			if (!str.empty()) {
				SendServerReply(RPL_USERHOST, { str });
			}
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return true;
	}

	if (!stricmp(cmd, "VERSION")) {
		SendServerReply(RPL_VERSION, { MQTT_IRC_VERSION, "meshcore-mqtt-irc" });
		return true;
	}


	if (!stricmp(cmd, "USER")) {
		SendServerReply(ERR_ALREADYREGISTRED, { "You may not reregister" });
		return true;
	}

	if (!stricmp(cmd, "NICK")) {
		if (nparms > 0) {
			SendServerReply(ERR_ERRONEUSNICKNAME, { "To change your nick, you have to change it in MeshCore" });
		} else {
			SendServerReply(ERR_NONICKNAMEGIVEN, { "No nickname given" });
		}
		return true;
	}

	return false;
}

void Client::handleIncomingAlways(char* cmd, char* parms[], int nparms) {
	if (!stricmp(cmd, "QUIT")) {
		printf("[irc] Received QUIT from client %s:%d\n", sock->sock->remote_ip, sock->sock->remote_port);
		SetClientDropped(false);
		return;
	}

	if (!stricmp(cmd, "PING")) {
		if (nparms > 0) {
			SendServerReply("PONG", { parms[0] });
		} else {
			sendErrorNeedMoreParams(cmd);
		}
		return;
	}

	if (!stricmp(cmd, "PONG")) {
		return;
	}

	if (state != CS_CONNECTED) {
		SendServerReply(ERR_NOTREGISTERED, { "You have not registered" });
	} else {
		SendServerReply(ERR_UNKNOWNCOMMAND, { cmd, "Unknown command" });
	}
}

void Client::welcomeUser() {
	_state = CS_CONNECTED;

	string msg = mprintf("Welcome to %s %s", config.irc.network_name.c_str(), user->hostmask.c_str());
	SendServerReply(RPL_WELCOME, { msg });

	msg = mprintf("Your host is %s, running version meshcore-mqtt-irc v" MQTT_IRC_VERSION "", config.irc.server_hostname.c_str());
	SendServerReply(RPL_YOURHOST, { msg });

	SendServerReply(RPL_CREATED, { "This server was created " __DATE__ " at " __TIME__ "" });

	vector<string> parms = {
		config.irc.server_hostname,
		MQTT_IRC_VERSION,
		"i",
		"ostv",
		"ov",
	};
	SendServerReply(RPL_MYINFO, parms);

	parms = {
		"MAXCHANNELS=40",
		"NICKLEN=30",
		"CHANNELLEN=32",
		"CHANTYPES=#",
		"are supported by this server"
	};
	SendServerReply(RPL_ISUPPORT, parms);

	msg = mprintf("There are %zu users and 0 invisible on 1 servers", users.size());
	SendServerReply(RPL_LUSERCLIENT, { msg });

	parms = {
		mprintf("%zu", chans.size()),
		"channels formed"
	};
	SendServerReply(RPL_LUSERCHANNELS, parms);

	SendServerReply(ERR_NOMOTD, { "MOTD File is missing" });

	for (auto& c : chans) {
		// send auto-joins for channels
		if (!c.second->isUserInChannel(user->irc_nick)) {
			c.second->addUserToChannel(user);
		} else {
			vector<string> parms = {
				":" + user->hostmask,
				"JOIN",
				c.second->irc_name,
			};
			SendLine(parms);
		}

		c.second->sendPostJoinNotices(this);		
	}
}
