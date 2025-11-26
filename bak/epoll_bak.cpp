#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <sys/epoll.h>
#include <vector>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096

void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_client_data(int client_fd)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // 循环读取直到EAGAIN（LT模式下可以这样处理）
    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';
        std::cout << "Received from client " << client_fd << ": " << buffer << std::endl;

        // 构建HTTP响应
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "Hello";

        send(client_fd, response.c_str(), response.size(), 0);
    }

    // 读取完成或出错
    if (bytes_read == 0)
    {
        // 客户端关闭连接
        std::cout << "Client " << client_fd << " disconnected" << std::endl;
    }
    else if (bytes_read < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            // 真正的错误
            perror("recv error");
        }
        // EAGAIN/EWOULDBLOCK 表示没有更多数据（LT模式下正常）
    }
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

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(8080);
    bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // 监听端口
    listen(listen_fd, 5);

    // 创建epoll实例
    int epfd = epoll_create1(0);
    if (epfd == -1)
    {
        perror("epoll_create1 failed");
        close(listen_fd);
        return 1;
    }

    // 注册监听事件 监听的类型为可读事件 监听的文件描述符为listen_fd
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;

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
            int event_fd = events[n].data.fd;
            if (event_fd == listen_fd)
            {
                // 有新的连接请求
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

                if (conn_fd < 0)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        break;
                    }
                    else
                    {
                        perror("accept failed");
                        continue;
                    }
                }

                // 设置连接套接字为非阻塞模式
                set_nonblocking(conn_fd);

                // 将连接套接字添加到epoll实例中
                struct epoll_event client_ev;
                client_ev.events = EPOLLIN; // 监听可读事件
                client_ev.data.fd = conn_fd;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &client_ev) == -1)
                {
                    perror("epoll_ctl client_fd failed");
                    close(conn_fd);
                    continue;
                }
                std::cout << "New client connected: " << inet_ntoa(client_addr.sin_addr) << " "
                          << conn_fd << std::endl;
            }
            else
            {
                // 8. 处理客户端数据（读事件）
                if (events[n].events & EPOLLIN)
                {
                    handle_client_data(event_fd);

                    // 关闭连接（HTTP/1.0 短连接）
                    epoll_ctl(epfd, EPOLL_CTL_DEL, event_fd, nullptr);
                    close(event_fd);
                }
            }
        }
    }

    // 清理资源 关闭epoll实例和监听套接字
    close(epfd);
    close(listen_fd);
    return 0;
}