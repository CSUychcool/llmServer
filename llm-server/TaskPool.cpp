#include "TaskPool.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>
#include <cstdio>

void TaskPool::workerFunc(TaskPool* pool) {
    while (true) {
        std::function<void()> fn;
        {
            std::unique_lock<std::mutex> lock(pool->m_mutex);
            pool->m_cv.wait(lock, [&] { return !pool->m_queue.empty() || pool->m_stopped.load(); });
            if (pool->m_stopped.load() && pool->m_queue.empty())
                return;
            fn = std::move(pool->m_queue.front());
            pool->m_queue.pop();
        }
        try { fn(); } catch (...) {}
    }
}

TaskPool::TaskPool(int nWorkers) : m_nWorkers(nWorkers) {}
TaskPool::~TaskPool() {
    m_stopped.store(true);
    m_cv.notify_all();
    for (auto* t : m_threads) { t->join(); delete t; }
    m_threads.clear();
}
void TaskPool::run() {
    for (int i = 0; i < m_nWorkers; ++i)
        m_threads.push_back(new std::thread(workerFunc, this));
}
void TaskPool::submit(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(fn));
    }
    m_cv.notify_one();
}
