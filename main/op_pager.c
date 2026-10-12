// main/op_pager.c —— 见 op_pager.h。
#include "op_pager.h"

#include <string.h>

static int char_width(uint32_t cp)
{
    // 粗分：CJK 及全角区算 2 单位（16px 中文字库的 ASCII 是半角 8px）。
    if (cp >= 0x1100 && (cp <= 0x115F ||                    // 谚文兼容字母
                         (cp >= 0x2E80 && cp <= 0xA4CF) ||  // CJK 部首～谚文音节
                         (cp >= 0xAC00 && cp <= 0xD7A3) ||  // 谚文音节
                         (cp >= 0xF900 && cp <= 0xFAFF) ||  // CJK 兼容表意
                         (cp >= 0xFE30 && cp <= 0xFE4F) ||  // CJK 兼容形式
                         (cp >= 0xFF00 && cp <= 0xFF60) ||  // 全角形式
                         (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                         (cp >= 0x20000))) {
        return 2;
    }
    return 1;
}

// 返回 p 起完整字符的字节数（写入 *cp）；数据不足返回 0（需再喂）；
// 非法首字节返回 1（按 U+FFFD 吃掉）。
static int utf8_decode(const char *p, size_t avail, uint32_t *cp)
{
    const unsigned char c = (unsigned char)*p;
    if (c < 0x80) { *cp = c; return 1; }
    int n = 0;
    uint32_t v = 0;
    if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    if ((size_t)n > avail) return 0;
    for (int i = 1; i < n; i++) {
        if (((unsigned char)p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return i; }
        v = (v << 6) | ((unsigned char)p[i] & 0x3F);
    }
    *cp = v;
    return n;
}

void op_pager_init(op_pager_t *pg, char *buf, size_t cap, int width_units, int rows)
{
    memset(pg, 0, sizeof(*pg));
    pg->buf = buf;
    pg->cap = cap;
    pg->width_units = width_units;
    pg->rows = rows;
    pg->follow = true;
    op_pager_reset(pg);
}

void op_pager_reset(op_pager_t *pg)
{
    pg->len = 0;
    pg->buf[0] = '\0';
    pg->lines = 1;
    pg->line_off[0] = 0;
    pg->line_len[0] = 0;
    pg->line_open = true;
    pg->cur_units = 0;
    pg->page = 0;
    pg->follow = true;
    pg->truncated = false;
    pg->pend_len = 0;
}

// 单个完整字符的落盘（\n 之外的路径）。
static void emit_char(op_pager_t *pg, const char *bytes, int n, uint32_t cp)
{
    const int w = char_width(cp);
    if (pg->line_open && pg->cur_units + w > pg->width_units && pg->cur_units > 0) {
        // 换行：当前行封口（buf 里不存 \n，靠行表切分），开新行。
        if (pg->lines >= OP_PAGER_MAX_LINES) { pg->truncated = true; return; }
        pg->line_off[pg->lines] = pg->len;
        pg->line_len[pg->lines - 1] = pg->cur_units;
        pg->line_len[pg->lines] = 0;
        pg->lines++;
        pg->cur_units = 0;
    }
    pg->line_open = true;
    memcpy(pg->buf + pg->len, bytes, (size_t)n);
    pg->len += (size_t)n;
    pg->cur_units += w;
}

void op_pager_append(op_pager_t *pg, const char *utf8)
{
    if (pg->truncated) return;
    // token 流按段到达，多字节字符可能被劈在段尾：pend 暂存，下段拼上。
    while (*utf8) {
        if (pg->pend_len) {
            while (*utf8 && pg->pend_len < 4) pg->pend[pg->pend_len++] = (uint8_t)*utf8++;
            uint32_t cp;
            const int n = utf8_decode((const char *)pg->pend, pg->pend_len, &cp);
            if (n == 0) {
                if (*utf8) continue;  // 还要更多字节，上面已经又搬了一轮。
                return;               // 输入耗尽，继续攒。
            }
            pg->pend_len = 0;
            if (*utf8 == '\0' && n == 1 && cp == 0xFFFD && pg->pend[0] < 0x80) {
                // 常规快路径不会到这；防御：单字节非法直接吃掉。
            }
            if (pg->len + 5 >= pg->cap) { pg->truncated = true; return; }
            emit_char(pg, (const char *)pg->pend, n, cp);
            continue;
        }
        if (*utf8 == '\n') {
            if (pg->len + 2 >= pg->cap) { pg->truncated = true; return; }
            pg->buf[pg->len++] = '\n';
            utf8++;
            if (pg->lines >= OP_PAGER_MAX_LINES) { pg->truncated = true; return; }
            pg->line_off[pg->lines] = pg->len;
            pg->line_len[pg->lines] = 0;
            pg->lines++;
            pg->cur_units = 0;
            continue;
        }
        uint32_t cp;
        int n = utf8_decode(utf8, strlen(utf8), &cp);
        if (n == 0) {
            // 劈半：把本段剩余字节搬进 pend，等下一段。
            size_t rest = 0;
            while (utf8[rest] && rest < 4) { pg->pend[rest] = (uint8_t)utf8[rest]; rest++; }
            pg->pend_len = rest;
            return;
        }
        if (pg->len + 5 >= pg->cap) { pg->truncated = true; return; }
        emit_char(pg, utf8, n, cp);
        utf8 += n;
    }
    pg->buf[pg->len] = '\0';
}

int op_pager_page_count(const op_pager_t *pg)
{
    int pages = (pg->lines + pg->rows - 1) / pg->rows;
    return pages < 1 ? 1 : pages;
}

int op_pager_page(const op_pager_t *pg)
{
    const int last = op_pager_page_count(pg) - 1;
    if (pg->follow) return last;
    return pg->page < last ? pg->page : last;
}

void op_pager_nav(op_pager_t *pg, int delta)
{
    const int last = op_pager_page_count(pg) - 1;
    const int cur = pg->follow ? last : (pg->page < last ? pg->page : last);
    pg->follow = false;
    int p = cur + delta;
    if (p < 0) p = 0;
    if (p > last) p = last;
    pg->page = p;
}

void op_pager_follow_end(op_pager_t *pg)
{
    pg->follow = true;
    pg->page = op_pager_page_count(pg) - 1;
}

bool op_pager_following(const op_pager_t *pg) { return pg->follow; }
bool op_pager_truncated(const op_pager_t *pg) { return pg->truncated; }

int op_pager_page_text(const op_pager_t *pg, int idx, char *out, size_t cap)
{
    if (idx < 0 || idx >= op_pager_page_count(pg)) return -1;
    const int first = idx * pg->rows;
    const int last = first + pg->rows < pg->lines ? first + pg->rows : pg->lines;
    size_t o = 0;
    for (int ln = first; ln < last; ln++) {
        const size_t start = pg->line_off[ln];
        const size_t end = ln + 1 < pg->lines ? pg->line_off[ln + 1] : pg->len;
        if (end > start && pg->buf[end - 1] == '\n') {
            if (o + (end - start - 1) + 2 > cap) return -1;
            memcpy(out + o, pg->buf + start, end - start - 1);
            o += end - start - 1;
        } else {
            if (o + (end - start) + 2 > cap) return -1;
            memcpy(out + o, pg->buf + start, end - start);
            o += end - start;
        }
        if (ln + 1 < last) out[o++] = '\n';
    }
    out[o] = '\0';
    return last - first;
}
