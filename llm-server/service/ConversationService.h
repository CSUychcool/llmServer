#pragma once

#include <string>
#include <vector>

// 会话数据行 (handler 层转 JSON)
struct ConvRow {
    long long id = 0;
    std::string title;
    std::string createdAt;
    std::string updatedAt;
    std::string summary;       // P1: 滚动压缩产生的历史摘要
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

    // 上下文管理: 取 beforeId 之前的历史, 从最新往最旧装, 累计估算 token 不超过 maxTokens。
    // outHasOlder: 是否存在"因预算装不下"而未被覆盖的旧消息 (P1 压缩 / P3 召回的触发依据)
    static bool loadHistoryByTokens(long long convId, long long beforeId, int maxTokens,
                                    int fetchLimit, std::vector<MsgRow>& out,
                                    bool& outHasOlder);

    // ---- 摘要 (P1) ----
    static bool getSummary(long long convId, std::string& out);
    // 读摘要同时返回摘要覆盖到的最后一条消息 id (summary_upto)
    static bool getSummaryEx(long long convId, std::string& out, long long& uptoMsgId);
    // 更新摘要同时记录"覆盖到的最后一条消息 id" (summary_upto)
    static bool setSummary(long long convId, const std::string& summary, long long uptoMsgId);
    // summary_upto 之后新增的消息条数 (afterId<=0 视为全量), 用于压缩触发节流
    static long long messagesSince(long long convId, long long uptoMsgId);

    // ---- 历史召回 (P3): 关键词粗召回, 供窗口外旧消息入上下文 ----
    static bool searchMessages(long long convId, const std::string& keyword,
                               int limit, std::vector<MsgRow>& out);

    // ---- 压缩素材 (P1) ----
    // 取最早 limit 条消息 (ORDER BY id ASC), 空返回 true
    static bool loadOldestMessages(long long convId, int limit, std::vector<MsgRow>& out,
                                   long long& outMaxId);
    // 删除最早 limit 条 (压缩入摘要后清理, 防止重复处理)
    static bool trimOldestMessages(long long convId, int limit);
};