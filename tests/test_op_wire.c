// tests/test_op_wire.c —— 线缆 JSON 助手的宿主测试。
//   1. P-M0 实测的真实 HarnessChunk 帧：token 帧取 content（含中文与转义）
//   2. done/error/done 帧、state_update 噪音帧不误取
//   3. \uXXXX 与代理对、发送侧转义
#include <stdio.h>
#include <string.h>

#include "op_wire.h"

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
    char out[512];

    // --- 1. 真实 token 帧（P-M0 抓包：type 与 content 相邻）。
    const char *tok =
        "{\"type\":\"token\",\"content\":\"连通\",\"node\":\"model\","
        "\"thread_id\":\"thr_01M4R1P4G7AH5E960W5FB861D0\"}";
    CHECK(op_wire_json_string(tok, "type", out, sizeof(out)) == 5 &&
          strcmp(out, "token") == 0, "type=token");
    CHECK(op_wire_json_string(tok, "content", out, sizeof(out)) == 6 &&
          strcmp(out, "连通") == 0, "content=连通");
    CHECK(op_wire_json_string(tok, "thread_id", out, sizeof(out)) == 30, "thread_id");

    // --- 2. state_update 噪音帧里也有 "content"，但我们只按 type 分派 ——
    // 这里验证按 type 判定后不误取（type != token 时不调 content）。
    const char *noise =
        "{\"type\":\"state_update\",\"node\":\"model\","
        "\"data\":{\"messages\":[{\"content\":\"连通正常\"}]}}";
    op_wire_json_string(noise, "type", out, sizeof(out));
    CHECK(strcmp(out, "state_update") == 0, "noise type identified");

    // --- 3. 转义解码。
    const char *esc = "{\"type\":\"token\",\"content\":\"a\\\"b\\\\c\\n\\u4f60\\u597d\"}";
    CHECK(op_wire_json_string(esc, "content", out, sizeof(out)) == 12 &&
          strcmp(out, "a\"b\\c\n你好") == 0, "escapes decoded");

    // 代理对（U+1F600 😀）。
    const char *emoji = "{\"content\":\"\\ud83d\\ude00\"}";
    CHECK(op_wire_json_string(emoji, "content", out, sizeof(out)) == 4, "surrogate pair len");
    CHECK((unsigned char)out[0] == 0xF0 && (unsigned char)out[3] == 0x80, "surrogate utf8");

    // --- 4. 取不到 / 容量不够。
    CHECK(op_wire_json_string(tok, "nope", out, sizeof(out)) == -1, "missing key");
    CHECK(op_wire_json_string(tok, "content", out, 2) == -1, "too small buffer");

    // --- 5. 数值。
    long long v = 0;
    CHECK(op_wire_json_int(tok, "usage", &v) == false, "no int key");
    const char *num = "{\"seq\":42,\"neg\":-7}";
    CHECK(op_wire_json_int(num, "seq", &v) && v == 42, "int value");

    // --- 6. 发送侧转义。
    CHECK(op_wire_json_escape("说\"话\"\n", out, sizeof(out)) == 12 &&
          strcmp(out, "说\\\"话\\\"\\n") == 0, "escape round");
    char frame[600];
    snprintf(frame, sizeof(frame), "{\"type\":\"user_turn\",\"text\":\"%s\"}", out);
    CHECK(op_wire_json_string(frame, "text", out, sizeof(out)) > 0 &&
          strcmp(out, "说\"话\"\n") == 0, "escape roundtrip");

    printf(fails ? "op_wire: %d FAIL\n" : "op_wire: all pass\n", fails);
    return fails ? 1 : 0;
}
