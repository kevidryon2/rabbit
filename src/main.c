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

/* Rabbit Version String */
#define RABBIT_VERS "Argente 1"

void sigpipe() {
	SetColor16(COLOR_RED);
	printf("EPIPE");
	ResetColor16();
}

bool exists(char *path) {
	FILE *fp = fopen(path, "r");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}

int filesize(FILE *fp) {
	int os = ftell(fp);
	fseek(fp, 0, SEEK_END);
	int s = ftell(fp);
	fseek(fp, os, SEEK_SET);
	return s;
}

typedef struct {
	int type; //0 = invalid, 1 = cached file, 2 = public file
	int datalen;
	char *data;
} loadFile_returnData;

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

void logdata(char *data) {
	for (int i=0; i<strlen(data); i++) {
		if (data[i] < ' ' || data[i] > '~') {
			printf("\\%sx%02x", "", (unsigned char)(data[i]));
		} else putchar(data[i]);
	}
}

const char *verbs[] = {"GET","POST","PUT","PATCH","DELETE","HEAD","OPTIONS"};
typedef enum {
	VERB_GET,
	VERB_POST,
	VERB_PUT,
	VERB_PATCH,
	VERB_DELETE,
	VERB_HEAD,
	VERB_OPTIONS
} HTTPVerb;

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
} RequestData;

LoadedScript *loadScript(char *data, int len);
int needle(char *n, char **h, int lh);
char *compile(char *script);
void exec(char *binscript);
int initserver();
void execscript(LoadedScript script, RequestData reqdata, char *resbuff);
int search_begin(char **restrict array, int num_elements, char *restrict string);
int startswith(char *s, char *c);
int endswith(char *restrict s, char *restrict end);
char *combine(char *restrict a, char *restrict b);
char *ntoken(char *const s, char *d, int t) {
	char *tk = malloc(strlen(s)+1);
	strcpy(tk, s);
	strtok(tk, d);
	for (int i=0; i<t; i++) {
		tk = strtok(0, d);
	}
	return tk;
}

char *escapestr(unsigned char *s) {
	unsigned char *o = malloc(BUFSIZ);

	memset(o, 0, BUFSIZ);
	
	int j;

	int oi = 0;
	for (int i=0; i<strlen(s); i++) {
		for (j=i; s[j] &&
			 	  s[j] != '/' &&
			 	  s[j] < 0x80 &&
			 	  s[j] > 0x1f; j++);
		j -= i;
		if (j > 0) {
			if (j > 25) j = 25;
			o[oi] = 'A'+j; oi++;
			memcpy(o+oi, s+i, j); oi += j, i += j;
			i--;
		} else {
			o[oi] = (s[i]>>4)+'a'; oi++;
			o[oi] = (s[i]%16)+'a'; oi++;
		}
	}
	
	return o;
}

void set_env_variable() {
	printf("You should set the RABBIT_PATH environment variable to the path you want to use as the server main directory.\n");
	exit(1);
}

int main(int argc, char **argv, char **envp) {
	char *buffer;
	char scriptpath[BUFSIZ];
	void *tempptr;
	struct dirent *ent;
	FILE *fp;
	int len;
	int sn = 0;
	unsigned short port;
	
	printf("Rabbit "RABBIT_VERS"\n");
	
	signal(SIGPIPE, sigpipe);
	
	/* Seed RNG */
	srand(time(NULL));
	
	if (argc<2) {
		port = 8080;
	} else {
		port = atoi(argv[1]);
	}

	printf("Using port %d\n\n", port);
	
	int serversock = initserver(port);
	char rootpath[BUFSIZ];
	char cwdbuffer[BUFSIZ/2];
	char *fullpath;
	loadFile_returnData read_data;
	
	/* Get server path */
	getcwd(cwdbuffer, BUFSIZ/2);
	strcpy(rootpath, cwdbuffer);
	if (!getenv("RABBIT_PATH")) set_env_variable();
	if (!(fullpath = realpath(getenv("RABBIT_PATH"), NULL))) {
		perror(getenv("RABBIT_PATH"));
		return 127;
	}
	
	strcpy(rootpath, fullpath);
	strcat(rootpath, "/");
	
	printf("Using directory %s\n", rootpath);
	
	printf("\nLoading scripts...\n");
	
	sprintf(scriptpath, "%s/scripts/", rootpath);
	
	DIR *dp = opendir(scriptpath);
	if (!dp) {
		printf("can't open script directory :( (Errno %d)\n", errno);
		printf("You should create the 'scripts', 'public', 'cache' (the latter as a ramdisk) directories in the main server folder.\n");
	}
	
	while ((ent = readdir(dp))) {
		if (ent->d_name[0] != '.') {
			if (endswith(ent->d_name, ".bns")) {
				
				/*Open file*/
				buffer = combine(scriptpath, ent->d_name);
				printf("Loading file %s... ", ent->d_name);
				fp = fopen(buffer, "r");
				
				if (!fp) {
					printf("can't open :( (Error %d)\n", errno);
					return 1;
				}
				
				/* Read file */
				free(buffer);
				fseek(fp, 0, SEEK_END);
				len = ftell(fp);
				buffer = malloc(len);
				
				fseek(fp, 0, SEEK_SET);
				fread(buffer, 1, len, fp);
				
				/* Load file */
				tempptr = loadScript(buffer, len);
				if (!tempptr) {
					printf("can't load :( (Error %d)\n", errno);
					return 1;
				}
				
				scripts[sn] = *(LoadedScript*)tempptr;
				
				printf("OK (Script %d)\n", sn);
				
				sn++;
				fclose(fp);
			}
		}
	};
	closedir(dp);
	
	printf("Loaded Rabbit. Accepting requests.\n\n");
	
	int csock;
	int cverb;
	int statcode;
	int script;
	int bodylen;
	
	struct sockaddr_in caddr;
	socklen_t caddrl;
	char reqbuff[BUFSIZ];
	char *resbuff = malloc(BUFSIZ);
	
	if (!resbuff) {
		perror("malloc");
		return -1;
	}
	
	RequestData reqdata;
	
	FILE *tmpfp;
	
	FILE *publicfp;
	FILE *cachedfp;
	
	char public_path[PATH_MAX];
	char cached_path[PATH_MAX];
	char *line;
	char *body;
	
	while (true) {
		
		/* Accept */
		csock = accept(serversock, &caddr, &caddrl);
		printf("Recieved Request (Address %d.%d.%d.%d, Port %d) ",
			   caddr.sin_addr.s_addr%256,
			   (caddr.sin_addr.s_addr>>8)%256,
			   (caddr.sin_addr.s_addr>>16)%256,
			   (caddr.sin_addr.s_addr>>24)%256,
			   be16toh(caddr.sin_port)
		);
		
		/* Clear buffers */
		memset(resbuff, 0, BUFSIZ);
		memset(reqbuff, 0, BUFSIZ);
		memset(&reqdata, 0, sizeof(reqdata));
		
		/* Read request */
		putchar('I');
		read(csock, reqbuff, BUFSIZ);
		
		/* Parse request */
		putchar('P');
		line = ntoken(reqbuff, "\r\n", 0);
		
		/* If 2nd token doesn't exist error */
		if (!ntoken(line, " ", 2)) {
			SetColor16(COLOR_RED);
			printf("H");
			ResetColor16();
			printf(" (Data: ");
			logdata(reqbuff);
			printf(")");
			sprintf(resbuff, "Bad Protocol\n");
			write(csock, resbuff, strlen(resbuff));
			goto endreq;
		}
		
		/* Read request data */
		strncpy(reqdata.rverb, ntoken(line, " ", 0), 7);
		strcpy(reqdata.path, ntoken(line, " ", 1));
		strncpy(reqdata.protocol, ntoken(line, " ", 2), 7);
		
#ifndef NO_REDIRECT_ROOT
		if (!strcmp(reqdata.path, "/")) {
			strcpy(reqdata.path, "/index.html");
		}
#elif REDIRECT_ROOT_PHP
		if (!strcmp(reqdata.path, "/")) {
			strcpy(reqdata.path, "/index.php");
		}
#endif
		/* Verify client is using HTTP 1.0 or HTTP 1.1 Protocol and using verb GET, POST, PUT, PATCH, DELETE, OPTIONS, or HEAD*/
		if (!(startswith(reqdata.protocol, "HTTP/1.0") ||
			  startswith(reqdata.protocol, "HTTP/1.1"))) {
			/* Using HTTP 0.9 */
			SetColor16(COLOR_RED);
			printf("H");
			ResetColor16();
			printf(" (Data: ");
			for (int i=0; i<32; i++)
				if (reqbuff[i]=='\n')
					putchar('_'); else putchar(reqbuff[i]);
			if (strlen(reqbuff)>32) {
				printf("...");
			}
			printf(")");
			printf("%20s", reqbuff);
			if (strlen(reqbuff)>20) {
				printf("...");
			}
			printf(")");
			sprintf(resbuff, "Bad Protocol\n");
			write(csock, resbuff, strlen(resbuff));
			goto endreq;
		} else if ((cverb = needle(reqdata.rverb, verbs, 7)) < 0) {
			/* Using invalid verb */
			SetColor16(COLOR_RED);
			printf("V (Bad verb %s)", reqdata.rverb);
			ResetColor16();
			sprintf(resbuff, "HTTP/1.0 501 Not Implemented\nServer: Rabbit/"RABBIT_VERS"\n\nInvalid Method");
			write(csock, resbuff, strlen(resbuff));
			goto endreq;
		}
		putchar('H');
		
		/* Search for a script to handle the request */
		for (int i=0; i<256; i++) {
			for (int j=0; j<16; j++) {
				if (scripts[i].paths[j])
				if (!strcmp(scripts[i].paths[j], reqdata.path)) {
					goto script;
				}
			}
		};
		
		
		/* TODO: Parse headers */
		
		goto noscript;
script:
		if (cverb == VERB_OPTIONS) {
			printf("Ts");
			/* TODO: Return verbs supported by script */
		}
		putchar('S');
		execscript(scripts[script], reqdata, resbuff);
		goto response;
		
noscript:
		/* If verb is OPTIONS return allowed options (GET, OPTIONS, HEAD) */
		if (cverb == VERB_OPTIONS) {
			printf("T");
			sprintf(resbuff, "HTTP/1.0 200 OK\r\nServer: Rabbit/"RABBIT_VERS"\r\nAllow: OPTIONS, GET, HEAD\r\n");
			write(csock, resbuff, strlen(resbuff));
			goto endreq;
			
		}
		
		/* Fetch file */
		putchar('F');
		memset(public_path, 0, PATH_MAX);
		sprintf(public_path, "%s/public/%s", rootpath, reqdata.path);
		
		/* If file doesn't exist in public directory return 404 Not Found */
		if (!(publicfp = fopen(public_path, "r"))) {
			SetColor16(COLOR_RED);
			printf("%d ", errno);
			printf("%s ", reqdata.path);
			ResetColor16();
			if (errno == 2) {
				sprintf(resbuff, "HTTP/1.0 404 Not Found\nServer: Rabbit/"RABBIT_VERS"\n\nError: File %s not found.\n", reqdata.path);
			} else {
				sprintf(resbuff, "HTTP/1.0 500 Internal Server Error\nServer: Rabbit/"RABBIT_VERS"\n\nError: Recieved errno %d while trying to read file %s.\n", errno, reqdata.path);
			}
			write(csock, resbuff, strlen(resbuff));
			goto endreq;
		}
		
		/* File exists */
		printf(" %s ", reqdata.path);
		memset(cached_path, 0, PATH_MAX);
		sprintf(cached_path, "%s/cache/%s", rootpath, escapestr(reqdata.path));
										 
		read_data = loadFile(public_path, cached_path, csock);

		printf("%d ", read_data.datalen);
		
response:
		/* Send response */
		putchar('O');
		
		sprintf(resbuff, "HTTP/1.0 200 OK\r\nServer: Rabbit/"RABBIT_VERS"\r\n\r\n");
		write(csock, resbuff, strlen(resbuff));
		
		write(csock, read_data.data, read_data.datalen);
		free(read_data.data);
		
endreq:
		/* Finish and flush */
		putchar('\n');
		fflush(stdout);
		close(csock);
	}
}
