// main/op_ws.c —— 见 op_ws.h。
#include "op_ws.h"

#include <esp_random.h>
#include <esp_timer.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/sockets.h"

#include "op_ws_proto.h"

static const char *TAG = "op_ws";

#define TX_CHUNK 2048

static int connect_host(const char *host, int port)
{
    const struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) return -1;
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    tv.tv_sec = 0;
    tv.tv_usec = 20 * 1000;  // 收方向用短超时轮询（op_ws_poll 里统一管理）。
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr = *(struct in_addr *)he->h_addr_list[0],
    };
    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        close(sock);
        return -1;
    }
    return sock;
}

bool op_ws_connect(op_ws_t *ws, const char *host, int port, const char *path_with_query,
                   uint8_t *rx_buf, size_t rx_cap, uint8_t *msg_buf)
{
    memset(ws, 0, sizeof(*ws));
    ws->sock = -1;
    ws->rx = rx_buf;
    ws->rx_cap = rx_cap;
    ws->msg = msg_buf;

    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    char key[32];
    op_ws_handshake_key(raw, key, sizeof(key));

    char req[1152];
    const int n = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "\r\n",
                           path_with_query, host, port, key);
    if (n < 0 || (size_t)n >= sizeof(req)) return false;

    ws->sock = connect_host(host, port);
    if (ws->sock < 0) {
        ESP_LOGW(TAG, "WS TCP 连接失败 %s:%d", host, port);
        return false;
    }
    if (write(ws->sock, req, (size_t)n) < 0) {
        op_ws_close(ws);
        return false;
    }

    // 读握手响应到第一个 \r\n\r\n。socket 的 RCVTIMEO 是 op_ws_poll 的短轮询
    // 超时（20ms）——公网往返不止这个数，所以这里按自己的截止时间重试读，
    // 只把「真错误/对端关闭」当作失败。
    char resp[1024];
    size_t got = 0;
    const int64_t deadline = esp_timer_get_time() + 5000LL * 1000;
    while (got < sizeof(resp) - 1) {
        const int r = read(ws->sock, resp + got, 1);  // 逐字节找头尾，握手只有一次。
        if (r > 0) {
            got += (size_t)r;
            if (got >= 4 && memcmp(resp + got - 4, "\r\n\r\n", 4) == 0) break;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (esp_timer_get_time() < deadline) continue;
            ESP_LOGW(TAG, "WS 握手响应超时");
            break;
        }
        break;  // 对端关闭或真错误。
    }
    resp[got] = '\0';
    if (got == 0 || strncmp(resp, "HTTP/1.1 101", 12) != 0) {
        ESP_LOGW(TAG, "WS 握手失败: %.60s", resp);
        op_ws_close(ws);
        return false;
    }
    const char *accept = strstr(resp, "Sec-WebSocket-Accept:");
    if (!accept) {
        op_ws_close(ws);
        return false;
    }
    accept += strlen("Sec-WebSocket-Accept:");
    while (*accept == ' ') accept++;
    char expect[32];
    op_ws_accept_value(key, expect, sizeof(expect));
    if (strncmp(accept, expect, strlen(expect)) != 0) {
        ESP_LOGW(TAG, "WS Accept 校验失败");
        op_ws_close(ws);
        return false;
    }
    ws->mask = esp_random();
    snprintf(ws->host, sizeof(ws->host), "%s", host);
    ws->port = port;
    snprintf(ws->path, sizeof(ws->path), "%s", path_with_query);
    ESP_LOGI(TAG, "WS 已连接 %s:%d%s", host, port, path_with_query);
    return true;
}

static bool send_all(int sock, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        const ssize_t w = write(sock, buf + off, len - off);
        if (w <= 0) return false;
        off += (size_t)w;
    }
    return true;
}

bool op_ws_send_text(op_ws_t *ws, const char *utf8)
{
    if (ws->sock < 0) return false;
    size_t total = strlen(utf8);
    size_t sent = 0;
    uint8_t frame[TX_CHUNK + 16];
    bool first = true;
    do {
        const size_t take = total - sent > TX_CHUNK ? TX_CHUNK : total - sent;
        const uint8_t op = first ? OP_WS_OP_TEXT : OP_WS_OP_CONT;
        uint8_t *payload = frame + 16;
        memcpy(payload, utf8 + sent, take);
        const size_t flen = op_ws_encode_frame(op, payload, take, ws->mask + sent,
                                               frame, sizeof(frame));
        if (flen == 0 || !send_all(ws->sock, frame, flen)) return false;
        first = false;
        sent += take;
    } while (sent < total);
    return true;
}

bool op_ws_send_ping(op_ws_t *ws)
{
    if (ws->sock < 0) return false;
    uint8_t frame[8];
    const size_t flen = op_ws_encode_frame(OP_WS_OP_PING, (const uint8_t *)"", 0,
                                           ws->mask, frame, sizeof(frame));
    return flen > 0 && send_all(ws->sock, frame, flen);
}

bool op_ws_send_pong(op_ws_t *ws, const uint8_t *payload, size_t len)
{
    if (ws->sock < 0 || len > 125) return len == 0;
    uint8_t frame[132];
    const size_t flen = op_ws_encode_frame(OP_WS_OP_PONG, payload, len, ws->mask,
                                           frame, sizeof(frame));
    return flen > 0 && send_all(ws->sock, frame, flen);
}

bool op_ws_send_close(op_ws_t *ws)
{
    if (ws->sock < 0) return false;
    uint8_t frame[8];
    const size_t flen = op_ws_encode_frame(OP_WS_OP_CLOSE, (const uint8_t *)"", 0,
                                           ws->mask, frame, sizeof(frame));
    send_all(ws->sock, frame, flen);
    return true;
}

// 读一次新数据进接收缓冲；有新字节返回 1，超时返回 0，错误/对端关闭 -1。
// 真机教训：旧实现「rx_len < want 才读」，缓冲里留有半帧时它立刻返回 1，
// op_ws_poll 的循环就变成不打断任何锁的纯空转——CPU 烧满、看门狗报警、
// token 全部饿死（Watchdog 抓到的正是这里）。
static int fill_rx(op_ws_t *ws, int recv_ms)
{
    struct timeval tv = { .tv_sec = recv_ms / 1000, .tv_usec = (recv_ms % 1000) * 1000 };
    setsockopt(ws->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (ws->rx_len >= ws->rx_cap) {
        // 只可能：超尺寸帧收满了还没排空（consume 应已在排丢弃）。
        // 这里读进小草稿丢掉，保证有进展，绝不因此断链——真机每轮必断的根因
        // 就是这个分支原先直接 -1。
        if (!ws->discarding) {
            snprintf(ws->last_err, sizeof(ws->last_err), "缓冲满且非丢弃态");
            return -1;
        }
        uint8_t scratch[512];
        const ssize_t r = read(ws->sock, scratch, sizeof(scratch));
        if (r > 0) {
            ws->discard_left -= (uint64_t)r;
            return 1;
        }
        if (r == 0) {
            snprintf(ws->last_err, sizeof(ws->last_err), "对端关闭(read=0)");
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        snprintf(ws->last_err, sizeof(ws->last_err), "read errno=%d", errno);
        return -1;
    }
    const ssize_t r = read(ws->sock, ws->rx + ws->rx_len, ws->rx_cap - ws->rx_len);
    if (r > 0) {
        ws->rx_len += (size_t)r;
        return 1;
    }
    if (r == 0) {
        snprintf(ws->last_err, sizeof(ws->last_err), "对端关闭(read=0)");
        return -1;  // 对端关闭。
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    snprintf(ws->last_err, sizeof(ws->last_err), "read errno=%d", errno);
    return -1;
}

// 消化收缓冲开头的一个帧。
//   1 = 一条完整数据消息就绪（*opcode/*payload/*payload_len 有效，
//       payload 指向 ws 内部缓冲、NUL 结尾，下一次调用前有效）
//   0 = 还没攒够（调用方继续 fill_rx）
//  -1 = 连接坏。ping 自动回 pong；close 回关后置 -1。
//
// 超尺寸数据帧（负载 ≥ 缓冲）按「边收边丢」处理：判定到超尺寸后记住剩余
// 负载量（discard_left），排空之前不解析任何帧头、负载随到随丢；带 fin 的
// 分片收完才结束丢弃。状态快照随会话历史无界增长，而设备只关心
// token/done/error——丢弃即可，连接始终保持。旧实现把完整性检查排在丢弃
// 判定之前，缓冲一满就误判链路死亡——真机每轮必断的最终根因。
static int consume_frame(op_ws_t *ws, uint8_t *opcode, uint8_t **payload, size_t *payload_len)
{
    for (;;) {
        // 丢弃排空：此刻流上仍是超尺寸帧的负载字节，按字节丢，不解析。
        if (ws->discarding && ws->discard_left > 0) {
            if (ws->rx_len == 0) return 0;  // 等更多负载
            const size_t take = ws->rx_len < ws->discard_left
                                    ? ws->rx_len
                                    : (size_t)ws->discard_left;
            ws->rx_len -= take;
            ws->discard_left -= take;
            if (ws->rx_len > 0) memmove(ws->rx, ws->rx + take, ws->rx_len);
            if (ws->discard_left > 0) return 0;
            // 排空了：若消息还有 CONT 分片，discarding 保持，下一轮解析它的帧头。
            if (!ws->discard_fin_pending) ws->discarding = false;
            return 0;  // 丢弃中的 CONT 头部在本轮重新 decode 处理。
        }

        op_ws_frame_header_t h;
        const int hl = op_ws_decode_header(ws->rx, ws->rx_len, &h);
        if (hl < 0) {
            snprintf(ws->last_err, sizeof(ws->last_err), "帧头非法(错位/RSV)");
            return -1;
        }
        if (hl == 0) return 0;

        const bool data_frame = h.opcode == OP_WS_OP_TEXT || h.opcode == OP_WS_OP_BIN ||
                                h.opcode == OP_WS_OP_CONT;
        const bool oversize = ws->discarding || (data_frame && h.payload_len >= ws->rx_cap);

        // 完整性检查只对「要完整解析的帧」生效；discarding/超尺寸帧边收边丢。
        if (!ws->discarding && !oversize && ws->rx_len < hl + h.payload_len) return 0;

        // 已收到的负载数（帧头必已在缓冲；服务端帧不带掩码）。
        size_t avail_pl = ws->rx_len - (size_t)hl;
        if (avail_pl > h.payload_len) avail_pl = h.payload_len;

        if (h.opcode == OP_WS_OP_PING) {
            if (avail_pl < h.payload_len) return 0;  // ping ≤125B，最多一次 read 到齐。
            op_ws_send_pong(ws, ws->rx + hl, (size_t)h.payload_len);
        } else if (h.opcode == OP_WS_OP_CLOSE) {
            snprintf(ws->last_err, sizeof(ws->last_err), "对端关闭");
            op_ws_send_close(ws);
            return -1;
        } else if (ws->discarding || oversize) {
            // 进入/继续丢弃：本帧已收部分连同帧头一起扔，剩余记入 discard_left。
            ws->discard_left = h.payload_len - avail_pl;
            ws->discard_fin_pending = !h.fin;
            ws->discarding = true;
            ws->rx_len = 0;  // 帧头 + 已收负载整段扔掉，等剩余负载。
            if (!ws->discarding) return 0;
            continue;  // 顶部排空循环继续吃已缓冲的剩余负载。
        } else if (h.opcode == OP_WS_OP_TEXT || h.opcode == OP_WS_OP_BIN) {
            if (h.fin) {  // 常见快路径：单帧完整消息。
                // payload 拷到装配缓冲交付，帧头+负载从 rx 整体消费（尾随的
                // 下一帧字节保留在 rx 里）——旧实现只删帧头不删负载，下一轮
                // 把负载首字节当帧头解析，一收 token 就错位断链（真机根因）。
                memcpy(ws->msg, ws->rx + hl, (size_t)h.payload_len);
                const size_t consumed = hl + (size_t)h.payload_len;
                ws->rx_len -= consumed;
                memmove(ws->rx, ws->rx + consumed, ws->rx_len);
                ws->msg[ws->msg_len = (size_t)h.payload_len] = '\0';
                *opcode = h.opcode;
                *payload = ws->msg;
                *payload_len = ws->msg_len;
                return 1;
            }
            // 分片首帧 → 起装配（消费掉帧头，避免重复解析）。
            memcpy(ws->msg, ws->rx + hl, (size_t)h.payload_len);
            ws->msg_len = (size_t)h.payload_len;
            ws->msg_opcode = h.opcode;
            ws->msg_open = true;
            ws->rx_len -= (size_t)hl;
            memmove(ws->rx, ws->rx + hl, ws->rx_len);
        } else if (h.opcode == OP_WS_OP_CONT) {
            if (!ws->msg_open) {
                snprintf(ws->last_err, sizeof(ws->last_err), "CONT 无装配");
                return -1;
            }
            if (ws->msg_len + avail_pl > ws->rx_cap) {
                // 分片长着长着超了缓冲：整条弃掉，等带 fin 的帧收尾。
                ws->discard_left = h.payload_len - avail_pl;
                ws->discard_fin_pending = !h.fin;
                ws->discarding = true;
                ws->rx_len = 0;
                if (h.fin) {
                    ws->msg_open = false;
                    return 0;  // 整条已弃，静默。
                }
                return 0;
            }
            memcpy(ws->msg + ws->msg_len, ws->rx + hl, avail_pl);
            ws->msg_len += avail_pl;
            const size_t seg = hl + avail_pl;
            ws->rx_len -= seg;
            memmove(ws->rx, ws->rx + seg, ws->rx_len);
            if (h.fin && avail_pl == h.payload_len) {
                // 分片末帧 → 整条消息就绪。
                ws->msg[ws->msg_len] = '\0';
                ws->msg_open = false;
                *opcode = ws->msg_opcode;
                *payload = ws->msg;
                *payload_len = ws->msg_len;
                return 1;
            }
            if (avail_pl == h.payload_len) continue;  // 该帧完整但非 fin：继续下一分片。
            return 0;  // 帧还没收全。
        } else {
            snprintf(ws->last_err, sizeof(ws->last_err), "opcode=%u", h.opcode);
            return -1;  // 未知 opcode（二进制协议没协商）。
        }
        // 非就绪帧（ping/pong）已消化，从缓冲移除后继续。
        const size_t consumed = hl + avail_pl;
        memmove(ws->rx, ws->rx + consumed, ws->rx_len - consumed);
        ws->rx_len -= consumed;
    }
}

int op_ws_poll(op_ws_t *ws, int recv_ms, uint8_t *opcode,
               uint8_t **payload, size_t *payload_len)
{
    // 先看装配缓冲里是否已有整条消息残留（不会：就绪即返回），直接进入循环。
    for (;;) {
        const int r = consume_frame(ws, opcode, payload, payload_len);
        if (r != 0) return r;
        // consume 返回 0 = 帧不完整；阻塞等新字节（recv_ms），有进展再解。
        const int f = fill_rx(ws, recv_ms);
        if (f <= 0) return f;
    }
}

void op_ws_close(op_ws_t *ws)
{
    if (ws->sock >= 0) close(ws->sock);
    ws->sock = -1;
    ws->rx_len = 0;
    ws->msg_open = false;
    ws->msg_len = 0;
    ws->discarding = false;
    ws->discard_left = 0;
    ws->discard_fin_pending = false;
}
