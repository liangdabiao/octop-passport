// main/op_pager.h —— 流式文本分页器：纯 C，不依赖 ESP-IDF / LVGL，
// 同一份代码跑在板子和宿主测试（tests/test_op_pager.c）里。
//
// 服务「回复是长篇 MD 文章，直接在 240×320 屏上读」：
//   - append() 流式喂入（WS token 到一段喂一段），UTF-8 安全（多字节不劈半）
//   - 行版式按显示宽度算：中日韩全角 = 2 单位，其余 = 1 单位（16px 字库的
//     ASCII 是半角）。行表只存行首偏移，增量维护，追加不重排
//   - 跟随模式：追加期间 UI 永远停末页（电传打字机）；任何翻页即暂停跟随
//   - 缓冲写满 → 冻结追加 + truncated 置位（UI 提示「内容过长，全文见控制台」）
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OP_PAGER_MAX_LINES 1024

typedef struct {
    char    *buf;
    size_t   cap;                 // 缓冲容量（不含 NUL）
    size_t   len;                 // 已用字节
    int      width_units;         // 一行的显示宽度（单位 = 半角）
    int      rows;                // 每页行数
    uint32_t line_off[OP_PAGER_MAX_LINES];  // 行首偏移（buf 内）
    int      line_len[OP_PAGER_MAX_LINES];   // 行的显示宽度（信息用）
    int      lines;               // 已成行数（含最后未满行）
    bool     line_open;           // 最后一行还在生长
    int      cur_units;           // 当前行已占宽度
    int      page;                // UI 当前页（0 基；follow 时以末页为准）
    bool     follow;              // 跟随末页
    bool     truncated;
    uint8_t  pend[4];             // 被劈开的多字节字符暂存（token 流按段来）
    size_t   pend_len;
} op_pager_t;

void op_pager_init(op_pager_t *pg, char *buf, size_t cap, int width_units, int rows);
void op_pager_reset(op_pager_t *pg);

// 追加一段 UTF-8 文本。\n 强制换行；缓冲满则冻结（truncated=1）。
void op_pager_append(op_pager_t *pg, const char *utf8);

int  op_pager_page_count(const op_pager_t *pg);   // ≥1
int  op_pager_page(const op_pager_t *pg);         // 当前页 0 基
void op_pager_nav(op_pager_t *pg, int delta);     // ±1 翻页；翻页即停跟随
void op_pager_follow_end(op_pager_t *pg);         // 恢复跟随并跳末页
bool op_pager_following(const op_pager_t *pg);
bool op_pager_truncated(const op_pager_t *pg);

// 取第 idx 页的文本（行间以 \n 连接），写 out（NUL 结尾），返回页内行数；
// idx 越界返回 -1。缓冲内容只读，UI 渲染前应持锁。
int op_pager_page_text(const op_pager_t *pg, int idx, char *out, size_t cap);
