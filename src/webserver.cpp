#include "webserver.h"
#include "connection.h"
#include "thread_pool.h"

#include <iostream>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>

Webserver::Webserver() {}

Webserver::~Webserver()
{
    // 清理资源 关闭epoll实例和监听套接字
    close(_epfd);
    close(_listen_fd);
    close(_notify_fd);
}

bool Webserver::init(const char *ip, int port, int thread_pool_size)
{
    // 创建套接字
    _listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listen_fd == -1)
    {
        perror("socket creation failed");
        return false;
    }

    // 设置为非阻塞模式（可选）
    _set_nonblocking(_listen_fd);

    // 设置地址复用选项
    int opt = 1;
    setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // 绑定地址和端口
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    server_addr.sin_port = htons(port);
    if (bind(_listen_fd, (sockaddr *)&server_addr, sizeof(server_addr)))
    {
        perror("bind failed");
        close(_listen_fd);
        return false;
    }

    // 监听端口
    listen(_listen_fd, 10);

    // 创建epoll实例
    Webserver::_epfd = epoll_create1(0);
    // ===============给 static 变量赋值 ==================
    // 保证创建的connection实例可以访问epfd
    Connection::epfd = Webserver::_epfd;
    // ================================================
    if (_epfd == -1)
    {
        perror("epoll_create1 failed");
        close(_listen_fd);
        return false;
    }

    // 注册监听事件 监听的类型为可读事件 监听的文件描述符为listen_fd

    _conn_listen.fd = _listen_fd;
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = &_conn_listen;
    
    // 将监听套接字添加到epoll实例
    if (epoll_ctl(_epfd, EPOLL_CTL_ADD, _listen_fd, &ev) == -1)
    {
        perror("epoll_ctl failed");
        close(_listen_fd);
        close(_epfd);
        return false;
    }

    // 初始化线程池
    // thread_pool = ThreadPool(this, thread_pool_size); // 创建一个包含5个线程的线程池

    _notify_fd = eventfd(0, EFD_NONBLOCK);

    _notify_conn.fd = _notify_fd;
    epoll_event notify_ev;

    notify_ev.events = EPOLLIN;
    notify_ev.data.ptr = &_notify_conn;
    epoll_ctl(_epfd, EPOLL_CTL_ADD, _notify_fd, &notify_ev);

    std::cout << "Server listening on port 8080..." << std::endl;

    return true;
}

void Webserver::loop()
{
    // 接受连接并处理请求
    while (1)
    {
        // 阻塞等待事件发生
        int nfds = epoll_wait(_epfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            perror("epoll_wait failed");
            break;
        }

        // 处理就绪的事件
        for (int n = 0; n < nfds; ++n)
        {
            Connection *ptr = static_cast<Connection *>(events[n].data.ptr);

            if (ptr->fd == _listen_fd)
            {
                // 处理新的连接请求
                _handle_connection();
            }
            else if (ptr->fd == _notify_fd)
            {
                // std::cout << "Handling notify event from thread pool." << std::endl;
                // 处理来自线程池的通知
                // 读取事件fd的值 清空事件
                uint64_t value;
                read(_notify_fd, &value, sizeof(uint64_t));
                // 处理通知事件
                _handle_notify(value);
            }
            else
            {
                // 处理客户端数据（读事件）
                if (events[n].events & EPOLLIN)
                {
                    // 将任务添加到线程池 相当于执行了 ptr->handle_client_data();
                    thread_pool.enqueue(ptr);
                }
                else if (events[n].events & EPOLLOUT)
                {
                    std::cout << "Handling EPOLLOUT event for fd: " << ptr->fd << std::endl;
                    thread_pool.enqueue(ptr);
                }
            }
        }
    }
}

void Webserver::push_notify(Connection* conn)
{
    {
        std::lock_guard<std::mutex> lock(notify_mutex);
        notify_queue.push(conn);
    }
    uint64_t one = 1;
    write(_notify_fd, &one, sizeof(uint64_t));
}

void Webserver::_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Webserver::_close_connection(Connection *conn)
{
    // std::cout << "Closing connection, fd: " << conn->fd << std::endl;
    epoll_ctl(_epfd, EPOLL_CTL_DEL, conn->fd, nullptr);
    close(conn->fd);
    delete conn;
}

void Webserver::_handle_connection()
{
    while (1)
    {
        // 有新的连接请求
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept(_listen_fd, (sockaddr *)&client_addr, &client_len);

        if (conn_fd < 0)
        {
            if (errno == EAGAIN && errno == EWOULDBLOCK)
                break;
            else
            {
                perror("accept failed");
                break;
            }
        }

        Connection *conn = new Connection;
        conn->fd = conn_fd;
        conn->read_offset = 0;
        conn->write_offset = 0;
        conn->client_addr = client_addr;
        conn->client_len = client_len;

        // 设置连接套接字为非阻塞模式
        _set_nonblocking(conn_fd);

        // 将连接套接字添加到epoll实例中
        epoll_event client_ev;
        client_ev.events = EPOLLIN | EPOLLONESHOT; // 监听可读事件
        client_ev.data.ptr = conn;  // 存储连接结构体指针

        // 添加到epoll
        if (epoll_ctl(_epfd, EPOLL_CTL_ADD, conn_fd, &client_ev) == -1)
        {
            perror("epoll_ctl client_fd failed");
            close(conn_fd);
            delete conn;
            continue;
        }
        // std::cout << "Accepted new connection, fd: " << conn_fd << std::endl;
    }
}

// TODO: 完成处理来自线程池的通知
void Webserver::_handle_notify(uint64_t value)
{
    // 处理来自线程池的通知 value表示有多少连接需要处理
    for(int i = 0; i < value; ++i)
    {
        Connection *conn;
        {
            std::lock_guard<std::mutex> lock(notify_mutex);
            conn = notify_queue.front();
            notify_queue.pop();
        }

        // std::cout << "Processing notify for connection fd: " << conn->fd << ", state: " << conn->state << std::endl;

        if(conn->state == WRITE){
            // 修改epoll事件为可写
            epoll_event ev;
            ev.events = EPOLLOUT | EPOLLONESHOT;
            ev.data.ptr = conn;
            epoll_ctl(_epfd, EPOLL_CTL_MOD, conn->fd, &ev);
        }else if(conn->state == READ){
            // 修改epoll事件为可读
            epoll_event ev;
            ev.events = EPOLLIN | EPOLLONESHOT;
            ev.data.ptr = conn;
            epoll_ctl(_epfd, EPOLL_CTL_MOD, conn->fd, &ev);
        }
        else{
            // 目前只处理WRITE\READ状态 其他状态直接关闭连接
            _close_connection(conn);
        }
    }
}

