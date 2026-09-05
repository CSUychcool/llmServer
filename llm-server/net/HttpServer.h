#pragma once

#include "EventLoop.h"
#include "Channel.h"
#include "TaskPool.h"
#include "ThreadPool.h"
#include <string>
#include <vector>

class HttpConnection;

class HttpServer {
public:
    HttpServer(unsigned short port, int threadNum);
    ~HttpServer();
    void run();

private:
    static int acceptConnection(void* arg);
    TaskPool* m_pool;          // 业务线程池(阻塞调用 ChatHandler)
    ThreadPool* m_workerPool;  // server_ddz 线程池: 每个 worker 线程内部 new EventLoop 并 run()

private:
    unsigned short m_port;
    int m_threadNum;
    EventLoop* m_mainLoop;
    int m_lfd;
};