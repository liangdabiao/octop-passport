// tests/test_op_ws_proto.c —— WS 帧协议的宿主测试。
//   1. Accept 值与 RFC 6455 §1.3 示例一致（权威锚点）
//   2. 出帧编码：小帧/扩展长度帧，MASK 掩码可逆
//   3. 入帧头部解析：不完整 → 0；非法 RSV → -1；64 位长度
#include <stdio.h>
#include <string.h>

#include "op_ws_proto.h"

static int fails;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
            fails++;                                            \
        }                                                       \
    } while (0)

int main(void)
{
    // --- 1. RFC 6455 示例："dGhlIHNhbXBsZSBub25jZQ==" →
    //         "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    char accept[32];
    const size_t n = op_ws_accept_value("dGhlIHNhbXBsZSBub25jZQ==", accept, sizeof(accept));
    CHECK(n == 28, "accept length");
    CHECK(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0, "accept value per RFC");

    // --- 2. 出帧：payload "Hi" + mask 0x01020304。
    // 带 MASK 的帧 = 1(FIN/op) + 1(MASK|len) + 4(mask key) + 2(payload) = 8。
    uint8_t out[64];
    const size_t flen = op_ws_encode_frame(OP_WS_OP_TEXT, (const uint8_t *)"Hi", 2,
                                           0x01020304u, out, sizeof(out));
    CHECK(flen == 8, "small frame length");
    CHECK(out[0] == 0x81, "FIN|text");
    CHECK(out[1] == 0x82, "MASK|len2");
    CHECK(out[2] == 0x01 && out[3] == 0x02 && out[4] == 0x03 && out[5] == 0x04, "mask bytes");
    CHECK(out[6] == ('H' ^ 0x01) && out[7] == ('i' ^ 0x02), "masked payload");

    // 掩码可逆（unmask 与 encode 的异或一致）。
    op_ws_unmask(out + 6, 2, 0x01020304u);
    CHECK(out[6] == 'H' && out[7] == 'i', "unmask restores");

    // --- 3. 扩展长度（16 位）：300 字节 payload，mask=0（仍写 4 字节掩码键）。
    uint8_t big[400];
    memset(big, 'x', sizeof(big));
    uint8_t big_out[512];
    const size_t blen = op_ws_encode_frame(OP_WS_OP_TEXT, big, 300, 0, big_out, sizeof(big_out));
    CHECK(blen == 300 + 8, "16-bit length frame size");
    op_ws_frame_header_t h;
    CHECK(op_ws_decode_header(big_out, blen, &h) == 8, "header len 4+4mask");
    CHECK(h.payload_len == 300 && h.fin && h.masked, "16-bit header fields");

    // --- 4. 64 位长度：头 10 字节。
    const size_t ll = op_ws_encode_frame(OP_WS_OP_BIN, big, 70000, 0, out, sizeof(out));
    (void)ll;  // out 只有 64B，编码返回 0（容量不够）——这里只测头解析。
    uint8_t hdr[12] = {0x82, 0x7F, 0, 0, 0, 0, 0, 0x01, 0x11, 0x70, 0, 0};
    CHECK(op_ws_decode_header(hdr, sizeof(hdr), &h) == 10, "64-bit header len 10");
    CHECK(h.payload_len == 0x11170, "64-bit length value");

    // --- 5. 不完整与非法。
    CHECK(op_ws_decode_header(out, 1, &h) == 0, "incomplete header -> 0");
    uint8_t bad[2] = {0x82, 0x02};
    bad[0] |= 0x40;  // RSV1 置位
    CHECK(op_ws_decode_header(bad, 2, &h) == -1, "RSV set -> -1");
    uint8_t badping[5] = {0x09, 0x80 | 200, 0, 0, 0};  // 控制帧 >125 且带掩码头
    CHECK(op_ws_decode_header(badping, 4, &h) == -1, "oversize control -> -1");

    printf(fails ? "op_ws_proto: %d FAIL\n" : "op_ws_proto: all pass\n", fails);
    return fails ? 1 : 0;
}
