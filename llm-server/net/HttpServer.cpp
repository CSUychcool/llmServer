#include "HttpServer.h"
#include "HttpConnection.h"
#include "Channel.h"
#include "Log.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <cstdio>

HttpServer::HttpServer(unsigned short port, int threadNum)
    : m_port(port), m_threadNum(threadNum)
{
    m_mainLoop = new EventLoop("MainLoop");
    m_pool = new TaskPool(threadNum);
    m_pool->run();
    // worker EventLoop 必须由子线程内部创建并 run() (EventLoop 线程 ID 绑定机制),
    // 所以复用 server_ddz 的 ThreadPool/WorkerThread, 而不是裸 new EventLoop
    m_workerPool = new ThreadPool(m_mainLoop, threadNum);
    m_workerPool->run();
}

HttpServer::~HttpServer() {
    close(m_lfd);
    delete m_workerPool;
    delete m_pool;
    delete m_mainLoop;
}

void HttpServer::run() {
    /* TCP listen */
    m_lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_lfd < 0) exit(1);

    int opt = 1;
    setsockopt(m_lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(m_lfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[HttpServer] bind failed"); exit(1);
    }
    if (listen(m_lfd, 512) < 0) {
        perror("[HttpServer] listen failed"); exit(1);
    }

    /* Register listening fd's ReadEvent → acceptConnection */
    Channel* channel = new Channel(m_lfd, FDEvent::ReadEvent,
                                   acceptConnection, nullptr, nullptr, this);
    m_mainLoop->addTask(channel, ElemType::ADD);

    tprintf("[HttpServer] Listening on port %d\n", m_port);
    fflush(stdout);

    m_mainLoop->run();
}

int HttpServer::acceptConnection(void* arg) {
    HttpServer* server = static_cast<HttpServer*>(arg);
    int cfd = ::accept(server->m_lfd, NULL, NULL);
    if (cfd < 0) return -1;

    // 日志: 客户端地址 + 分配到哪个 worker
    struct sockaddr_in caddr{};
    socklen_t clen = sizeof(caddr);
    ::getpeername(cfd, (struct sockaddr*)&caddr, &clen);
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));

    // Round-Robin select a worker loop
    EventLoop* evLoop = server->m_workerPool->takeWorkerEventLoop();
    tprintf("[HttpServer] accept cfd=%d from %s:%d -> %s (t%lu)\n",
           cfd, ip, ntohs(caddr.sin_port), evLoop->getThreadName().c_str(),
           (unsigned long)hash<std::thread::id>{}(evLoop->getThreadID()) % 10000);
    fflush(stdout);

    new HttpConnection(cfd, evLoop, server->m_pool);
    return 0;
}