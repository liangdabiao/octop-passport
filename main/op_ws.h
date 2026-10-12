// main/op_ws.h —— WebSocket 客户端的 socket 粘合层（帧协议在 op_ws_proto，
// 纯 C 可测；本模块管 TCP、握手与收发循环，只在板上编译）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "op_ws_proto.h"

typedef struct {
    int      sock;                 // -1 = 未连接
    char     host[128];
    int      port;
    char     path[768];
    uint8_t *rx;                   // 收包攒帧缓冲（构造时给容量）
    size_t   rx_cap;
    size_t   rx_len;
    uint8_t *msg;                  // 分片装配缓冲（与 rx 等长，构造时一并给）
    size_t   msg_len;
    uint8_t  msg_opcode;
    bool     msg_open;             // 正在装配一条分片消息
    bool     discarding;           // 帧超尺寸：读到帧尾为止整帧丢弃。状态快照
                                   // 会随会话历史无界增长，固定缓冲必然装不下，
                                   // 而设备只关心 token/done/error——丢弃即可，
                                   // 连接保持（真机：直接判死会每轮必断）。
    uint64_t discard_left;         // 丢弃帧剩余的负载字节
    bool     discard_fin_pending;  // 丢弃中的消息还有 CONT 分片未到
    uint32_t mask;                 // 本连接的出帧掩码基值
    char     last_err[48];         // 最近一次 -1 的原因（屏幕诊断用）
} op_ws_t;

// 建连 + 握手（HTTP Upgrade）。rx_buf / msg_buf 由调用方分配（各自 rx_cap；
// 同一任务独占使用），msg_buf 用于分片消息装配。成功返回 true。
bool op_ws_connect(op_ws_t *ws, const char *host, int port, const char *path_with_query,
                   uint8_t *rx_buf, size_t rx_cap, uint8_t *msg_buf);

// 发送一条完整文本帧（分块超过缓冲也不怕：按 2KB 分片发 CONT 帧）。
bool op_ws_send_text(op_ws_t *ws, const char *utf8);
bool op_ws_send_ping(op_ws_t *ws);  // 空闲保活：防服务端掐空闲连接
bool op_ws_send_pong(op_ws_t *ws, const uint8_t *payload, size_t len);
bool op_ws_send_close(op_ws_t *ws);

// 轮询收一条完整消息。
//   返回 >0：*opcode 为 OP_WS_OP_TEXT/BINARY 时，payload 指向内部缓冲
//            （NUL 结尾，**下一次调用前有效**）；PING/CLOSE 已自动应答/消化，
//            继续等下一条（内部循环）。
//   返回 0 ：超时（recv_ms 内没有完整消息）。
//   返回 -1：连接坏（对端关闭/协议错/socket 错），调用方重连。
int  op_ws_poll(op_ws_t *ws, int recv_ms, uint8_t *opcode,
                uint8_t **payload, size_t *payload_len);

void op_ws_close(op_ws_t *ws);
