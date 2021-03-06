#ifndef SOCKETTOOLS_H_ 
#define SOCKETTOOLS_H_

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <sys/epoll.h>
#include <sys/time.h>

#include <unistd.h>
#include <errno.h>
#include <error.h>

#include "../hppTools/NoCopy.hpp"
#include "../hppTools/ConditionMutex.hpp"

#define LOG(fmt, arg...) do {fprintf(stdout, "[NOTICE] [%s:%d]:"fmt"\n", __FILE__,  __LINE__, ##arg);} while(0);
#define ERROR(fmt, arg...) do {fprintf(stdout, "[FATAL] [%s:%d]:"fmt"\n", __FILE__, __LINE__, ##arg);} while(0);

class SocketPools;
typedef struct SocketItem {
	int socket;
	int socketStatus;
	int socketActiveTime;
	void* args;
	struct timeval socketQueueTime;
	SocketItem():socket(0), socketStatus(0), socketActiveTime(0), args(NULL) {
		memset(&socketQueueTime, 0, sizeof(socketQueueTime));
	}
}SocketItem;	//SocketItem
template <class T>
class SocketBlockQueue: multiTools::NoCopy {
 public:
 	explicit SocketBlockQueue(size_t length):Length_(length), Head_(0), Tail_(0), Size_(0), Smutex_(), Nullmutex_(Smutex_) {
		Array_ = new (std::nothrow)T[Length_];
		if (NULL == Array_) {
			ERROR("alloc SocketBlockQueue failed!");
		}
	}
	~SocketBlockQueue() {
		if (Array_) {
			delete Array_;
			Array_ = NULL;
		}
	}
	size_t Length() const {
		multiTools::SuperMutexFriend localLock(&Smutex_);
		return Size_;
	}
	bool IsEmpty() const {
		return Length() == 0;
	}
	bool IsFull() const {
		return Length() == Length_;
	}
	int Push(T offset, SocketItem* sitem, int status) {
		if (offset < 0 || NULL == sitem) {
			ERROR("offset is negative or sitem is null");
			return -1;
		}
		multiTools::SuperMutexFriend localLock(&Smutex_);
		if (((Head_ + 1) % Length_) == Tail_) {
			ERROR("insert socket:%d failed, queue array[max_size:%d] overflow", sitem->socket, (int)Length_);
			return -1;	
		}
		Size_ += 1;
		Array_[Head_] = offset;
		//设置参数和时间	
		sitem->socketStatus = status;
		gettimeofday(&(sitem->socketQueueTime), NULL);
		sitem->socketActiveTime = sitem->socketQueueTime.tv_sec;
		Head_ = (Head_ + 1) % Length_;
		Nullmutex_.Notify();
		return 0;
	}
	int Pop(T &offset, SocketItem* array, int status) {
		if (NULL == array) {
			ERROR("socketitem is null");
			return -1;
		}
		multiTools::SuperMutexFriend localLock(&Smutex_);
		while (Tail_ == Head_) {
			Nullmutex_.Wait();	
		}
		Size_ -= 1;
		offset = Array_[Tail_];
		//
		if (array[offset].socketStatus != status) {
			return -1;
		}
		Tail_ = (Tail_ + 1) % Length_;
		return 0;
	}
 private:
	size_t Length_;
	size_t Head_;
	size_t Tail_;
	size_t Size_;
	T* Array_;
	mutable multiTools::SuperMutex Smutex_;
	multiTools::ConditionMutex Nullmutex_;
}; //SocketBlockQueue

class SocketPools 
{
 public:
	typedef int (*EventCallBack)(int socket, void **arg);
	SocketPools() {
		SocketListenFd_ = -1;
		SocketNum_ = MAX_SOCKET_NUM;
		SocketLen_ = 0;
		SocketLastActive_ = INT_MAX;
		SocketItemArray_ = new (std::nothrow)SocketItem[SocketNum_];
		if (NULL == SocketItemArray_) {
			ERROR("%s", "alloc SocketItemArray_ failed!");
			return;
		}
		EpollCheck_ = new (std::nothrow)struct epoll_event[SocketNum_];
		if (NULL == EpollCheck_) {
			ERROR("%s", "alloc EpollCheck_  failed!");
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
		
		ReadyQueue_ = new (std::nothrow)SocketBlockQueue<int>(MAX_QUEUE_LEN);
		if (NULL == ReadyQueue_) {
			ERROR("alloc BlockQueue failed!");
			return;
		}
		SocketPoolsRun_ = true;
	}
	~SocketPools() {
		//todo
	}
	bool PoolHasRun() const { return SocketPoolsRun_;}
	int SetListenSocket(int listenSocket) {
		if (0 > listenSocket) {
			ERROR("listensocket:%d is invalid!", listenSocket);
			return -1;
		}
		SocketListenFd_ = listenSocket;
		struct epoll_event ev;
		ev.data.fd = -1;
		ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		if (epoll_ctl(EpollFd_, EPOLL_CTL_ADD, SocketListenFd_, &ev) < 0) {
			ERROR("epoll add failed!socket[%d] error info:%s", SocketListenFd_, strerror(errno));
			return -1;
		}
		return 0;
	}
	/*设置Socket长度*/
	int SetSocketNum(size_t snum) {
		if (0 >= snum) {
			ERROR("%s", "the snum of set is negative!");
			return -1;
		}
		if (NULL != SocketItemArray_) {
			delete[] SocketItemArray_;
			SocketItemArray_ = NULL;
		}
		SocketNum_ = snum;
		SocketItemArray_ =	new (std::nothrow)SocketItem[SocketNum_];
		if (NULL == SocketItemArray_) {
			ERROR("%s", "alloc socket failed!");
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
		EpollCheck_ = new (std::nothrow)struct epoll_event[SocketNum_];
		if (NULL == EpollCheck_) {
			ERROR("%s", "alloc epollcheck failed!");
			return -1;
		}
		EpollFd_ = epoll_create(SocketNum_);
		if (0 > EpollFd_) {
			ERROR("%s", "epoll create failed!");
			return -1;
		}
		return 0;
	}
	/*设置连接超时时间*/
	int SetSocketConnTimeOut(int time) {
		if (0 >= time) {
			ERROR("%s", "the time is not valide");
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
			ERROR("%s", "the time is not valide");
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
			ERROR("%s", "the time is not valide");
			return -1;
		}
		SocketWriteTimeOut_ = time;
		if (SocketWriteTimeOut_ < SocketReadTimeOut_ && SocketWriteTimeOut_ < SocketConnTimeOut_) {
			SocketMinTimeOut_ = SocketWriteTimeOut_;
		}
		return 0;
	}
	/*获取异步fd*/
	int GetEpollFd() const {
		return EpollFd_;
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
	/**/
	int CheckTimeOut() {
		int CurrentTime = time(NULL);
		if (SocketLastActive_ != INT_MAX && CurrentTime - SocketLastActive_ < SocketMinTimeOut_) {
			return 0;
		}
		if (NULL == SocketItemArray_ || 0 > SocketLen_) {
			return -1;
		}
		int ReadyNum = 0;
		int ReadNum = 0;
		int WriteNum = 0;
		int BusyNum = 0;
		int LastActiveOffset = 0;
		SocketLastActive_ = INT_MAX;
		for (size_t i = 0; i < SocketLen_; i++) {
			switch (SocketItemArray_[i].socketStatus) {
				case NOT_USED:
					break;
				case READY:
					if (CurrentTime >= SocketItemArray_[i].socketActiveTime + SocketConnTimeOut_) {
						LOG("socket index %d timeout,last_active[%d],conn_timeout[%d],current_time[%d]", (int)i, SocketItemArray_[i].socketActiveTime, SocketConnTimeOut_, CurrentTime);
						if (NULL != EventCallBackList[SOCKET_LISTENTIMEOUT]) {
							EventCallBackList[SOCKET_LISTENTIMEOUT](SocketItemArray_[i].socket, (void**)(&SocketItemArray_[i].args));
						}
						ResetSocket(i, false);
						continue;
					}
					ReadyNum++;
					if (SocketLastActive_ > SocketItemArray_[i].socketActiveTime) {
						SocketLastActive_ = SocketItemArray_[i].socketActiveTime;
					}
					LastActiveOffset = i;
					break;
				case READ_BUSY:
					if (CurrentTime >= SocketItemArray_[i].socketActiveTime + SocketReadTimeOut_) {
						LOG("socket index %d timeout,last_active[%d],read_timeout[%d],current_time[%d]", (int)i, SocketItemArray_[i].socketActiveTime, SocketReadTimeOut_, CurrentTime);
						if (NULL != EventCallBackList[SOCKET_READTIMEOUT]) {
							EventCallBackList[SOCKET_READTIMEOUT](SocketItemArray_[i].socket, (void**)(&SocketItemArray_[i].args));
						}
						ResetSocket(i, false);
						continue;
					}
					ReadNum++;
					if (SocketLastActive_ > SocketItemArray_[i].socketActiveTime) {
						SocketLastActive_ = SocketItemArray_[i].socketActiveTime;
					}
					LastActiveOffset = i;
					break;
				case WRITE_BUSY:
					if (CurrentTime >= SocketItemArray_[i].socketActiveTime + SocketWriteTimeOut_) {
						LOG("socket index %d timeout,last_active[%d],write_timeout[%d],current_time[%d]", (int)i, SocketItemArray_[i].socketActiveTime, SocketWriteTimeOut_, CurrentTime);
						if (NULL != EventCallBackList[SOCKET_WRITETIMEOUT]) {
							EventCallBackList[SOCKET_WRITETIMEOUT](SocketItemArray_[i].socket, (void**)(&SocketItemArray_[i].args));
						}
						ResetSocket(i, false);
						continue;
					}
					WriteNum++;
					if (SocketLastActive_ > SocketItemArray_[i].socketActiveTime) {
						SocketLastActive_ = SocketItemArray_[i].socketActiveTime;
					}
					LastActiveOffset = i;
					break;
				case BUSY:
					BusyNum++;
					SocketItemArray_[i].socketActiveTime = CurrentTime;
					LastActiveOffset = i;
					break;
				default:
					LOG("unkonw socket status %d", SocketItemArray_[i].socketStatus);
					break;
			}
		}
		SocketLen_ = LastActiveOffset + 1;
		//线程问题
		if (ReadyNum == 0 && (BusyNum + WriteNum + BusyNum) > 0) {
			LOG("Ready:%d Busy:%d Read:%d Write:%d", ReadyNum, BusyNum, ReadNum, WriteNum);
		}
		return 0;
	}
	int EpollWait(int timeout) {
		int nfds;
		if (0 > EpollFd_) {		//可能会有问题
			ERROR("epoll fd is not valide[%d]!", EpollFd_);
			return -1;
		}
		while (true) {
			nfds = epoll_wait(EpollFd_, EpollCheck_, SocketNum_, timeout);
			if (0 > nfds) {
				if (errno == EINTR) {
					continue;
				}
				LOG("epoll_wait failed.[%d]:%m", errno);
			}
			break;
		}
		return nfds;
	}
	int CheckItem() {
		CheckTimeOut();
		int ChangeNum = EpollWait(EpollTimeOut_);
		if (0 >= ChangeNum) {
			return ChangeNum;
		}
		int offset = -1;
		int ret = -1;
		for (int i = 0; i < ChangeNum; i++) {
			if (EpollCheck_[i].data.fd == -1 && SocketListenFd_ > 0) {
				int WorkSock = AcceptSock();
				if (WorkSock < 0) {
					ERROR("Accept socket error!");
					continue;
				}
				if (InsertSocket(WorkSock) < 0) {
					while (close(WorkSock) < 0 && errno == EINTR) {};
					ERROR("insert WorkSock into queue failed!");
				}
			} else if (EpollCheck_[i].data.fd >= 0) {
				//
				offset = EpollCheck_[i].data.fd;
				if (EpollCheck_[i].events & EPOLLHUP) {
					ERROR("socket %d closed!", SocketItemArray_[offset].socket);
					ResetSocket(offset, false);
				} else if (EpollCheck_[i].events & EPOLLERR) {
					ERROR("socket %d error!", SocketItemArray_[offset].socket);
					ResetSocket(offset, false);
				} else if (EpollCheck_[i].events & EPOLLIN) {
					//do read
					if (NULL != EventCallBackList[SOCKET_READ]) {
						//todo
						ret = 0;
					} else {
						ret = 0;
					}
					switch (ret) {
						case 0:
							if (0 != PutReadyQueue(offset)) {
								ResetSocket(offset, false);
							}
							break;
						case 1:
							break;
						default:
							break;
					}
				} else if (EpollCheck_[i].events & EPOLLOUT) {
					//do output
				} else {
					LOG("offset %d is close!", offset);
					ResetSocket(offset, false);
				}
			}
		}
		return 0;
	}
	int PutReadyQueue(int offset) {
		//状态检查
		if (SocketItemArray_[offset].socketStatus != READY &&
			SocketItemArray_[offset].socketStatus != READ_BUSY) {
			ERROR("socket:%d offset:%d status:%d is wrong!", SocketItemArray_[offset].socket, offset, SocketItemArray_[offset].socketStatus);
			return -1;
		}
		if (0 > ReadyQueue_->Push(offset, &SocketItemArray_[offset], BUSY)) {
			ERROR("insert into readyqueue_ failed!");
			return -1;
		}
		return 0;
	}
	int GetReadyQueue(int& offset, int& socket) {
		int fetch_offset = -1;
		int fetch_socket = -1;
		if (NULL == SocketItemArray_) {
			ERROR("socket array is null!");
			return -1;
		}
		if (0 > ReadyQueue_->Pop(fetch_offset, SocketItemArray_, BUSY)) {
			ERROR("get offset from readyqueue_ failed!");
			offset = -1;
			socket = -1;
			return -1;
		}
		fetch_socket = SocketItemArray_[fetch_offset].socket;
		if (NULL != EventCallBackList[SOCKET_FETCH]) {
			//todo
		}
		offset = fetch_offset;
		socket = fetch_socket;
		return 0;
	}
	int GetOffsetFromArray() {
		int ret;
		if (NULL == SocketItemArray_) {
			ERROR("SocketItemArray_ is NULL!");
			return -1;
		}
		for (size_t i = 0; i < SocketLen_; i++) {
			if (NOT_USED == SocketItemArray_[i].socketStatus) {
				return i;
			}
		}
		if (SocketLen_ >= SocketNum_) {
			ERROR("socketlen is larger than socketnum!");
			return -1;
		}
		ret = SocketLen_++;
		return ret;
	}
	int InsertSocketArray(int worksock) {
		if (0 > worksock) {
			ERROR("worksock is not valide[%d]!", worksock);
			return -1;
		}
		int current_offset = GetOffsetFromArray();
		if (current_offset == -1) {
			ERROR("get current_offset is not valide!");
			return -1;
		}
		SocketItemArray_[current_offset].socket = worksock;
		SocketItemArray_[current_offset].socketActiveTime = time(NULL);
		SocketItemArray_[current_offset].socketStatus = READY;
		LOG("set socket:%d status:%d", worksock, SocketItemArray_[current_offset].socketStatus);
		return current_offset;
	}
	//flags = 0 监听读事件，flags = 1监听写事件
	int InsertSocket(int worksock, void* arg = NULL, int flags = 0) {
		int offset = InsertSocketArray(worksock);
		LOG("offset:%d", offset);
		if (0 > offset) {
			ERROR("insert socket failed!");
			return -1;
		}
		if (NULL != arg) {
			SocketItemArray_[offset].args = arg;
		} else {
			//todo
		}
		uint32_t events = 0;
		if (flags == 0) {
			events = EPOLLONESHOT | EPOLLIN | EPOLLHUP | EPOLLERR;
		} else {
			events = EPOLLOUT | EPOLLHUP | EPOLLERR;
		}
		if (0 > EpollAddEvents(offset, events)) {
			if (NULL != SocketItemArray_[offset].args) {
				SocketItemArray_[offset].args = NULL;
			}
			ClearSocket(offset);
			return -1;
		}
		return 0;
	}
	int ClearSocket(int offset) {
		if (offset < 0 || offset >= SocketLen_)	{
			ERROR("invalid offset %d!", offset); 
			return -1;
		}
		if (NULL ==  SocketItemArray_) {
			ERROR("the socketArray is NULL!");
			return -1;
		}
		if (NOT_USED == SocketItemArray_[offset].socketStatus) {
			return 0;
		}
		if (0 > SocketItemArray_[offset].socket) {
			ERROR("socket[%d] in offset[%d] is invalid!",SocketItemArray_[offset].socket, offset);
			return -1;
		}
		EpollDelEvents(offset);
		if (NULL != EventCallBackList[SOCKET_CLEAR]) {
			//todo
		}
		SocketItemArray_[offset].socket = -1;
		SocketItemArray_[offset].socketStatus = NOT_USED;
		return 0;
	}
	int ResetSocket(int offset, bool keep_alive) {
		int sock = -1;
		int ret = -1;
		if (offset < 0 || offset >= SocketLen_) {
			ERROR("invalid offset %d!", offset);
			return -1;
		}
		if (NULL == SocketItemArray_) {
			ERROR("socketitemarray is null!");
			return -1;
		}
		if (NOT_USED == SocketItemArray_[offset].socketStatus) {
			ERROR("socket status is wrong[%d]!", SocketItemArray_[offset].socketStatus);
			return -1;
		}
		sock = SocketItemArray_[offset].socket;
		if (sock < 0) {
			ERROR("get socket from offset is invalue[%d]!", SocketItemArray_[offset].socket);
			return -1;
		}
		if (!keep_alive) {
			LOG("close socket[%d]", sock);
			if (NULL != EventCallBackList[SOCKET_CLEAR]) {
				EventCallBackList[SOCKET_CLEAR](SocketItemArray_[offset].socket, (void**)(&SocketItemArray_[offset].args));
				//???
				SocketItemArray_[offset].args = NULL;
			}
			//EpollDelSocket(sock, offset);
			while ((ret = close(sock)) < 0 && errno == EINTR) {};
			if (ret < 0) {
				ERROR("close socket offset[%d],sock[%d],error[%d]:%m",offset, sock, errno);
				return -1;
			}
			SocketItemArray_[offset].socket = -1;
			SocketItemArray_[offset].socketStatus = NOT_USED;
		} else {
			SocketItemArray_[offset].socketActiveTime = time(NULL);
			SocketItemArray_[offset].socketStatus = READY;
			EpollSetEvents(offset, EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLONESHOT);
		}
		return 0;
	}
	int AcceptSock() {
		int work_sock = -1;
		if (NULL != EventCallBackList[SOCKET_ACCEPT]) {
			work_sock = EventCallBackList[SOCKET_ACCEPT](SocketListenFd_, NULL);
			if (0 > work_sock) {
				ERROR("accept sock callback fail");
				return -1;
			}
			return work_sock;
		}
		//默认处理
		//LOG("do not set default acceptSock function!");
		while (true) {
			work_sock = accept(SocketListenFd_, NULL, NULL);
			if (0 > work_sock) {
				if (errno == ECONNABORTED) {
					continue;
				} else {
					ERROR("accept(%d) call failed.error[%d] info is %s.", SocketListenFd_, errno, strerror(errno));
					work_sock = -1;
				}
			} else {
				int on = 1;
				setsockopt(work_sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
			}
			break;
		}
		return work_sock;
	}
	int EpollAddEvents(int offset, uint32_t events) {
		//判断offset
		if (0 > EpollFd_) {
			ERROR("epollfd[%d] is invalid!", EpollFd_);
			return -1;
		}
		struct epoll_event ev;
		ev.data.fd = offset;
		ev.events = events;
		if (epoll_ctl(EpollFd_, EPOLL_CTL_ADD, SocketItemArray_[offset].socket, &ev) < 0) {
			ERROR("epoll add failed!socket[%d] offset[%d],error info:%s", SocketItemArray_[offset].socket, offset, strerror(errno));
			return -1;
		}
		return 0;
	}
	int EpollSetEvents(int offset, uint32_t events) {
		//判断offset
		if (0 > EpollFd_) {
			ERROR("epollfd[%d] is invalid!", EpollFd_);
			return -1;
		}
		struct epoll_event ev;
		ev.data.fd = offset;
		ev.events = events;
		if (epoll_ctl(EpollFd_, EPOLL_CTL_MOD, SocketItemArray_[offset].socket, &ev) < 0) {
			ERROR("epoll mod failed!socket[%d] offset[%d],error info:%s", SocketItemArray_[offset].socket, offset, strerror(errno));
			return -1;
		}
		return 0;
	}
	int EpollDelEvents(int offset) {
		//判断offset
		struct epoll_event ev;
		ev.data.fd = offset;
		ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
		if (epoll_ctl(EpollFd_, EPOLL_CTL_DEL, SocketItemArray_[offset].socket, &ev) < 0) {
			ERROR("epoll del failed!socket[%d] offset[%d]", SocketItemArray_[offset].socket, offset);
			return -1;
		}
		return 0;
	}
	int CloseSocket() {
		int num = 0;
		for (int i = 0; i < SocketLen_; i++) {
			num++;
			ResetSocket(i, false);
		}
		return num;
	}
 private:
	 enum {MAX_SOCKET_NUM = 1024};
	 enum {MAX_QUEUE_LEN = 500};
	 enum {MAX_CONN_TIMEOUT = 2};
	 enum {MAX_READ_TIMEOUT = 1};
	 enum {MAX_WRITE_TIMEOUT = 1};
	 enum {MAX_EPOLL_TIMEOUT = 10};
	 typedef enum {
		NOT_USED = 0,
		READY,
		READ_BUSY,
		BUSY,
		WRITE_BUSY
	 }SocketStatus;	//SocketStatus
	 typedef enum {
		SOCKET_ACCEPT = 0,
		SOCKET_INIT,
		SOCKET_LISTENTIMEOUT,
		SOCKET_READ,
		SOCKET_READTIMEOUT,
		SOCKET_WRITE,
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
	 int SocketLastActive_;			//上一次SocketLen_中socket活动时间
	 SocketItem* SocketItemArray_;	//存放socketItem的地方

	 struct epoll_event* EpollCheck_;	//epoll监听
	 int EpollFd_;						//epoll读写文件描述符
	 bool SocketPoolsRun_;				//连接池是否启动
	 EventCallBack EventCallBackList[SOCKET_EVENT_NUM];	//设置回调函数
	 /*超时设置 连接、读、写、最小、Epkll超时*/
	 int SocketConnTimeOut_;
	 int SocketReadTimeOut_;
	 int SocketWriteTimeOut_;
	 int SocketMinTimeOut_;
	 int EpollTimeOut_;
	 //队列
	 SocketBlockQueue<int>* ReadyQueue_;
}; //SocketPools

#endif //SOCKETTOOLS_H_
