#include <thread>
#include <queue>
#include <mutex>

enum TaskType {READ, WRITE, CLOSE};

template <typename T>
class Task{
public:
    T* conn;
    TaskType type;

    Task(T* c, TaskType t): conn(c), type(t) {}
};

template <typename T>
class ThreadPool
{
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();
    bool enqueue(Task<T> *task);

private:
    std::vector<std::thread> thread_list;
    std::queue<Task<T> *> tasks;

    std::mutex queue_mutex;
    // std::condition_variable condition;
    bool stop = false;

    void worker();
};

template <typename T>
ThreadPool<T>::ThreadPool(size_t numThreads)
{
    // 初始化线程池，创建工作线程
    for (size_t i = 0; i < numThreads; ++i)
    {
        thread_list.emplace_back(std::thread(&ThreadPool::worker, this));
    }

}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    // 清理资源，停止线程等
    delete thread_list;
}

template <typename T>
bool ThreadPool<T>::enqueue(Task<T> *task)
{
    // 将任务添加到任务队列
    std::lock_guard<std::mutex> lock(queue_mutex);
    tasks.push(task);
    return true;
}

template <typename T>
void ThreadPool<T>::worker()
{
    // 工作线程函数，从任务队列中获取任务并执行
    while (true)
    {
        Task<T> *task;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (tasks.empty())
                continue;
            task = tasks.front();
            tasks.pop();
        }
        // 执行任务
        if(task->type == READ){
            task->conn->handle_client_data();
        } else if(task->type == WRITE){
            task->conn->handle_write();
        }
    }
}