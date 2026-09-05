#include "ConvHandler.h"
#include "net/HttpContext.h"
#include "service/ConversationService.h"
#include <json/json.h>
#include <json/reader.h>
#include <cstdlib>

using namespace std;

// 对话/消息 HTTP 层: 解析请求 -> 调 ConversationService -> 组装 JSON
// (SQL 全部在 service/ConversationService; 鉴权由 Router 中间件完成)
void ConvHandler::handle(HttpContext& ctx) {
    long long uid = ctx.uid;   // Router 中间件已校验 token 并填充

    // ---- 对话列表 (GET /api/convs) ----
    if (ctx.method == "GET" && ctx.url == "/api/convs") {
        vector<ConvRow> convs;
        ConversationService::list(uid, convs);
        Json::Value resp;
        resp["code"] = 0;
        Json::Value arr(Json::arrayValue);
        for (const auto& c : convs) {
            Json::Value o;
            o["id"] = to_string(c.id);
            o["title"] = c.title;
            o["created_at"] = c.createdAt;
            o["updated_at"] = c.updatedAt;
            arr.append(o);
        }
        resp["convs"] = arr;
        ctx.resp.sendJson(200, "OK", resp);
        return;
    }

    // ---- 新建对话 (POST /api/convs) {title?} ----
    if (ctx.method == "POST" && ctx.url == "/api/convs") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
            ctx.resp.sendErr("参数格式错误"); return;
        }
        ConvRow c;
        if (!ConversationService::create(uid, req.get("title", "").asString(), c)) {
            ctx.resp.sendErr("创建失败"); return;
        }
        Json::Value resp;
        resp["code"] = 0;
        Json::Value o;
        o["id"] = to_string(c.id);
        o["title"] = c.title;
        o["created_at"] = c.createdAt;
        o["updated_at"] = c.updatedAt;
        resp["conv"] = o;
        ctx.resp.sendJson(200, "OK", resp);
        return;
    }

    // ---- 改名 (POST /api/convs/rename) {id, title} ----
    if (ctx.method == "POST" && ctx.url == "/api/convs/rename") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
            ctx.resp.sendErr("参数格式错误"); return;
        }
        long long id = atoll(req.get("id", "0").asString().c_str());
        if (id <= 0) { ctx.resp.sendErr("参数格式错误"); return; }
        ConversationService::rename(uid, id, req.get("title", "").asString());
        Json::Value resp;
        resp["code"] = 0;
        ctx.resp.sendJson(200, "OK", resp);
        return;
    }

    // ---- 删除 (POST /api/convs/delete) {id} ----
    if (ctx.method == "POST" && ctx.url == "/api/convs/delete") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
            ctx.resp.sendErr("参数格式错误"); return;
        }
        long long id = atoll(req.get("id", "0").asString().c_str());
        if (id <= 0) { ctx.resp.sendErr("参数格式错误"); return; }
        ConversationService::remove(uid, id);
        Json::Value resp;
        resp["code"] = 0;
        ctx.resp.sendJson(200, "OK", resp);
        return;
    }

    // ---- 消息列表 (POST /api/convs/messages) {conv_id} ----
    if (ctx.method == "POST" && ctx.url == "/api/convs/messages") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
            ctx.resp.sendErr("参数格式错误"); return;
        }
        long long convId = atoll(req.get("conv_id", "0").asString().c_str());
        if (convId <= 0) { ctx.resp.sendErr("参数格式错误"); return; }
        vector<MsgRow> msgs;
        if (!ConversationService::loadMessages(uid, convId, msgs)) {
            ctx.resp.sendErr("对话不存在或无权访问"); return;
        }
        Json::Value resp;
        resp["code"] = 0;
        Json::Value arr(Json::arrayValue);
        for (const auto& m : msgs) {
            Json::Value o;
            o["role"] = m.role;
            o["content"] = m.content;
            o["created_at"] = m.createdAt;
            arr.append(o);
        }
        resp["messages"] = arr;
        ctx.resp.sendJson(200, "OK", resp);
        return;
    }

    ctx.resp.sendErr("unknown conv route");
}