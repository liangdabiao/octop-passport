// main/op_wire.h —— Octop 线缆协议的纯 C 助手：不依赖 ESP-IDF / cJSON，
// 同一份代码跑在板子和宿主测试（tests/test_op_wire.c）里。
//
// 覆盖三类需求（对话协议够用的子集，刻意不做通用 JSON 解析器）：
//   1. 从 HarnessChunk 帧里取指定键的字符串/整数值（帧是平的或浅嵌套；
//      只按「键名出现位置」线性查找，调用方先用 type 判定帧种类再取值）
//   2. JSON 字符串值解码（含 \" \\ \/ \b \f \n \r \t 与 \uXXXX → UTF-8）
//   3. 发送侧的 JSON 字符串转义（控制字符与引号反斜杠；UTF-8 原样通过）
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 在 payload 里找 "key":"value" 形式的字符串值并解码到 out（NUL 结尾）。
// 返回写入长度（不含 NUL）；找不到 / 缓冲不够返回 -1。key 必须带引号精确匹配。
int op_wire_json_string(const char *payload, const char *key, char *out, size_t cap);

// 找 "key":123 形式的数值。找不到返回 false。
bool op_wire_json_int(const char *payload, const char *key, long long *out);

// 把 utf8 转义成 JSON 字符串内容（不含首尾引号）写入 out，返回长度；
// 容量不够返回 -1。
int op_wire_json_escape(const char *utf8, char *out, size_t cap);
