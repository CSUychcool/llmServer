#pragma once

#include <string>
#include <vector>

// 会话数据行 (handler 层转 JSON)
struct ConvRow {
    long long id = 0;
    std::string title;
    std::string createdAt;
    std::string updatedAt;
};

// 消息行
struct MsgRow {
    std::string role;       // "user" | "ai"
    std::string content;
    std::string createdAt;
};

// 会话领域: conversations + messages 的表 CRUD 与查询
// 所有 SQL 只出现在此层与 Db 之间; Handler 不得直接拼 SQL
class ConversationService {
public:
    // ---- 对话 ----
    static bool list(long long uid, std::vector<ConvRow>& out);         // 按 updated_at 降序
    // 新建对话 (title 空则默认"新对话"), 成功填充 out 并返回 true
    static bool create(long long uid, const std::string& title, ConvRow& out);
    static bool rename(long long uid, long long convId, const std::string& title);
    static bool remove(long long uid, long long convId);                 // 级联删消息
    static bool ownsConversation(long long uid, long long convId);

    // ---- 消息 ----
    // 取某对话全部分组消息 (按 id 升序); 非本人对话返回 false
    static bool loadMessages(long long uid, long long convId, std::vector<MsgRow>& out);
    // 追加一条消息并 touch 对话的 updated_at; 成功返回该行 id, 失败 -1
    static long long appendMessage(long long convId, const std::string& role,
                                   const std::string& content);
    // 取某对话"某条消息之前"的最近 limit 条历史 (按 id 升序返回), 供模型上下文
    static bool loadHistory(long long convId, long long beforeId, int limit,
                            std::vector<MsgRow>& out);
};