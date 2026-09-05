#pragma once

struct HttpContext;

// /api/control/* HTTP 层: 模型状态与切换 (需登录)
// 逻辑走 service/ModelService
class ControlHandler {
public:
    static void handle(HttpContext& ctx);
};