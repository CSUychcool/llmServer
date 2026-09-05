#pragma once

struct HttpContext;

// /api/convs* HTTP 层: 解析请求 -> 调 ConversationService -> 组装 JSON
class ConvHandler {
public:
    static void handle(HttpContext& ctx);
};