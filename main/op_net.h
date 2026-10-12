// main/op_net.h —— raw-socket HTTP 客户端（登录/建线程/STT 用）。
//
// 家族真机教训（hot-daily）：esp_http_client 在这台 C3+lwip 上 TCP 通但
// 组件内部失败；astock-quote 的 raw socket 方案验证稳定，沿袭之。全部明文
// http——Octop 反代 HTTPS 属于部署侧话题（方案 v2 §4.3），固件不做 TLS。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define OP_NET_BODY_MAX (32 * 1024)

typedef struct {
    int    status;                    // HTTP 状态码；0 = 传输失败
    char  *body;                      // 响应体（HTTP/1.0 + Connection: close，
                                      // 头体按 \r\n\r\n 切分），调用方 free()
    size_t body_len;
} op_net_response_t;

// 发一个请求并收完整响应。host/port/path/status_line 均为 NUL 串；
// extra_headers 为 \r\n 结尾的多行（可为 NULL）；body 可为 NULL（GET）。
// 失败时 response.status == 0 且 body == NULL。
void op_net_request(const char *host, int port, const char *method, const char *path,
                    const char *extra_headers, const char *body,
                    op_net_response_t *resp);

// 从 "scheme://host:port/path" 拆出 host / port / path。
// scheme 只认 http（https 返回 false——固件不做 TLS）。成功返回 true。
bool op_net_split_url(const char *url, char *host, size_t host_cap, int *port,
                      char *path, size_t path_cap);
