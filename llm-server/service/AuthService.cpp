#include "AuthService.h"
#include "Db.h"
#include "Log.h"
#include "Hash.h"
#include <cstdio>
#include <cstdlib>

// 密码 = 加盐 SHA-256 (salt 十六进制 + password 原文)
std::string AuthService::hashPassword(const std::string& salt, const std::string& password) {
    Hash h(HashType::Sha256);
    h.addData(salt);
    h.addData(password);
    return h.result(Hash::Type::Hex);
}

std::string AuthService::randomHex(int bytes) {
    std::string out;
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return out;
    unsigned char buf[64];
    size_t n = fread(buf, 1, (size_t)(bytes > 64 ? 64 : bytes), f);
    fclose(f);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out.push_back(hex[buf[i] >> 4]);
        out.push_back(hex[buf[i] & 0x0F]);
    }
    return out;
}

std::string AuthService::issueSessionToken(long long userId) {
    std::string token = randomHex(32);           // 64 个 hex 字符
    if (token.size() < 64) return "";
    std::string sql = "INSERT INTO sessions(token, user_id, expires_at) "
                      "VALUES('" + token + "', " + std::to_string(userId) +
                      ", DATE_ADD(NOW(), INTERVAL 7 DAY))";
    if (!Db::update(sql)) return "";
    return token;
}

long long AuthService::userIdForToken(const std::string& token) {
    if (token.empty()) return -1;
    std::string esc = Db::escape(token);
    std::string sql = "SELECT user_id FROM sessions "
                      "WHERE token='" + esc + "' AND expires_at > NOW()";
    if (!Db::query(sql)) return -1;
    if (!Db::next()) return -1;
    return atoll(Db::value(0).c_str());
}

AuthResult AuthService::registerUser(const std::string& username,
                                     const std::string& email,
                                     const std::string& password) {
    AuthResult r;
    if (username.empty() || username.size() > 64 || password.size() < 6 || password.size() > 128) {
        r.msg = "用户名或密码长度不合法";
        return r;
    }
    if (email.empty() || email.size() > 128) { r.msg = "邮箱不合法"; return r; }

    std::string salt = randomHex(16);
    if (salt.size() < 32) { r.msg = "服务器随机源异常"; return r; }
    std::string uh = hashPassword(salt, password);

    std::string sql = "INSERT INTO users(username, email, salt, password_hash) VALUES('" +
        Db::escape(username) + "','" + Db::escape(email) + "','" + salt + "','" + uh + "')";
    long long uid = Db::insert(sql);
    if (uid < 0) { r.msg = "用户名或邮箱已被注册"; return r; }

    r.token = issueSessionToken(uid);
    if (r.token.empty()) { r.msg = "会话创建失败"; return r; }
    r.code = 0;
    r.userId = uid;
    r.username = username;
    r.email = email;
    return r;
}

AuthResult AuthService::login(const std::string& username, const std::string& password) {
    AuthResult r;
    std::string sql = "SELECT id, username, email, salt, password_hash FROM users "
                      "WHERE username='" + Db::escape(username) + "' LIMIT 1";
    if (!Db::query(sql) || !Db::next()) { r.msg = "用户不存在"; return r; }
    long long uid = atoll(Db::value(0).c_str());
    std::string uname = Db::value(1);
    std::string email = Db::value(2);
    std::string salt  = Db::value(3);
    std::string hash  = Db::value(4);

    if (hashPassword(salt, password) != hash) { r.msg = "密码错误"; return r; }

    r.token = issueSessionToken(uid);
    if (r.token.empty()) { r.msg = "会话创建失败"; return r; }
    r.code = 0;
    r.userId = uid;
    r.username = uname;
    r.email = email;
    return r;
}

void AuthService::logout(const std::string& token) {
    if (token.empty()) return;
    std::string sql = "DELETE FROM sessions WHERE token='" + Db::escape(token) + "'";
    Db::update(sql);
}

AuthResult AuthService::me(const std::string& token) {
    AuthResult r;
    if (token.empty()) { r.msg = "未登录"; return r; }
    std::string esc = Db::escape(token);
    std::string sql = "SELECT u.id, u.username, u.email FROM sessions s "
                      "JOIN users u ON u.id = s.user_id "
                      "WHERE s.token='" + esc + "' AND s.expires_at > NOW()";
    if (!Db::query(sql) || !Db::next()) { r.msg = "未登录或已过期"; return r; }
    r.code = 0;
    r.userId = atoll(Db::value(0).c_str());
    r.username = Db::value(1);
    r.email = Db::value(2);
    return r;
}