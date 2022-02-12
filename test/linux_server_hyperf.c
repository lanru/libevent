#include <sys/signal.h>
#include <event.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include "event2/listener.h"


void
signal_cb(int fd, short event, void *argc)
{
	struct event_base *base = (struct event_base *)argc;
	struct timeval delay = {2, 0};
	printf("Caught an interrupt signal; exiting cleanly in two seconds...\n");
	event_base_loopexit(base, &delay);
}

void
timeout_cb(int fd, short event, void *argc)
{
	printf("timeout\n");
}


void
test()
{
	struct event_base *base = event_init();

	struct event *signal_event = evsignal_new(base, SIGINT, signal_cb, base);
	event_add(signal_event, NULL);

	struct timeval tv = {1, 0};
	struct event *timeout_event = evtimer_new(base, timeout_cb, NULL);
	event_add(timeout_event, &tv);

	event_base_dispatch(base);

	event_free(timeout_event);
	event_free(signal_event);
	event_base_free(base);
}

int
test1()
{
	puts("init a event_base!");
	struct event_base *base; //定义一个event_base
	base = event_base_new(); //初始化一个event_base
	const char *x = event_base_get_method(
		base); //查看用了哪个IO多路复用模型，linux一下用epoll
	printf("METHOD:%s\n", x);
	int y = event_base_dispatch(
		base); //事件循环。因为我们这边没有注册事件，所以会直接退出
	event_base_free(base); //销毁libevent
	return 1;
}


//读取客户端
void
do_read(evutil_socket_t fd, short event, void *arg)
{
	//继续等待接收数据
	char buf[1024]; //数据传送的缓冲区
	int len;
	if ((len = recv(fd, buf, 1024, 0)) > 0) {
		buf[len] = '\0';
		printf("%s\n", buf);
		if (send(fd, buf, len, 0) < 0) { //将接受到的数据写回客户端
			perror("write");
		}
	}
}


//回调函数，用于监听连接进来的客户端socket
void
do_accept(evutil_socket_t fd, short event, void *arg)
{
	int client_socketfd;			//客户端套接字
	struct sockaddr_in client_addr; //客户端网络地址结构体
	int in_size = sizeof(struct sockaddr_in);
	//客户端socket
	client_socketfd = accept(fd, (struct sockaddr *)&client_addr,
		&in_size); //等待接受请求，这边是阻塞式的
	if (client_socketfd < 0) {
		puts("accpet error");
		exit(1);
	}

	//类型转换
	struct event_base *base_ev = (struct event_base *)arg;

	// socket发送欢迎信息
	char *msg = "Welcome to My socket";
	int size = send(client_socketfd, msg, strlen(msg), 0);

	//创建一个事件，这个事件主要用于监听和读取客户端传递过来的数据
	//持久类型，并且将base_ev传递到do_read回调函数中去
	struct event *ev;
	ev = event_new(base_ev, client_socketfd, EV_TIMEOUT | EV_READ | EV_PERSIST,
		do_read, base_ev);
	event_add(ev, NULL);
}

int
test_socket()
{
	int server_socketfd;			//服务端socket
	struct sockaddr_in server_addr; //服务器网络地址结构体
	memset(&server_addr, 0, sizeof(server_addr)); //数据初始化--清零
	server_addr.sin_family = AF_INET;			  //设置为IP通信
	server_addr.sin_addr.s_addr =
		INADDR_ANY; //服务器IP地址--允许连接到所有本地地址上
	server_addr.sin_port = htons(8001); //服务器端口号

	//创建服务端套接字
	server_socketfd = socket(PF_INET, SOCK_STREAM, 0);
	if (server_socketfd < 0) {
		puts("socket error");
		return 0;
	}

	evutil_make_listen_socket_reuseable(server_socketfd); //设置端口重用
	evutil_make_socket_nonblocking(server_socketfd);	  //设置无阻赛

	//绑定IP
	if (bind(server_socketfd, (struct sockaddr *)&server_addr,
			sizeof(struct sockaddr)) < 0) {
		puts("bind error");
		return 0;
	}

	//监听,监听队列长度 5
	listen(server_socketfd, 10);

	//创建event_base 事件的集合，多线程的话 每个线程都要初始化一个event_base
	struct event_base *base_ev;
	base_ev = event_base_new();
	const char *x =
		event_base_get_method(base_ev); //获取IO多路复用的模型，linux一般为epoll
	printf("METHOD:%s\n", x);

	//创建一个事件，类型为持久性EV_PERSIST，回调函数为do_accept（主要用于监听连接进来的客户端）
	//将base_ev传递到do_accept中的arg参数
	struct event *ev;
	ev = event_new(base_ev, server_socketfd, EV_TIMEOUT | EV_READ | EV_PERSIST,
		do_accept, base_ev);

	//注册事件，使事件处于 pending的等待状态
	event_add(ev, NULL);

	//事件循环
	event_base_dispatch(base_ev);

	//销毁event_base
	event_base_free(base_ev);
	return 1;
}


#define MAX_LINE    256

void read_cb(struct bufferevent *bev, void *arg) {
//	struct evbuffer *buf = (struct evbuffer *)arg;
//	char line[MAX_LINE+1];
//	int n;
//	evutil_socket_t fd = bufferevent_getfd(bev);
//	while (n = bufferevent_read(bev, line, MAX_LINE), n > 0) {
//		line[n] = '\0';
//
//		//将读取到的内容放进缓冲区
//		evbuffer_add(buf, line, n);
//
//		//搜索匹配缓冲区中是否有==，==号来分隔每次客户端的请求
//		const char *x = "==";
//		struct evbuffer_ptr ptr = evbuffer_search(buf, x, strlen(x), 0);
//		if (ptr.pos != -1) {
//			bufferevent_write_buffer(bev, buf); //使用buffer的方式输出结果
//		}
//	}

	/* This callback is invoked when there is data to read on bev. */
	struct evbuffer *input = bufferevent_get_input(bev);
	struct evbuffer *output = bufferevent_get_output(bev);

	/* Copy all the data from the input buffer to the output buffer. */
	evbuffer_add_buffer(output, input);
}
void write_cb(struct bufferevent *bev, void *arg) {}
void error_cb(struct bufferevent *bev, short event, void *arg) {
	evutil_socket_t fd = bufferevent_getfd(bev);
	printf("fd = %u, ", fd);
	if (event & BEV_EVENT_TIMEOUT) {
		printf("Timed out\n");
	} else if (event & BEV_EVENT_EOF) {
		printf("connection closed\n");
	} else if (event & BEV_EVENT_ERROR) {
		printf("some other error\n");
	}
	//清空缓冲区
	struct evbuffer *buf = (struct evbuffer *)arg;
	evbuffer_free(buf);
	bufferevent_free(bev);
}

//回调函数，用于监听连接进来的客户端socket
void do_accept_buffer(evutil_socket_t fd, short event, void *arg) {
	int client_socketfd;//客户端套接字
	struct sockaddr_in client_addr; //客户端网络地址结构体
	int in_size = sizeof(struct sockaddr_in);
	//客户端socket
	client_socketfd = accept(fd, (struct sockaddr *) &client_addr, &in_size); //等待接受请求，这边是阻塞式的
	if (client_socketfd < 0) {
		puts("accpet error");
		exit(1);
	}

	//类型转换
	struct event_base *base_ev = (struct event_base *) arg;

	//socket发送欢迎信息
	char * msg = "Welcome to My socket";
	int size = send(client_socketfd, msg, strlen(msg), 0);

	//创建一个事件，这个事件主要用于监听和读取客户端传递过来的数据
	//持久类型，并且将base_ev传递到do_read回调函数中去
	//struct event *ev;
	//ev = event_new(base_ev, client_socketfd, EV_TIMEOUT|EV_READ|EV_PERSIST, do_read, base_ev);
	//event_add(ev, NULL);

	//创建一个evbuffer，用来缓冲客户端传递过来的数据
	struct evbuffer *buf = evbuffer_new();
	//创建一个bufferevent
	struct bufferevent *bev = bufferevent_socket_new(base_ev, client_socketfd, BEV_OPT_CLOSE_ON_FREE);
	//设置读取方法和error时候的方法，将buf缓冲区当参数传递
	bufferevent_setcb(bev, read_cb, NULL, error_cb, buf);
	//设置类型
	bufferevent_enable(bev, EV_READ|EV_WRITE|EV_PERSIST);
	//设置水位
	bufferevent_setwatermark(bev, EV_READ, 0, 0);
}

int test_ev_buffer(){

	int server_socketfd; //服务端socket
	struct sockaddr_in server_addr;   //服务器网络地址结构体
	memset(&server_addr,0,sizeof(server_addr)); //数据初始化--清零
	server_addr.sin_family = AF_INET; //设置为IP通信
	server_addr.sin_addr.s_addr = INADDR_ANY;//服务器IP地址--允许连接到所有本地地址上
	server_addr.sin_port = htons(8001); //服务器端口号

	//创建服务端套接字
	server_socketfd = socket(PF_INET,SOCK_STREAM,0);
	if (server_socketfd < 0) {
		puts("socket error");
		return 0;
	}

	evutil_make_listen_socket_reuseable(server_socketfd); //设置端口重用
	evutil_make_socket_nonblocking(server_socketfd); //设置无阻赛

	//绑定IP
	if (bind(server_socketfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr))<0) {
		puts("bind error");
		return 0;
	}

	//监听,监听队列长度 5
	listen(server_socketfd, 10);

	//创建event_base 事件的集合，多线程的话 每个线程都要初始化一个event_base
	struct event_base *base_ev;
	base_ev = event_base_new();
	const char *x =  event_base_get_method(base_ev); //获取IO多路复用的模型，linux一般为epoll
	printf("METHOD:%s\n", x);

	//创建一个事件，类型为持久性EV_PERSIST，回调函数为do_accept（主要用于监听连接进来的客户端）
	//将base_ev传递到do_accept中的arg参数
	struct event *ev;
	ev = event_new(base_ev, server_socketfd, EV_TIMEOUT|EV_READ|EV_PERSIST, do_accept_buffer, base_ev);

	//注册事件，使事件处于 pending的等待状态
	event_add(ev, NULL);

	//事件循环
	event_base_dispatch(base_ev);

	//销毁event_base
	event_base_free(base_ev);
	return 1;
}

int
main()
{
	test_ev_buffer();
}
