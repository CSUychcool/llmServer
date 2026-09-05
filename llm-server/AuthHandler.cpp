#include "AuthHandler.h"
#include "Db.h"
#include "Api.h"
#include "Log.h"
#include "Hash.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 前端沿用 btoa 明文无意义: 改为服务端加盐 SHA-256
std::string AuthHandler::hashPassword(const std::string& salt, const std::string& password) {
    Hash h(HashType::Sha256);
    h.addData(salt);
    h.addData(password);
    return h.result(Hash::Type::Hex);
}

std::string AuthHandler::randomHex(int bytes) {
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

std::string AuthHandler::issueSessionToken(long long userId) {
    std::string token = randomHex(32);           // 64 个 hex 字符
    if (token.size() < 64) return "";
    std::string sql = "INSERT INTO sessions(token, user_id, expires_at) "
                      "VALUES('" + token + "', " + std::to_string(userId) +
                      ", DATE_ADD(NOW(), INTERVAL 7 DAY))";
    if (!Db::update(sql)) return "";
    return token;
}

long long AuthHandler::userIdForToken(const std::string& token) {
    if (token.empty()) return -1;
    std::string esc = Db::escape(token);
    std::string sql = "SELECT user_id FROM sessions "
                      "WHERE token='" + esc + "' AND expires_at > NOW()";
    if (!Db::query(sql)) return -1;
    if (!Db::next()) return -1;
    return atoll(Db::value(0).c_str());
}

void AuthHandler::handle(int wfd, const std::string& url, const std::string& token,
                         const char* body, size_t bodyLen) {
    if (url.rfind("/api/auth/", 0) != 0) { sendErr(wfd, "unknown auth route"); return; }
    std::string route = url.substr(10);   // 去掉 "/api/auth/"
    if (route == "register") handleRegister(wfd, body, bodyLen);
    else if (route == "login")    handleLogin(wfd, body, bodyLen);
    else if (route == "logout")   handleLogout(wfd, token);
    else if (route == "me")       handleMe(wfd, token);
    else sendErr(wfd, "unknown auth route");
}

void AuthHandler::handleRegister(int wfd, const char* body, size_t bodyLen) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(std::string(body ? body : "", bodyLen), req)) {
        sendErr(wfd, "参数格式错误"); return;
    }
    std::string username = req.get("username", "").asString();
    std::string email    = req.get("email", "").asString();
    std::string password = req.get("password", "").asString();

    if (username.empty() || username.size() > 64 || password.size() < 6 || password.size() > 128) {
        sendErr(wfd, "用户名或密码长度不合法"); return;
    }
    if (email.empty() || email.size() > 128) { sendErr(wfd, "邮箱不合法"); return; }

    std::string salt = randomHex(16);
    if (salt.size() < 32) { sendErr(wfd, "服务器随机源异常"); return; }
    std::string uh = hashPassword(salt, password);

    std::string sql = "INSERT INTO users(username, email, salt, password_hash) VALUES('" +
        Db::escape(username) + "','" + Db::escape(email) + "','" + salt + "','" + uh + "')";
    long long uid = Db::insert(sql);
    if (uid < 0) {
        // 1062 = 唯一键冲突
        sendErr(wfd, "用户名或邮箱已被注册"); return;
    }
    std::string token = issueSessionToken(uid);
    if (token.empty()) { sendErr(wfd, "会话创建失败"); return; }

    Json::Value resp;
    resp["code"] = 0;
    resp["token"] = token;
    Json::Value u;
    u["id"] = (Json::Int64)uid;
    u["username"] = username;
    u["email"] = email;
    resp["user"] = u;
    sendJson(wfd, 200, "OK", resp);
}

void AuthHandler::handleLogin(int wfd, const char* body, size_t bodyLen) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(std::string(body ? body : "", bodyLen), req)) {
        sendErr(wfd, "参数格式错误"); return;
    }
    std::string username = req.get("username", "").asString();
    std::string password = req.get("password", "").asString();

    std::string sql = "SELECT id, username, email, salt, password_hash FROM users "
                      "WHERE username='" + Db::escape(username) + "' LIMIT 1";
    if (!Db::query(sql) || !Db::next()) { sendErr(wfd, "用户不存在"); return; }
    long long uid = atoll(Db::value(0).c_str());
    std::string uname = Db::value(1);
    std::string email = Db::value(2);
    std::string salt  = Db::value(3);
    std::string hash  = Db::value(4);

    if (hashPassword(salt, password) != hash) { sendErr(wfd, "密码错误"); return; }

    std::string token = issueSessionToken(uid);
    if (token.empty()) { sendErr(wfd, "会话创建失败"); return; }

    Json::Value resp;
    resp["code"] = 0;
    resp["token"] = token;
    Json::Value u;
    u["id"] = (Json::Int64)uid;
    u["username"] = uname;
    u["email"] = email;
    resp["user"] = u;
    sendJson(wfd, 200, "OK", resp);
}

void AuthHandler::handleLogout(int wfd, const std::string& token) {
    if (token.empty()) { sendUnauthorized(wfd); return; }
    std::string sql = "DELETE FROM sessions WHERE token='" + Db::escape(token) + "'";
    Db::update(sql);
    Json::Value resp;
    resp["code"] = 0;
    sendJson(wfd, 200, "OK", resp);
}

void AuthHandler::handleMe(int wfd, const std::string& token) {
    if (token.empty()) { sendUnauthorized(wfd); return; }
    std::string esc = Db::escape(token);
    std::string sql = "SELECT u.id, u.username, u.email FROM sessions s "
                      "JOIN users u ON u.id = s.user_id "
                      "WHERE s.token='" + esc + "' AND s.expires_at > NOW()";
    if (!Db::query(sql) || !Db::next()) { sendUnauthorized(wfd); return; }
    Json::Value resp;
    resp["code"] = 0;
    Json::Value u;
    u["id"] = (Json::Int64)atoll(Db::value(0).c_str());
    u["username"] = Db::value(1);
    u["email"] = Db::value(2);
    resp["user"] = u;
    sendJson(wfd, 200, "OK", resp);
}