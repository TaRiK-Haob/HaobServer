#ifndef WEBSERVER_H
#define WEBSERVER_H
#include "webserver.h"
#endif // WEBSERVER_H

Webserver::Webserver(){}

Webserver::~Webserver(){}

bool Webserver::init(const char *ip, int port)
{
    // 创建套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket creation failed");
        return false;
    }

    // 设置为非阻塞模式（可选）
    set_nonblocking(listen_fd);

    // 设置地址复用选项
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    server_addr.sin_port = htons(port);
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        perror("bind failed");
        close(listen_fd);
        return false;
    }

    // 监听端口
    listen(listen_fd, 10);

    // 创建epoll实例
    Webserver::epfd = epoll_create1(0);
    // ===============给 static 变量赋值 ==================
    // 保证创建的connection实例可以访问epfd
    Connection::epfd = Webserver::epfd;
    // ================================================
    if (epfd == -1)
    {
        perror("epoll_create1 failed");
        close(listen_fd);
        return false;
    }

    // 注册监听事件 监听的类型为可读事件 监听的文件描述符为listen_fd
    Connection *conn_listen = new Connection;
    conn_listen->fd = listen_fd;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = conn_listen;

    // 将监听套接字添加到epoll实例
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1)
    {
        perror("epoll_ctl failed");
        close(listen_fd);
        close(epfd);
        return false;
    }

    // 创建线程池
    // ThreadPool<Connection> thread_pool(4);

    std::cout << "Server listening on port 8080..." << std::endl;

    return true;
}

void Webserver::loop()
{
    // 接受连接并处理请求
    while (1)
    {
        // 阻塞等待事件发生
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            perror("epoll_wait failed");
            break;
        }

        // 处理就绪的事件
        for (int n = 0; n < nfds; ++n)
        {
            Connection *ptr = static_cast<Connection *>(events[n].data.ptr);

            if (ptr->fd == listen_fd)
            {
                while (true)
                {
                    // 有新的连接请求
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);

                    int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

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
                    set_nonblocking(conn_fd);

                    // 将连接套接字添加到epoll实例中
                    struct epoll_event client_ev;
                    client_ev.events = EPOLLIN; // 监听可读事件
                    client_ev.data.ptr = conn;  // 存储连接结构体指针

                    // 添加到epoll
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &client_ev) == -1)
                    {
                        perror("epoll_ctl client_fd failed");
                        close(conn_fd);
                        delete conn;
                        continue;
                    }
                    std::cout << "Accepted new connection, fd: " << conn_fd << std::endl;
                }
            }
            else
            {
                // 处理客户端数据（读事件）
                if (events[n].events & EPOLLIN)
                {
                    // 创建事件，并加入线程池
                    // Task<Connection> *task = new Task<Connection>(ptr, READ);
                    // thread_pool.enqueue(task);

                    bool ret = ptr->handle_client_data();

                    // 连接关闭或错误
                    if (ret)
                    {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, ptr->fd, nullptr);
                        close(ptr->fd);
                        delete ptr;
                        // std::cout << "Connection closed" << std::endl;
                    }
                }
                else if (events[n].events & EPOLLOUT)
                {
                    // // 处理客户端数据（写事件）
                    // // 创建事件，并加入线程池
                    // Task<Connection> *task = new Task<Connection>(ptr, WRITE);
                    // thread_pool.enqueue(task);

                    // 没写完的再尝试写
                    bool ret = ptr->handle_write();

                    // 发送完成或错误
                    if (ret)
                    {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, ptr->fd, nullptr);
                        close(ptr->fd);
                        delete ptr;
                    }
                }
            }
        }
    }

    // 清理资源 关闭epoll实例和监听套接字
    close(epfd);
    close(listen_fd);
}

void Webserver::set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Webserver::close_connection(Connection *conn)
{
    close(conn->fd);
    delete conn;
}