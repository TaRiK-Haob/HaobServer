#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <queue>
#include <mutex>
#include <vector>

class Webserver; // 前向声明
class Connection; // 前向声明

class ThreadPool
{
public:
    ThreadPool(Webserver *webserver, size_t numThreads);
    ~ThreadPool();
    bool enqueue(Connection *conn);

private:
    Webserver *webserver;
    std::vector<std::thread> thread_list;
    std::queue<Connection *> tasks;

    std::mutex queue_mutex;
    // std::condition_variable condition;
    bool stop = false;

    void worker();
};

#endif // THREAD_POOL_H