// main/op_store.h —— 业务配置的 NVS 持久化（namespace "octop"）。
// Wi-Fi 凭据归 esp-wifi-connect 组件的 SsidManager 管，这里只管 Octop 侧：
//   服务器地址 / 账号 / 密码 /（可选）STT 接口
//   登录态（JWT，24h，带签发时间用于过期判断）
//   绑定的 Agent 与专属 thread（P-M0 实测：user_turn 的 session_key 会被
//   服务端忽略，必须显式带 thread_id，见 docs 方案 v2 §10）
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define OP_STORE_URL_MAX     128
#define OP_STORE_USER_MAX    64
#define OP_STORE_PASS_MAX    64
#define OP_STORE_STT_URL_MAX 160
#define OP_STORE_STT_KEY_MAX 128
#define OP_STORE_JWT_MAX     512
#define OP_STORE_AGENT_MAX   32
#define OP_STORE_THREAD_MAX  48

typedef struct {
    char url[OP_STORE_URL_MAX];        // 形如 http://192.168.1.10:8088（不带尾斜杠）
    char user[OP_STORE_USER_MAX];
    char pass[OP_STORE_PASS_MAX];
    char stt_url[OP_STORE_STT_URL_MAX];  // 空 = 用 Octop 内置 /voice/stt
    char stt_key[OP_STORE_STT_KEY_MAX];
} op_config_t;

esp_err_t op_store_init(void);

// 读业务配置；has_config() 为假说明还没配过网，启动时该进配网门户。
esp_err_t op_store_load_config(op_config_t *cfg);
esp_err_t op_store_save_config(const op_config_t *cfg);
bool      op_store_has_config(void);

// 登录态与线程绑定（运行期更新，深睡/重启后复用）。
esp_err_t op_store_load_jwt(char *jwt, size_t cap, int64_t *issued_at);
esp_err_t op_store_save_jwt(const char *jwt, int64_t issued_at);
esp_err_t op_store_load_agent(char *agent_id, size_t cap);
esp_err_t op_store_save_agent(const char *agent_id);
esp_err_t op_store_load_thread(char *thread_id, size_t cap);
esp_err_t op_store_save_thread(const char *thread_id);
esp_err_t op_store_clear_thread(void);
