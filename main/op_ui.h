// main/op_ui.h —— 屏幕层：状态栏 + 正文阅读区 + 页脚。LVGL 对象只在本模块
// 碰（持 bsp_lvgl_lock 后），数据来自 op_app / op_octop / op_pager。
#pragma once

#include <stdbool.h>

#include "op_octop.h"
#include "op_pager.h"

void op_ui_init(void);

// 分页器本体（UI 模块持有缓冲；op_app 的按键/回调直接操作它，改完调
// op_ui_refresh_reader()，注意 LVGL 锁）。
op_pager_t *op_ui_pager(void);

// 状态栏一行（连接状态 + detail）；网络任务经 op_app 转发调用。
void op_ui_set_status(op_octop_state_t st, const char *detail);

// 阅读页全量刷新（分页器内容变了；在 LVGL 锁内调用）。
void op_ui_refresh_reader(void);

// 底部提示行（按键提示 / 页码 / 截断标记）。
void op_ui_set_hint(const char *text);
