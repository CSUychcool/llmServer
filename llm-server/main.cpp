#include "Log.h"
#include "LlmServer.h"
#include "ChatHandler.h"
#include "Db.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>

static std::string readConfig(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[main] Cannot open %s, using defaults\n", path);
        return "{}";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char* argv[]) {
    // Chdir to server root
    chdir("/home/yc_21/server_ddz");

    unsigned short port = 9000;
    int threads = 4;
    std::string configPath = "/home/yc_21/server_ddz/llm-server/config.ollama.json";

    // Parse simple config from stdin or args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i+1 < argc) port = atoi(argv[++i]);
        else if (arg == "--threads" && i+1 < argc) threads = atoi(argv[++i]);
        else if (arg == "--config" && i+1 < argc) configPath = argv[++i];
        else if (arg == "--help") {
            tprintf("Usage: %s [--port PORT] [--threads N] [--config PATH]\n", argv[0]);
            return 0;
        }
    }

    tprintf("[Main] Starting LLM Server on port %d, %d worker threads, config=%s\n",
           port, threads, configPath.c_str());

    // Load upstream config (Ollama/vLLM 上游配置)
    ChatHandler::parseConfig(configPath.c_str());

    // 初始化数据库 (用户/会话/对话/消息), 失败则拒绝启动
    if (!Db::initFromConfig(configPath.c_str())) {
        fprintf(stderr, "[Main] DB init failed — check 'db' section in config: %s\n", configPath.c_str());
        return 1;
    }

    // Create and run LlmServer
    LlmServer* server = new LlmServer(port, threads);
    server->run();

    delete server;
    return 0;
}
