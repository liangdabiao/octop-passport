// main/op_wifi.h —— WiFi 连接与配网的唯一入口（esp-wifi-connect 组件的 C 封装）。
//
// 组件（C++，hot-daily/english-podcast 同款，本硬件验证过）自己管理：凭据
// NVS、STA 自动重连、配网热点 + 192.168.4.1 门户。应用侧只需要这几个 C 钩子。
// 全部 C++ 细节（Initialize/事件回调/StartConfigAp/StartStation）都住在
// op_wifi.cc 里，main.c 保持纯 C。
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 WiFi 并按凭据状态自动选择模式：有存档 → STA 自动连；无存档 →
// 开配网热点。on_connected / on_disconnected 在 WiFi 事件上下文回调（不许
// 调 LVGL；实现方只记状态，UI 轮询或经锁后刷新）。
void op_wifi_start(void (*on_connected)(void), void (*on_disconnected)(void));

// 手动打开配网热点（标题页「Wi-Fi 配网」）。手机连热点后访问 192.168.4.1。
void op_wifi_open_config_ap(void);

// 清除已存凭据并重开配网热点（配网页的「重新配网」）。
void op_wifi_reset_credentials(void);

// 注册配网门户「业务字段」保存回调（octop_url 等，JSON 对象字符串）。
// 门户提交时在网络/HTTPD 上下文回调，实现方只做 NVS 写入。
void op_wifi_set_custom_save(void (*cb)(const char *json_payload));

// 当前是否已联网（STA 拿到 IP）。
bool op_wifi_is_connected(void);

// 显式设置 DNS（hot-daily 真机教训：DHCP DNS 会导致首次 HTTP 超时）。
void op_wifi_ensure_dns(void);

#ifdef __cplusplus
}
#endif
