// main/op_net.c —— 见 op_net.h。骨架沿袭 astock-quote sq_net.c 的 raw socket
// 写法（同板验证），加了状态码解析与头体切分。HTTP/1.0 + Connection: close：
// 服务器回完就关，无需解析 Content-Length / chunked。
#include "op_net.h"

#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/sockets.h"
// netdb.h / errno.h 由 lwip/sockets.h 传递提供（astock 同款）。

static const char *TAG = "op_net";

bool op_net_split_url(const char *url, char *host, size_t host_cap, int *port,
                      char *path, size_t path_cap)
{
    if (strncmp(url, "http://", 7) != 0) return false;  // https 固件不做。
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    const char *colon = (const char *)memchr(p, ':', (size_t)(host_end - p));
    const size_t hlen = colon ? (size_t)(colon - p) : (size_t)(host_end - p);
    if (hlen == 0 || hlen >= host_cap) return false;
    memcpy(host, p, hlen);
    host[hlen] = '\0';
    *port = 80;
    if (colon) {
        *port = atoi(colon + 1);
        if (*port <= 0 || *port > 65535) return false;
    }
    if (slash) {
        if (strlen(slash) >= path_cap) return false;
        strcpy(path, slash);
    } else {
        if (path_cap < 2) return false;
        strcpy(path, "/");
    }
    return true;
}

static int connect_host(const char *host, int port)
{
    const struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) {
        ESP_LOGW(TAG, "DNS 解析失败: %s", host);
        return -1;
    }
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr = *(struct in_addr *)he->h_addr_list[0],
    };
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "TCP 连接失败 %s:%d (errno=%d)", host, port, errno);
        close(sock);
        return -1;
    }
    return sock;
}

void op_net_request(const char *host, int port, const char *method, const char *path,
                    const char *extra_headers, const char *body,
                    op_net_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));

    const int body_len = body ? (int)strlen(body) : 0;
    char req[1152];
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.0\r\n"
                     "Host: %s:%d\r\n"
                     "User-Agent: octop-passport/0.1\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n"
                     "%s"
                     "%s"
                     "Content-Length: %d\r\n"
                     "\r\n",
                     method, path, host, port,
                     extra_headers ? extra_headers : "",
                     body ? "Content-Type: application/json\r\n" : "",
                     body_len);
    if (n < 0 || (size_t)n >= sizeof(req)) {
        ESP_LOGW(TAG, "请求头溢出");
        return;
    }

    const int sock = connect_host(host, port);
    if (sock < 0) return;
    if (write(sock, req, (size_t)n) < 0 || (body_len && write(sock, body, (size_t)body_len) < 0)) {
        ESP_LOGW(TAG, "请求发送失败 (errno=%d)", errno);
        close(sock);
        return;
    }

    char *raw = (char *)malloc(OP_NET_BODY_MAX);
    if (!raw) { close(sock); return; }
    size_t received = 0;
    for (;;) {
        if (received >= OP_NET_BODY_MAX - 1) break;
        const int got = read(sock, raw + received, OP_NET_BODY_MAX - 1 - received);
        if (got <= 0) break;
        received += (size_t)got;
    }
    close(sock);
    raw[received] = '\0';

    char *head_end = strstr(raw, "\r\n\r\n");
    if (!head_end || received < 12) { free(raw); return; }
    *head_end = '\0';
    // "HTTP/1.x NNN ..." → 状态码从第 10 列起（"HTTP/1.0 " 长 9）。
    resp->status = (received >= 12) ? atoi(raw + 9) : 0;
    if (resp->status < 100) { free(raw); return; }

    char *body_start = head_end + 4;
    resp->body_len = received - (size_t)(body_start - raw);
    resp->body = (char *)malloc(resp->body_len + 1);
    if (!resp->body) { resp->status = 0; free(raw); return; }
    memcpy(resp->body, body_start, resp->body_len);
    resp->body[resp->body_len] = '\0';
    free(raw);
}
