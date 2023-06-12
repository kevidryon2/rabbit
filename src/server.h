/*
Rabbit, a small, lightwight, fast, and customizable web server.
Copyright (C) 2023 kevidryon2

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "bns.h"

/* Rabbit Version String */
#define RABBIT_VERS "Argente 2"

LoadedScript *scripts;
int nloadedscripts;

#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((b)>(a)?(a):(b))

typedef struct {
	char protocol[8];
	char rverb[8];
	char path[4096];
	struct {
		char key[32];
		char value[32];
	} headers[64];
	char *body;
	
	int verb;
} RequestData;

typedef struct {
	int type; //0 = invalid, 1 = cached file, 2 = public file
	int datalen;
	char *data;
} loadFile_returnData;

typedef enum {
	VERB_GET,
	VERB_POST,
	VERB_PUT,
	VERB_PATCH,
	VERB_DELETE,
	VERB_HEAD,
	VERB_OPTIONS
} HTTPVerb;
