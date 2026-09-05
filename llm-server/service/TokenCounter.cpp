#include "TokenCounter.h"

// 粗糙但足够保守: 宁可少装也不超窗 (个人聊天场景精度要求低)
int TokenCounter::estimateTokens(const std::string& s) {
    int cjk = 0, ascii = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            ascii++;
            i++;
        } else {
            cjk++;
            size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0 ? 3 : 2);
            i += len;
        }
    }
    return cjk + (ascii + 3) / 4;   // ASCII 向上取整到整 token
}