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
#include <sys/eventfd.h>

#include "connection.h"
#include "thread_pool.h"

#define MAX_EVENTS 1024

class Webserver{
public:
    Webserver();
    ~Webserver();

    bool init(const char * ip, int port);
    void loop();
    void push_notify(Connection* conn);

private:
    void _set_nonblocking(int fd);
    void _close_connection(Connection *conn);
    void _handle_connection();
    void _handle_notify(uint64_t value);

    epoll_event events[MAX_EVENTS];

    ThreadPool *thread_pool = nullptr;

    int _listen_fd = -1;
    int _epfd = -1;
    int _notify_fd = -1;

    std::queue<Connection*> notify_queue;
    std::mutex notify_mutex;
};