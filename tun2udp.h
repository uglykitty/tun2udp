#include <stdlib.h>
//#include <netinet/in.h> // for sockaddr_in
//#include <sys/epoll.h>
#include <event.h>
#include <vector>

struct thread_t;

enum client_mode_t {
	CM_CLIENT,
	CM_SERVER,
};

enum transport_protocol_t {
	TP_UDP,
	TP_TCP,
};

struct client_t {
	thread_t* thread = NULL;
	int id = -1;
	event* ev = NULL;
	timeval tv_timeout = { 0 };
	evutil_socket_t fd = -1;
	//transport_protocol_t tp = TP_UDP;
	transport_protocol_t tp = TP_TCP;
	//epoll_event epEvent = { 0 };
	unsigned char buf[2500];
	ssize_t bufLen = 0;
	sockaddr local = { 0 };
	sockaddr remote = { 0 };
	char remoteAddr[100] = { 0 };
	client_mode_t mode = CM_CLIENT;
	//time_t aliveTime = 0;

	event* ev_reconnect = NULL;
	timeval tv_reconnect = { 1, 0 };

	int authorized = 0;
};

struct thread_t {
	event_base* evb = NULL;

	//int epfd = -1;
	client_t* tun = NULL;
	client_t* listen = NULL;
	std::vector<client_t*> clients;
	int curClient = 0;
	//time_t checkAliveTime;
	event* signalUsr1 = NULL;
};
