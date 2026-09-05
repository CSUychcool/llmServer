#include "ConvHandler.h"
#include "AuthHandler.h"
#include "Db.h"
#include "Api.h"
#include "Log.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <cstdio>
#include <cstdlib>

using namespace std;

void ConvHandler::handle(int wfd, const string& method, const string& url,
                         const string& token, const char* body, size_t bodyLen) {
    long long uid = AuthHandler::userIdForToken(token);
    if (uid < 0) { sendUnauthorized(wfd); return; }

    // ---- 对话列表 (GET /api/convs) ----
    if (method == "GET" && url == "/api/convs") {
        string sql = "SELECT id, title, created_at, updated_at FROM conversations "
                     "WHERE user_id=" + to_string(uid) + " ORDER BY updated_at DESC, id DESC";
        if (!Db::query(sql)) { sendErr(wfd, "查询失败"); return; }
        Json::Value resp;
        resp["code"] = 0;
        Json::Value arr(Json::arrayValue);
        while (Db::next()) {
            Json::Value c;
            c["id"] = Db::value(0);
            c["title"] = Db::value(1);
            c["created_at"] = Db::value(2);
            c["updated_at"] = Db::value(3);
            arr.append(c);
        }
        resp["convs"] = arr;
        sendJson(wfd, 200, "OK", resp);
        return;
    }

    // ---- 新建对话 (POST /api/convs) {title?} ----
    if (method == "POST" && url == "/api/convs") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(body ? body : "", bodyLen), req)) {
            sendErr(wfd, "参数格式错误"); return;
        }
        string title = req.get("title", "").asString();
        if (title.empty()) title = "新对话";
        if (title.size() > 120) title = title.substr(0, 120);
        string sql = "INSERT INTO conversations(user_id, title) VALUES(" +
                     to_string(uid) + ",'" + Db::escape(title) + "')";
        long long id = Db::insert(sql);
        if (id < 0) { sendErr(wfd, "创建失败"); return; }
        // 取回时间字段
        Json::Value resp;
        resp["code"] = 0;
        Json::Value c;
        c["id"] = to_string(id);
        c["title"] = title;
        c["created_at"] = "NOW";
        c["updated_at"] = "NOW";
        if (Db::query("SELECT created_at, updated_at FROM conversations WHERE id=" + to_string(id))) {
            if (Db::next()) {
                c["created_at"] = Db::value(0);
                c["updated_at"] = Db::value(1);
            }
        }
        resp["conv"] = c;
        sendJson(wfd, 200, "OK", resp);
        return;
    }

    // ---- 改名 (POST /api/convs/rename) {id, title} ----
    if (method == "POST" && url == "/api/convs/rename") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(body ? body : "", bodyLen), req)) {
            sendErr(wfd, "参数格式错误"); return;
        }
        long long id = atoll(req.get("id", "0").asString().c_str());
        string title = req.get("title", "").asString();
        if (id <= 0) { sendErr(wfd, "参数格式错误"); return; }
        if (title.empty() || title.size() > 120) title = title.substr(0, 120);
        string sql = "UPDATE conversations SET title='" + Db::escape(title) + "' "
                     "WHERE id=" + to_string(id) + " AND user_id=" + to_string(uid);
        Db::update(sql);
        Json::Value resp;
        resp["code"] = 0;
        sendJson(wfd, 200, "OK", resp);
        return;
    }

    // ---- 删除 (POST /api/convs/delete) {id} ----
    if (method == "POST" && url == "/api/convs/delete") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(body ? body : "", bodyLen), req)) {
            sendErr(wfd, "参数格式错误"); return;
        }
        long long id = atoll(req.get("id", "0").asString().c_str());
        if (id <= 0) { sendErr(wfd, "参数格式错误"); return; }
        // 仅删除属于自己的对话 (先删消息, 再删对话)
        Db::update("DELETE FROM messages WHERE conv_id IN "
                   "(SELECT id FROM conversations WHERE id=" + to_string(id) +
                   " AND user_id=" + to_string(uid) + ")");
        Db::update("DELETE FROM conversations WHERE id=" + to_string(id) +
                   " AND user_id=" + to_string(uid));
        Json::Value resp;
        resp["code"] = 0;
        sendJson(wfd, 200, "OK", resp);
        return;
    }

    // ---- 消息列表 (POST /api/convs/messages) {conv_id} ----
    if (method == "POST" && url == "/api/convs/messages") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(string(body ? body : "", bodyLen), req)) {
            sendErr(wfd, "参数格式错误"); return;
        }
        long long convId = atoll(req.get("conv_id", "0").asString().c_str());
        if (convId <= 0) { sendErr(wfd, "参数格式错误"); return; }
        string ow = "SELECT id FROM conversations WHERE id=" + to_string(convId) +
                    " AND user_id=" + to_string(uid);
        if (!Db::query(ow) || !Db::next()) { sendErr(wfd, "对话不存在或无权访问"); return; }
        string sql = "SELECT role, content, created_at FROM messages "
                     "WHERE conv_id=" + to_string(convId) + " ORDER BY id ASC";
        if (!Db::query(sql)) { sendErr(wfd, "查询失败"); return; }
        Json::Value resp;
        resp["code"] = 0;
        Json::Value arr(Json::arrayValue);
        while (Db::next()) {
            Json::Value m;
            m["role"] = Db::value(0);
            m["content"] = Db::value(1);
            m["created_at"] = Db::value(2);
            arr.append(m);
        }
        resp["messages"] = arr;
        sendJson(wfd, 200, "OK", resp);
        return;
    }

    sendErr(wfd, "unknown conv route");
}