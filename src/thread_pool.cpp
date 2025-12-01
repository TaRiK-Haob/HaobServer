#include "thread_pool.h"
#include "webserver.h"
#include "connection.h"

#include <iostream>

// TODO：加入condition_variable优化等待

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
    stop = true;
    // 清理资源，停止线程等
    for (std::thread &thread : thread_list)
    {
        if (thread.joinable())
            thread.join();
    }
    thread_list.clear();
}

bool ThreadPool::enqueue(Connection *conn)
{
    // 如果连接已经在处理队列中，直接返回
    if (conn->in_pool.load())
        return false;

    conn->in_pool.store(true);

    // 将任务添加到任务队列
    std::lock_guard<std::mutex> lock(queue_mutex);
    tasks.push(conn);
    return true;
}

void ThreadPool::worker()
{
    // 工作线程函数，从任务队列中获取任务并执行
    while (true)
    {
        // 检查是否需要停止线程
        if(stop)
            break;

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
            // std::cout << "ThreadPool " << std::this_thread::get_id() << ": Handling READ for fd " << conn->fd << std::endl;
            conn->handle_client_data();
        }
        else if (conn->state == WRITE)
        {
            // std::cout << "ThreadPool " << std::this_thread::get_id() << ": Handling WRITE for fd " << conn->fd << std::endl;
            conn->handle_write();
        }

        // 通知主线程处理结果，移除in_pool标志
        conn->in_pool.store(false);
        webserver->push_notify(conn);
    }
}