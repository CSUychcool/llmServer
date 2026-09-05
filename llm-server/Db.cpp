#include "Db.h"
#include "Log.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <mysql.h>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <cstring>

// ---------------- 内部状态 ----------------
static MYSQL*             g_conn    = nullptr;   // MySQL 连接(单连接, 互斥保护)
static MYSQL_RES*         g_result  = nullptr;   // 最近一次 SELECT 的结果集
static MYSQL_ROW          g_row     = nullptr;   // 当前行
static std::mutex         g_mutex;               // 串行化所有 DB 操作
static std::string        g_host    = "127.0.0.1";
static unsigned short     g_port    = 3306;
static std::string        g_user    = "llm_chat";
static std::string        g_pass;
static std::string        g_db      = "llm_chat";

static void freeResult() {
    if (g_result) { mysql_free_result(g_result); g_result = nullptr; }
    g_row = nullptr;
}

static bool doConnect() {
    if (g_conn) { mysql_close(g_conn); g_conn = nullptr; }
    g_conn = mysql_init(nullptr);
    if (!g_conn) return false;
    mysql_options(g_conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (!mysql_real_connect(g_conn, g_host.c_str(), g_user.c_str(), g_pass.c_str(),
                            g_db.c_str(), g_port, nullptr, 0)) {
        tprintf("[Db] connect failed: %s\n", mysql_error(g_conn));
        mysql_close(g_conn);
        g_conn = nullptr;
        return false;
    }
    tprintf("[Db] connected to %s:%d/%s as %s\n", g_host.c_str(), g_port, g_db.c_str(), g_user.c_str());
    fflush(stdout);
    return true;
}

// 建表 (幂等, 首次启动自动初始化)
static bool createTables() {
    static const char* ddl[] = {
        "CREATE TABLE IF NOT EXISTS users ("
            "id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
            "username VARCHAR(64) NOT NULL UNIQUE,"
            "email VARCHAR(128) NOT NULL UNIQUE,"
            "salt CHAR(32) NOT NULL,"
            "password_hash CHAR(64) NOT NULL,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        "CREATE TABLE IF NOT EXISTS sessions ("
            "token CHAR(64) PRIMARY KEY,"
            "user_id INT UNSIGNED NOT NULL,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "expires_at DATETIME NOT NULL,"
            "INDEX(user_id), INDEX(expires_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        "CREATE TABLE IF NOT EXISTS conversations ("
            "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
            "user_id INT UNSIGNED NOT NULL,"
            "title VARCHAR(120) NOT NULL DEFAULT '新对话',"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "INDEX(user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        "CREATE TABLE IF NOT EXISTS messages ("
            "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
            "conv_id BIGINT UNSIGNED NOT NULL,"
            "role ENUM('user','ai') NOT NULL,"
            "content MEDIUMTEXT NOT NULL,"
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "INDEX(conv_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
    };
    for (const char* sql : ddl) {
        if (mysql_real_query(g_conn, sql, (unsigned long)strlen(sql)) != 0) {
            tprintf("[Db] create table failed: %s\n", mysql_error(g_conn));
            fflush(stdout);
            return false;
        }
    }
    return true;
}

// ---------------- 对外接口 ----------------

bool Db::initFromConfig(const char* configFile) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ifstream f(configFile);
    Json::Value root;
    Json::Reader reader;
    if (!f.is_open() || !reader.parse(f, root)) {
        tprintf("[Db] cannot read config: %s\n", configFile ? configFile : "(null)");
        fflush(stdout);
        return false;
    }
    const Json::Value& db = root["db"];
    if (!db.isObject()) {
        tprintf("[Db] no 'db' section in %s\n", configFile ? configFile : "(null)");
        fflush(stdout);
        return false;
    }
    g_host = db.get("host", "127.0.0.1").asString();
    g_port = (unsigned short)db.get("port", 3306).asInt();
    g_user = db.get("user", "llm_chat").asString();
    g_pass = db.get("password", "").asString();
    g_db   = db.get("database", "llm_chat").asString();
    if (!doConnect()) return false;
    if (!createTables()) { freeResult(); mysql_close(g_conn); g_conn = nullptr; return false; }
    return true;
}

bool Db::isReady() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_conn != nullptr;
}

bool Db::ensureConnected() {
    if (g_conn) return true;
    return doConnect();
}

std::string Db::escape(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_conn) return s;
    char* buf = new char[s.size() * 2 + 1];
    long n = mysql_real_escape_string(g_conn, buf, s.data(), (unsigned long)s.size());
    std::string out = n >= 0 ? std::string(buf, (size_t)n) : s;
    delete[] buf;
    return out;
}

bool Db::update(const std::string& sql) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureConnected()) return false;
    if (mysql_real_query(g_conn, sql.data(), (unsigned long)sql.size()) != 0) {
        tprintf("[Db] update err: %s\n    sql=%.160s\n", mysql_error(g_conn), sql.c_str());
        fflush(stdout);
        if (!doConnect()) return false;          // 断连重连后重试一次
        if (mysql_real_query(g_conn, sql.data(), (unsigned long)sql.size()) != 0) return false;
    }
    return true;
}

long long Db::insert(const std::string& sql) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureConnected()) return -1;
    if (mysql_real_query(g_conn, sql.data(), (unsigned long)sql.size()) != 0) {
        tprintf("[Db] insert err: %s\n    sql=%.160s\n", mysql_error(g_conn), sql.c_str());
        fflush(stdout);
        if (!doConnect()) return -1;
        if (mysql_real_query(g_conn, sql.data(), (unsigned long)sql.size()) != 0) return -1;
    }
    return (long long)mysql_insert_id(g_conn);
}

bool Db::query(const std::string& sql) {
    std::lock_guard<std::mutex> lock(g_mutex);
    freeResult();
    if (!ensureConnected()) return false;
    if (mysql_real_query(g_conn, sql.data(), (unsigned long)sql.size()) != 0) {
        tprintf("[Db] query err: %s\n    sql=%.160s\n", mysql_error(g_conn), sql.c_str());
        fflush(stdout);
        if (!doConnect()) return false;
        if (mysql_real_query(g_conn, sql.data(), (unsigned long)sql.size()) != 0) return false;
    }
    g_result = mysql_store_result(g_conn);
    return g_result != nullptr;
}

bool Db::next() {
    if (!g_result) return false;
    g_row = mysql_fetch_row(g_result);
    return g_row != nullptr;
}

std::string Db::value(int index) {
    if (!g_result || !g_row) return "";
    unsigned int n = mysql_num_fields(g_result);
    if (index < 0 || (unsigned)index >= n) return "";
    return g_row[index] ? g_row[index] : "";
}

void Db::close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    freeResult();
    if (g_conn) { mysql_close(g_conn); g_conn = nullptr; }
}