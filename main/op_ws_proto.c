// main/op_ws_proto.c —— 见 op_ws_proto.h。SHA1 与 base64 内联自带（都是
// 公开算法的直译），避免为了一个 Accept 校验把 mbedtls 拖进宿主测试。
#include "op_ws_proto.h"

#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------- SHA1 ----
// RFC 3174 直译（公开算法）。内部把 padding 一并处理，调用方只给原始消息。
typedef struct {
    uint32_t h[5];
    uint8_t  buf[64];
    size_t   buf_len;
    uint64_t total_len;
} sha1_ctx_t;

static uint32_t sha1_rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

static void sha1_block(sha1_ctx_t *ctx, const uint8_t p[64])
{
    uint32_t w[80];
    for (int t = 0; t < 16; t++) {
        w[t] = ((uint32_t)p[t * 4] << 24) | ((uint32_t)p[t * 4 + 1] << 16) |
               ((uint32_t)p[t * 4 + 2] << 8) | (uint32_t)p[t * 4 + 3];
    }
    for (int t = 16; t < 80; t++) {
        w[t] = sha1_rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    }
    uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3], e = ctx->h[4];
    for (int t = 0; t < 80; t++) {
        uint32_t f, k;
        if (t < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
        else if (t < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        const uint32_t tmp = sha1_rol(a, 5) + f + e + k + w[t];
        e = d; d = c; c = sha1_rol(b, 30); b = a; a = tmp;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;
}

static void sha1_init(sha1_ctx_t *ctx)
{
    ctx->h[0] = 0x67452301; ctx->h[1] = 0xEFCDAB89; ctx->h[2] = 0x98BADCFE;
    ctx->h[3] = 0x10325476; ctx->h[4] = 0xC3D2E1F0;
    ctx->buf_len = 0;
    ctx->total_len = 0;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len)
{
    ctx->total_len += len;
    while (len > 0) {
        const size_t take = 64 - ctx->buf_len < len ? 64 - ctx->buf_len : len;
        memcpy(ctx->buf + ctx->buf_len, data, take);
        ctx->buf_len += take;
        data += take;
        len -= take;
        if (ctx->buf_len == 64) {
            sha1_block(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

static void sha1_final(sha1_ctx_t *ctx, uint8_t digest[20])
{
    const uint64_t bits = ctx->total_len * 8;
    const uint8_t pad = 0x80;
    sha1_update(ctx, &pad, 1);
    const uint8_t zero = 0;
    while (ctx->buf_len != 56) sha1_update(ctx, &zero, 1);
    uint8_t len_be[8];
    for (int t = 0; t < 8; t++) len_be[t] = (uint8_t)(bits >> (56 - 8 * t));
    sha1_update(ctx, len_be, 8);
    for (int t = 0; t < 5; t++) {
        digest[t * 4]     = (uint8_t)(ctx->h[t] >> 24);
        digest[t * 4 + 1] = (uint8_t)(ctx->h[t] >> 16);
        digest[t * 4 + 2] = (uint8_t)(ctx->h[t] >> 8);
        digest[t * 4 + 3] = (uint8_t)(ctx->h[t]);
    }
}

// ----------------------------------------------------------- base64 ----
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *in, size_t len, char *out, size_t cap)
{
    size_t need = (len + 2) / 3 * 4 + 1;
    if (cap < need) return 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < len ? (uint32_t)in[i + 1] << 8 : 0) |
                           (i + 2 < len ? (uint32_t)in[i + 2] : 0);
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

// ------------------------------------------------------- 公开接口 ----
size_t op_ws_handshake_key(const uint8_t raw16[16], char *out, size_t cap)
{
    return b64_encode(raw16, 16, out, cap);
}

size_t op_ws_accept_value(const char *client_key, char *out, size_t cap)
{
    char concat[128];
    const int n = snprintf(concat, sizeof(concat), "%s" OP_WS_GUID, client_key);
    if (n <= 0 || (size_t)n >= sizeof(concat)) return 0;
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)concat, (size_t)n);
    uint8_t digest[20];
    sha1_final(&ctx, digest);
    return b64_encode(digest, 20, out, cap);
}

size_t op_ws_encode_frame(uint8_t opcode, const uint8_t *payload, size_t len,
                          uint32_t mask, uint8_t *out, size_t cap)
{
    size_t header = 2;
    if (len > 125) header += len > 0xFFFF ? 8 : 2;
    if (cap < header + len) return 0;

    out[0] = (uint8_t)(0x80 | opcode);  // 本协议里出帧都是单帧完整消息，FIN 恒 1。
    size_t o = 1;
    if (len <= 125) {
        out[o++] = (uint8_t)(0x80 | len);  // bit7 = MASK，client→server 恒带。
    } else if (len <= 0xFFFF) {
        out[o++] = (uint8_t)(0x80 | 126);
        out[o++] = (uint8_t)(len >> 8);
        out[o++] = (uint8_t)len;
    } else {
        out[o++] = (uint8_t)(0x80 | 127);
        for (int t = 7; t >= 0; t--) out[o++] = (uint8_t)((uint64_t)len >> (8 * t));
    }
    out[o++] = (uint8_t)(mask >> 24);
    out[o++] = (uint8_t)(mask >> 16);
    out[o++] = (uint8_t)(mask >> 8);
    out[o++] = (uint8_t)mask;

    for (size_t i = 0; i < len; i++) {
        out[o + i] = payload[i] ^ (uint8_t)(mask >> (8 * (3 - i % 4)));
    }
    return o + len;
}

int op_ws_decode_header(const uint8_t *buf, size_t len, op_ws_frame_header_t *header)
{
    if (len < 2) return 0;
    if (buf[0] & 0x70) return -1;  // RSV 位必须为 0（无扩展协商）。
    memset(header, 0, sizeof(*header));
    header->fin = (buf[0] & 0x80) != 0;
    header->opcode = buf[0] & 0x0F;
    header->masked = (buf[1] & 0x80) != 0;
    uint64_t plen = buf[1] & 0x7F;
    size_t pos = 2;
    if (plen == 126) {
        if (len < 4) return 0;
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        pos = 4;
    } else if (plen == 127) {
        if (len < 10) return 0;
        plen = 0;
        for (int t = 0; t < 8; t++) plen = (plen << 8) | buf[2 + t];
        pos = 10;
    }
    if (header->masked) pos += 4;
    if (plen > (uint64_t)1 << 30) return -1;  // 1GB 上限纯粹防畸形值。
    // 控制帧（ping/pong/close）不允许 >125 且必须 FIN。
    if (header->opcode >= 0x8 && (plen > 125 || !header->fin)) return -1;
    header->payload_len = plen;
    header->header_len = pos;
    return (int)pos;
}

void op_ws_unmask(uint8_t *payload, size_t len, uint32_t mask)
{
    if (!mask) return;
    for (size_t i = 0; i < len; i++) {
        payload[i] ^= (uint8_t)(mask >> (8 * (3 - i % 4)));
    }
}
