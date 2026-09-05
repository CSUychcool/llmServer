#pragma once

#include <string>

struct LlmConfig {
    std::string upstreamHost = "127.0.0.1";
    int upstreamPort = 11434;
    std::string upstreamPath = "/v1/chat/completions";
    std::string model = "qwen2.5:7b-instruct-q4_K_M";
    // 静态网页根目录(内网穿透场景: 让 9000 同时托管 index.html), 空则不提供网页
    std::string webRoot = "";
};

class ChatHandler {
public:
    // method/url: 用于区分并处理 GET 静态页面(如 / -> index.html) 和 POST /api/chat
    // token: Authorization 头去掉 "Bearer " 前缀后的鉴权值 (/api/chat 需要登录)
    static void handle(int wfd, const char* method, const char* url, const char* token,
                       const char* rawBody, size_t bodyLen);
    static bool parseConfig(const char* configFile);
    static LlmConfig config;
};
