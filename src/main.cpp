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
#include "webserver.h"

int main()
{
    Webserver server;
    if (!server.init("127.0.0.1", 8080))
    {
        std::cerr << "Failed to initialize server" << std::endl;
        exit(1);
    }

    server.loop();
    
    return 0;
}