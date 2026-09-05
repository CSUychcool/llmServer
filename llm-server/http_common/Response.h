#pragma once

#include <string>
#include <json/json.h>

// HTTP 响应出口: 持有客户端 fd, 所有写回都经它。
// 规约: Handler/Service 不得直接调用 ::send/::close —— 一律走这里。
class Response {
public:
    explicit Response(int fd);
    ~Response();                       // 未完成则自动关闭 fd
    bool finished() const { return m_finished; }

    // ---- 一次性 JSON 响应 (发送后自动关闭 fd) ----
    void sendJson(int code, const char* reason, const Json::Value& json);
    // HTTP 200 + {code:1, msg} 业务错误
    void sendErr(const char* msg);
    // HTTP 401 未登录/过期
    void sendUnauthorized();

    // ---- 静态文件 ----
    // 若 url 命中 webRoot 下文件则发送(自动关闭 fd)并返回 true; 未命中返回 false(不关闭)
    bool serveWebRootFile(const std::string& webRoot, const std::string& url);
    // 直接发送 path 指向的文件, 成功(自动关闭)返回 true
    bool sendFile(const std::string& path, const std::string& contentType);

    // ---- SSE 流式 (聊天用; 结束后调用 finish) ----
    void beginStream();                            // text/event-stream 响应头
    void sseChunk(const std::string& chunk);       // "data: xxx\n\n"
    void finish();                                 // 显式关闭 fd

private:
    void closeFd();
    void sendAll(const char* data, size_t len);

    int m_fd;
    bool m_finished;
};