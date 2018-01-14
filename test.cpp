#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <fcntl.h>

#include "SocketTools.hpp"
SocketPools epending_pool;

void InitPool() {
	int lis_socket = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(20008);
	bind(lis_socket, (sockaddr*)&server_addr, sizeof(server_addr));
	listen(lis_socket, 500);
	epending_pool.SetListenSocket(lis_socket);
	epending_pool.SetSocketConnTimeOut(5);
}

int do_read(int socket, void* content, int len, int& readlen, int timeout) {
	int left = len;
	int readbufflen = 0;
	char* ccontent = (char*)content;
	while (left > 0) {
		struct pollfd fds[1];
		fds[0].fd = socket;
		fds[0].events = POLLIN | POLLHUP | POLLERR;
		fds[0].revents = 0;
		int ret = poll(fds, 1, timeout);
		readbufflen = 0;
		if (ret <= 0 || (fds[0].revents & (POLLHUP | POLLERR))) {
			if (ret == -1) {
				if (errno == EINTR)
					continue;
			}else if (ret < 0){
				return -1;
			}
		}else{
			readbufflen = read(socket, ccontent, left);
		}
		if (readbufflen == 0)
			break;
		else {
			ccontent += readbufflen;
			left -= readbufflen;
		}
	}
	readlen = len - left;
	return 0;
}

int do_write(int socket, void* content, int len, int timeout) {
	int on=1;
	int n=-1;
	char* ccontent = (char*)content;
	int ret = fcntl(socket, F_GETFL, 0);
	if (0 == (ret & O_NONBLOCK)) {
		fcntl(socket, F_SETFL, ret | O_NONBLOCK);
		setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
	}
	int left = len;
	while (left > 0) {
		n = 0;
		struct pollfd fds[1];
		fds[0].fd = socket;
		fds[0].events = POLLOUT;
		fds[0].revents = 0;
		int ret = poll(fds, 1, timeout);
		if (ret < 0) {
			continue;
		}else if (ret == 0) {
			return -1;
		} else {
			n = write(socket, ccontent, left);
		}
		
		left -= n;
		ccontent += n;
	}
	return 0;
}

static void* server_thread(void* args) {
	while (epending_pool.PoolHasRun()) {
		epending_pool.CheckItem();
	}
	return NULL;
}

static void* client_thread(void* args) {
	int socket;
	int offset;
	int readlen;
	char buffer[2048*2048];
	while (true) {
		epending_pool.GetReadyQueue(offset, socket);
		//LOG("offset:%d socket:%d", offset, socket);
		int len = do_read(socket, buffer, 2048*2048, readlen, 2);
		if (len < 0 || readlen == 0) {
			LOG("len:%d, readlen:%d",len, readlen);
			epending_pool.ResetSocket(offset, false);
			continue;
		}
		buffer[readlen] = '\0';
		//LOG("Read content:%s len:%d from Socket[%d]", buffer, len, socket);
		len = do_write(socket, buffer, strlen(buffer), 2);
		//LOG("Send content:%s from Socket[%d]", buffer, socket);
		epending_pool.ResetSocket(offset, true);
	}
}

int main(int argc, char* argv[]) {
	//SocketPools epending_pool;
	InitPool();
	pthread_t listen;
	pthread_t client;
	pthread_create(&listen, NULL, server_thread, NULL);
	pthread_create(&client, NULL, client_thread, NULL);
	pthread_join(listen, NULL);
	pthread_join(client, NULL);
	return 0;
}
