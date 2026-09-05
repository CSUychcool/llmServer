#include "Log.h"
#include "ControlHandler.h"
#include "ChatHandler.h"
#include "AuthHandler.h"
#include "Api.h"
#include <json/json.h>
#include <json/reader.h>
#include <json/writer.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// start_model.sh 的绝对路径, 参数是模型选择: ollama-qwen2.5 / ollama-qwen3 / vllm-awq
static const char* kStartScript =
    "/home/yc_21/server_ddz/llm-server/start_model.sh";

// ---- 与 web 前端约定的协议 ----
// 允许的 (backend, model) 组合 -> start_model.sh 参数
struct ModelEntry {
    const char* backend;
    const char* model;
    const char* choice;
};
static const ModelEntry kModelTable[] = {
    {"ollama", "qwen2.5:7b-instruct-q4_K_M", "ollama-qwen2.5"},
    {"ollama", "qwen3:8b",                   "ollama-qwen3"},
    {"vllm",   "Qwen2.5-7B-Instruct-AWQ",    "vllm-awq"},
};

// sendJson 统一由 Api.h 提供 (inline)
void ControlHandler::handle(int wfd, const std::string& url, const std::string& token,
                            const char* body, size_t bodyLen) {
    // 控制接口需登录
    if (AuthHandler::userIdForToken(token) < 0) { sendUnauthorized(wfd); return; }

    if (url == "/api/control/status") {
        handleStatus(wfd);
    } else if (url == "/api/control/switch") {
        handleSwitch(wfd, body, bodyLen);
    } else {
        Json::Value err;
        err["code"] = 404;
        err["msg"] = "unknown control endpoint: " + url;
        sendJson(wfd, 404, "Not Found", err);
    }
}

void ControlHandler::handleStatus(int wfd) {
    // 直接读 ChatHandler 当前生效的静态配置
    Json::Value resp;
    resp["code"] = 0;
    resp["backend"] = (ChatHandler::config.upstreamPort == 8000) ? "vllm" : "ollama";
    resp["model"] = ChatHandler::config.model;
    resp["upstream_url"] =
        "http://" + ChatHandler::config.upstreamHost + ":" +
        std::to_string(ChatHandler::config.upstreamPort);
    resp["upstream_path"] = ChatHandler::config.upstreamPath;
    sendJson(wfd, 200, "OK", resp);
}

void ControlHandler::handleSwitch(int wfd, const char* body, size_t bodyLen) {
    Json::Value req;
    Json::Reader reader;
    if (!reader.parse(std::string(body, bodyLen), req)) {
        Json::Value err;
        err["code"] = 400;
        err["msg"] = "invalid JSON";
        sendJson(wfd, 400, "Bad Request", err);
        return;
    }

    std::string backend = req.get("backend", "ollama").asString();
    std::string model   = req.get("model", "").asString();

    const char* choice = nullptr;
    for (const auto& e : kModelTable) {
        if (backend == e.backend && model == e.model) {
            choice = e.choice;
            break;
        }
    }
    if (choice == nullptr) {
        Json::Value err;
        err["code"] = 400;
        err["msg"] = "unsupported backend/model: " + backend + "/" + model;
        err["supported"] = "ollama/qwen2.5:7b-instruct-q4_K_M, "
                           "ollama/qwen3:8b, vllm/Qwen2.5-7B-Instruct-AWQ";
        sendJson(wfd, 400, "Bad Request", err);
        return;
    }

    // 先返回成功响应, 再由子进程延迟执行切换脚本 (脚本会杀掉本进程)
    Json::Value resp;
    resp["code"] = 0;
    resp["msg"] = "switching to " + backend + "/" + model;
    sendJson(wfd, 200, "OK", resp);

    tprintf("[ControlHandler] switching to %s (backend=%s model=%s), resp sent to client\n",
           choice, backend.c_str(), model.c_str());
    fflush(stdout);

    // fork 子进程: 等 1s 让客户端收到响应, 再执行脚本
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程: 脱离会话, 重建 stdin/out/err, exec 脚本
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        sleep(1);
        execl(kStartScript, "start_model.sh", choice, (char*)nullptr);
        _exit(127);  // exec 失败
    }
    // 父进程继续生活, 等脚本 kill 自己并重启新 llm-server
    if (pid > 0) {
        tprintf("[ControlHandler] forked switcher pid=%d, will restart shortly\n", pid);
        fflush(stdout);
    }
}