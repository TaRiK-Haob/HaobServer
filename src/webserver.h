#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <iostream>

#include "connection.h"
#include "thread_pool.h"

#define MAX_EVENTS 1024

class Webserver{
public:
    Webserver();
    ~Webserver();

    bool init(const char * ip, int port);
    void loop();

private:
    void set_nonblocking(int fd);
    void close_connection(Connection *conn);
    struct epoll_event events[MAX_EVENTS];
    int listen_fd = -1;
    int epfd = -1;
};