#pragma once

#include <string>

struct HttpContext;

// 聊天业务 Handler: GET 静态页面(公开) + POST /api/chat(需登录)
// 只做请求解析与编排; 上游代理走 service/LlmGateway, SQL 走 service/ConversationService
class ChatHandler {
public:
    static void handle(HttpContext& ctx);
};