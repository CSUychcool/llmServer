#pragma once

#include <string>

// /api/convs 相关路由: 对话列表/新建/改名/删除 + 消息查询
class ConvHandler {
public:
    // url 形如 /api/convs、/api/convs/rename 等; token 已去除 "Bearer " 前缀
    static void handle(int wfd, const std::string& method, const std::string& url,
                       const std::string& token, const char* body, size_t bodyLen);
};