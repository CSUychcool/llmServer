#include "Log.h"
#include "ControlHandler.h"
#include "net/HttpContext.h"
#include "service/ModelService.h"
#include <json/json.h>
#include <json/reader.h>

// /api/control/* HTTP 层: 模型状态与切换 (鉴权由 Router 中间件完成)
void ControlHandler::handle(HttpContext& ctx) {

    if (ctx.url == "/api/control/status") {
        ctx.resp.sendJson(200, "OK", ModelService::status());
        return;
    }

    if (ctx.url == "/api/control/switch") {
        Json::Value req;
        Json::Reader reader;
        if (!reader.parse(std::string(ctx.body ? ctx.body : "", ctx.bodyLen), req)) {
            Json::Value err;
            err["code"] = 400;
            err["msg"] = "invalid JSON";
            ctx.resp.sendJson(400, "Bad Request", err);
            return;
        }

        std::string backend = req.get("backend", "ollama").asString();
        std::string model   = req.get("model", "").asString();
        std::string choice  = ModelService::matchChoice(backend, model);

        if (choice.empty()) {
            Json::Value err;
            err["code"] = 400;
            err["msg"] = "unsupported backend/model: " + backend + "/" + model;
            err["supported"] = "ollama/qwen2.5:7b-instruct-q4_K_M, "
                               "ollama/qwen3:8b, vllm/Qwen2.5-7B-Instruct-AWQ";
            ctx.resp.sendJson(400, "Bad Request", err);
            return;
        }

        // 先回成功响应(Response 发送即关闭 fd), 再后台重启脚本
        Json::Value resp;
        resp["code"] = 0;
        resp["msg"] = "switching to " + backend + "/" + model;
        ctx.resp.sendJson(200, "OK", resp);

        ModelService::restartInBackground(choice);
        return;
    }

    Json::Value err;
    err["code"] = 404;
    err["msg"] = "unknown control endpoint: " + ctx.url;
    ctx.resp.sendJson(404, "Not Found", err);
}