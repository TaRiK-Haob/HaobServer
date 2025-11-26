// #include<cstdio>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string>
#include<unistd.h>
#include<iostream>

int main() {
    // 创建套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    // 绑定地址和端口
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(8080);
    bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    // 监听端口
    listen(fd, 5);
    std::cout << "Server listening on port 8080..." << std::endl;

    // 接受连接并处理请求
    while(1){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept(fd, (struct sockaddr*)&client_addr, &client_len);
        if (conn_fd < 0) {
            perror("accept");
            continue;
        }
        std::cout << "Client connected\n" << inet_ntoa(client_addr.sin_addr) << std::endl;

        // 读取HTTP请求（简单处理，实际应该解析请求）
        char buffer[1024];
        int bytes_read = recv(conn_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            std::cout << "Request received (first line): ";
            for (int i = 0; i < bytes_read && buffer[i] != '\n'; i++) {
                std::cout << buffer[i];
            }
            std::cout << std::endl;
        }

        std::string http_response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 5\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Hello"
            "\r\n";

        send(conn_fd, http_response.c_str(), http_response.size(), 0);

        shutdown(conn_fd, SHUT_WR);

        close(conn_fd);
    }
    close(fd);
    return 0;
}