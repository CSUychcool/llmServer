#include "ConversationService.h"
#include "Db.h"
#include <cstdlib>
#include <cstdio>
#include <utility>

using namespace std;

bool ConversationService::list(long long uid, vector<ConvRow>& out) {
    string sql = "SELECT id, title, created_at, updated_at FROM conversations "
                 "WHERE user_id=" + to_string(uid) + " ORDER BY updated_at DESC, id DESC";
    if (!Db::query(sql)) return false;
    while (Db::next()) {
        ConvRow r;
        r.id = atoll(Db::value(0).c_str());
        r.title = Db::value(1);
        r.createdAt = Db::value(2);
        r.updatedAt = Db::value(3);
        out.push_back(r);
    }
    return true;
}

bool ConversationService::create(long long uid, const string& title, ConvRow& out) {
    string t = title;
    if (t.empty()) t = "新对话";
    if (t.size() > 120) t = t.substr(0, 120);
    string sql = "INSERT INTO conversations(user_id, title) VALUES(" +
                 to_string(uid) + ",'" + Db::escape(t) + "')";
    long long id = Db::insert(sql);
    if (id < 0) return false;
    out.id = id;
    out.title = t;
    if (Db::query("SELECT created_at, updated_at FROM conversations WHERE id=" + to_string(id))) {
        if (Db::next()) { out.createdAt = Db::value(0); out.updatedAt = Db::value(1); }
    }
    return true;
}

bool ConversationService::rename(long long uid, long long convId, const string& title) {
    string t = title;
    if (t.size() > 120) t = t.substr(0, 120);
    string sql = "UPDATE conversations SET title='" + Db::escape(t) + "' "
                 "WHERE id=" + to_string(convId) + " AND user_id=" + to_string(uid);
    return Db::update(sql);
}

bool ConversationService::remove(long long uid, long long convId) {
    Db::update("DELETE FROM messages WHERE conv_id IN "
               "(SELECT id FROM conversations WHERE id=" + to_string(convId) +
               " AND user_id=" + to_string(uid) + ")");
    return Db::update("DELETE FROM conversations WHERE id=" + to_string(convId) +
                      " AND user_id=" + to_string(uid));
}

bool ConversationService::ownsConversation(long long uid, long long convId) {
    string sql = "SELECT id FROM conversations WHERE id=" + to_string(convId) +
                 " AND user_id=" + to_string(uid);
    return Db::query(sql) && Db::next();
}

bool ConversationService::loadMessages(long long uid, long long convId, vector<MsgRow>& out) {
    if (!ownsConversation(uid, convId)) return false;
    string sql = "SELECT role, content, created_at FROM messages "
                 "WHERE conv_id=" + to_string(convId) + " ORDER BY id ASC";
    if (!Db::query(sql)) return false;
    while (Db::next()) {
        MsgRow r;
        r.role = Db::value(0);
        r.content = Db::value(1);
        r.createdAt = Db::value(2);
        out.push_back(r);
    }
    return true;
}

long long ConversationService::appendMessage(long long convId, const string& role,
                                             const string& content) {
    string sql = "INSERT INTO messages(conv_id, role, content) VALUES(" +
                 to_string(convId) + ",'" + role + "','" + Db::escape(content) + "')";
    long long id = Db::insert(sql);
    if (id >= 0) Db::update("UPDATE conversations SET updated_at=NOW() WHERE id=" + to_string(convId));
    return id;
}

bool ConversationService::loadHistory(long long convId, long long beforeId, int limit,
                                      vector<MsgRow>& out) {
    string before = beforeId > 0 ? " id < " + to_string(beforeId) : "1=1";   // 1=1 避免 SQL 语法空
    string sql = "SELECT role, content FROM messages WHERE conv_id=" + to_string(convId) +
                 " AND " + before + " ORDER BY id DESC LIMIT " + to_string(limit);
    vector<pair<string, string> > rev;   // 先倒序收集再倒回
    if (!Db::query(sql)) return false;
    while (Db::next()) rev.push_back(make_pair(Db::value(0), Db::value(1)));
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) {
        MsgRow r;
        r.role = it->first;
        r.content = it->second;
        out.push_back(r);
    }
    return true;
}