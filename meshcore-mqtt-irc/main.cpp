/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshirc.h"

CONFIG config;
DSL_Sockets socks;
DSL_Sockets_Events* ev = NULL;
list<shared_ptr<Client>> clients;

bool LoadConfig() {
	if (access("meshcore-irc.conf", 0) != 0) {
		printf("Warning: No meshcore-irc.conf found, using defaults...\n");
		return true;
	}

	ConfigSection root;
	if (!root.LoadFromFile("meshcore-irc.conf")) {
		printf("Error parsing meshcore-irc.conf!\n");
		return false;
	}

	auto irc = root.GetSection("IRC");
	if (irc != NULL) {
		auto v = irc->GetValue("ServerName");
		if (v) config.irc.server_hostname = v->AsString();
		v = irc->GetValue("NetworkName");
		if (v) config.irc.network_name = v->AsString();
		v = irc->GetValue("BindIP");
		if (v) config.irc.bind_ip = v->AsString();
		v = irc->GetValue("Port");
		if (v) { config.irc.listen_port = (uint16)v->AsInt(); }
		v = irc->GetValue("AllowNickChange");
		if (v) config.irc.allow_nick_change = v->AsBool();
		v = irc->GetValue("AutoVoiceIdleUsers");
		if (v) config.irc.auto_voice_idle_users = v->AsBool();
		v = irc->GetValue("LogToConsole");
		if (v) config.irc.log_to_console = v->AsBool();
		v = irc->GetValue("LogToFile");
		if (v) config.irc.log_to_file = v->AsBool();

		v = irc->GetValue("ExpireUsersAfter");
		if (v) {
			int64 x = parse_duration_str(v->AsString());
			if (x > 0) {
				config.irc.expire_users_after = max((int64)1, x);
			}
		}
		v = irc->GetValue("IdleUsersAfter");
		if (v) {
			int64 x = parse_duration_str(v->AsString());
			if (x > 0) {
				config.irc.idle_users_after = max((int64)1, x);
			}
		}
		v = irc->GetValue("PartUsersAfter");
		if (v) {
			int64 x = parse_duration_str(v->AsString());
			if (x > 0) {
				config.irc.part_users_after = max((int64)1, x);
			}
		}
	}

	auto mqtt = root.GetSection("MQTT");
	if (mqtt != NULL) {
		auto v = mqtt->GetValue("Host");
		if (v) config.mqtt.host = v->AsString();
		v = mqtt->GetValue("Port");
		if (v) config.mqtt.port = (uint16)v->AsInt();
		v = mqtt->GetValue("Username");
		if (v) config.mqtt.username = v->AsString();
		v = mqtt->GetValue("Password");
		if (v) config.mqtt.password = v->AsString();
		v = mqtt->GetValue("TopicPrefix");
		if (v) config.mqtt.topic_prefix = v->AsString();
		v = mqtt->GetValue("LogToConsole");
		if (v) config.mqtt.log_to_console = v->AsBool();
		v = mqtt->GetValue("LogToFile");
		if (v) config.mqtt.log_to_file = v->AsBool();
	}

	return true;
}

void do_shutdown() {
	clients.clear();
	chans.clear();
	users.clear();

	if (config.mqtt.client != NULL) {
		delete config.mqtt.client;
		config.mqtt.client = NULL;
	}

	if (config.mqtt.log_fp != NULL) {
		fclose(config.mqtt.log_fp);
		config.mqtt.log_fp = NULL;
	}

	if (config.irc.lSock != NULL) {
		ev->Remove(config.irc.lSock, true);
		config.irc.lSock = NULL;
	}

	for (auto& t : config.timers) {
		ev->FreeTimer(t);
	}
	config.timers.clear();

	if (ev != NULL) {
		delete ev;
		ev = NULL;
	}

	db_quit();

	printf("Goodbye.\n");
}

#if defined(WIN32)
/**
 * Handles Ctrl+C, hitting the Close button, etc., on Windows
 */
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType) {
	//dwCtrlType == CTRL_CLOSE_EVENT ||
	if (dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
		if (!config.shutdown_now) {
			if (dwCtrlType != CTRL_CLOSE_EVENT) {
				printf("Caught Ctrl+C (or similar), setting shutdown_now=true\n");
			}
			config.shutdown_now = true;
		}
		return TRUE;
	}
	return TRUE;
}
#else
/**
 * Catches Ctrl+C, etc. on Linux/Unix
 */
static void catch_sigint(int signo) {
	if (!config.shutdown_now) {
		printf("Caught SIGINT, setting shutdown_now=true\n");
		config.shutdown_now = true;
	}
}
#endif

void client_read_cb(DSL_SOCKET_LIBEVENT* sock, short flags) {
	Client* c = (Client*)sock->user_ptr;
	c->onRecv();
}

void client_write_db(DSL_SOCKET_LIBEVENT* sock, short flags) {
	Client* c = (Client*)sock->user_ptr;
	if (c->sendbuf.len > 0) {
		int n = socks.Send(sock->sock, c->sendbuf.data, (int)c->sendbuf.len, false);
		if (n > 0) {
			//string sent(c->sendbuf.data, n);
			//printf("sent %d bytes: %s\n", n, sent.c_str());
			buffer_remove_front(&c->sendbuf, n);
			if (c->sendbuf.len) {
				// We have more to send, so re-enable the write callback
				ev->EnableWrite(sock);
			}
		} else if (n == 0) {
			printf("[irc] Client from %s:%d closed connection\n", sock->sock->remote_ip, sock->sock->remote_port);
			c->SetClientDropped(false);
		} else {
			printf("[irc] Error sending to client from %s:%d: %s\n", sock->sock->remote_ip, sock->sock->remote_port, socks.GetLastErrorString(sock->sock));
			c->SetClientDropped(false);
		}
	}
}

void accept_incoming_connection(DSL_SOCKET_LIBEVENT* lsock, short flags) {
	auto sock = socks.Accept(lsock->sock);
	if (sock != NULL) {
		auto esock = ev->Add(sock, client_read_cb, client_write_db, NULL, NULL);
		if (esock != NULL) {
			if (config.self_user) {
				printf("[irc] Accepted new client from %s:%d\n", sock->remote_ip, sock->remote_port);
				auto c = make_shared<Client>(esock, config.self_user);
				esock->user_ptr = c.get();
				clients.push_back(std::move(c));
			} else {
				printf("[irc] Error accepting new client from %s:%d, could not get self user.\n", sock->remote_ip, sock->remote_port);
			}
		} else {
			printf("[irc] Error accepting new client from %s:%d\n", sock->remote_ip, sock->remote_port);
			socks.Close(sock);
		}
	}
}

void timer_mosquitto_loop(DSL_SOCKET_LIBEVENT* esock, short flags) {
	mosquitto_loop();
}

void timer_maintenance(DSL_SOCKET_LIBEVENT* esock, short flags) {
	if (config.shutdown_now) {
		ev->LoopBreak();
	}

	int64 timeout_ts = time(NULL) - 600;
	int64 auth_ts = time(NULL) - 60;
	for (auto x = clients.begin(); x != clients.end();) {
		auto c = *x;
		if (c->state == CS_DROP || (c->state == CS_DROP_AFTER_SENT && c->sendbuf.len <= 0) || c->lastRecv < timeout_ts) {
			x = clients.erase(x);
		} else if (c->state == CS_AUTH && c->timeConnected < auth_ts) {
			// hasn't completed authentication in 60 seconds
			c->SendServerNotice("Time limit expired while waiting for registration", "AUTH");
			c->SendLine({ "ERROR", "Time limit expired while waiting for registration" });
			c->SetClientDropped(true);
			x++;
		} else {
			x++;
		}
	}
}

void timer_idle_check(DSL_SOCKET_LIBEVENT* esock, short flags) {
	for (auto x = users.begin(); x != users.end();) {
		auto u = x->second;

		if (!u->is_away && !u->hadRecentAction()) {
			u->markAway();
		}

		if (u->isOurNode()) {
			x++;
			continue;
		}

		if (u->shouldExpireUser()) {
			printf("Haven't seen user %s (%s) in over %lld seconds, removing from server...\n", u->meshcore_nick, u->irc_nick, config.irc.expire_users_after);
			for (auto& chan : chans) {
				chan.second->partUserFromChannel(u, false, true);
			}
			SendQuitNoticesFor(u.get(), "User no longer seen on MeshCore network");
			x = users.erase(x);
			printf("There are %zu users and %zu channels in memory.\n", users.size(), chans.size());
		} else {
			x++;
		}
	}

	for (auto& c : chans) {
		c.second->handleIdleUsers();
	}
}

void timer_send_pings(DSL_SOCKET_LIBEVENT* esock, short flags) {
	int64 ts = time(NULL) - 120;
	for (auto& c : clients) {
		if (c->state == CS_CONNECTED) {
			if (c->lastRecv < ts) {
				c->SendPing();
			}
		}
	}
}

int main(int argc, const char* argv[]) {
	printf("Starting up meshcore-mqtt-irc v" MQTT_IRC_VERSION " ...\n\n");
	dsl_init();
	atexit(dsl_cleanup);

	if (!LoadConfig()) {
		exit(1);
	}

	atexit(do_shutdown);

#if defined(WIN32)
	SetConsoleTitle("meshcore-mqtt-irc/" PLATFORM "");
	SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);
	SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
	struct sigaction sa_old;
	struct sigaction sa_new;

	// set up signal handling
	sa_new.sa_handler = catch_sigint;
	sigemptyset(&sa_new.sa_mask);
	sa_new.sa_flags = 0;
	sigaction(SIGINT, &sa_new, &sa_old);
#endif

	if (!db_init()) {
		exit(1);
	}

	config.mqtt.client = new MQTT_IRC_Client(config.mqtt.host, config.mqtt.port, config.mqtt.username, config.mqtt.password, config.mqtt.topic_prefix);

	ev = new DSL_Sockets_Events(&socks);
	auto sock = socks.Create();
	socks.SetNonBlocking(sock);
	socks.SetReuseAddr(sock);
	if (!socks.BindToAddr(sock, config.irc.bind_ip.c_str(), config.irc.listen_port)) {
		printf("Error binding listening socket: %s\n", socks.GetLastErrorString(sock));
		socks.Close(sock);
		exit(1);
	}
	if (!socks.Listen(sock)) {
		printf("Error listen()ing listening socket: %s\n", socks.GetLastErrorString(sock));
		socks.Close(sock);
		exit(1);
	}
	config.irc.lSock = ev->Add(sock, accept_incoming_connection);
	
	auto t = ev->AddTimer(timer_mosquitto_loop);
	config.timers.push_back(t);
	ev->EnableRecv(t, 100);

	t = ev->AddTimer(timer_maintenance);
	config.timers.push_back(t);
	ev->EnableRecv(t, 500);

	t = ev->AddTimer(timer_idle_check);
	config.timers.push_back(t);
	ev->EnableRecv(t, 10000);	

	t = ev->AddTimer(timer_send_pings);
	config.timers.push_back(t);
	ev->EnableRecv(t, 30000);

	printf("Waiting for channels and contacts lists...\n");

	ev->LoopWithFlags(0);

	printf("Shutting down, waiting for clients to exit...\n");

	// Don't accept any more clients
	ev->DisableRecv(config.irc.lSock);

	for (auto& c : clients) {
		if (c->state == CS_CONNECTED) {
			c->SendServerNotice("Server is shutting down...");
			c->SetClientDropped(true);
		} else {
			c->SetClientDropped(false);
		}
	}
	while (clients.size()) {
		ev->LoopWithTimeout(250);
	}

	exit(0);
}
