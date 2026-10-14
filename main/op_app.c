// main/op_app.c —— 见 op_app.h。
//
// P-M1 按键语义（验证全链路用；P-M2 把「OK 短按」换成按住说话）：
//   上/下 短按   阅读页翻页（翻页即停跟随）
//   确定  短按   READY → 发一条预设测试句；STREAMING → 取消本轮
//   下    长按   打开配网门户（热点 OctopPassport，浏览器 192.168.4.1）
#include "op_app.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

#include "bsp_display.h"
#include "lvgl.h"

#include "op_octop.h"
#include "op_store.h"
#include "op_wire.h"
#include "op_ui.h"
#include "op_wifi.h"
#include "op_voice.h"
#include <string.h>
#include "fap_screenshot.h"

static const char *TAG = "op_app";

// 网络任务上下文回调 → 需要碰 LVGL 的都丢到 LVGL 定时器里做（不直接拿锁，
// 避免 esp_timer/任务上下文与 LVGL 抢锁的家族老坑）。用一组 volatile 快照 +
// 20ms 周期定时器统一上屏。
static volatile bool s_dirty_status;
static volatile bool s_dirty_reader;
static volatile bool s_reset_reader;  // 新一轮开始，正文清空重来
static char s_status_detail[96];
static char s_last_hint[96];   // 页脚去重（与 ui_timer 配合）
static char s_last_note[32];   // 状态栏语音提示去重
static volatile unsigned s_key_events;  // 按键事件计数（页脚显示：按键链路自检）
static char s_last_key[16] = "-";  // 最近一次按键名
static char s_stream_tail[96];   // 流式期间的提示（收到字节数）

static void on_state(op_octop_state_t st, const char *detail, void *user)
{
    (void)user;
    snprintf((char *)s_status_detail, sizeof(s_status_detail), "%s", detail ? detail : "");
    // 分页器只有 append、没有轮次边界：上一轮的长文会一直占着 16KB 缓冲，
    // 写满后 truncated 永久置位，append 直接变空操作——真机表现为「几轮之后
    // 再也不出字」。每轮开始先把正文清空。
    if (st == OP_ST_STREAMING) s_reset_reader = true;
    s_dirty_status = true;
}

static void on_token(const char *utf8, void *user)
{
    (void)user;
    // 流式 token 不能丢：拿不到锁就等（网络任务阻塞 200ms 可接受，
    // LVGL 一帧渲染远快于此）。锁超时丢弃会直接缺字。
    if (!bsp_lvgl_lock(200)) {
        ESP_LOGW("op_app", "token 丢弃：LVGL 锁 200ms 不可得");
        return;
    }
    op_pager_append(op_ui_pager(), utf8);
    bsp_lvgl_unlock();
    s_dirty_reader = true;
}

// 配网门户提交的业务字段（JSON 对象字符串，字段可选）。在 HTTPD 上下文回调，
// 只做 NVS 写入；日志只报键的存在，不落任何值。
static void on_portal_save(const char *json)
{
    op_config_t cfg = {0};
    op_store_load_config(&cfg);  // 字段可选：没填的保留旧值。
    int saved = 0;
    if (op_wire_json_string(json, "octop_url", cfg.url, sizeof(cfg.url)) > 0) saved++;
    if (op_wire_json_string(json, "octop_user", cfg.user, sizeof(cfg.user)) > 0) saved++;
    if (op_wire_json_string(json, "octop_pass", cfg.pass, sizeof(cfg.pass)) > 0) saved++;
    if (op_wire_json_string(json, "stt_url", cfg.stt_url, sizeof(cfg.stt_url)) > 0) saved++;
    if (op_wire_json_string(json, "stt_key", cfg.stt_key, sizeof(cfg.stt_key)) > 0) saved++;
    ESP_LOGI(TAG, "门户业务字段：收到 %d 项（url=%s user=%s stt_url=%s）",
             saved, cfg.url[0] ? "有" : "无", cfg.user[0] ? "有" : "无",
             cfg.stt_url[0] ? "有" : "无");
    op_store_save_config(&cfg);
}

static void on_turn_done(bool ok, const char *detail, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "本轮结束 ok=%d %s", ok, detail ? detail : "");
    (void)detail;
    s_dirty_reader = true;
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    // 新一轮开始：清空正文重来。本回调跑在 LVGL 任务上（锁已持有），网络任务
    // 的 append 被同一把锁挡在门外，所以清空不会和追加打架。
    if (s_reset_reader) {
        s_reset_reader = false;
        op_pager_reset(op_ui_pager());
        op_ui_refresh_reader();
    }
    // 语音状态上状态栏（录音中/识别中/上次失败原因）。文字变了才刷；且不再
    // 提前 return ——旧写法一次识别失败就把整条刷新链永久挡住，屏上再也不出字。
    const char *vs = op_voice_status_text();
    if (vs[0]) {
        if (strcmp(vs, s_last_note) != 0) {
            snprintf(s_last_note, sizeof(s_last_note), "%s", vs);
            op_ui_set_note(vs);
        }
    } else {
        s_last_note[0] = '\0';
    }
    // 页脚收帧统计：文字变化才 set_text（lv_label_set_text 每次都会重排重绘，
    // 20ms 一次的无条件刷新在单核 C3 上是持续渲染风暴，会饿死低优先级任务
    // ——真机表现为按键完全无响应）。
    char hint[96];
    snprintf(hint, sizeof(hint), "按键%sx%u rx=%u %s",
             s_last_key, s_key_events, op_octop_frames_rx(), op_octop_diag());
    if (strcmp(hint, s_last_hint) != 0) {
        snprintf(s_last_hint, sizeof(s_last_hint), "%s", hint);
        op_ui_set_hint(hint);
    }
    if (s_dirty_status) {
        s_dirty_status = false;
        op_ui_set_status(op_octop_state(), s_status_detail);
    }
    if (s_dirty_reader) {
        s_dirty_reader = false;
        // 跟随模式才自动刷新到末页；手动翻页中不抢页。
        if (op_pager_following(op_ui_pager())) {
            op_pager_follow_end(op_ui_pager());
        }
        op_ui_refresh_reader();
        // 屏当调试台：页脚带 token 计数与断链原因（串口日志被截屏驱动改道后
        // 基本不可见，真机排障全靠屏）。
        char hint[96];
        snprintf(hint, sizeof(hint), "tok | %s", op_octop_diag());
        op_ui_set_hint(hint);
    }
}

void op_app_start(void)
{
    op_ui_init();
    lv_timer_create(ui_timer_cb, 20, NULL);
    fap_screenshot_start();

    op_voice_init();  // 语音任务（按住 OK 说话）；缺失会导致按键无任何反应。
    op_wifi_set_custom_save(on_portal_save);
    op_wifi_start(NULL, NULL);

    static const op_octop_callbacks_t cb = {
        .on_state = on_state,
        .on_token = on_token,
        .on_turn_done = on_turn_done,
        .user = NULL,
    };
    op_octop_start(&cb);
}

void op_app_key(op_key_t key)
{
    // 按键指示：真机排障期把按键事件计数落到页脚（ui_timer 带出）。
    s_key_events++;
    static const char *const NAMES[] = { "UP", "DOWN", "OK", "OK按", "OK长", "DOWN长" };
    snprintf(s_last_key, sizeof(s_last_key), "%s", NAMES[(int)key]);
    switch (key) {
        case OP_KEY_UP:
            if (!bsp_lvgl_lock(500)) return;
            op_pager_nav(op_ui_pager(), -1);
            op_ui_refresh_reader();
            bsp_lvgl_unlock();
            break;
        case OP_KEY_DOWN:
            if (!bsp_lvgl_lock(500)) return;
            op_pager_nav(op_ui_pager(), 1);
            op_ui_refresh_reader();
            bsp_lvgl_unlock();
            break;
        case OP_KEY_OK_PRESS:
            op_voice_press();  // 按住 OK 说话（P-M2 语音路径）
            break;
        case OP_KEY_OK:
            // 松开 OK：结束录音 → STT。快速点按（<0.4s）由 op_voice 内部丢弃；
            // 若正在流式回复 → 取消本轮。
            op_voice_release();
            if (op_octop_state() == OP_ST_STREAMING) {
                op_octop_cancel_turn();
            }
            break;
        case OP_KEY_DOWN_LONG:
            op_wifi_open_config_ap();
            break;
        default:
            break;
    }
}
