#ifndef SOCKETTOOLS_H_
#define SOCKETTOOLS_H_

#include <iostream>
#include <cstdlib>
#include <cstdio>

#define LOG(format, arg...) do {fprintf(stdout, "[%s] [%s] : "fmt, __FILE__, __func__, ##arg);} while(0);
#define ERROR(format, arg...) do {fprintf(stderr, "[%s] [%s] : "fmt, __FILE__, __func__, ##arg);} while(0);

class SocketPools 
{
 public:
	typedef int (EventCallBack*)(int socket, void **arg);
	SocketPools() {
		SocketListenFd_ = -1;
		SocketNum_ = MAX_SOCKET_NUM;
		SocketLen_ = 0;
		SocketItemArray_ = new (std::nothrow)SocketItem[SocketNum_];
		if (NULL == SocketItemArray_) {
			ERROR("%s", "alloc SocketItemArray_ failed!\n");
			return;
		}
		EpollCheck_ = new (std::throw)struct epoll_event[SocketNum_];
		if (NULL == EpollCheck_) {
			ERROR("%s", "alloc EpollCheck_  failed!\n");
			return;
		}
		EpollFd_ = epoll_create(SocketNum_);
		for (size_t eventIndex = 0; eventIndex < SOCKET_EVENT_NUM; eventIndex++) {
			EventCallBackList[eventIndex] = NULL;
		}
		/*超时时间的设置*/
		SocketConnTimeOut_ = MAX_CONN_TIMEOUT;
		SocketReadTimeOut_ = MAX_READ_TIMEOUT;
		SocketWriteTimeOut_ = MAX_WRITE_TIMEOUT;
		SocketMinTimeOut_ = SocketReadTimeOut_;
		EpollTimeOut_ = MAX_EPOLL_TIMEOUT;
		
		SocketPoolsRun_ = true;
	}
	/*设置Socket长度*/
	int SetSocketNum(size_t snum) {
		if (0 >= snum) {
			ERROR("%s", "the snum of set is negative!\n");
			return -1;
		}
		if (NULL != SocketItemArray_) {
			delete[] SocketItemArray_;
			SocketItemArray_ = NULL;
		}
		SocketNum_ = snum;
		SocketItemArray_ =	new (std::nothrow)SocketItem[SocketNum_];
		if (NULL == SocketItemArray_) {
			ERROR("%s", "alloc socket failed!\n");
			return -1;
		}
		if (0 < EpollFd_) {
			while (close(EpollFd_) < 0 && errno == EINTR) {};
			EpollFd_ = -1;
		}
		if (NULL != EpollCheck_) {
			delete[] EpollCheck_;
			EpollCheck_ = NULL;
		}
		EpollCheck_ = new (std::throw)struct epoll_event[SocketNum_];
		if (NULL == EpollCheck_) {
			ERROR("%s", "alloc epollcheck failed!\n");
			return -1;
		}
		EpollFd_ = epoll_create(SocketNum_);
		if (0 > EpollFd_) {
			ERROR("%s", "epoll create failed!\n");
			return -1;
		}
		return 0;
	}
	/*设置连接超时时间*/
	int SetSocketConnTimeOut_(int time) {
		if (0 >= time) {
			ERROR("%s", "the time is not valide\n");
			return -1;
		}
		SocketConnTimeOut_ = time;
		if (SocketConnTimeOut_ < SocketReadTimeOut_ && SocketConnTimeOut_ < SocketWriteTimeOut_) {
			SocketMinTimeOut_ = SocketConnTimeOut_;
		}
		return 0;
	}
	/*设置读超时时间*/
	int SetSocketReadTimeOut(int time) {
		if (0 >= time) {
			ERROR("%s", "the time is not valide\n");
			return -1;
		}
		SocketReadTimeOut_ = time;
		if (SocketReadTimeOut_ < SocketConnTimeOut_ && SocketReadTimeOut_ < SocketWriteTimeOut_) {
			SocketMinTimeOut_ = SocketReadTimeOut_;
		}
		return 0;
	}
	/*设置写超时时间*/
	int SetSocketWriteTimeOut(int time) {
		if (0 >= time) {
			ERROR("%s", "the time is not valide\n");
			return -1;
		}
		SocketWriteTimeOut_ = time;
		if (SocketWriteTimeOut_ < SocketReadTimeOut_ && SocketWriteTimeOut_ < SocketConnTimeOut_) {
			SocketMinTimeOut_ = SocketWriteTimeOut_;
		}
		return 0;
	}
	/*获取连接超时时间*/
	int GetSocketConnTimeOut() const {
		return SocketConnTimeOut_;
	}
	/*获取读超时时间*/
	int GetSocketReadTimeOut() const {
		return SocketReadTimeOut_;
	}
	/*获取写超时时间*/
	int GetSocketWriteTimeOut() const {
		return SocketWriteTimeOut_;
	}
 private:
	 enum {MAX_SOCKET_NUM = 1024};
	 enum {MAX_QUEUE_LEN = 500};
	 enum {MAX_CONN_TIMEOUT = 2};
	 enum {MAX_READ_TIMEOUT = 1};
	 enum {MAX_WRITE_TIMEOUT = 1};
	 enum {MAX_EPOLL_TIMEOUT = 10};
	 typedef enum {
		NOT_USED = 0;
		READY,
		READ_BUSY,
		BUSY,
		WRITE_BUSY
	 }SocketStatus;	//SocketStatus
	 typedef struct SocketItem {
		int socket;
		int socketStatus;
		int socketActiveTime;
		void* arge;
		struct timeval socketQueueTime;
	 }SocketItem;	//SocketItem
	 typedef enum {
		SOCKET_ACCEPT = 0;
		SOCKET_INIT,
		SOCKET_LISTENTIMEOUT,
		SOCKET_READ,
		SOCKET_READTIMEOUT,
		SOCKET_WRITE;
		SOCKET_WRITETIMEOUT,
		SOCKET_CLEAR,
		SOCKET_FETCH,
		SOCKET_QUEUEFAIL,
		SOCKET_INSERTFAIL,
		SOCKET_EVENT_NUM,
	 }SocketEvent;	//SocketEvent
	 int SocketListenFd_;			//socket监听具柄
	 size_t SocketNum_;				//使用socket数量
	 size_t SocketLen_;				//正在使用socket数量
	 SocketItem* SocketItemArray_;	//存放socketItem的地方

	 struct epoll_event* EpollCheck_;	//epoll监听
	 int EpollFd_;						//epoll读写文件描述符
	 bool SocketPoolsRun_;				//连接池是否启动
	 EventCallBack EventCallBackList[SOCKET_EVENT_NUM];	//设置回调函数
	 /*超时设置 连接、读、写、最小、Epoll超时*/
	 int SocketConnTimeOut_;
	 int SocketReadTimeOut_;
	 int SocketWriteTimeOut_;
	 int SocketMinTimeOut_;
	 int EpollTimeOut_;
}; //SocketPools

#endif //SOCKETTOOLS_H_
