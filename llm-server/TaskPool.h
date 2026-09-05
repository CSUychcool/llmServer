#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>

class TaskPool {
public:
    // All members are public so the static worker function can access them from another translation unit
    std::queue<std::function<void()>> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stopped{false};
    int m_nWorkers;
    std::vector<std::thread*> m_threads;

public:
    TaskPool(int nWorkers);
    ~TaskPool();
    void run();
    void submit(std::function<void()> fn);
private:
    static void workerFunc(TaskPool* pool);
};
