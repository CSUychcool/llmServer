#include "Log.h"
#include "LlmConnection.h"
#include "ControlHandler.h"
#include "AuthHandler.h"
#include "ConvHandler.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

LlmConnection::LlmConnection(int fd, EventLoop* evloop, TaskPool* pool)
    : m_evLoop(evloop), m_pool(pool), m_fd(fd)
{
    m_readBuf  = new Buffer(16384);
    m_writeBuf = new Buffer(16384);
    m_req      = new HttpRequest();

    m_channel = new Channel(fd, FDEvent::ReadEvent,
                            processRead, processWrite, destroy, this);
    evloop->addTask(m_channel, ElemType::ADD);
}

LlmConnection::~LlmConnection() {
    delete m_readBuf;
    delete m_writeBuf;
    delete m_req;
}

int LlmConnection::processRead(void* arg) {
    LlmConnection* conn = static_cast<LlmConnection*>(arg);
    int sock = conn->m_channel->getSocket();
    int n = conn->m_readBuf->socketRead(sock);
    tprintf("[LlmConnection] fd=%d READ %d bytes\n", sock, n);
    if (n <= 0) {
        // 对端关闭或出错
        tprintf("[LlmConnection] fd=%d read closed/err -> shutdown\n", sock);
        conn->m_evLoop->addTask(conn->m_channel, ElemType::DELETE);
        return 0;
    }

    // 手动驱动 HttpRequest 状态机（不用 parseHttpRequest，
    // 因为它会内部调用 prepareMsg 同步发响应，不适合流式场景）
    // 注意: parseRequestHeader 每次只消费一行 header, 所以循环条件要看
    // 解析函数是否继续消费了数据（返回 true），而不是看状态有没有变。
    PrecessState st = conn->m_req->getState();
    bool progress = true;
    while (st != PrecessState::ParseReqDone && progress) {
        switch (st) {
        case PrecessState::ParseReqLine:
            progress = conn->m_req->parseRequestLine(conn->m_readBuf);
            break;
        case PrecessState::ParseReqHeaders:
            progress = conn->m_req->parseRequestHeader(conn->m_readBuf);
            break;
        case PrecessState::ParseReqBody:
            progress = conn->m_req->parseRequestBody(conn->m_readBuf);
            break;
        default:
            progress = false;
            break;
        }
        st = conn->m_req->getState();
    }

    if (conn->m_req->getState() == PrecessState::ParseReqDone && !conn->m_detached) {
        tprintf("[LlmConnection] fd=%d HTTP request parsed (method=%s url=%s body=%zu bytes), detaching to worker\n",
               sock, conn->m_req->getMethod().c_str(), conn->m_req->getUrl().c_str(),
               conn->m_req->getBody().size());
        conn->onParseComplete();
    }
    return 0;
}

int LlmConnection::processWrite(void* arg) {
    LlmConnection* conn = static_cast<LlmConnection*>(arg);
    int count = conn->m_writeBuf->sendData(conn->m_channel->getSocket());
    if (count > 0 && conn->m_writeBuf->readableSize() == 0) {
        conn->m_channel->setCurrentEvent(FDEvent::ReadEvent);
        conn->m_evLoop->addTask(conn->m_channel, ElemType::MODIFY);
    }
    return 0;
}

void LlmConnection::onParseComplete() {
    std::string body = m_req->getBody();
    std::string url = m_req->getUrl();   // 必须在 DELETE（触发 delete m_req）之前拷贝
    std::string method = m_req->getMethod();   // 同上: 拷贝后 m_req 即可释放

    // 提取鉴权头 Authorization: Bearer <token> (去掉 "Bearer " 前缀)
    std::string token = m_req->getHeader("Authorization");
    if (token.rfind("Bearer ", 0) == 0) token = token.substr(7);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.erase(0, 1);
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t' || token.back() == '\r' || token.back() == '\n')) token.pop_back();

    // CORS 预检: 浏览器跨源 POST/带自定义头 会先发 OPTIONS。
    // 这是同步短应答, 直接在 EventLoop 里用原始 fd(m_fd) 处理, 不进入业务线程,
    // 也不需要 dup —— 避免 fd 语义混乱(日志/发送必须同一 fd)。
    if (method == "OPTIONS") {
        tprintf("[LlmConnection] fd=%d OPTIONS preflight url=%s -> reply CORS\n", m_fd, url.c_str());
        fflush(stdout);
        static const char preflight[] =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Vary: Origin\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            "Access-Control-Max-Age: 86400\r\n"
            "Connection: keep-alive\r\n\r\n";
        ::send(m_fd, preflight, sizeof(preflight) - 1, MSG_NOSIGNAL);
        // 预检不进入业务层: 直接走销毁链关连接
        m_detached = true;
        m_evLoop->addTask(m_channel, ElemType::DELETE);
        return;
    }

    // dup 一份 fd 交给工作线程（原 fd 走 EventLoop 销毁链会被 close）
    int wfd = dup(m_fd);
    if (wfd < 0) {
        perror("[LlmConnection] dup");
        m_evLoop->addTask(m_channel, ElemType::DELETE);
        return;
    }

    tprintf("[LlmConnection] fd=%d DETACH -> dup wfd=%d, submit %zu-byte body to TaskPool\n",
           m_fd, wfd, body.size());
    fflush(stdout);

    m_detached = true;
    // 从 EventLoop 摘除该 channel（销毁链：remove → destroyCallback → delete conn + close(fd)）
    m_evLoop->addTask(m_channel, ElemType::DELETE);

    // 按 URL 路由:
    //   /api/auth/*     -> 注册/登录/登出/会话恢复
    //   /api/convs*     -> 对话/消息 CRUD
    //   /api/control/*  -> 模型状态/切换 (需登录)
    //   其余            -> ChatHandler: 静态页面(公开) + /api/chat(需登录)
    m_pool->submit([wfd, body, url, method, token]() {
        if (url.rfind("/api/auth", 0) == 0) {
            AuthHandler::handle(wfd, url, token, body.c_str(), body.size());
        } else if (url.rfind("/api/convs", 0) == 0) {
            ConvHandler::handle(wfd, method.c_str(), url, token, body.c_str(), body.size());
        } else if (url.rfind("/api/control/", 0) == 0) {
            ControlHandler::handle(wfd, url, token, body.c_str(), body.size());
        } else {
            ChatHandler::handle(wfd, method.c_str(), url.c_str(), token.c_str(), body.c_str(), body.size());
        }
    });
}

int LlmConnection::destroy(void* arg) {
    LlmConnection* conn = static_cast<LlmConnection*>(arg);
    tprintf("[LlmConnection] fd=%d DESTROY: closing fd (EventLoop side)\n", conn->m_fd);
    fflush(stdout);
    ::close(conn->m_fd);
    delete conn;
    return 0;
}