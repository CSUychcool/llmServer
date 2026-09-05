#pragma once

#include <string>

// 粗糙 token 估算 (Qwen 系列口径):
//   - CJK/宽字符: 1 token / 字
//   - ASCII: 1 token / 4 字符 (向上取整)
// 用途: 服务端按固定窗口预算从最新往最旧装历史, 避免超窗被上游静默截断。
// 前端 JS 保持同一公式用于显示 (见 index.html estimateTokens)。
class TokenCounter {
public:
    static int estimateTokens(const std::string& s);
};