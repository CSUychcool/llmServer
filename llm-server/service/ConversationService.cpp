#include "ConversationService.h"
#include "Db.h"
#include "TokenCounter.h"
#include <cstdlib>
#include <cstdio>
#include <algorithm>
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

bool ConversationService::loadHistoryByTokens(long long convId, long long beforeId,
                                              int maxTokens, int fetchLimit,
                                              vector<MsgRow>& out, bool& outHasOlder) {
    outHasOlder = false;
    if (maxTokens <= 0) { outHasOlder = true; return true; }   // 无预算且(很可能)有旧消息
    string before = beforeId > 0 ? " id < " + to_string(beforeId) : "1=1";
    string sql = "SELECT role, content FROM messages WHERE conv_id=" + to_string(convId) +
                 " AND " + before + " ORDER BY id DESC LIMIT " + to_string(fetchLimit);
    if (!Db::query(sql)) return false;

    int used = 0;
    int fetched = 0;
    vector<MsgRow> collected;
    while (Db::next()) {
        fetched++;
        MsgRow r;
        r.role = Db::value(0);
        r.content = Db::value(1);
        int tokens = TokenCounter::estimateTokens(r.content) + 4;   // +role 与引号开销
        if (used + tokens > maxTokens) { outHasOlder = true; break; }
        used += tokens;
        collected.push_back(r);
    }
    if (fetched >= fetchLimit) outHasOlder = true;   // 上限拉满 => 可能还有更旧
    for (auto it = collected.rbegin(); it != collected.rend(); ++it) out.push_back(*it);
    return true;
}

bool ConversationService::getSummary(long long convId, string& out) {
    string sql = "SELECT summary FROM conversations WHERE id=" + to_string(convId);
    if (!Db::query(sql) || !Db::next()) return false;
    out = Db::value(0);
    return true;
}

bool ConversationService::getSummaryEx(long long convId, string& out, long long& uptoMsgId) {
    string sql = "SELECT summary, summary_upto FROM conversations WHERE id=" + to_string(convId);
    if (!Db::query(sql) || !Db::next()) return false;
    out = Db::value(0);
    uptoMsgId = atoll(Db::value(1).c_str());
    return true;
}

bool ConversationService::setSummary(long long convId, const string& summary, long long uptoMsgId) {
    string sql = "UPDATE conversations SET summary='" + Db::escape(summary) +
                 "', summary_upto=" + to_string(uptoMsgId) +
                 " WHERE id=" + to_string(convId);
    return Db::update(sql);
}

long long ConversationService::messagesSince(long long convId, long long uptoMsgId) {
    string sql = "SELECT COUNT(*) FROM messages WHERE conv_id=" + to_string(convId) +
                 (uptoMsgId > 0 ? (" AND id > " + to_string(uptoMsgId)) : "");
    if (!Db::query(sql) || !Db::next()) return 0;
    return atoll(Db::value(0).c_str());
}

bool ConversationService::searchMessages(long long convId, const string& keyword,
                                        int limit, vector<MsgRow>& out) {
    if (keyword.empty()) return true;
    string kw = Db::escape(keyword);
    string sql = "SELECT role, content FROM messages WHERE conv_id=" + to_string(convId) +
                 " AND content LIKE '%" + kw + "%' ORDER BY id DESC LIMIT " + to_string(limit);
    if (!Db::query(sql)) return false;
    vector<MsgRow> rev;
    while (Db::next()) {
        MsgRow r;
        r.role = Db::value(0);
        r.content = Db::value(1);
        rev.push_back(r);
    }
    std::reverse(rev.begin(), rev.end());   // 按时间正序返回
    out.insert(out.end(), rev.begin(), rev.end());
    return true;
}

bool ConversationService::loadOldestMessages(long long convId, int limit,
                                             vector<MsgRow>& out, long long& outMaxId) {
    string sql = "SELECT id, role, content FROM messages WHERE conv_id=" + to_string(convId) +
                 " ORDER BY id ASC LIMIT " + to_string(limit);
    if (!Db::query(sql)) return false;
    outMaxId = 0;
    while (Db::next()) {
        MsgRow r;
        outMaxId = atoll(Db::value(0).c_str());
        r.role = Db::value(1);
        r.content = Db::value(2);
        out.push_back(r);
    }
    return true;
}

bool ConversationService::trimOldestMessages(long long convId, int limit) {
    string sql = "DELETE FROM messages WHERE conv_id=" + to_string(convId) +
                 " ORDER BY id ASC LIMIT " + to_string(limit);
    return Db::update(sql);
}