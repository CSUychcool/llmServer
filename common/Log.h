#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#define  DEBUG   1

#if DEBUG
/*
*  如果不加 do ... while(0) 在进行条件判断的时候(只有一句话), 省略了{}, 就会出现语法错误
*  if
*     xxxxx
*  else
*     xxxxx
*  宏被替换之后, 在 else 前面会出现一个 ;  --> 语法错误
*/
#define LOG(type, fmt, args...)  \
  do{\
    time_t _t = time(NULL); struct tm _tm; localtime_r(&_t, &_tm);\
    printf("%02d:%02d:%02d %s: %s@%s, line: %d\n***LogInfo[", _tm.tm_hour, _tm.tm_min, _tm.tm_sec, type, __FILE__, __FUNCTION__, __LINE__);\
    printf(fmt, ##args);\
    printf("]\n\n");\
  }while(0)
#define Debug(fmt, args...) LOG("DEBUG", fmt, ##args)
#define Error(fmt, args...) do{LOG("ERROR", fmt, ##args);exit(0);}while(0)

// 带时间戳的统一输出 (供业务文件 printf 日志使用, 非 Log 宏风格)
static inline void tprintf(const char* fmt, ...) {
    time_t _t = time(NULL); struct tm _tm; localtime_r(&_t, &_tm);
    printf("%02d:%02d:%02d ", _tm.tm_hour, _tm.tm_min, _tm.tm_sec);
    va_list _ap; va_start(_ap, fmt);
    vprintf(fmt, _ap);
    va_end(_ap);
    fflush(stdout);
}
#else
#define LOG(fmt, args...)
#define Debug(fmt, args...)
#define Error(fmt, args...)
#endif


