#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include "SocketTools.hpp"
SocketPools epending_pool;

static void* server_thread(void* args) {
	int lis_socket = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(20008);
	bind(lis_socket, (sockaddr*)&server_addr, sizeof(server_addr));
	listen(lis_socket, 10);
	epending_pool.SetListenSocket(lis_socket);
	while (epending_pool.PoolHasRun()) {
		epending_pool.CheckItem();
	}
	return NULL;
}

int main(int argc, char* argv[]) {
	//SocketPools epending_pool;
	pthread_t listen;
	pthread_create(&listen, NULL, server_thread, NULL);
	pthread_join(listen, NULL);
	return 0;
}
