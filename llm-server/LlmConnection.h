#pragma once

#include "EventLoop.h"
#include "Buffer.h"
#include "Channel.h"
#include "HttpRequest.h"
#include "TaskPool.h"
#include "ChatHandler.h"
#include <string>
#include <functional>

class LlmConnection {
public:
    LlmConnection(int fd, EventLoop* evloop, TaskPool* pool);
    ~LlmConnection();

private:
    static int processRead(void* arg);
    static int processWrite(void* arg);
    static int destroy(void* arg);
    void onParseComplete();

private:
    EventLoop* m_evLoop;
    Channel* m_channel;
    Buffer* m_readBuf;
    Buffer* m_writeBuf;
    HttpRequest* m_req;
    TaskPool* m_pool;
    bool m_httpDone{false};
    bool m_detached{false};
    int m_fd{-1};           // socket fd，解析完成后 dup 给工作线程
};