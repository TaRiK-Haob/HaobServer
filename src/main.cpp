#include "webserver.h"
#include <iostream>
#include <signal.h>

volatile sig_atomic_t g_running = 1;

void handle_signal(int sig)
{
    g_running = 0;
}

int main()
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    Webserver server;

    // TODO: 服务器初始化参数
    if (!server.init("127.0.0.1", 8080, 6))
    {
        std::cerr << "Failed to initialize server" << std::endl;
        exit(1);
    }

    while (g_running)
    {
        server.loop();
    }

    return 0;
}
