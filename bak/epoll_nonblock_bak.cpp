#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <sys/epoll.h>
#include <vector>
#include <algorithm>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
static int epfd = -1;

// TODO：处理 send的问题 线程池

struct Connection
{
    int fd;
    std::vector<char> read_buffer;
    std::vector<char> write_buffer;
    size_t write_offset;
    size_t read_offset;

    sockaddr_in client_addr;
    socklen_t client_len;
};

void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void close_connection(Connection *conn)
{
    close(conn->fd);
    delete conn;
}

bool handle_write(Connection *conn)
{
    // std::cout << "Handle write event on fd: " << conn->fd << std::endl;
    // 立即尝试写，写到 EAGAIN 或写完
    while (conn->write_offset < conn->write_buffer.size())
    {
        ssize_t wn = send(conn->fd,
                          conn->write_buffer.data() + conn->write_offset,
                          conn->write_buffer.size() - conn->write_offset, 0);
        if (wn > 0)
        {
            // 写了 wn 字节
            // std::cout << "Sent " << wn << " bytes on fd: " << conn->fd << std::endl;
            conn->write_offset += wn;
            continue;
        }
        else if (wn < 0)
        {
            // 写到非阻塞边界，需要等epoll再次触发可写事件
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // std::cout << "Send would block, need to wait for EPOLLOUT: " << conn->fd << std::endl;
                // 写不完，注册 EPOLLOUT，稍后继续写
                epoll_event ev;
                ev.events = EPOLLOUT;
                ev.data.ptr = conn;

                epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev);
                return false;
            }
            else
            {
                // 其他错误，关闭连接
                // std::cout << "Send error: " << conn->fd << std::endl;
                return true;
            }
        }
    }
    // std::cout << "Send complete on fd: " << conn->fd << std::endl;
    return true; // 写完
}

bool handle_client_data(Connection *conn)
{
    // 读取数据
    // 注意这里：循环读取，直到读到非阻塞边界
    // EAGAIN不代表读取结束，只是socket_buffer当前没有数据可读，需要等epoll再次触发可读事件
    // 因此进入send阶段需要考虑读缓冲区的数据的完整性

    bool closed = false;

    // std::cout << "Handle read event on fd: " << conn->fd << std::endl;
    while (true)
    {
        char buffer[BUFFER_SIZE];

        ssize_t n = recv(conn->fd, buffer, BUFFER_SIZE, 0);

        if (n > 0)
        {
            conn->read_buffer.insert(conn->read_buffer.end(), buffer, buffer + n);
            conn->read_offset += n;
            continue;
        }
        else if (n == 0)
        {
            // 客户端关闭连接
            std::cout << "Client closed connection: " << conn->fd << std::endl;
            closed = true;
            break;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 读到非阻塞边界，后续由epoll重新唤醒
            }
            else
            {
                // 其他错误
                closed = true;
                std::cout << "Unkonwn error: " << conn->fd << std::endl;
                break;
            }
        }
    }

    // 处理HTTP请求 做简单的解析 判断请求完整性
    bool request_complete = false;
    if (conn->read_offset >= 4)
    {
        // 简单查找方法
        auto it = std::search(conn->read_buffer.begin(), conn->read_buffer.end(),
                              "\r\n\r\n", "\r\n\r\n" + 4);
        if (it != conn->read_buffer.end())
        {
            request_complete = true;
        }
    }

    if (!request_complete)
    {
        // 请求不完整，继续等待更多数据
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = conn;
        epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev);
        return false;
    }

    // std::cout << "Received complete HTTP request on fd: " << conn->fd << std::endl;
    // 构造HTTP响应
    std::string body = "Hello World!\nYour IP is:" +
                       std::string(inet_ntoa(conn->client_addr.sin_addr)) + "\n";
    std::string header = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Connection: close\r\n"
                         "Content-Length: " +
                         std::to_string(body.size()) + "\r\n"
                                                       "\r\n";

    // 准备发送数据
    conn->write_buffer.insert(conn->write_buffer.end(), header.begin(), header.end());
    conn->write_buffer.insert(conn->write_buffer.end(), body.begin(), body.end());
    conn->write_offset = 0;

    // 直接尝试写响应
    bool done = handle_write(conn);

    // 直接写完了就返回true关闭连接
    return done; // true表示连接关闭，false表示继续保持连接
}

int main()
{
    // 创建套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket creation failed");
        return 1;
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
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(8080);
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        perror("bind failed");
        close(listen_fd);
        return 1;
    }

    // 监听端口
    listen(listen_fd, 5);

    // 创建epoll实例
    epfd = epoll_create1(0);
    if (epfd == -1)
    {
        perror("epoll_create1 failed");
        close(listen_fd);
        return 1;
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
        return 1;
    }

    std::cout << "Server listening on port 8080..." << std::endl;

    // 创建事件数组
    // std::vector<struct epoll_event> events(MAX_EVENTS);
    struct epoll_event events[MAX_EVENTS];

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
                    bool ret = handle_client_data(ptr);

                    // 连接关闭或错误
                    if (ret)
                    {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, ptr->fd, nullptr);
                        close(ptr->fd);
                        delete ptr;
                        std::cout << "Connection closed" << std::endl;
                    }
                }
                else if (events[n].events & EPOLLOUT)
                {
                    // std::cout << "DEAL EPOLLOUT event on fd: " << ptr->fd << std::endl;
                    // 没写完的再尝试写
                    bool ret = handle_write(ptr);

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
    return 0;
}