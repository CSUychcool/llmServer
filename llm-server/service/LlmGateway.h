#pragma once

#include <string>
#include <json/json.h>

class Response;

// 上游大模型网关: 连接 Ollama/vLLM, 转发 OpenAI 风格请求, 把 SSE 流式回传给客户端,
// 并累积完整回复。OPENAI 请求体的组装(messages)在上层 Handler; 网关只做"传输+解析"。
class LlmGateway {
public:
    // 上游 host/port/path 由 AppConfig 提供
    // 成功返回 true; 完整回复回填 aiFull; SSE 经 resp 写回
    static bool chatStream(const Json::Value& openaiReq, Response& resp, std::string& aiFull);
};