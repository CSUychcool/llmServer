#pragma once

#include <string>
#include <json/json.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>

// llm-server 统一的裸 HTTP JSON 响应助手 (与 ControlHandler::sendJson 同风格)
inline void sendJson(int wfd, int code, const char* reason, const Json::Value& json) {
    char head[256];
    snprintf(head, sizeof(head),
             "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n\r\n",
             code, reason);
    std::string body = Json::FastWriter().write(json);
    std::string resp(head);
    resp += body;
    ::send(wfd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
    ::close(wfd);
}

// 业务错误: HTTP 200 + code!=0 (与 ControlHandler 错误约定一致)
inline void sendErr(int wfd, const char* msg) {
    Json::Value j;
    j["code"] = 1;
    j["msg"] = msg;
    sendJson(wfd, 200, "OK", j);
}

// 未登录: 一律 HTTP 401 (前端用状态码全局拦截回登录页)
inline void sendUnauthorized(int wfd) {
    Json::Value j;
    j["code"] = 401;
    j["msg"] = "未登录或登录已过期";
    sendJson(wfd, 401, "Unauthorized", j);
}