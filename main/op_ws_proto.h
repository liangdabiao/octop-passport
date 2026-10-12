// main/op_ws_proto.h —— WebSocket 帧协议的纯 C 实现，不依赖 ESP-IDF / lwip，
// 同一份代码跑在板子上（op_ws.c 的 socket 粘合层）和宿主测试（tests/test_op_ws_proto.c）里。
//
// 为什么手写：家族真机教训（hot-daily）——esp_http_client 在这台 C3+lwip 上
// TCP 通但组件内部失败；esp_websocket_client 走 tcp_transport，是另一套代码，
// 但同属"组件内部"路径，没在本硬件验证过。raw socket 在两个联网应用里都稳，
// 所以 WS 也落在 raw socket 上，只有帧协议需要自己写（约 200 行，全部可测）。
//
// 覆盖面（对话协议够用的子集）：
//   - 握手：Sec-WebSocket-Key 生成（随机源由调用方注入）、Accept 校验值计算
//   - 出帧：client→server 必须带 MASK；text / close
//   - 入帧：解头部（FIN/opcode/mask/长度 7/16/64 位），payload 由调用方继续读；
//     server→client 按标准不带 mask（带了也支持解）
//   - 分片（continuation）在粘合层拼装，这里只给判定助手
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OP_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// RFC 6455 opcode。
typedef enum {
    OP_WS_OP_CONT  = 0x0,
    OP_WS_OP_TEXT  = 0x1,
    OP_WS_OP_BIN   = 0x2,
    OP_WS_OP_CLOSE = 0x8,
    OP_WS_OP_PING  = 0x9,
    OP_WS_OP_PONG  = 0xA,
} op_ws_opcode_t;

// 解出来的帧头。
typedef struct {
    bool     fin;
    uint8_t  opcode;      // op_ws_opcode_t
    bool     masked;
    uint64_t payload_len; // 帧头声明的负载长度（字节）
    size_t   header_len;  // 本帧头部的总字节数（2 / 4 / 10）
} op_ws_frame_header_t;

// 生成 16 字节握手 key（要求调用方已用真随机源填好 raw16）。写出
// base64(key) 到 out（≥28 字节含 NUL），返回长度。
size_t op_ws_handshake_key(const uint8_t raw16[16], char *out, size_t cap);

// 计算 Sec-WebSocket-Accept = base64(SHA1(key + GUID))。out ≥ 29 字节。
size_t op_ws_accept_value(const char *client_key, char *out, size_t cap);

// 编码一帧到 out。client→server 恒带 MASK（mask 用伪随机源注入，测试里固定）。
// 返回写入字节数；容量不够返回 0。
size_t op_ws_encode_frame(uint8_t opcode, const uint8_t *payload, size_t len,
                          uint32_t mask, uint8_t *out, size_t cap);

// 从流式缓冲里尝试解一个帧头。
//   返回 >0：帧头完整，*header 有效，调用方读 payload_len 字节负载；
//   返回 0  ：数据不够，再攒攒；
//   返回 -1 ：帧非法（RSV 位非零、控制帧超长等）。
int op_ws_decode_header(const uint8_t *buf, size_t len, op_ws_frame_header_t *header);

// 解 mask（server→client 通常不带；带了就异或掉）。原地解密。
void op_ws_unmask(uint8_t *payload, size_t len, uint32_t mask);
