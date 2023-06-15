#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/ip.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include "hirolib.h"
#include "bns.h"
#include "server.h"

LoadedScript *scripts;
int nloadedscripts = 1;

const char *verbs[] = {"GET","POST","PUT","PATCH","DELETE","HEAD","OPTIONS"};

char *ntoken(char *const s, char *d, int t);
int startswith(char *s, char *c);
int needle(char *n, char **h, int lh);

bool exists(char *path) {
	FILE *fp = fopen(path, "r");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}

RequestData *RabbitParseRequest(const char *const reqbuff) {
	RequestData *reqdata = malloc(sizeof(RequestData));
	char *line;
	char *tmp;
	
	if (!reqdata) {
		perror("malloc");
		exit(1);
	}
	
	line = ntoken(reqbuff, "\x0d\x0a", 0);
	
	/* If 2nd token doesn't exist error */
	if (!(tmp = ntoken(line, " ", 2))) {
		errno=1; return NULL;
	}
	
	/* Read request data */
	tmp = ntoken(line, " ", 0);
	strncpy(reqdata->rverb, tmp, 7);
	free(tmp);
	
	tmp = ntoken(line, " ", 1);
	strncpy(reqdata->path, tmp, 4096);
	free(tmp-4);
	
	tmp = ntoken(line, " ", 2);
	strncpy(reqdata->protocol, tmp, 8);
	free(tmp-6);
	
	free(line);
	
#ifndef NO_REDIRECT_ROOT
	if (!strcmp(reqdata->path, "/")) {
		strcpy(reqdata->path, "/index.html");
	}
#elif REDIRECT_ROOT_PHP
	if (!strcmp(reqdata->path, "/")) {
		strcpy(reqdata->path, "/index.php");
	}
#endif
	/* Verify client is using HTTP 1.0 or HTTP 1.1 Protocol and using verb GET, POST, PUT, PATCH, DELETE, OPTIONS, or HEAD*/
	if (!(startswith(reqdata->protocol, "HTTP/1.0") ||
		  startswith(reqdata->protocol, "HTTP/1.1"))) {
		errno=2; printf("(%s) (%s) (%s) (%s)", line,
				reqdata->rverb, reqdata->path, reqdata->protocol); return 0;
	} else if ((reqdata->verb = needle(reqdata->rverb, verbs, 7)) < 0) {
		/* Using invalid verb */
		errno=3; return 0;
	}
	if (!reqdata) printf("þþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþþ");
	return reqdata;
}

//Returns a socket fd
int initserver(unsigned short port) {
	
	int sock;
	struct sockaddr_in addr = {
		AF_INET,
		htons(port),
		0
	};
	
	printf("Creating socket... ");
	sock = socket(AF_INET, SOCK_STREAM, 0);
	
	printf("OK\nBinding... ");
	if (bind(sock, &addr, sizeof(addr))) {
		printf("can't bind :( (error %d)\n", errno);
		exit(127);
	}
	
	int t = 1;
										
	if (!setsockopt(sock, IPPROTO_TCP, SO_REUSEPORT, &t, sizeof(int))) {
		printf("can't setsockopt :( (error %d)\n", errno);
		exit(127);
	}
										
	printf("OK\nListening... ");
	if (listen(sock, 4096)) {
		printf("can't listen :( (error %d)\n", errno);
	}
	
	printf("OK\n\n");
	return sock;
}

loadFile_returnData loadFile(char *pubpath, char *cachepath, int csock) {
	if (!pubpath) {errno=EINVAL; return (loadFile_returnData){0};};
	loadFile_returnData data;

	FILE *pubfile;
	FILE *cachefile;

#ifndef DISABLE_CACHE
	if (!exists(cachepath)) {
#else
	if (true) {
#endif
		//If cached file doesn't exist, cache file
		pubfile = fopen(pubpath, "r");
		cachefile = fopen(cachepath, "w");
		if (!cachefile) {
			printf("can't open file %s :( (Error %d)\n", cachepath, errno);
			exit(1);
		}

		data.datalen = filesize(pubfile);
		data.data = malloc(data.datalen);
		if (!data.data) {
			perror("malloc");
			exit(1);
		}

		fread(data.data, 1, data.datalen, pubfile);
		fwrite(data.data, 1, data.datalen, cachefile);

		fclose(pubfile);
		fclose(cachefile);
	} else {
		cachefile = fopen(cachepath, "r");

		data.datalen = filesize(cachefile);
		data.data = malloc(data.datalen);
		if (!data.data) {
			perror("malloc");
			exit(1);
		}

		fread(data.data, 1, data.datalen, cachefile);
		fclose(cachefile);
	}
	return data;
}
