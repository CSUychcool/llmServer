#pragma once

#include <string>

// 模型切换控制接口 — 供 web 前端通过 HTTP 调用
//   POST /api/control/switch  {"backend":"ollama|vllm","model":"..."}
//   GET  /api/control/status
class ControlHandler {
public:
    // token: Authorization 头去掉 "Bearer " 前缀后的鉴权值 (所有控制接口需登录)
    static void handle(int wfd, const std::string& url, const std::string& token,
                       const char* body, size_t bodyLen);

private:
    static void handleStatus(int wfd);
    static void handleSwitch(int wfd, const char* body, size_t bodyLen);
};