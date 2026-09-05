//
// Created by subingwen
//

#ifndef SERVER_DDZ_JSONPARSE_H
#define SERVER_DDZ_JSONPARSE_H
#include <string>
#include <jsoncpp/json/json.h>
#include <memory>

// 定义存储数据的结构体
struct DBInfo
{
    std::string ip;
    unsigned  short port;
    std::string user;
    std::string passwd;
    std::string dbname;
};

class JsonParse
{
public:
    enum DBType{Mysql, Redis};
    JsonParse(std::string fileName = "./config/config.json");
    // 获取数据
    std::shared_ptr<DBInfo> getDatabaseInfo(DBType type);
private:
    Json::Value m_root;
};


#endif //SERVER_DDZ_JSONPARSE_H
