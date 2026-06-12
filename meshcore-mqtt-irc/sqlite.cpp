#include "meshirc.h"

DB_SQLite* db = NULL;

bool db_init() {

	db = new DB_SQLite();
	if (!db->Open("meshcore-mqtt-irc.db")) {
		printf("Error opening database 'meshcore-mqtt-irc.db': %s\n", db->GetErrorString().c_str());
		return false;
	}

	if (!db->NoResultQuery("CREATE TABLE IF NOT EXISTS ChannelSettings (ID INTEGER PRIMARY KEY AUTOINCREMENT, Channel TEXT COLLATE NOCASE, Name TEXT COLLATE NOCASE, Value TEXT DEFAULT '')")) {
		printf("Error creating ChannelSettings table: %s\n", db->GetErrorString().c_str());
		return false;
	}

	if (!db->NoResultQuery("CREATE UNIQUE INDEX IF NOT EXISTS ChannelSettingsIdx ON ChannelSettings (Channel, Name)")) {
		printf("Error creating ChannelSettingsIdx index: %s\n", db->GetErrorString().c_str());
		return false;
	}

	if (!db->NoResultQuery("CREATE TABLE IF NOT EXISTS OfflineMessages (ID INTEGER PRIMARY KEY AUTOINCREMENT, Command TEXT DEFAULT '')")) {
		printf("Error creating OfflineMessages table: %s\n", db->GetErrorString().c_str());
		return false;
	}

	return true;
}

void db_quit() {
}

bool AddOfflineMessage(const vector<string>& parms) {
	if (parms.size() == 0) {
		return false;
	}

	UniValue obj(UniValue::VARR);
	for (auto& x : parms) {
		obj.push_back(x);
	}

	SC_Row row;
	row.Values["Command"] = obj.write();
	return db->Insert("OfflineMessages", row);
}

void SendOfflineMessages(Client* cli) {
	if (cli->state != CS_CONNECTED) {
		return;
	}

	auto res = db->Query("SELECT * FROM OfflineMessages ORDER BY ID ASC");
	if (res == NULL || db->NumRows(res) == 0) {
		db->FreeResult(res);
		return;
	}

	cli->SendServerNotice("Beginning replay of messages you received while offline...");

	SC_Row row;
	while (db->FetchRow(res, row)) {
		UniValue obj;
		if (!obj.read(row.Get("Command")) || !obj.isArray()) {
			printf("Error decoding JSON for offline message: %s\n", row.Get("Command").c_str());
			continue;
		}

		bool all_good = true;
		vector<string> parms;
		auto& vals = obj.getValues();
		for (size_t i=0; i < vals.size(); i++) {
			if (!vals[i].isStr()) {
				printf("Value is not string in JSON for offline message: %s\n", row.Get("Command").c_str());
				all_good = false;
				break;
			}
			string str = vals[i].get_str();
			if (i == 1 && !stricmp(str.c_str(), "PRIVMSG")) {
				str = "NOTICE"; // replay as a notice to make sure we don't send bot replies to old messages
			}
			parms.push_back(str);
		}
		if (!all_good) { continue; }

		cli->SendLine(parms);

		db->NoResultQuery(mprintf("DELETE FROM OfflineMessages WHERE ID=%lld", atoi64(row.Get("ID").c_str())));
	}
	db->FreeResult(res);

	cli->SendServerNotice("Replay complete.");
}

bool SetChannelSetting(const string& chan, const string& name, const string& value) {
	SC_Row row;
	row.Values["Channel"] = chan;
	row.Values["Name"] = name;
	row.Values["Value"] = value;
	return db->Replace("ChannelSettings", row);
}

string GetChannelSetting(const string& chan, const string& name, const string& sDefault) {
	auto res = db->Query(db->MPrintf("SELECT Value FROM ChannelSettings WHERE Channel=%Q AND Name=%Q LIMIT 1", chan.c_str(), name.c_str()));
	if (res != NULL) {
		SC_Row row;
		if (db->FetchRow(res, row)) {
			db->FreeResult(res);
			return row.Get("Value", sDefault);
		}
		db->FreeResult(res);
	}

	return sDefault;
}

