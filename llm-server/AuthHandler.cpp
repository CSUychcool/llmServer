#include "AuthHandler.h"
#include "net/HttpContext.h"
#include "service/AuthService.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

// 组装 {code:0, token, user:{id,username,email}}
static void sendAuthOk(HttpContext& ctx, const AuthResult& r) {
    Json::Value resp;
    resp["code"] = 0;
    resp["token"] = r.token;
    Json::Value u;
    u["id"] = (Json::Int64)r.userId;
    u["username"] = r.username;
    u["email"] = r.email;
    resp["user"] = u;
    ctx.resp.sendJson(200, "OK", resp);
}

void AuthHandler::handle(HttpContext& ctx) {
    if (ctx.url.rfind("/api/auth/", 0) != 0) { ctx.resp.sendErr("unknown auth route"); return; }
    std::string route = ctx.url.substr(10);   // 去掉 "/api/auth/"
    if (route == "register")      handleRegister(ctx);
    else if (route == "login")    handleLogin(ctx);
    else if (route == "logout")   handleLogout(ctx);
    else if (route == "me")       handleMe(ctx);
    else ctx.resp.sendErr("unknown auth route");
}

void AuthHandler::handleRegister(HttpContext& ctx) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(std::string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
        ctx.resp.sendErr("参数格式错误"); return;
    }
    AuthResult r = AuthService::registerUser(
        req.get("username", "").asString(),
        req.get("email", "").asString(),
        req.get("password", "").asString());
    if (r.code != 0) { ctx.resp.sendErr(r.msg.c_str()); return; }
    sendAuthOk(ctx, r);
}

void AuthHandler::handleLogin(HttpContext& ctx) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(std::string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
        ctx.resp.sendErr("参数格式错误"); return;
    }
    AuthResult r = AuthService::login(
        req.get("username", "").asString(),
        req.get("password", "").asString());
    if (r.code != 0) { ctx.resp.sendErr(r.msg.c_str()); return; }
    sendAuthOk(ctx, r);
}

void AuthHandler::handleLogout(HttpContext& ctx) {
    if (ctx.token.empty()) { ctx.resp.sendUnauthorized(); return; }
    AuthService::logout(ctx.token);
    Json::Value resp;
    resp["code"] = 0;
    ctx.resp.sendJson(200, "OK", resp);
}

void AuthHandler::handleMe(HttpContext& ctx) {
    AuthResult r = AuthService::me(ctx.token);
    if (r.code != 0) { ctx.resp.sendUnauthorized(); return; }
    Json::Value resp;
    resp["code"] = 0;
    Json::Value u;
    u["id"] = (Json::Int64)r.userId;
    u["username"] = r.username;
    u["email"] = r.email;
    resp["user"] = u;
    ctx.resp.sendJson(200, "OK", resp);
}