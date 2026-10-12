// tests/test_op_pager.c —— op_pager 的宿主测试。
//   1. CJK/拉丁混排折行宽度正确、UTF-8 不劈半
//   2. 流式追加（token 一段段来）与一次性追加结果一致
//   3. 翻页边界、跟随模式、截断标志
//   4. \n 强制换行 + 空行保留
#include <stdio.h>
#include <string.h>

#include "op_pager.h"

static op_pager_t pg;
static char buf[4096];

static int fails;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg);           \
            fails++;                                                      \
        }                                                                 \
    } while (0)

int main(void)
{
    // --- 1. 折行：宽 10 单位，CJK=2、ASCII=1。
    op_pager_init(&pg, buf, sizeof(buf), 10, 3);
    op_pager_append(&pg, "ABCDEFGHIJ");          // 恰好 10 单位 → 1 行
    CHECK(pg.lines == 1, "10 latin = one line");
    op_pager_append(&pg, "甲乙丙丁戊");          // 10 单位 → 第 2 行
    CHECK(pg.lines == 2, "5 CJK wraps to next line");
    op_pager_append(&pg, "XY");                  // 2 单位，第 2 行 5CJK+XY=12>10 → 第 3 行
    CHECK(pg.lines == 3, "mixed wrap");

    // --- 2. 分页：每页 3 行，目前 3 行 = 1 页。
    CHECK(op_pager_page_count(&pg) == 1, "one page");
    char text[512];
    CHECK(op_pager_page_text(&pg, 0, text, sizeof(text)) == 3, "page has 3 lines");
    CHECK(strstr(text, "ABCDEFGHIJ") && strstr(text, "甲乙丙丁戊"), "page text content");

    // --- 3. 流式 = 整段（逐字追加与一次追加，行数一致）。
    op_pager_t a, b;
    char ba[2048], bb[2048];
    op_pager_init(&a, ba, sizeof(ba), 10, 3);
    op_pager_init(&b, bb, sizeof(bb), 10, 3);
    const char *src = "一二三四五六七八九十ABCDEF\n\nhello world 你好";
    op_pager_append(&a, src);
    for (const char *p = src; *p; p++) {
        char one[2] = {*p, 0};
        op_pager_append(&b, one);  // 逐字节（刻意把 UTF-8 劈开喂）
    }
    CHECK(a.lines == b.lines, "streamed == whole");
    CHECK(a.len == b.len, "same length");

    // --- 4. 翻页与跟随。
    op_pager_init(&pg, buf, sizeof(buf), 10, 2);
    for (int i = 0; i < 5; i++) op_pager_append(&pg, "行\n");
    CHECK(op_pager_page_count(&pg) == 3, "5 lines / 2 rows = 3 pages");
    CHECK(op_pager_page(&pg) == 2, "follow keeps last page");
    op_pager_nav(&pg, -1);
    CHECK(op_pager_page(&pg) == 1, "nav -1");
    CHECK(!op_pager_following(&pg), "nav pauses follow");
    op_pager_nav(&pg, -99);
    CHECK(op_pager_page(&pg) == 0, "clamped to 0");
    op_pager_nav(&pg, 99);
    CHECK(op_pager_page(&pg) == 2, "clamped to last");
    op_pager_follow_end(&pg);
    CHECK(op_pager_following(&pg) && op_pager_page(&pg) == 2, "follow jumps to end");

    // --- 5. 截断。
    op_pager_init(&pg, buf, 64, 10, 2);
    op_pager_append(&pg, "一段足够长的中文内容把小缓冲撑爆——继续追加应当冻结并置截断标志。");
    CHECK(op_pager_truncated(&pg), "truncated flag set");
    const size_t frozen = pg.len;
    op_pager_append(&pg, "更多内容");
    CHECK(pg.len == frozen, "append frozen after truncation");

    // --- 6. 空行保留。
    op_pager_init(&pg, buf, sizeof(buf), 10, 3);
    op_pager_append(&pg, "A\n\nB");
    CHECK(op_pager_page_text(&pg, 0, text, sizeof(text)) == 3, "3 physical lines");
    CHECK(strcmp(text, "A\n\nB") == 0, "empty line preserved");

    printf(fails ? "op_pager: %d FAIL\n" : "op_pager: all pass\n", fails);
    return fails ? 1 : 0;
}
