#pragma once

#include <string>
#include <utility>
#include "http_common/Response.h"

// 一次请求的上下文: 网络层解包后统一打包交给 Handler。
// uid 由路由层的 requireAuth 中间件填充 (公开接口为 -1)
struct HttpContext {
    std::string method;
    std::string url;
    std::string token;      // Authorization 去掉 "Bearer " 前缀后的值
    long long uid = -1;     // 鉴权通过后的用户 id
    const char* body;       // 请求体 (GET 为空串)
    size_t bodyLen;
    Response resp;          // 响应出口: Handler 通过这里写回

    HttpContext(int fd, std::string m, std::string u, std::string t,
                const char* b, size_t l)
        : method(std::move(m)), url(std::move(u)), token(std::move(t)),
          body(b), bodyLen(l), resp(fd) {}

    HttpContext(const HttpContext&) = delete;
    HttpContext& operator=(const HttpContext&) = delete;
};