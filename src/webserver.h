#ifndef WEBSERVER_H
#define WEBSERVER_H

#define MAX_EVENTS 1024

#include <sys/epoll.h>
#include <mutex>
#include <queue>
#include <cstdint>
#include "connection.h"
#include "thread_pool.h"

class ThreadPool;
class Connection;

class Webserver
{
public:
    Webserver();
    ~Webserver();

    bool init(const char *ip, int port, int thread_pool_size);
    void loop();
    void push_notify(Connection *conn);

private:
    void _set_nonblocking(int fd);
    void _close_connection(Connection *conn);
    void _handle_connection();
    void _handle_notify(uint64_t value);

    epoll_event events[MAX_EVENTS];

    ThreadPool thread_pool = ThreadPool(this, 5);
    Connection _conn_listen = Connection();
    Connection _notify_conn = Connection();

    int _listen_fd = -1;
    int _epfd = -1;
    int _notify_fd = -1;

    std::queue<Connection *> notify_queue;
    std::mutex notify_mutex;
};

#endif // WEBSERVER_H