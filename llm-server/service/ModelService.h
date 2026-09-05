#pragma once

#include <string>
#include <json/json.h>

// 模型控制领域: 状态快照 / 切换组合校验 / 后台重启
// (原 ControlHandler 内 fork+exec 逻辑; HTTP 层不平移到这里)
class ModelService {
public:
    // 当前生效模型快照 {code:0, backend, model, upstream_url, upstream_path,
    //                   context_window, reserve_output_tokens}
    static Json::Value status();

    // 校验 (backend, model) 是否可切换; 合法返回对应 start_model.sh 参数, 否则空串
    static std::string matchChoice(const std::string& backend, const std::string& model);

    // fork 子进程延迟执行 start_model.sh <choice> (子进程脱离会话/重建 fd)
    static void restartInBackground(const std::string& choice);
};