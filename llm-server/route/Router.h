#pragma once

#include "net/HttpContext.h"

// 路由 + 鉴权中间件: 统一在这里决定"哪些路径需要登录",
// 并把通过校验的 uid 写入 HttpContext。Handler 只信任 ctx.uid, 不再各自查 token。
class Router {
public:
    static void dispatch(HttpContext& ctx);

private:
    // 校验 token: 成功填充 ctx.uid; 失败发 401 并返回 false
    static bool requireAuth(HttpContext& ctx);
};