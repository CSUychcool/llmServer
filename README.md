# llmServer

自托管、跑在你本地 GPU 上的 AI 助手，前后端一体、零第三方依赖前端。

**仓库内容**：`llm-server/`（自研 C++ HTTP 服务）+ 它编译所需的基础设施目录 `common/ http/ reactor/ thread/ crypto/`。

## 架构

```
index.html (单文件前端, 深空主题)
   └─ http://localhost:9000 → llm-server (C++17 自研 HTTP 服务器)
                        ├─ /api/chat    -> 转发 Ollama(11434) 或 vLLM(8000), SSE 流式
                        ├─ /api/auth/*  -> 注册/登录/Token 会话 (MySQL)
                        ├─ /api/convs*  -> 对话/消息 CRUD (MySQL)
                        ├─ /api/control/* -> 模型状态/切换
                        └─ GET /         -> 返回 index.html (配置 web_root)
```

## 特性

- SSE 流式回复 · Markdown 渲染 · 代码一键复制
- 服务器端账号系统：注册 / 登录 / Token 会话 / 退出，密码加盐 SHA-256 落库
- 对话与消息全部存 MySQL → 刷新不丢、跨设备同步
- 多会话列表 · 首条消息自动命名 · 一键导出
- 同源 API 自适应 → 本地 `file://` 或 frp/Cloudflare Tunnel 公网 HTTPS 都免配置
- 模型一键切换（Ollama ↔ vLLM，切换自动重启服务，会话不丢）

## 技术栈

C++17 · 自研事件驱动 HTTP（Epoll + 线程池）· jsoncpp · MySQL 8 · OpenSSL(SHA-256) · 原生 JavaScript

## 构建

依赖：g++ / cmake / `libmysqlclient-dev` / `libjsoncpp-dev` / OpenSSL。

```bash
cmake -S llm-server -B build
cmake --build build -j4
# 产物在 build/llm-server
```

> 本仓库 `llm-server/CMakeLists.txt` 的 `EXECUTABLE_OUTPUT_PATH` 保留了本机路径 `/home/yc_21/server_ddz/bin`；克隆回来后按需修改该行即可。

## 运行

1. 准备 MySQL 库与账号，把 `llm-server/config.ollama.example.json` 复制为 `config.ollama.json` 并填入 `db` 段（host/port/user/password/database），首次启动会自动建表
2. 启动上游：Ollama（`ollama serve`，默认 11434）或 vLLM
3. 启动服务：`llm-server --config llm-server/config.ollama.json`，默认监听 9000
4. 浏览器打开 `index.html`，注册账号后即可使用；聊天记录与账号都在服务器 MySQL 中

> 注意：`main.cpp` 与 `ControlHandler.cpp` 中保留了本机绝对路径（工作目录、重启脚本 `start_model.sh`、`web_root` 指向 `/mnt/c/Users/yc_21/llm-chat`），按你的环境修改。

## 安全提醒

将服务暴露到公网前（frp/隧道）至少做到：所有 `/api/*` 均已要求登录（Token），但请确认前端页面不包含涉密内容，并按需加一层 Basic Auth / 放行 IP。