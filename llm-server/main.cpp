#include "Log.h"
#include "net/HttpServer.h"
#include "AppConfig.h"
#include "Db.h"
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char* argv[]) {
    // Chdir to server root (便于相对路径 & start_model.sh 的工作预期)
    chdir("/home/yc_21/server_ddz");

    std::string configPath = "/home/yc_21/server_ddz/llm-server/config.ollama.json";
    int cliPort = -1, cliThreads = -1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i+1 < argc) cliPort = atoi(argv[++i]);
        else if (arg == "--threads" && i+1 < argc) cliThreads = atoi(argv[++i]);
        else if (arg == "--config" && i+1 < argc) configPath = argv[++i];
        else if (arg == "--help") {
            tprintf("Usage: %s [--port PORT] [--threads N] [--config PATH]\n", argv[0]);
            return 0;
        }
    }

    // 统一配置: 单次解析 (upstream / db / web_root / 模型控制)
    if (!AppConfig::load(configPath.c_str())) {
        fprintf(stderr, "[Main] config load failed: %s\n", configPath.c_str());
        return 1;
    }

    unsigned short port = AppConfig::get().port;
    int threads = AppConfig::get().threads;
    if (cliPort >= 0) port = (unsigned short)cliPort;       // CLI 覆盖 config
    if (cliThreads >= 0) threads = cliThreads;

    tprintf("[Main] Starting LLM Server on port %d, %d worker threads, config=%s\n",
           port, threads, configPath.c_str());

    // 数据库 (AppConfig 已装载), 失败则拒绝启动
    if (!Db::init()) {
        fprintf(stderr, "[Main] DB init failed — check 'db' section in config: %s\n", configPath.c_str());
        return 1;
    }

    // Create and run server
    HttpServer* server = new HttpServer(port, threads);
    server->run();

    delete server;
    return 0;
}