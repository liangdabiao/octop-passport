// main/op_ui.c —— 见 op_ui.h。布局（240×320 竖屏，16px 中文字库）：
//   首页（未配网/出错）：大标题 + 三步配网引导 —— 开机第一屏就是「怎样使用」。
//   阅读页：状态栏(含电量) / 正文 17 行 / 页脚页码与按键提示；连接成功后切入。
#include "op_ui.h"

#include <stdio.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "lvgl.h"

#include "op_octop.h"
#include "op_pager.h"

LV_FONT_DECLARE(op_font_16);
LV_FONT_DECLARE(op_font_24);

// 行列必须与 LVGL 实际渲染一致，否则每页末尾几行被裁到屏外（真机踩过：
// 15 字行宽 240px > 标签 232px，LVGL 二次折行让行数翻倍）。标签宽 232px、
// 字高约 22px：14 个全角（28 半角单位）× 12 行，正文区 y=26..298。
#define ROWS        12
#define WIDTH_UNITS 28

static lv_obj_t *s_status_label;   // 阅读页顶部状态栏（含电量）
static lv_obj_t *s_body_label;     // 阅读页正文
static lv_obj_t *s_hint_label;     // 阅读页页脚
static lv_obj_t *s_home_scr;       // 引导首页（未连接时显示）
static lv_obj_t *s_home_tip;       // 首页底部行（复用为按键指示）
static lv_obj_t *s_home_status;    // 首页状态行
static lv_obj_t *s_read_scr;       // 阅读页
static bool     s_on_home = true;

// 正文缓冲与分页器本体放这里（op_app 通过 op_ui_pager() 访问）。
static char s_text_buf[16 * 1024];  // ≈32 页（约 5300 字）；无 PSRAM 板上的静态 RAM 预算取舍。
static char s_page_buf[2 * 1024];   // 单页 ≤ 17 行 × ~57B（UTF-8 中文 3B），2KB 富余
static op_pager_t s_pager;

op_pager_t *op_ui_pager(void) { return &s_pager; }

static void build_read_screen(void)
{
    s_read_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_read_scr, lv_color_hex(0x101418), 0);

    s_status_label = lv_label_create(s_read_scr);
    lv_obj_set_style_text_font(s_status_label, &op_font_16, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x0F1216), 0);
    lv_obj_set_style_bg_color(s_status_label, lv_color_hex(0x9AA7B4), 0);
    lv_obj_set_style_bg_opa(s_status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_status_label, 4, 0);
    lv_label_set_text(s_status_label, "Octop Passport");
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 2);

    s_body_label = lv_label_create(s_read_scr);
    lv_obj_set_style_text_font(s_body_label, &op_font_16, 0);
    lv_obj_set_style_text_color(s_body_label, lv_color_hex(0xD7DEE6), 0);
    lv_label_set_long_mode(s_body_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_body_label, 232);
    lv_obj_align(s_body_label, LV_ALIGN_TOP_LEFT, 4, 26);
    lv_label_set_text(s_body_label, "");

    s_hint_label = lv_label_create(s_read_scr);
    lv_obj_set_style_text_font(s_hint_label, &op_font_16, 0);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0x6B7684), 0);
    lv_obj_align(s_hint_label, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(s_hint_label, "OK=发送  上下=翻页");
}

static void build_home_screen(void)
{
    // 「怎样使用」引导首页：三步配网指引；开机第一屏。
    s_home_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_home_scr, lv_color_hex(0x101418), 0);

    lv_obj_t *title = lv_label_create(s_home_scr);
    lv_obj_set_style_text_font(title, &op_font_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EDF2), 0);
    lv_label_set_text(title, "Octop Passport");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    s_home_status = lv_label_create(s_home_scr);
    lv_obj_set_style_text_font(s_home_status, &op_font_16, 0);
    lv_obj_set_style_text_color(s_home_status, lv_color_hex(0x8A96A3), 0);
    lv_label_set_text(s_home_status, "");
    lv_obj_align(s_home_status, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *steps = lv_label_create(s_home_scr);
    lv_obj_set_style_text_font(steps, &op_font_16, 0);
    lv_obj_set_style_text_color(steps, lv_color_hex(0xD7DEE6), 0);
    lv_obj_set_width(steps, 224);
    lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
    lv_label_set_text(steps,
        "还没有连接 Octop，\n"
        "三步配网：\n"
        "\n"
        "1. 长按「下」键打开配网\n"
        "2. 手机连接热点 OctopPassport\n"
        "3. 浏览器打开 192.168.4.1\n"
        "    填 Wi-Fi 和 Octop 账号\n"
        "\n"
        "完成后自动连接并登录。");
    lv_obj_align(steps, LV_ALIGN_TOP_LEFT, 8, 86);

    s_home_tip = lv_label_create(s_home_scr);
    lv_obj_set_style_text_font(s_home_tip, &op_font_16, 0);
    lv_obj_set_style_text_color(s_home_tip, lv_color_hex(0x6B7684), 0);
    lv_label_set_text(s_home_tip, "长按下键 重新配网");
    lv_obj_align(s_home_tip, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void op_ui_init(void)
{
    build_home_screen();
    build_read_screen();
    lv_screen_load(s_home_scr);
    s_on_home = true;
    op_pager_init(&s_pager, s_text_buf, sizeof(s_text_buf), WIDTH_UNITS, ROWS);
}

// 状态决定显示哪一屏：IDLE/WAIT_WIFI/ERROR → 引导首页；其余 → 阅读页。
static void select_screen(op_octop_state_t st)
{
    const bool want_home = (st == OP_ST_WAIT_WIFI || st == OP_ST_ERROR || st == OP_ST_IDLE);
    if (want_home == s_on_home) return;
    s_on_home = want_home;
    lv_screen_load(want_home ? s_home_scr : s_read_scr);
}

void op_ui_set_status(op_octop_state_t st, const char *detail)
{
    select_screen(st);
    char line[160];
    if (s_on_home) {
        snprintf(line, sizeof(line), "%s%s%s", op_octop_state_text(st),
                 detail && detail[0] ? " · " : "", detail && detail[0] ? detail : "");
        lv_label_set_text(s_home_status, line);
        return;
    }
    char head[48];
    if (detail && detail[0]) {
        snprintf(head, sizeof(head), "%s·%s", op_octop_state_text(st), detail);
    } else {
        snprintf(head, sizeof(head), "%s", op_octop_state_text(st));
    }
    const int soc = bsp_battery_soc();
    if (soc >= 0) {
        snprintf(line, sizeof(line), "%s %d%%", head, soc);
    } else {
        snprintf(line, sizeof(line), "%s --", head);
    }
    lv_label_set_text(s_status_label, line);
}

void op_ui_refresh_reader(void)
{
    if (s_on_home) return;
    op_pager_page_text(&s_pager, op_pager_page(&s_pager), s_page_buf, sizeof(s_page_buf));
    lv_label_set_text(s_body_label, s_page_buf);
    char hint[96];
    if (op_pager_truncated(&s_pager)) {
        snprintf(hint, sizeof(hint), "内容过长，全文见控制台 · %d/%d 页",
                 op_pager_page(&s_pager) + 1, op_pager_page_count(&s_pager));
    } else {
        snprintf(hint, sizeof(hint), "第 %d/%d 页 · OK=发送 上下=翻页",
                 op_pager_page(&s_pager) + 1, op_pager_page_count(&s_pager));
    }
    lv_label_set_text(s_hint_label, hint);
}

void op_ui_set_hint(const char *text)
{
    lv_label_set_text(s_hint_label, text);
    if (s_home_tip) lv_label_set_text(s_home_tip, text);  // 首页底部同步显示（按键自检）
}

void op_ui_set_note(const char *text)
{
    // 两个屏的顶部条共用语义：在哪个屏就更新哪个屏的提示。
    if (s_on_home && s_home_status) {
        lv_label_set_text(s_home_status, text);
        return;
    }
    if (s_status_label) lv_label_set_text(s_status_label, text);
}
