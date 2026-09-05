#pragma once
#include "Buffer.h"
#include "HttpResponse.h"
#include <functional>
#include <map>
using namespace std;

// 当前的解析状态
enum class PrecessState:char
{
    ParseReqLine,
    ParseReqHeaders,
    ParseReqBody,
    ParseReqDone
};
// 定义http请求结构体
class HttpRequest
{
public:
    HttpRequest();
    ~HttpRequest();
    // 重置
    void reset();
    // 添加请求头
    void addHeader(const string key, const string value);
    // 根据key得到请求头的value
    string getHeader(const string key);
    // 解析请求行
    bool parseRequestLine(Buffer* readBuf);
    // 解析请求头
    bool parseRequestHeader(Buffer* readBuf);
    // 解析请求体（POST body）——从 readBuf 中读走 Content-Length 字节
    bool parseRequestBody(Buffer* readBuf);
    // 解析http请求协议
    bool parseHttpRequest(Buffer* readBuf, HttpResponse* response, Buffer* sendBuf, int socket);
    // 处理http请求协议
    bool processHttpRequest(HttpResponse* response);
    // 解码字符串
    string decodeMsg(string from);
    const string getFileType(const string name);
    static void sendDir(string dirName, Buffer* sendBuf, int cfd);
    static void sendFile(string dirName, Buffer* sendBuf, int cfd);
    inline void setMethod(string method)
    {
        m_method = method;
    }
    inline void seturl(string url)
    {
        m_url = url;
    }
    inline void setVersion(string version)
    {
        m_version = version;
    }
    // 获取处理状态
    inline PrecessState getState()
    {
        return m_curState;
    }
    inline void setState(PrecessState state)
    {
        m_curState = state;
    }
    // GET body
    inline string getBody()
    {
        return m_body;
    }
    inline const string& getMethod() const { return m_method; }
    inline const string& getUrl() const { return m_url; }
    // 路由钩子 — POST /api/chat 等自定义接口注入
    using RouterFunc = function<void(const string& body, HttpResponse*)>;
    inline void setRouter(RouterFunc fn)
    {
        m_router = move(fn);
    }

private:
    char* splitRequestLine(const char* start, const char* end,
        const char* sub, function<void(string)> callback);
    int hexToDec(char c);

private:
    string m_method;
    string m_url;
    string m_version;
    string m_body;                 // POST body（补充）
    map<string, string> m_reqHeaders;
    PrecessState m_curState;
    int curContentLength = 0;       // Content-Length 值（补充）
    RouterFunc m_router;            // 自定义路由回调（补充）
};