#include "Log.h"
#include "ChatHandler.h"
#include "AuthHandler.h"
#include "Db.h"
#include "Api.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <json/writer.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>
#include <fstream>
#include <vector>
#include <utility>

// 定义静态成员
LlmConfig ChatHandler::config;

bool ChatHandler::parseConfig(const char* configFile) {
    std::ifstream f(configFile);
    if (!f.is_open()) {
        fprintf(stderr, "[ChatHandler] cannot open config %s\n", configFile ? configFile : "(null)");
        return false;
    }
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(f, root)) {
        fprintf(stderr, "[ChatHandler] config parse failed\n");
        return false;
    }
    // upstream_url 形如 http://127.0.0.1:11434
    std::string url = root.get("upstream_url", "http://127.0.0.1:11434").asString();
    // 简单解析 host:port
    size_t sp = url.find("://");
    if (sp != std::string::npos) {
        std::string rest = url.substr(sp + 3);
        size_t colon = rest.find(':');
        if (colon != std::string::npos) {
            config.upstreamHost = rest.substr(0, colon);
            config.upstreamPort = atoi(rest.substr(colon + 1).c_str());
        } else {
            config.upstreamHost = rest;
            config.upstreamPort = 443;
        }
    }
    config.upstreamPath = root.get("upstream_path", config.upstreamPath).asString();
    config.model = root.get("model", config.model).asString();
    config.webRoot = root.get("web_root", config.webRoot).asString();
    return true;
}

// ---- 静态网页服务: GET / -> webRoot/index.html (内网穿透场景, 让单个端口同时提供网页+API) ----

// 尝试从 webRoot 读取 url 对应的文件并作为 HTTP 响应发出去。
// 命中则本函数负责发送并 close(wfd), 返回 true; 未命中(或非 GET)返回 false 走正常业务。
static bool serveWebRootFile(int wfd, const char* method, const char* url) {
    std::string webRoot = ChatHandler::config.webRoot;
    if (webRoot.empty()) return false;
    if (!method || strcmp(method, "GET") != 0) return false;

    std::string u = url ? url : "/";
    // 去掉 query 参数(?后面的内容不属于路径)
    size_t q = u.find('?');
    if (q != std::string::npos) u = u.substr(0, q);
    // 防御: 拒绝路径穿越
    if (u.find("..") != std::string::npos) return false;
    // 目录根 -> index.html
    if (u.empty() || u == "/") u = "/index.html";

    if (!u.empty() && u[0] == '/') {
        std::string path = webRoot + u;
        std::ifstream f(path, std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            std::string body = ss.str();
            // 简单按扩展名区分类型
            const char* ctype = "text/html; charset=utf-8";
            if (u.rfind(".css") != std::string::npos)      ctype = "text/css; charset=utf-8";
            else if (u.rfind(".js")  != std::string::npos) ctype = "text/javascript; charset=utf-8";
            else if (u.rfind(".png") != std::string::npos) ctype = "image/png";
            else if (u.rfind(".jpg") != std::string::npos || u.rfind(".jpeg") != std::string::npos)
                ctype = "image/jpeg";
            else if (u.rfind(".svg") != std::string::npos) ctype = "image/svg+xml";
            char head[256];
            int hl = snprintf(head, sizeof(head),
                "HTTP/1.1 200 OK\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                ctype, body.size());
            ::send(wfd, head, hl, MSG_NOSIGNAL);
            // 大文件分批发送
            size_t off = 0;
            while (off < body.size()) {
                int n = ::send(wfd, body.data() + off, body.size() - off, MSG_NOSIGNAL);
                if (n <= 0) break;
                off += (size_t)n;
            }
            ::close(wfd);
            tprintf("[ChatHandler] wfd=%d served static %s -> %s (%zu bytes)\n",
                    wfd, u.c_str(), path.c_str(), body.size());
            fflush(stdout);
            return true;
        }
    }
    return false;  // 静态文件不存在, 交还正常业务路由
}

static void sendChunkedData(const std::string& chunk, int fd) {
    // SSE streaming: data: {...}\n\n
    std::string payload = "data: ";
    payload += chunk;
    payload += "\n\n";
    ::send(fd, payload.c_str(), payload.size(), MSG_NOSIGNAL);
}

static void sendError(int wfd, int code, const char* reason, const char* msg) {
    char head[128];
    snprintf(head, sizeof(head),
             "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
             code, reason);
    // Need to build error body
    Json::Value err;
    err["error"] = msg;
    std::string body = Json::FastWriter().write(err);
    std::string resp = head;
    resp += body;
    ::send(wfd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
    ::close(wfd);
}

// 去除 HTTP chunked 编码残留的帧头（hex 长度行）与 CRLF,
// 让 block 从纯 SSE 的 "data: ..." 开始。
static void stripChunkedFraming(std::string& s) {
    while (true) {
        // 剥离残留的 \r\n
        if (s.rfind("\r\n", 0) == 0) { s.erase(0, 2); continue; }
        if (!s.empty() && s[0] == '\n') { s.erase(0, 1); continue; }
        // 剥离形如 "1a3\r\n" 的 chunk 长度行
        size_t nl = s.find("\r\n");
        if (nl != std::string::npos && nl > 0) {
            bool allHex = true;
            for (size_t i = 0; i < nl; ++i)
                if (!isxdigit((unsigned char)s[i])) { allHex = false; break; }
            if (allHex) { s.erase(0, nl + 2); continue; }
        }
        break;
    }
}

void ChatHandler::handle(int wfd, const char* method, const char* url, const char* token,
                         const char* rawBody, size_t bodyLen) {
    tprintf("[ChatHandler] wfd=%d handle() start, method=%s url=%s bodyLen=%zu\n",
           wfd, method ? method : "(null)", url ? url : "(null)", bodyLen);
    fflush(stdout);

    // 静态网页: GET /xxx(在 webRoot 下存在) -> 直接返回文件。命中即已 close(wfd)
    if (serveWebRootFile(wfd, method, url)) return;

    // /api/chat 必须登录
    if (!url || strncmp(url, "/api/chat", 9) != 0) {
        sendErr(wfd, "unknown route");
        return;
    }
    long long uid = AuthHandler::userIdForToken(token ? token : "");
    if (uid < 0) { sendUnauthorized(wfd); return; }

    // 其余按原逻辑: POST /api/chat 聊天业务
    Json::Value req;
    Json::Reader reader;
    // Correct signature: parse(input, root) where input is std::string or iterator pair
    if (!reader.parse(std::string(rawBody, bodyLen), req)) {
        tprintf("[ChatHandler] wfd=%d ERROR: invalid JSON body\n", wfd);
        sendError(wfd, 400, "Bad Request", "Invalid JSON body");
        return;
    }

    std::string prompt = req["prompt"].asString();
    std::string systemPrompt = req.get("system_prompt", "You are a helpful AI assistant.").asString();
    tprintf("[ChatHandler] wfd=%d parsed: prompt=(%zu chars) system_prompt=(%zu chars)\n",
           wfd, prompt.size(), systemPrompt.size());

    // ---- 对话持久化 + 历史来源 ----
    // conv_id > 0: 该校验对话归属、把 user/AI 消息写库, 历史优先从 DB 取最近 20 条
    long long convId = atoll(req.get("conv_id", "0").asString().c_str());
    bool persist = convId > 0;
    std::string userMsgId;   // 本次 user 消息行 id (用于"取该条之前的历史")
    if (persist) {
        std::string ow = "SELECT id FROM conversations WHERE id=" + std::to_string(convId) +
                         " AND user_id=" + std::to_string(uid);
        if (!Db::query(ow) || !Db::next()) { sendErr(wfd, "对话不存在或无权访问"); return; }
        std::string ms = "INSERT INTO messages(conv_id, role, content) VALUES(" +
                         std::to_string(convId) + ",'user','" + Db::escape(prompt) + "')";
        long long mid = Db::insert(ms);
        if (mid > 0) userMsgId = std::to_string(mid);
        Db::update("UPDATE conversations SET updated_at=NOW() WHERE id=" + std::to_string(convId));
    }

    // Build OpenAI messages array
    Json::Value messages(Json::arrayValue);

    if (!systemPrompt.empty()) {
        Json::Value sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt;
        messages.append(sysMsg);
    }

    // Add history messages (前端显式传则用之, 否则从 DB 取最近 20 条)
    Json::Value hist = req.get("history", Json::arrayValue);
    if (hist.isArray() && hist.size() > 0) {
        tprintf("[ChatHandler] wfd=%d history messages: %u\n", wfd, hist.size());
        for (Json::ArrayIndex i = 0; i < hist.size(); ++i) {
            messages.append(hist[i]);
        }
    } else if (persist) {
        std::string hq = "SELECT role, content FROM messages WHERE conv_id=" +
                         std::to_string(convId) + " AND id < " +
                         (userMsgId.empty() ? "18446744073709551615" : userMsgId) +
                         " ORDER BY id DESC LIMIT 20";
        std::vector<std::pair<std::string, std::string> > hs;   // 先倒序收集再倒回
        if (Db::query(hq)) {
            while (Db::next()) hs.push_back(std::make_pair(Db::value(0), Db::value(1)));
        }
        for (auto it = hs.rbegin(); it != hs.rend(); ++it) {
            Json::Value m;
            m["role"] = it->first;
            m["content"] = it->second;
            messages.append(m);
        }
        tprintf("[ChatHandler] wfd=%d db history loaded: %zu\n", wfd, hs.size());
        fflush(stdout);
    }

    // Add current user message
    Json::Value userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    messages.append(userMsg);

    // Create the full OpenAI request JSON
    Json::Value openaiReq;
    openaiReq["model"] = config.model;
    openaiReq["messages"] = messages;
    openaiReq["stream"] = true;
    openaiReq["temperature"] = 0.7;

    std::string requestBody = Json::FastWriter().write(openaiReq);

    // 记录转发的上游请求
    tprintf("[ChatHandler] wfd=%d upstream request: model=%s messages=%u stream=true body=%zu bytes\n",
           wfd, config.model.c_str(), messages.size(), requestBody.size());
    fflush(stdout);

    // Connect to upstream (Ollama/vLLM)
    std::string host = config.upstreamHost;
    int port = config.upstreamPort;
    std::string path = config.upstreamPath;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[ChatHandler] socket");
        sendError(wfd, 500, "Internal Server Error", "socket() failed");
        return;
    }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &srv.sin_addr) != 1) {
        tprintf("[ChatHandler] wfd=%d ERROR: cannot resolve upstream host %s\n", wfd, host.c_str());
        sendError(wfd, 502, "Bad Gateway", "cannot resolve upstream host");
        ::close(sock);
        return;
    }

    // Blocking connect (short timeout via SO_SNDTIMEO)
    tprintf("[ChatHandler] wfd=%d connecting upstream %s:%d ...\n", wfd, host.c_str(), port);
    fflush(stdout);
    if (::connect(sock, (sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("[ChatHandler] connect");
        tprintf("[ChatHandler] wfd=%d ERROR: cannot connect upstream %s:%d\n", wfd, host.c_str(), port);
        sendError(wfd, 502, "Bad Gateway", "cannot connect upstream");
        ::close(sock);
        return;
    }
    tprintf("[ChatHandler] wfd=%d upstream CONNECTED\n", wfd);
    fflush(stdout);

    // Send HTTP POST request to upstream
    std::string requestLine = "POST " + path + " HTTP/1.1\r\n";
    requestLine += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    requestLine += "Content-Type: application/json\r\n";
    requestLine += "Content-Length: " + std::to_string(requestBody.size()) + "\r\n";
    requestLine += "Connection: close\r\n\r\n";

    ::send(sock, requestLine.c_str(), requestLine.size(), MSG_NOSIGNAL);
    ::send(sock, requestBody.c_str(), requestBody.size(), MSG_NOSIGNAL);
    tprintf("[ChatHandler] wfd=%d POST %s%s sent to upstream\n", wfd, host.c_str(),
           config.upstreamPath.c_str());
    fflush(stdout);

    // Send response header to client
    static const char respHeader[] =
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n\r\n";
    ::send(wfd, respHeader, sizeof(respHeader)-1, MSG_NOSIGNAL);
    tprintf("[ChatHandler] wfd=%d SSE header (200 OK, text/event-stream) sent to client\n", wfd);
    fflush(stdout);

    // Read SSE stream from upstream
    char readBuf[8192];
    std::string accum;
    bool done = false;
    int chunkCount = 0;      // 转发给客户端的 content chunk 数
    long bytesRelayed = 0;   // 转发总字节
    bool firstChunk = true;
    bool headersSkipped = false;  // 上游 HTTP 响应头是否已跳过
    std::string aiFull;           // 累积 AI 完整回复 (用于落库)

    while (!done) {
        int n = recv(sock, readBuf, sizeof(readBuf), 0);
        if (n <= 0) break;

        accum.append(readBuf, n);

        // 首次 recv 会连 HTTP 响应头一起读到, 先跳过响应头 (到 \r\n\r\n)
        if (!headersSkipped) {
            size_t he = accum.find("\r\n\r\n");
            if (he != std::string::npos) {
                accum.erase(0, he + 4);
                headersSkipped = true;
            }
        }

        // Process accumulated data line by line for SSE
        size_t pos;
        while (!done && (pos = accum.find("\n\n")) != std::string::npos) {
            std::string block = accum.substr(0, pos + 2);
            accum.erase(0, pos + 2);

            // Ollama 上游是 HTTP chunked 传输, 剥离 hex 长度行和残留 CRLF
            stripChunkedFraming(block);

            // Parse each data line in the block
            size_t dataPos;
            while ((dataPos = block.find("data: ")) != std::string::npos) {
                block.erase(dataPos, 6); // Remove "data: " prefix
                size_t endLine = block.find('\n');
                std::string dataChunk = (endLine != std::string::npos) ? block.substr(0, endLine) : block;
                block.erase(0, endLine != std::string::npos ? endLine + 1 : block.size());

                // Check for [DONE] marker
                if (dataChunk.find("\"[DONE]") != std::string::npos || dataChunk == "[DONE]") {
                    tprintf("[ChatHandler] wfd=%d received [DONE] from upstream\n", wfd);
                    sendChunkedData("[DONE]", wfd);
                    done = true;
                    break;
                }

                // Extract delta.content from the JSON chunk
                Json::Value chunkObj;
                Json::Reader chunkReader;
                if (chunkReader.parse(dataChunk, chunkObj)) {
                    Json::Value& choices = chunkObj["choices"];
                    if (choices.isArray() && choices.size() > 0) {
                        Json::Value& choice = choices[0];
                        Json::Value& delta = choice["delta"];
                        std::string content = delta.get("content", "").asString();
                        if (!content.empty()) {
                            if (firstChunk) {
                                tprintf("[ChatHandler] wfd=%d FIRST token chunk: \"%s...\"\n",
                                       wfd, content.substr(0, 20).c_str());
                                firstChunk = false;
                            }
                            chunkCount++;
                            bytesRelayed += (long)content.size();
                            aiFull += content;
                            sendChunkedData(content, wfd);
                        }
                    }
                } else {
                    // 诊断: 解析失败的原始 chunk
                    tprintf("[ChatHandler] wfd=%d DEBUG parse FAILED, raw=[%.160s]\n",
                           wfd, dataChunk.c_str());
                    fflush(stdout);
                }
            }
        }
    }

    // AI 回复落库 (与服务端已存的 user 消息配对)
    if (persist && !aiFull.empty()) {
        std::string as = "INSERT INTO messages(conv_id, role, content) VALUES(" +
                         std::to_string(convId) + ",'ai','" + Db::escape(aiFull) + "')";
        Db::update(as);
        Db::update("UPDATE conversations SET updated_at=NOW() WHERE id=" + std::to_string(convId));
        tprintf("[ChatHandler] wfd=%d persisted AI reply (%zu bytes)\n", wfd, aiFull.size());
        fflush(stdout);
    }

    ::close(sock);
    ::close(wfd);
    tprintf("[ChatHandler] wfd=%d COMPLETED: %d chunks / %ld bytes relayed, close(wfd) and upstream sock\n",
           wfd, chunkCount, bytesRelayed);
    fflush(stdout);
}