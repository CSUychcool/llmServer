#pragma once

#include <string>

// 鉴权结果 (handler 直接转 JSON 响应)
struct AuthResult {
    int code = 1;             // 0=成功, 1=业务失败
    std::string msg;
    std::string token;        // 成功时返回的会话令牌
    long long userId = -1;
    std::string username;
    std::string email;
};

// 鉴权领域: 密码哈希 / 随机串 / 用户注册登录 / 会话(DB sessions 表)
// 所有 SQL 只出现在此层与 Db 之间; Handler 不得直接拼 SQL
class AuthService {
public:
    // ---- 注册 / 登录 / 注销 / 会话恢复 ----
    static AuthResult registerUser(const std::string& username,
                                   const std::string& email,
                                   const std::string& password);
    static AuthResult login(const std::string& username, const std::string& password);
    static void logout(const std::string& token);
    // 校验 token; 有效返回用户信息 (code=0), 否则 code=1
    static AuthResult me(const std::string& token);

    // ---- 供路由层/其他 service 复用 ----
    // 校验 token 是否有效且未过期, 返回 user_id; -1 = 无效
    static long long userIdForToken(const std::string& token);
    static std::string hashPassword(const std::string& salt, const std::string& password);
    static std::string randomHex(int bytes);

    // 为用户签发新会话 token 并落库 (7 天有效), 失败返回空串
    static std::string issueSessionToken(long long userId);
};