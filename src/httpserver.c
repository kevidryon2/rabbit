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
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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
