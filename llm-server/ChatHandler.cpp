#include "Log.h"
#include "ChatHandler.h"
#include "net/HttpContext.h"
#include "service/ConversationService.h"
#include "service/LlmGateway.h"
#include "service/TokenCounter.h"
#include "config/AppConfig.h"
#include <json/json.h>
#include <json/reader.h>
#include <cstdlib>
#include <vector>

void ChatHandler::handle(HttpContext& ctx) {
    // ---- 静态页面: 公开 ----
    if (ctx.method == "GET" &&
        ctx.resp.serveWebRootFile(AppConfig::get().webRoot, ctx.url)) {
        return;   // 已命中并由 Response 发送
    }

    // ---- 仅接受 POST /api/chat ----
    if (ctx.url.rfind("/api/chat", 0) != 0) { ctx.resp.sendErr("unknown route"); return; }

    // 鉴权已由 route/Router 中间件完成: ctx.uid 即当前用户

    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(std::string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
        ctx.resp.sendErr("参数格式错误"); return;
    }
    std::string prompt = req["prompt"].asString();
    std::string systemPrompt = req.get("system_prompt", "You are a helpful AI assistant.").asString();

    // ---- 持久化 user 消息 + 归属校验 ----
    long long convId = atoll(req.get("conv_id", "0").asString().c_str());
    bool persist = convId > 0;
    std::string userMsgId;   // 本次 user 消息行 id, 用于取"它之前"的历史
    long long uid = ctx.uid; // Router 中间件已鉴权
    if (persist) {
        if (!ConversationService::ownsConversation(uid, convId)) {
            ctx.resp.sendErr("对话不存在或无权访问"); return;
        }
        long long mid = ConversationService::appendMessage(convId, "user", prompt);
        if (mid > 0) userMsgId = std::to_string(mid);
    }

    // ---- 组装 OpenAI messages (system + history + user) ----
    Json::Value messages(Json::arrayValue);
    if (!systemPrompt.empty()) {
        Json::Value sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt;
        messages.append(sysMsg);
    }
    Json::Value hist = req.get("history", Json::arrayValue);
    if (hist.isArray() && hist.size() > 0) {
        for (Json::ArrayIndex i = 0; i < hist.size(); ++i) messages.append(hist[i]);
    } else if (persist) {
        // 预算分配: 窗口 - 输出预留 - 系统提示 - 本次提问 = 可装历史
        int budget = AppConfig::get().usableHistoryTokens()
                     - TokenCounter::estimateTokens(systemPrompt)
                     - TokenCounter::estimateTokens(prompt);
        std::vector<MsgRow> hs;
        bool hasOlder = false;
        ConversationService::loadHistoryByTokens(convId,
            userMsgId.empty() ? 0 : atoll(userMsgId.c_str()),
            budget, AppConfig::get().historyFetchLimit, hs, hasOlder);
        for (const auto& h : hs) {
            Json::Value m;
            m["role"] = h.role;
            m["content"] = h.content;
            messages.append(m);
        }
        tprintf("[ChatHandler] db history loaded: %zu msgs (budget %d tok)\n", hs.size(), budget);
        fflush(stdout);
    }
    Json::Value userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    messages.append(userMsg);

    // ---- 调用上游网关 (SSE 流式回传 + 累积 AI 回复) ----
    Json::Value openaiReq;
    openaiReq["model"] = AppConfig::get().model;
    openaiReq["messages"] = messages;
    openaiReq["stream"] = true;
    openaiReq["temperature"] = 0.7;

    std::string aiFull;
    LlmGateway::chatStream(openaiReq, ctx.resp, aiFull);

    // ---- AI 回复落库 ----
    if (persist && !aiFull.empty()) {
        ConversationService::appendMessage(convId, "ai", aiFull);
        tprintf("[ChatHandler] persisted AI reply (%zu bytes)\n", aiFull.size());
        fflush(stdout);
    }
    // Response 已由网关在退出时关闭, 无需再处理
}