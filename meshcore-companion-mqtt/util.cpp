/*
 * Copyright (c) 2026, Drift Solutions
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "meshmqtt.h"

string trim_nulls(const string& str) {
	size_t n = str.find((char)0, 0);
	if (n != str.npos) {
		return str.substr(0, n);
	}
	return str;
}

string trim_nulls(const char* str, size_t len) {
	return trim_nulls(string(str, len));
}
