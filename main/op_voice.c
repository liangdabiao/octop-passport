// main/op_voice.c —— 按住 OK 说话：录音 → 流式上传 STT → 转录文本排队发问。
//
// 数据通路（无大缓冲：C3 无 PSRAM，30s 原始 PCM 要 960KB，堆装不下）：
//   按下 OK → 立即向 STT 端点发起 chunked POST（multipart，字段 audio，
//   WAV 头尺寸写占位——识别端按实际流长解析）→ 本任务 10ms 一帧循环
//   bsp_audio_read → 攒 200ms 写一个 chunked 块 → 松开 OK 发终止块 →
//   读 {"text":...} → 交给 op_octop 排队发送。
//
// STT 两态（op_store.stt_url）：
//   空   → Octop 内置 {octop}/api/voice/stt（Bearer JWT，字段 audio+language）
//   有值 → 自定义端点（OpenAI 兼容形：POST stt_url，字段 file，Bearer stt_key）
#include "op_voice.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_audio.h"

#include "op_net.h"
#include "op_octop.h"
#include "op_store.h"
#include "op_wire.h"

static const char *TAG = "op_voice";

#define SAMPLE_BYTES     640            // 10ms @16k/16bit/mono
#define CHUNK_FRAMES     20             // 20 帧 × 10ms = 200ms 一个 chunk
#define REC_MAX_FRAMES   (30 * 100)     // 单次录音硬上限 30s（100 帧/秒）
#define REC_MIN_FRAMES   40             // 短于 0.4s 视为误触（见 op_app 的页脚承诺）
#define STT_BODY_MAX     (4 * 1024)

typedef enum {
    V_IDLE = 0,
    V_REC,        // 按住中：边录边传
    V_WAIT_STT,   // 已松开：等 STT 返回转录
} v_state_t;

static struct {
    v_state_t state;
    volatile bool press_req;       // 输入任务置位
    volatile bool release_req;
    char status[32];

    op_net_stream_t st;
    uint8_t frame[SAMPLE_BYTES];
    char chunk[SAMPLE_BYTES * CHUNK_FRAMES];
    size_t chunk_len;
    uint32_t frames;               // 已录/已发的 10ms 帧数
} s_v;

const char *op_voice_status_text(void)
{
    if (s_v.state == V_IDLE && s_v.status[0] && strstr(s_v.status, "失败")) {
        return s_v.status;  // 失败原因保留显示，直到下一次动作
    }
    switch (s_v.state) {
        case V_REC:      return "录音中";
        case V_WAIT_STT: return "识别中";
        default:         return "";
    }
}

static void set_state(v_state_t st)
{
    s_v.state = st;
    // 运行期日志是关掉的，屏上的失败原因是用户唯一的反馈来源：回到空闲时
    // 不能把它冲掉（旧写法紧跟着就把「麦克读取失败」刷成「空闲」，屏上只剩
    // 一句笼统的「没反应」）。
    if (st == V_IDLE && strstr(s_v.status, "失败")) return;
    snprintf(s_v.status, sizeof(s_v.status), "%s",
             st == V_REC ? "录音中" : st == V_WAIT_STT ? "识别中" : "空闲");
    ESP_LOGI(TAG, "%s", s_v.status);
}

// ------------------------------------------------------ STT 目标解析 ----
// 输出：host/port/path（请求行用）+ extra_headers（鉴权头）。
// octop 模式：路径 {octop}/api/voice/stt，Bearer JWT。
// custom 模式：stt_url 原样（OpenAI 兼容形），Bearer stt_key。
static bool stt_target(char *host, size_t host_cap, int *port, char *path,
                       size_t path_cap, char *extra, size_t extra_cap)
{
    op_config_t cfg = {0};
    if (op_store_load_config(&cfg) != ESP_OK || cfg.url[0] == '\0') return false;

    char base[OP_STORE_STT_URL_MAX];
    if (cfg.stt_url[0]) {
        // custom：stt_url 本身是完整 URL（必须 http://，固件不做 TLS）。
        if (strncmp(cfg.stt_url, "http://", 7) != 0) return false;
        snprintf(base, sizeof(base), "%s", cfg.stt_url);
        if (cfg.stt_key[0]) {
            snprintf(extra, extra_cap, "Authorization: Bearer %s\r\n", cfg.stt_key);
        } else {
            extra[0] = '\0';
        }
    } else {
        // Octop 内置：{octop}/api/voice/stt + 当前 JWT。
        if (strncmp(cfg.url, "http://", 7) != 0) return false;
        snprintf(base, sizeof(base), "%s/api/voice/stt", cfg.url);
        const char *jwt = op_octop_jwt();
        snprintf(extra, extra_cap, "Authorization: Bearer %s\r\n", jwt ? jwt : "");
    }

    const char *slash = strchr(base + 7, '/');
    if (!slash) return false;
    const size_t hl = (size_t)(slash - (base + 7));
    if (hl == 0 || hl >= host_cap) return false;
    memcpy(host, base + 7, hl);
    host[hl] = '\0';
    *port = 80;
    const char *colon = strchr(host, ':');
    if (colon) {
        *port = atoi(colon + 1);
        if (*port <= 0) return false;
    }
    if (strlen(slash) >= path_cap) return false;
    strcpy(path, slash);
    return true;
}

// WAV 44 字节头：16k/16bit/mono。尺寸字段写占位值——流式上传时长度未知，
// 识别端（Tencent/Whisper 系）都按实际流读，占位不影响解析。
static void wav_header(char out[44], uint32_t data_hint)
{
    const uint32_t riff = 36 + data_hint;
    const uint32_t byte_rate = 16000 * 2;
    memcpy(out + 0, "RIFF", 4);
    memcpy(out + 4, &riff, 4);
    memcpy(out + 8, "WAVEfmt ", 8);
    const uint32_t fmt = 16;
    memcpy(out + 16, &fmt, 4);
    const uint16_t pcm = 1, ch = 1;
    memcpy(out + 20, &pcm, 2);
    memcpy(out + 22, &ch, 2);
    const uint32_t rate = 16000;
    memcpy(out + 24, &rate, 4);
    memcpy(out + 28, &byte_rate, 4);
    const uint16_t align = 2, bits = 16;
    memcpy(out + 32, &align, 2);
    memcpy(out + 34, &bits, 2);
    memcpy(out + 36, "data", 4);
    memcpy(out + 40, &data_hint, 4);
}

// ------------------------------------------------------ 录音 → STT ----
// 返回转录文本（heap，调用方 free）；失败返回 NULL。
static char *rec_and_transcribe(void)
{
    char host[96], path[192], extra[OP_STORE_STT_KEY_MAX + 32];
    if (!stt_target(host, sizeof(host), &s_v.st.port, path, sizeof(path),
                    extra, sizeof(extra))) {
        return NULL;
    }

    static const char BOUNDARY[] = "OPPassportVoice";
    char pre[256];
    const int pre_n = snprintf(pre, sizeof(pre),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"a.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        BOUNDARY);

    char wav[44];
    wav_header(wav, 0xFFFFF0);  // 尺寸占位：流式上传长度未知，识别端按流读。

    if (!op_net_stream_post_begin(&s_v.st, host, s_v.st.port, path, extra)) {
        ESP_LOGW(TAG, "STT 连接失败");
        return NULL;
    }
    if (!op_net_stream_write(&s_v.st, pre, (size_t)pre_n) ||
        !op_net_stream_write(&s_v.st, wav, sizeof(wav))) {
        op_net_stream_abort(&s_v.st);
        ESP_LOGW(TAG, "STT 请求头发送失败");
        return NULL;
    }

    // 录音循环：10ms 一帧，攒 200ms 写一个 chunked 块。松开/30s 到顶即收尾。
    // 音频编解码懒初始化（P-M1 无音频所以启动流程没初始化；幂等）。
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败，无法录音");
        snprintf(s_v.status, sizeof(s_v.status), "音频初始化失败");
        op_net_stream_abort(&s_v.st);
        return NULL;
    }
    set_state(V_REC);
    s_v.chunk_len = 0;
    s_v.frames = 0;
    int read_fails = 0;
    while (!s_v.release_req && s_v.frames < REC_MAX_FRAMES) {
        if (bsp_audio_read(s_v.frame, SAMPLE_BYTES) != ESP_OK) {
            // 连续失败 1 秒（约 200 次×5ms）视为音频链路坏，快速中止。
            if (++read_fails > 200) {
                ESP_LOGE(TAG, "麦克风连续读取失败，中止录音");
                op_net_stream_abort(&s_v.st);
                snprintf(s_v.status, sizeof(s_v.status), "麦克读取失败");
                return NULL;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        read_fails = 0;
        memcpy(s_v.chunk + s_v.chunk_len, s_v.frame, SAMPLE_BYTES);
        s_v.chunk_len += SAMPLE_BYTES;
        s_v.frames++;
        if (s_v.chunk_len >= sizeof(s_v.chunk) - SAMPLE_BYTES) {
            if (!op_net_stream_write(&s_v.st, s_v.chunk, s_v.chunk_len)) {
                ESP_LOGW(TAG, "STT 上传断");
                return NULL;
            }
            s_v.chunk_len = 0;
        }
    }

    // 误触护栏：不足 0.4s 的按抬直接丢。半秒不到的静音喂给 MiMo，它会幻觉出
    // 「嗯。」，等于凭空向 Agent 发了一轮提问（P-M0 实测）。
    if (s_v.frames < REC_MIN_FRAMES) {
        ESP_LOGW(TAG, "录音过短（%u 帧），本轮丢弃", (unsigned)s_v.frames);
        op_net_stream_abort(&s_v.st);
        snprintf(s_v.status, sizeof(s_v.status), "说太短，识别失败");
        return NULL;
    }

    // 收尾：冲掉残余块 + language 字段 + 终止边界 → 读响应。
    if (s_v.chunk_len > 0 &&
        !op_net_stream_write(&s_v.st, s_v.chunk, s_v.chunk_len)) {
        ESP_LOGW(TAG, "STT 收尾发送失败");
        return NULL;
    }
    char trailer[128];
    const int tn = snprintf(trailer, sizeof(trailer),
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
        "zh-CN\r\n"
        "--%s--\r\n",
        BOUNDARY, BOUNDARY);
    if (!op_net_stream_write(&s_v.st, trailer, (size_t)tn)) {
        ESP_LOGW(TAG, "STT 收尾发送失败");
        return NULL;
    }

    op_net_response_t resp;
    op_net_stream_finish(&s_v.st, &resp);
    char *text = NULL;
    if (resp.status == 200 && resp.body) {
        static char out[2048];
        if (op_wire_json_string(resp.body, "text", out, sizeof(out)) > 0) {
            text = strdup(out);
        } else {
            ESP_LOGW(TAG, "STT 响应无 text 字段");
        }
    } else {
        ESP_LOGW(TAG, "STT 失败 status=%d body=%.120s", resp.status,
                 resp.body ? resp.body : "");
    }
    free(resp.body);
    return text;
}

// ------------------------------------------------------ 任务 ----
// UTF-8 码点数（只数非续字节）。
static int codepoints(const char *s)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0) != 0x80) n++;
    }
    return n;
}

static void voice_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_v.press_req) {
            s_v.press_req = false;
            s_v.chunk_len = 0;
            s_v.frames = 0;

            char *text = rec_and_transcribe();
            // 松手标志在收尾后才清：本任务 50ms 才醒一轮，快速点按时 release
            // 往往已经落在 press 之后——开头就清掉会把这一轮的松手吞了，录音
            // 一路顶到 30s 上限。
            s_v.release_req = false;
            set_state(V_IDLE);
            // 少于两个字一律当「没听清」：空转录、或 MiMo 幻觉出的一两个字
            // （「嗯。」），放过去都是一次白跑的 Agent 轮次。
            if (text && codepoints(text) >= 2) {
                op_octop_submit_transcript(text);
            } else {
                // rec_and_transcribe 里已经写了具体判因（麦克坏、说太短）就保留，
                // 否则笼统报「没听清」。
                if (!strstr(s_v.status, "失败")) {
                    snprintf(s_v.status, sizeof(s_v.status), "没听清，识别失败");
                }
                ESP_LOGW(TAG, "转录过短/为空，本轮丢弃：%.40s", text ? text : "(空)");
            }
            free(text);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void op_voice_init(void)
{
    memset(&s_v, 0, sizeof(s_v));
    xTaskCreate(voice_task, "op_voice", 8192, NULL, 5, NULL);
}

void op_voice_press(void)   { s_v.press_req = true; }
void op_voice_release(void) { s_v.release_req = true; }
