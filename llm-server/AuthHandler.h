#pragma once

#include <string>

// /api/auth/* 路由: 注册/登录/登出/会话恢复
class AuthHandler {
public:
    // url 形如 /api/auth/register 等; token 为请求头 Authorization 提取后的内容(去掉 Bearer)
    static void handle(int wfd, const std::string& url, const std::string& token,
                       const char* body, size_t bodyLen);

    // 校验 token: 有效返回 user_id, 否则返回 -1
    static long long userIdForToken(const std::string& token);

    // 供其他 handler 复用的密码哈希: sha256_hex(salt || password)
    static std::string hashPassword(const std::string& salt, const std::string& password);
    // 生成 n 字节随机 -> hex 字符串 (n*2 个 hex 字符)
    static std::string randomHex(int bytes);

private:
    static void handleRegister(int wfd, const char* body, size_t bodyLen);
    static void handleLogin(int wfd, const char* body, size_t bodyLen);
    static void handleLogout(int wfd, const std::string& token);
    static void handleMe(int wfd, const std::string& token);
    // 为用户 id 签发新会话 token 并落库 (sessions 表, 7 天有效)
    static std::string issueSessionToken(long long userId);
};