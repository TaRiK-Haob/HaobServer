#include "webserver.h"
#include <iostream>

int main()
{
    Webserver server;

    // TODO: 服务器初始化参数
    if (!server.init("127.0.0.1", 8080, 6))
    {
        std::cerr << "Failed to initialize server" << std::endl;
        exit(1);
    }

    server.loop();
    
    return 0;
}
