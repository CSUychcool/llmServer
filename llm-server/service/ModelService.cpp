#include "ModelService.h"
#include "Log.h"
#include "config/AppConfig.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstdlib>

Json::Value ModelService::status() {
    const AppConfig& c = AppConfig::get();
    Json::Value resp;
    resp["code"] = 0;
    resp["backend"] = (c.upstreamPort == 8000) ? "vllm" : "ollama";
    resp["model"] = c.model;
    resp["upstream_url"] = "http://" + c.upstreamHost + ":" + std::to_string(c.upstreamPort);
    resp["upstream_path"] = c.upstreamPath;
    resp["context_window"] = c.contextWindow;
    resp["reserve_output_tokens"] = c.reserveOutputTokens;
    return resp;
}

std::string ModelService::matchChoice(const std::string& backend, const std::string& model) {
    for (const auto& e : AppConfig::get().models()) {
        if (backend == e.backend && model == e.model) return e.choice;
    }
    return "";
}

void ModelService::restartInBackground(const std::string& choice) {
    const std::string& script = AppConfig::get().startScript;
    tprintf("[ModelService] switching via %s %s\n", script.c_str(), choice.c_str());
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        sleep(1);   // 等客户端收到切换响应
        execl(script.c_str(), "start_model.sh", choice.c_str(), (char*)nullptr);
        _exit(127); // exec 失败
    }
    if (pid > 0) {
        tprintf("[ModelService] forked switcher pid=%d, will restart shortly\n", pid);
        fflush(stdout);
    }
}