// main/op_octop.h —— Octop 客户端：登录 → 建线程 → WS 聊天的整条链路。
//
// 独立任务（不碰 LVGL）。P-M0 实测定型的协议事实（方案 v2 §10）：
//   - POST /api/auth/login {username,password} → {access_token}（24h，过期重登）
//   - POST /api/agents/{id}/threads → {thread_id}（每台设备一条专属线程，
//     user_turn 的 session_key 会被服务端忽略，必须显式带 thread_id）
//   - WS  /api/agents/{agent}/chat/ws?token=<jwt>
//       发 {"type":"user_turn","text":...,"thread_id":...}
//       收 HarnessChunk：只认 token(content)/error/done，其余 ~20 条
//       state_snapshot/state_update 噪音全部丢弃
// 回调（on_token/on_state/on_error）在**网络任务上下文**执行，
// 实现方自己拿 LVGL 锁（沿袭 sq_net 的约定）。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "op_store.h"

typedef enum {
    OP_ST_IDLE = 0,     // 未启动
    OP_ST_WAIT_WIFI,    // 等 Wi-Fi
    OP_ST_LOGIN,        // 登录中
    OP_ST_PICK_AGENT,   // 拉取/绑定 Agent
    OP_ST_NEW_THREAD,   // 建/取线程
    OP_ST_CONNECTING,   // WS 握手中
    OP_ST_READY,        // 已连接，可发问
    OP_ST_STREAMING,    // 回复流式接收中
    OP_ST_ERROR,        // 不可恢复错误（配置缺失/连续失败）
} op_octop_state_t;

// state 的可读文案（UI 直接用）。
const char *op_octop_state_text(op_octop_state_t st);

typedef struct {
    // 网络任务上下文回调。
    void (*on_state)(op_octop_state_t st, const char *detail, void *user);
    void (*on_token)(const char *utf8, void *user);      // 增量回复文本
    void (*on_turn_done)(bool ok, const char *detail, void *user);
    void *user;
} op_octop_callbacks_t;

void op_octop_start(const op_octop_callbacks_t *cb);

// 请求发出一轮（text 为已完成 STT 的文字；P-M1 里是按键注入的测试句）。
// 返回 false = 当前状态不可发（READY 之外）。
bool op_octop_send_turn(const char *text);

// 流式中取消本轮（WS cancel 帧）；READY 态为无害空操作。
void op_octop_cancel_turn(void);

// 丢弃 NVS 里的线程绑定，下一轮重新建线程（菜单「新对话」）。
void op_octop_new_thread(void);

op_octop_state_t op_octop_state(void);
const char *op_octop_diag(void);
unsigned op_octop_frames_rx(void);      // 收到的 WS 消息总数
const char *op_octop_last_type(void);   // 最近一帧的 type
const char *op_octop_thread_id(void);
