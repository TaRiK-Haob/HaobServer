#ifndef CONNECTION_H
#define CONNECTION_H
#endif // CONNECTION_H
#include <vector>
#include <netinet/in.h>

#define BUFFER_SIZE 4096
enum State {READ, WRITE, READ_DONE, WRITE_DONE, CLOSE, ERROR};
// TODO: 添加更多状态如解析HTTP请求等

class Connection
{
public:
    static int epfd; // epoll实例的文件描述符
    int fd;
    std::vector<char> read_buffer;
    std::vector<char> write_buffer;
    size_t write_offset;
    size_t read_offset;

    sockaddr_in client_addr;
    socklen_t client_len;

    void handle_client_data();
    void handle_write();

    State state = READ;
};