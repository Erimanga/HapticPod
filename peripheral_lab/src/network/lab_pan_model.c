/*
 * lab_pan_model.c — HTTP 首行解析：提取合法状态码，是否测试通过由网络层判定。
 */
#include "lab_pan.h"
#include <string.h>
/* 只解析 HTTP/1.0 或 HTTP/1.1 首行的三位状态码，格式不符返回失败。 */
int lab_pan_http_status(const char *data, size_t length)
{
    if (length<12 || (memcmp(data,"HTTP/1.0 ",9) && memcmp(data,"HTTP/1.1 ",9))) return -1;
    if (data[9]<'1' || data[9]>'5' || data[10]<'0' || data[10]>'9' || data[11]<'0' || data[11]>'9') return -1;
    if (length>12 && data[12]!=' ' && data[12]!='\r') return -1;
    return (data[9]-'0')*100+(data[10]-'0')*10+data[11]-'0';
}
