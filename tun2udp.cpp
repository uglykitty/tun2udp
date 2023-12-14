#include <string.h>
#include <time.h>

#include <sys/ioctl.h>
#include <unistd.h> // for read
#include <fcntl.h>	// for fcntl
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if.h> // for ifreq
#include <linux/if_tun.h> // for IFF_TUN TUNSETIFF
#include <signal.h>

#include <log4cplus/version.h>
#if LOG4CPLUS_VERSION/1000/1000 >=2
#include <log4cplus/log4cplus.h>
#else
#include <log4cplus/logger.h>
#include <log4cplus/layout.h>
#include <log4cplus/loggingmacros.h>
#include <log4cplus/fileappender.h>
#endif

#include "tun2udp.h"

#define AUTHORIZED_SECRET	"Guofang"

const unsigned char htable[] = {
	0xaf,0xc0,0x28,0x7c,0x2,0xd2,0xe9,0xf0,0xf3,0x78,0x1b,0x81,0x68,0x97,0x27,0x6e,0xda,
	0x57,0xe3,0xa5,0xfb,0x58,0xee,0xbc,0x5d,0xea,0xd6,0xa8,0xf9,0xe5,0x44,0xf2,0xba,
	0x1,0xe,0x8a,0x1f,0xb8,0x86,0x82,0x66,0x61,0x29,0x65,0xa2,0x6c,0xf,0xfa,0xd9,
	0xb5,0xdb,0x90,0x2c,0xd3,0x89,0x7f,0x76,0x26,0xc,0x9,0xfc,0x62,0x98,0x9e,0xd0,
	0xae,0x46,0xe8,0x25,0xfe,0xf7,0x20,0x5,0x5a,0x45,0x91,0x4a,0x4f,0xb9,0xa9,0x8,
	0x74,0x79,0xac,0xd1,0x11,0xf6,0xa1,0x8f,0x67,0x54,0xc3,0xef,0x6f,0x15,0x84,0x9a,
	0xe6,0x13,0xec,0xf5,0x7,0x73,0xf1,0x38,0xc7,0xcf,0xb0,0x21,0xbe,0xed,0x77,0xa6,
	0x8b,0x93,0x9d,0x7d,0x32,0x3,0xc9,0xcb,0x34,0x6a,0x19,0xb1,0xbb,0x42,0x55,0x51,
	0x1a,0xb6,0x2d,0x1e,0x10,0xb4,0x14,0xcd,0x22,0xf8,0x6b,0x49,0xde,0x3e,0xc8,0x8d,
	0xa3,0x18,0x9f,0x92,0x5b,0x88,0x6d,0x43,0x37,0xfd,0x7a,0xeb,0xc2,0xaa,0xca,0x3c,
	0xa,0x64,0x8c,0x2e,0xbd,0xe7,0x71,0x23,0x75,0xb7,0x36,0x60,0x4e,0xb,0xdf,0x39,
	0x3f,0x8e,0xa4,0x72,0x63,0x87,0x48,0x40,0x94,0x35,0x24,0x30,0x2b,0x5c,0xe2,0xd5,
	0xe4,0xad,0x69,0xa7,0x9c,0xdd,0xe0,0xd,0xb2,0x1c,0x33,0x4c,0xd8,0x99,0x52,0xc6,
	0x83,0xc1,0x4b,0x59,0xcc,0x4d,0x41,0x17,0x95,0x3a,0xce,0xe1,0x85,0x31,0x2f,0x7e,
	0x96,0xb3,0x16,0x4,0xf4,0x47,0x2a,0x6,0xc5,0x80,0x5e,0x3d,0x1d,0x7b,0x3b,0x9b,
	0xa0,0xbf,0x50,0xc4,0x70,0xdc,0x12,0xd4,0x53,0x56,0x5f,0xab,0x0,0xd7,0xff,
};

client_t* client_accept(thread_t* thread, const client_t* server);
client_t* client_connect(thread_t* thread, const sockaddr* remote);

log4cplus::Logger logger;

void addAppender()
{
	// log4cplus::SharedAppenderPtr appender(new log4cplus::FileAppender(LOG4CPLUS_TEXT("tun2udp.log"), std::ios::app));
	log4cplus::SharedAppenderPtr appender(new log4cplus::DailyRollingFileAppender(LOG4CPLUS_TEXT("tun2udp.log")));

#if __cplusplus >= 201402L
	appender->setLayout(std::unique_ptr<log4cplus::Layout>
#else
	appender->setLayout(std::auto_ptr<log4cplus::Layout>
#endif
		(new log4cplus::PatternLayout(LOG4CPLUS_TEXT("%D{%Y%m%d %H:%M:%S,%Q} [%i] %p %m%n"))));

	logger.addAppender(appender);
}

void data_xor(unsigned char* buf, ssize_t bufLen)
{
	for (auto i = 0; i < bufLen; i++) {
		buf[i] ^= htable[i % sizeof(htable)];
	}
}

thread_t* thread_new()
{
	auto thread = new thread_t;
	thread->evb = event_base_new();
	//thread->epfd = epoll_create(64);
	thread->signalUsr1 = evsignal_new(thread->evb, SIGUSR1, [](evutil_socket_t fd, short event, void* ctx) {

		auto thread = static_cast<thread_t*>(ctx);
		logger.removeAllAppenders();
		addAppender();

		}, thread);
	event_add(thread->signalUsr1, NULL);
	return thread;
}

void thread_add_client(thread_t* thread, client_t* client)
{
	client->id = thread->clients.size();
	thread->clients.push_back(client);
}

void thread_remove_client(thread_t* thread, client_t* client)
{
	if (client->id < 0) {
		return;
	}
	auto clientSize = thread->clients.size();
	auto lastIndex = clientSize - 1;
	if (client->id < lastIndex) {
		thread->clients[client->id] = thread->clients[lastIndex];
		thread->clients[client->id]->id = client->id;
	}
	thread->clients.pop_back();
	thread->curClient = 0;
	client->id = -1;
}

void show_clients(thread_t* thread)
{
	//printf("show_clients\n");
	//auto i = 0;
	//for (auto& client : thread->clients) {
	//	printf("%d, %s\n", i++, client->remoteAddr);
	//}
}

void close_client(thread_t* thread, client_t* client)
{
	if (client->id > -1) {
		thread_remove_client(thread, client);
	}

	if (client->mode == CM_CLIENT) {
		client_connect(thread, &client->remote);
	}
	LOG4CPLUS_INFO(logger, LOG4CPLUS_TEXT("close ") << client->remoteAddr);
	evutil_closesocket(client->fd);
	if (client->ev_reconnect) {
		event_free(client->ev_reconnect);
	}
	event_free(client->ev);
	delete client;
}

void client_reconnect_later(thread_t* thread, client_t* client)
{
	thread_remove_client(thread, client);
	if (!client->ev_reconnect) {
		client->ev_reconnect = evtimer_new(thread->evb, [](evutil_socket_t, short, void*ctx) {
			
			auto client = static_cast<client_t*>(ctx);
			auto thread = client->thread;
			close_client(thread, client);

			}, client);
	}
	evtimer_add(client->ev_reconnect, &client->tv_reconnect);
}

ssize_t sendPkg(client_t* client, const void* buf, ssize_t recvLen)
{
	uint16_t pkgLen = htons(recvLen);

	iovec _iovec[2];
	_iovec[0].iov_base = &pkgLen;
	_iovec[0].iov_len = sizeof(pkgLen);
	_iovec[1].iov_base = const_cast<void*>(buf);
	_iovec[1].iov_len = recvLen;

	return writev(client->fd, _iovec, 2);
}

void on_client_event(evutil_socket_t fd, short what, void* ctx)
{
	auto client = static_cast<client_t*>(ctx);
	auto thread = client->thread;

	if (what & EV_TIMEOUT) {
		close_client(thread, client);
		return;
	}

	if (what & EV_READ) {
		ssize_t recvLen = 0;

		if (client == thread->tun) {
			recvLen = read(client->fd, client->buf, sizeof(client->buf));
			data_xor(client->buf, recvLen);
			auto clientSize = thread->clients.size();
			if (clientSize) {
				auto curClient = thread->clients[thread->curClient];
				LOG4CPLUS_DEBUG(logger, LOG4CPLUS_TEXT("send ") << recvLen << " bytes to index "
					<< thread->curClient << " addr " << curClient->remoteAddr);
				ssize_t sendLen = 0;
				if (client->tp == TP_UDP) {
					sendLen = send(curClient->fd, client->buf, recvLen, 0);
				}
				else {
					sendLen = sendPkg(curClient, client->buf, recvLen);
				}
				if (sendLen > 0) {
					thread->curClient++;
					if (thread->curClient == clientSize) {
						thread->curClient = 0;
					}
				}
				else {
					LOG4CPLUS_WARN(logger, LOG4CPLUS_TEXT("send to ") << client->remoteAddr << " error");

					client_reconnect_later(thread, curClient);
					return;
				}
			}
		}
		else {
			if (client == thread->listen) {
				if (client->tp == TP_UDP) {
					socklen_t addrLen = sizeof(client->remote);
					recvLen = recvfrom(client->fd, client->buf, sizeof(client->buf), 0, &client->remote, &addrLen);
					auto newClient = client_accept(thread, client);
					LOG4CPLUS_DEBUG(logger, LOG4CPLUS_TEXT("") << recvLen << " bytes received from addr %s"
						<< newClient->remoteAddr);
				}
				else {
					client_accept(thread, client);
					recvLen = 0;
				}
			}
			else {
				recvLen = recv(client->fd, client->buf + client->bufLen, sizeof(client->buf) - client->bufLen, 0);
				if (recvLen <= 0) {
					LOG4CPLUS_WARN(logger, LOG4CPLUS_TEXT("recv failed with ") << recvLen << " from addr " << client->remoteAddr);
					if (client->mode == CM_CLIENT) {
						client_reconnect_later(thread, client);
					}
					else {
						close_client(thread, client);
					}
					return;
				}
				client->bufLen += recvLen;
				LOG4CPLUS_DEBUG(logger, LOG4CPLUS_TEXT("") << recvLen << " bytes received from addr %s" << client->remoteAddr);
			}

			if (recvLen) {
				if (client->tp == TP_UDP) {
					data_xor(client->buf, recvLen);
					write(thread->tun->fd, client->buf, recvLen);
				}
				else {
					auto hdrLen = sizeof(uint16_t);
					while (true) {
						if (client->bufLen < hdrLen) {
							break;
						}
						auto offset = 0;
						auto dataLen = ntohs(*(uint16_t*)(client->buf + offset)); offset += hdrLen;
						auto pkgLen = hdrLen + dataLen;
						if (client->bufLen < pkgLen) {
							break;
						}
						auto data = client->buf + offset;
						data_xor(data, dataLen);

						if (client->authorized) {
							write(thread->tun->fd, data, dataLen);
						}
						else {
							if (strlen(AUTHORIZED_SECRET) == dataLen
								&& 0 == memcmp(AUTHORIZED_SECRET, data, dataLen)) {
								LOG4CPLUS_INFO(logger, LOG4CPLUS_TEXT("authorized from ") << client->remoteAddr);
								client->authorized = 1;
								thread_add_client(thread, client);
								if (client->mode == CM_SERVER) {
									data_xor(data, dataLen);
									sendPkg(client, data, dataLen);
								}
							}
						}

						if (pkgLen == client->bufLen) {
							client->bufLen = 0;
							break;
						}
						memmove(client->buf, client->buf + pkgLen, client->bufLen - pkgLen);
						client->bufLen -= pkgLen;
					}
				}
			}
		}
	}

	if (what & EV_WRITE) {
		//thread_add_client(thread, client);
		client->ev->ev_events &= ~EV_WRITE;
		LOG4CPLUS_INFO(logger, LOG4CPLUS_TEXT("connected to ") << client->remoteAddr);
		auto secretLen = strnlen(AUTHORIZED_SECRET, 100);
		auto secret = malloc(secretLen);
		memcpy(secret, AUTHORIZED_SECRET, secretLen);
		data_xor(reinterpret_cast<unsigned char*>(secret), secretLen);
		sendPkg(client, secret, secretLen);
		free(secret);
	}

	if (client->tv_timeout.tv_sec || client->tv_timeout.tv_usec) {
		event_add(client->ev, &client->tv_timeout);
	}
	else {
		event_add(client->ev, NULL);
	}
}

client_t* client_new_tun(thread_t* thread, const char* tun)
{
	auto client = new client_t;
	client->fd = open("/dev/net/tun", O_RDWR);
	if (client->fd == -1) {
		perror("open");
		delete client;
		return NULL;
	}
	struct ifreq ifr = { 0 };
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	strcpy(ifr.ifr_name, tun);
	auto result = ioctl(client->fd, TUNSETIFF, (void*)&ifr);
	if (result == -1) {
		perror("ioctl");
		close(client->fd);
		delete client;
		return NULL;
	}

	client->thread = thread;
	thread->tun = client;

	client->ev = event_new(thread->evb, client->fd, EV_READ, on_client_event, client);
	event_add(client->ev, NULL);
	return client;
}

client_t* client_listen(thread_t* thread, uint16_t port)
{
	auto client = new client_t;

	if (TP_TCP == client->tp) {
		client->fd = socket(AF_INET, SOCK_STREAM, 0);
	}
	else { // udp
		client->fd = socket(AF_INET, SOCK_DGRAM, 0);
	}
	evutil_make_listen_socket_reuseable(client->fd);
	auto local = reinterpret_cast<sockaddr_in*>(&client->local);
	local->sin_family = AF_INET;
	local->sin_port = htons(port);
	auto result = bind(client->fd, &client->local, sizeof(client->local));
	if (result) {
		perror("bind");
	}

	if (TP_TCP == client->tp) {
		result = listen(client->fd, 0);
	}

	client->thread = thread;
	thread->listen = client;

	client->ev = event_new(thread->evb, client->fd, EV_READ, on_client_event, client);
	event_add(client->ev, NULL);

	LOG4CPLUS_INFO(logger, LOG4CPLUS_TEXT("server listening on 0.0.0.0:") << port);
	return client;
}

client_t* client_connect(thread_t* thread, const sockaddr* remote)
{
	auto remote_in = reinterpret_cast<const sockaddr_in*>(remote);
	char addr[100] = { 0 };
	evutil_inet_ntop(remote_in->sin_family, &remote_in->sin_addr, addr, sizeof(addr) - 1);

	auto client = new client_t;
	client->thread = thread;
	snprintf(client->remoteAddr, sizeof(client->remoteAddr) - 1, "%s:%u", addr, ntohs(remote_in->sin_port));
	client->remote = *remote;
	if (client->tp == TP_UDP) {
		client->fd = socket(AF_INET, SOCK_DGRAM, 0);
	}
	else {
		client->fd = socket(AF_INET, SOCK_STREAM, 0);
		auto flags = fcntl(client->fd, F_GETFL, 0);
		flags = fcntl(client->fd, F_GETFL, 0);
		fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);
	}
	auto result = connect(client->fd, &client->remote, sizeof(client->remote));

	short what = EV_READ;

	if (result == 0) {
		//thread_add_client(thread, client);
	}
	else if (errno == EINPROGRESS) {
		what |= EV_WRITE;
	}
	else {
		evutil_closesocket(client->fd);
		delete client;
		return NULL;
	}

	client->ev = event_new(thread->evb, client->fd, what, on_client_event, client);
	event_add(client->ev, NULL);
	return client;
}

client_t* client_connect(thread_t* thread, const char* host, const char* port)
{
	addrinfo hint = { 0 };
	hint.ai_protocol = IPPROTO_IP;
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_STREAM;

	addrinfo* info = NULL;
	auto result = getaddrinfo(host, port, &hint, &info);

	if (result) {
		LOG4CPLUS_WARN(logger, LOG4CPLUS_TEXT("get addr ") << host << ":" << port << " failed");
		return NULL;
	}
	return client_connect(thread, info->ai_addr);
}

client_t* client_accept(thread_t* thread, const client_t* server)
{
	auto client = new client_t;
	client->mode = CM_SERVER;

	if (client->tp == TP_UDP) {
		client->fd = socket(server->local.sa_family, SOCK_DGRAM, 0);
		evutil_make_listen_socket_reuseable(client->fd);

		client->local = server->local;
		auto result = bind(client->fd, &client->local, sizeof(client->local));
		if (result) {
			perror("bind");
		}
		client->remote = server->remote;
		result = connect(client->fd, &client->remote, sizeof(client->remote));
	}
	else {
		socklen_t addrLen = sizeof(client->remote);
		client->fd = accept(server->fd, &client->remote, &addrLen);
	}

	client->thread = thread;

	//thread_add_client(thread, client);

	auto remote = reinterpret_cast<sockaddr_in*>(&client->remote);
	char addr[100] = { 0 };
	evutil_inet_ntop(remote->sin_family, &remote->sin_addr, addr, sizeof(addr) - 1);
	snprintf(client->remoteAddr, sizeof(client->remoteAddr) - 1, "%s:%u", addr, ntohs(remote->sin_port));
	LOG4CPLUS_INFO(logger, LOG4CPLUS_TEXT("a new fd ") << client->fd << " from " << client->remoteAddr);

	client->ev = event_new(thread->evb, client->fd, EV_READ | EV_TIMEOUT, on_client_event, client);
	client->tv_timeout.tv_sec = 60;
	event_add(client->ev, &client->tv_timeout);

	show_clients(thread);
	return client;
}

void thread_dispatch(thread_t* thread)
{
	event_base_dispatch(thread->evb);
	event_base_free(thread->evb);
}

int main(int argc, char* argv[])
{
	auto sighander = [](int signo) {
		exit(0);
		};

	signal(SIGINT, sighander);
	signal(SIGKILL, sighander);

	if (argc < 3) {
		printf("argv error\n");
		return 1;
	}

#if LOG4CPLUS_VERSION/1000/1000 >=2
	log4cplus::Initializer initializer;
#endif

	logger = log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("main"));
	logger.setLogLevel(log4cplus::INFO_LOG_LEVEL);
	addAppender();

	auto tun = argv[1]; // tun
	auto port = argv[2]; // port

	auto thread = thread_new();
	if (NULL == client_new_tun(thread, tun)) {
		return 1;
	}

	if (argc < 4) {
		client_listen(thread, atoi(port));
	}
	else {
		for (auto i = 3; i < argc; i++) {
			client_connect(thread, argv[i], port);
		}
	}
	thread_dispatch(thread);

	return 0;
}


static void generate_htable()
{
	srand(time(NULL));
	std::vector<int> s;
	for (auto i = 0; i <= 0xff; i++) {
		s.push_back(i);
	}
	std::vector<int> d;
	for (auto i = 0; i <= 0xff; i++) {
		auto ss = s.size();
		auto si = rand() % ss;
		auto sli = ss - 1;
		d.push_back(s[si]);
		if (si < sli) {
			s[si] = s[sli];
		}
		s.pop_back();
	}

	printf("const unsigned char htable[] = {\n");
	for (auto i = 0; i <= 0xff; i++) {
		printf("0x%x,", d[i]);
		if (i > 0 && i % 16 == 0) {
			printf("\n");
		}
	}
	printf("\n};\n");
}
