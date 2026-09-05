#pragma once

#include <string>

struct HttpContext;

// /api/auth/* HTTP 层: 解析请求 -> 调 AuthService -> 组装 JSON 响应
// (业务逻辑与 SQL 在 service/AuthService, 此处不碰 SQL / 不碰 fd)
class AuthHandler {
public:
    static void handle(HttpContext& ctx);
private:
    static void handleRegister(HttpContext& ctx);
    static void handleLogin(HttpContext& ctx);
    static void handleLogout(HttpContext& ctx);
    static void handleMe(HttpContext& ctx);
};