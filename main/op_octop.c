// main/op_octop.c —— 见 op_octop.h。
#include "op_octop.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "op_net.h"
#include "op_store.h"
#include "op_wire.h"
#include "op_ws.h"

static const char *TAG = "op_octop";

#define WS_RX_CAP     (4 * 1024)   // 单条 HarnessChunk ~1KB、content 上限 2KB，4KB 富余。
#define TURN_TEXT_MAX 1024
#define TURN_ESC_MAX  (TURN_TEXT_MAX * 2 + 16)  // 中文原样过，控制字符才膨胀。
#define JWT_AGE_LIMIT_S (20 * 3600)              // 24h 有效期，20h 就主动重登。
#define RETRY_MAX_S   30

typedef struct {
    op_octop_callbacks_t cb;
    op_octop_state_t state;
    char detail[96];

    op_config_t cfg;
    char jwt[OP_STORE_JWT_MAX];
    char agent[OP_STORE_AGENT_MAX];
    char thread[OP_STORE_THREAD_MAX];

    // WS 资源（网络任务独占）。
    op_ws_t ws;
    uint8_t ws_rx[WS_RX_CAP];
    uint8_t ws_msg[WS_RX_CAP];

    volatile bool cancel_req;
    volatile bool new_thread_req;
    volatile bool turn_req;
    bool turn_closed;
    char diag[96];      // 屏上诊断（页脚显示）：断链原因 / token 计数
    volatile unsigned frames_rx;          // 收到的 WS 消息总数（含丢弃）
    char last_type[24];                   // 最近一帧的 type 字段          // 本轮收到 done/error（任务内标志，非跨线程）。
    char turn_text[TURN_TEXT_MAX];
} octop_t;

static octop_t s_o;

static void octo_task(void *arg);
static bool http_api(const char *method, const char *path, const char *body,
                     op_net_response_t *resp);

const char *op_octop_state_text(op_octop_state_t st)
{
    switch (st) {
        case OP_ST_IDLE:        return "未启动";
        case OP_ST_WAIT_WIFI:   return "等待网络";
        case OP_ST_LOGIN:       return "登录中";
        case OP_ST_PICK_AGENT:  return "选择 Agent";
        case OP_ST_NEW_THREAD:  return "建立会话";
        case OP_ST_CONNECTING:  return "连接中";
        case OP_ST_READY:       return "Octop 已连接";
        case OP_ST_STREAMING:   return "回复中";
        case OP_ST_ERROR:       return "连接失败";
        default:                return "?";
    }
}

static void set_state(op_octop_state_t st, const char *detail)
{
    s_o.state = st;
    snprintf(s_o.detail, sizeof(s_o.detail), "%s", detail ? detail : "");
    ESP_LOGI(TAG, "状态 → %s %s", op_octop_state_text(st), s_o.detail);
    if (s_o.cb.on_state) s_o.cb.on_state(st, s_o.detail, s_o.cb.user);
}

op_octop_state_t op_octop_state(void) { return s_o.state; }
const char *op_octop_diag(void) { return s_o.diag; }
unsigned op_octop_frames_rx(void) { return s_o.frames_rx; }
const char *op_octop_last_type(void) { return s_o.last_type; }
const char *op_octop_thread_id(void) { return s_o.thread; }

void op_octop_start(const op_octop_callbacks_t *cb)
{
    memset(&s_o, 0, sizeof(s_o));
    s_o.cb = *cb;
    op_store_load_config(&s_o.cfg);
    op_store_load_agent(s_o.agent, sizeof(s_o.agent));
    set_state(OP_ST_IDLE, NULL);
    xTaskCreate(octo_task, "op_octo", 10240, NULL, 5, NULL);  // run_turn 栈上 ~4.3KB（esc+frame）。
}

bool op_octop_send_turn(const char *text)
{
    // 排队制：服务端会关闭空闲的 dashboard WS，设备大部分时间在重连窗口里。
    // 若只在 READY 才收，按键会被静默吞掉（真机「按了没反应」的根因）。
    // 这里永远接收，octo_task 在链路就绪后的下一拍发出。
    snprintf(s_o.turn_text, sizeof(s_o.turn_text), "%s", text);
    s_o.turn_req = true;
    return true;
}

void op_octop_cancel_turn(void) { s_o.cancel_req = true; }

void op_octop_new_thread(void)
{
    s_o.new_thread_req = true;
    op_store_clear_thread();
}

// ------------------------------------------------------------ HTTP 步骤 ----
// cfg.url 形如 http://host:port（门户保存时去掉了尾斜杠与路径）。
static bool api_root(char *root, size_t cap)
{
    // 大众用户在门户里常只填 host:port——没有 scheme 时自动按 http:// 处理
    // （固件不做 TLS，https 见方案 v2 §4.3 的反代口径）。
    if (strncmp(s_o.cfg.url, "https://", 8) == 0) return false;
    if (strncmp(s_o.cfg.url, "http://", 7) == 0) {
        snprintf(root, cap, "%s", s_o.cfg.url);
    } else {
        snprintf(root, cap, "http://%s", s_o.cfg.url);
    }
    const size_t n = strlen(root);
    if (n && root[n - 1] == '/') root[n - 1] = '\0';
    return true;
}

// JWT 失效后走一遍 login → 返回 true 表示手上 jwt 可用。
static bool ensure_login(void)
{
    int64_t iat = 0;
    if (op_store_load_jwt(s_o.jwt, sizeof(s_o.jwt), &iat) == ESP_OK && s_o.jwt[0] &&
        iat > 0 && (int64_t)(esp_timer_get_time() / 1000000) - iat < JWT_AGE_LIMIT_S) {
        return true;  // NVS 里的登录态还年轻。
    }
    set_state(OP_ST_LOGIN, NULL);
    char body[320];
    char user_esc[OP_STORE_USER_MAX * 2], pass_esc[OP_STORE_PASS_MAX * 2];
    op_wire_json_escape(s_o.cfg.user, user_esc, sizeof(user_esc));
    op_wire_json_escape(s_o.cfg.pass, pass_esc, sizeof(pass_esc));
    snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}",
             user_esc, pass_esc);
    op_net_response_t r;
    if (!http_api("POST", "/auth/login", body, &r) || r.status != 200) {
        const int st = r.status;
        free(r.body);
        set_state(OP_ST_ERROR, st == 401 || st == 400 ? "账号或密码不对" : "登录连不上");
        return false;
    }
    if (op_wire_json_string(r.body, "access_token", s_o.jwt, sizeof(s_o.jwt)) < 0) {
        free(r.body);
        set_state(OP_ST_ERROR, "响应无 token");
        return false;
    }
    free(r.body);
    op_store_save_jwt(s_o.jwt, (int64_t)(esp_timer_get_time() / 1000000));
    return true;
}

static bool http_api(const char *method, const char *path, const char *body,
                     op_net_response_t *resp)
{
    char root[OP_STORE_URL_MAX];
    char host[96], base[256];
    int port;
    if (!api_root(root, sizeof(root)) ||
        !op_net_split_url(root, host, sizeof(host), &port, base, sizeof(base))) {
        set_state(OP_ST_ERROR, "服务器地址必须是 http://");
        memset(resp, 0, sizeof(*resp));  // 提前返回不经 op_net_request，调用方会
        return false;                    // free(resp->body)，必须先清零（真机踩过）。
    }
    char path_full[300], extra[OP_STORE_JWT_MAX + 32];
    snprintf(path_full, sizeof(path_full), "/api%s", path);
    if (s_o.jwt[0]) {
        snprintf(extra, sizeof(extra), "Authorization: Bearer %s\r\n", s_o.jwt);
    } else {
        extra[0] = '\0';
    }
    op_net_request(host, port, method, path_full, extra, body, resp);
    return resp->status != 0;
}

// 挑 Agent：优先 NVS 记住的（要还在列表里），否则取列表第一个。
static bool pick_agent(void)
{
    set_state(OP_ST_PICK_AGENT, NULL);
    op_net_response_t r;
    if (!http_api("GET", "/agents", NULL, &r)) {
        set_state(OP_ST_ERROR, "连不上服务器");
        return false;
    }
    if (r.status == 401) { free(r.body); return false; }  // 让上层重登。
    bool ok = false;
    if (s_o.agent[0]) {
        char needle[64];
        snprintf(needle, sizeof(needle), "\"agent_id\":\"%s\"", s_o.agent);
        if (strstr(r.body, needle)) ok = true;
        else s_o.agent[0] = '\0';
    }
    if (!ok) {
        const char *p = strstr(r.body, "\"agent_id\":\"");
        if (p) {
            p += strlen("\"agent_id\":\"");
            size_t i = 0;
            while (p[i] && p[i] != '"' && i < sizeof(s_o.agent) - 1) {
                s_o.agent[i] = p[i];
                i++;
            }
            s_o.agent[i] = '\0';
            ok = s_o.agent[0] != '\0';
            if (ok) op_store_save_agent(s_o.agent);
        }
    }
    free(r.body);
    if (!ok) set_state(OP_ST_ERROR, "账号下没有 Agent，请到控制台创建");
    return ok;
}

static bool ensure_thread(void)
{
    if (!s_o.new_thread_req && s_o.thread[0]) return true;
    if (!s_o.thread[0] &&
        op_store_load_thread(s_o.thread, sizeof(s_o.thread)) == ESP_OK && s_o.thread[0] &&
        !s_o.new_thread_req) {
        return true;
    }
    set_state(OP_ST_NEW_THREAD, NULL);
    char path[96];
    snprintf(path, sizeof(path), "/agents/%s/threads", s_o.agent);
    op_net_response_t r;
    if (!http_api("POST", path, "{}", &r)) {
        set_state(OP_ST_ERROR, "连不上服务器");
        return false;
    }
    if (r.status == 401) { free(r.body); return false; }
    if (r.status != 201 || op_wire_json_string(r.body, "thread_id", s_o.thread,
                                               sizeof(s_o.thread)) < 0) {
        ESP_LOGW(TAG, "建线程失败 status=%d body=%.160s", r.status,
                 r.body ? r.body : "(空)");
        free(r.body);
        set_state(OP_ST_ERROR, "建线程失败");
        return false;
    }
    free(r.body);
    op_store_save_thread(s_o.thread);
    s_o.new_thread_req = false;
    ESP_LOGI(TAG, "线程: %s", s_o.thread);
    return true;
}

// ------------------------------------------------------------ WS 收发 ----
static void handle_ws_message(const char *msg)
{
    char type[24];
    if (op_wire_json_string(msg, "type", type, sizeof(type)) < 0) {
        s_o.frames_rx++;
        snprintf(s_o.last_type, sizeof(s_o.last_type), "?无type");
        return;
    }
    s_o.frames_rx++;
    snprintf(s_o.last_type, sizeof(s_o.last_type), "%.23s", type);
    if (strcmp(type, "token") == 0) {
        char content[2048];
        if (op_wire_json_string(msg, "content", content, sizeof(content)) > 0) {
            if (s_o.cb.on_token) s_o.cb.on_token(content, s_o.cb.user);
        }
    } else if (strcmp(type, "error") == 0) {
        char detail[96];
        if (op_wire_json_string(msg, "message", detail, sizeof(detail)) < 0) {
            snprintf(detail, sizeof(detail), "服务端错误");
        }
        s_o.turn_closed = true;
        if (s_o.cb.on_turn_done) s_o.cb.on_turn_done(false, detail, s_o.cb.user);
    } else if (strcmp(type, "done") == 0) {
        s_o.turn_closed = true;
        if (s_o.cb.on_turn_done) s_o.cb.on_turn_done(true, NULL, s_o.cb.user);
    }
    // state_snapshot / state_update / usage 等一律忽略（P-M0 实测 ~21 条/轮）。
}

// 连 WS。失败置 ERROR/返回 false。
static bool ws_connect(void)
{
    set_state(OP_ST_CONNECTING, NULL);
    char root[OP_STORE_URL_MAX];
    char host[96], base[256];
    int port;
    if (!api_root(root, sizeof(root)) ||
        !op_net_split_url(root, host, sizeof(host), &port, base, sizeof(base))) {
        set_state(OP_ST_ERROR, "服务器地址必须是 http://");
        return false;
    }
    char path[768];
    snprintf(path, sizeof(path), "/api/agents/%s/chat/ws?token=%s", s_o.agent, s_o.jwt);
    if (!op_ws_connect(&s_o.ws, host, port, path, s_o.ws_rx, sizeof(s_o.ws_rx),
                       s_o.ws_msg)) {
        set_state(OP_ST_ERROR, "WS 连不上，稍后重试");
        return false;
    }
    set_state(OP_ST_READY, s_o.agent);
    return true;
}

// 完整跑一轮：发 user_turn → 等 done/error → 回 READY。
// 返回 true = 正常收尾；false = 链路断了（上层走重连）。
static bool run_turn(void)
{
    set_state(OP_ST_STREAMING, NULL);
    char esc[TURN_ESC_MAX];
    op_wire_json_escape(s_o.turn_text, esc, sizeof(esc));
    char frame[TURN_ESC_MAX + 128];
    snprintf(frame, sizeof(frame),
             "{\"type\":\"user_turn\",\"text\":\"%s\",\"thread_id\":\"%s\"}",
             esc, s_o.thread);
    if (!op_ws_send_text(&s_o.ws, frame)) {
        snprintf(s_o.diag, sizeof(s_o.diag), "发送失败 %s", s_o.ws.last_err);
        ESP_LOGW(TAG, "user_turn 发送失败");
        return false;
    }
    s_o.turn_closed = false;
    s_o.cancel_req = false;

    int idle_polls = 0, tokens = 0;
    bool got_frame = false;
    int64_t last_stat = esp_timer_get_time();
    while (!s_o.turn_closed) {
        if (s_o.cancel_req) {
            char cframe[OP_STORE_THREAD_MAX * 2 + 32];
            snprintf(cframe, sizeof(cframe), "{\"type\":\"cancel\",\"thread_id\":\"%s\"}",
                     s_o.thread);
            op_ws_send_text(&s_o.ws, cframe);
            s_o.cancel_req = false;
        }
        uint8_t op;
        uint8_t *payload;
        size_t plen;
        const int r = op_ws_poll(&s_o.ws, 500, &op, &payload, &plen);
        if (r == 0) {
            // 首帧 45s 不到：本轮几乎必然排在了积压队列尾部（历次按压在同一
            // 线程上堆积，逐轮串行处理）。换一条全新线程重发——新会话永远在
            // 队首，秒回。旧线程留在服务端，控制台仍可查看。
            if (!got_frame && idle_polls > 90) {
                snprintf(s_o.diag, sizeof(s_o.diag), "45s无首帧，换新线程");
                ESP_LOGW(TAG, "45s 无首帧，换新线程重发");
                s_o.new_thread_req = true;
                op_store_clear_thread();
                s_o.turn_req = true;  // 原句排队到新线程
                return false;
            }
            if (++idle_polls > 720) {  // 首帧已到：流中 6 分钟无帧才判僵死。
                snprintf(s_o.diag, sizeof(s_o.diag), "360s无数据 tok=%d", tokens);
                ESP_LOGW(TAG, "WS 360s 无数据，判僵死 tokens=%d", tokens);
                return false;
            }
            continue;
        }
        if (r < 0) {
            snprintf(s_o.diag, sizeof(s_o.diag), "断 tok=%d %s", tokens,
                     s_o.ws.last_err[0] ? s_o.ws.last_err : "socket错");
            ESP_LOGW(TAG, "链路断 tokens=%d %s", tokens, s_o.diag);
            // 半开连接（服务端掐空闲）会让 user_turn 发进黑洞：本轮一个帧
            // 都没收到就断 → 重连后自动重发，否则按键等于丢失。
            if (tokens == 0) s_o.turn_req = true;
            return false;
        }
        idle_polls = 0;
        got_frame = true;
        tokens++;
        snprintf(s_o.diag, sizeof(s_o.diag), "tok=%d", tokens);
        if (esp_timer_get_time() - last_stat >= 5000000) {
            ESP_LOGI(TAG, "流式中 tokens=%d", tokens);
            last_stat = esp_timer_get_time();
        }
        handle_ws_message((const char *)payload);
    }
    ESP_LOGI(TAG, "本轮完成 tokens=%d", tokens);
    set_state(OP_ST_READY, s_o.agent);
    return true;
}

// ------------------------------------------------------------ 任务 ----
static void octo_task(void *arg)
{
    (void)arg;
    int backoff = 1;
    for (;;) {
        if (!op_store_has_config()) {
            set_state(OP_ST_WAIT_WIFI, "请长按下键打开配网");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        // 账号/登录/Agent/线程，任何一步 401 都重登一次再试。
        bool need_relogin = false;
        if (!ensure_login()) { vTaskDelay(pdMS_TO_TICKS(backoff * 1000)); goto bump; }
        if (!pick_agent()) {
            if (s_o.state == OP_ST_ERROR) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
            need_relogin = true;
        }
        if (!need_relogin && !ensure_thread()) {
            if (s_o.state == OP_ST_ERROR) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
            need_relogin = true;
        }
        if (need_relogin) {
            s_o.jwt[0] = '\0';
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!ws_connect()) { vTaskDelay(pdMS_TO_TICKS(backoff * 1000)); goto bump; }
        backoff = 1;

        // 已连接：服务每轮请求或保持心跳。排队中的按键在就绪后立即补发。
        for (;;) {
            if (s_o.turn_req) {
                s_o.turn_req = false;
                if (!run_turn()) break;  // 链路坏 → 重连。
                continue;  // 链路可能还活着，立即再看有没有排队的下一发。
            }
            uint8_t op;
            uint8_t *payload;
            size_t plen;
            const int r = op_ws_poll(&s_o.ws, 500, &op, &payload, &plen);
            if (r < 0) {
                // 对照实验：记录确切断因（对照我的 ping 是否为死因）。
                ESP_LOGW(TAG, "空闲链路断：%s", s_o.ws.last_err[0] ? s_o.ws.last_err : "socket错");
                break;
            }
            if (r > 0) handle_ws_message((const char *)payload);
        }
        op_ws_close(&s_o.ws);
        if (s_o.state == OP_ST_STREAMING) set_state(OP_ST_READY, s_o.agent);
        set_state(OP_ST_CONNECTING, "掉线，重连中");
bump:
        vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
        backoff = backoff < RETRY_MAX_S ? backoff * 2 : RETRY_MAX_S;
    }
}
