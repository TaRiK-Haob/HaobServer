#include "connection.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <arpa/inet.h>

int Connection::epfd = -1;

bool Connection::handle_write()
{
    // std::cout << "Handle write event on fd: " << conn->fd << std::endl;
    // 立即尝试写，写到 EAGAIN 或写完
    while (this->write_offset < this->write_buffer.size())
    {
        ssize_t wn = send(this->fd,
                          this->write_buffer.data() + this->write_offset,
                          this->write_buffer.size() - this->write_offset, 0);
        if (wn > 0)
        {
            // 写了 wn 字节
            this->write_offset += wn;
            continue;
        }
        else if (wn < 0)
        {
            // 写到非阻塞边界，需要等epoll再次触发可写事件
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 写不完，注册 EPOLLOUT 继续写
                // 退出

                epoll_event ev;
                ev.events = EPOLLOUT | EPOLLONESHOT;
                ev.data.ptr = this;

                epoll_ctl(epfd, EPOLL_CTL_MOD, this->fd, &ev);
                std::cout << "Send buffer full, wait for EPOLLOUT on fd: " << this->fd << std::endl;
                return false;
            }
            else
            {
                // 其他错误，关闭连接
                std::cout << "Send error: in write" << this->fd << std::endl;
                return true;
            }
        }
    }
    // std::cout << "Send complete on fd: " << conn->fd << std::endl;
    return true; // 写完
}

bool Connection::handle_client_data()
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

        ssize_t n = recv(this->fd, buffer, BUFFER_SIZE, 0);

        if (n > 0)
        {
            this->read_buffer.insert(this->read_buffer.end(), buffer, buffer + n);
            this->read_offset += n;
            continue;
        }
        else if (n == 0)
        {
            // 客户端关闭连接
            // std::cout << "Client closed connection: " << conn->fd << std::endl;
            closed = true;
            break;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 因为是EPOLLOUT + 水平触发，所以读到非阻塞边界就停止
                // 因此只需要退出循环，后续等待epoll_wait重新唤醒
                break; // 读到非阻塞边界，后续由epoll重新唤醒
            }
            else
            {
                // 其他错误
                closed = true;
                std::cout << "Unkonwn error: in read" << this->fd << std::endl;
                break;
            }
        }
    }

    // 处理HTTP请求 做简单的解析 判断请求完整性
    bool request_complete = false;
    if (this->read_offset >= 4)
    {
        // 简单查找方法
        auto it = std::search(this->read_buffer.begin(), this->read_buffer.end(),
                              "\r\n\r\n", "\r\n\r\n" + 4);
        if (it != this->read_buffer.end())
        {
            request_complete = true;
        }
    }

    if (!request_complete)
    {
        // 请求不完整，继续等待更多数据
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = this;
        epoll_ctl(epfd, EPOLL_CTL_MOD, this->fd, &ev);
        return false;
    }

    // 显示输出收到的请求
    // std::cout << "Received complete HTTP request on fd: "
    //           << std::string(this->read_buffer.begin(), this->read_buffer.end())
    //           << std::endl;

    // std::cout << "Received complete HTTP request on fd: " << conn->fd << std::endl;
    // 构造HTTP响应
    std::string body = "Hello World!\nYour IP is:" +
                       std::string(inet_ntoa(this->client_addr.sin_addr)) + "\n";
    std::string header = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Connection: close\r\n"
                         "Content-Length: " +
                         std::to_string(body.size()) + "\r\n"
                                                       "\r\n";

    // 准备发送数据
    this->write_buffer.insert(this->write_buffer.end(), header.begin(), header.end());
    this->write_buffer.insert(this->write_buffer.end(), body.begin(), body.end());
    this->write_offset = 0;

    // 直接尝试写响应
    bool done = this->handle_write();

    // 直接写完了就返回true关闭连接
    return done; // true表示连接关闭，false表示继续保持连接
}