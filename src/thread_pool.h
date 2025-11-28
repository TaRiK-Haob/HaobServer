#include <thread>
#include <queue>
#include <mutex>

class Webserver; // 前向声明

class ThreadPool
{
public:
    ThreadPool(Webserver *webserver, size_t numThreads);
    ~ThreadPool();
    bool enqueue(Connection *task);

private:
    Webserver *webserver;
    std::vector<std::thread> thread_list;
    std::queue<Connection *> tasks;

    std::mutex queue_mutex;
    // std::condition_variable condition;
    bool stop = false;

    void worker();
};


ThreadPool::ThreadPool(Webserver *webserver, size_t numThreads)
{
    this->webserver = webserver;
    // 初始化线程池，创建工作线程
    for (size_t i = 0; i < numThreads; ++i)
    {
        thread_list.emplace_back(std::thread(&ThreadPool::worker, this));
    }
}


ThreadPool::~ThreadPool()
{
    // 清理资源，停止线程等
    for (std::thread &thread : thread_list)
    {
        if (thread.joinable())
            thread.join();
    }
    thread_list.clear();
}


bool ThreadPool::enqueue(Connection *task)
{
    // 将任务添加到任务队列
    std::lock_guard<std::mutex> lock(queue_mutex);
    tasks.push(task);
    return true;
}

void ThreadPool::worker()
{
    // 工作线程函数，从任务队列中获取任务并执行
    while (true)
    {
        // 获取任务
        Connection *conn;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (tasks.empty())
                continue;
            conn = tasks.front();
            tasks.pop();
        }

        // 执行任务
        // worker只负责读写操作，业务逻辑交给主线程处理
        if (conn->state == READ)
        {
            conn->handle_client_data();
        }
        else if (conn->state == WRITE)
        {
            conn->handle_write();
        }

        // 通知主线程处理结果
        webserver->push_notify(conn);
    }
}