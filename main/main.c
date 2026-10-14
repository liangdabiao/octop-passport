// main/main.c —— Octop Passport 的入口。
//
// 只做三件事：把 BSP 外设点起来、把按键事件搬进队列、把界面交给 op_app 管。
// Wi-Fi/配网、Octop 客户端、分页阅读都在 op_* 模块里，这里刻意保持薄。
//
// 硬件只有三个键（上/下/确定，共用一个 ADC 分压引脚），语义：
//   上/下 短按     阅读页翻页
//   确定 按住/松开 说话（按下起录，松开送识别）；若正在回复则同时取消本轮
//   下    长按     打开配网门户
//
// 输入任务的栈给到 8KB：页面处理链很浅，默认 3584 偏紧（LVGL 调用 +
// snprintf + 折行缓冲都在这条链上），翻一倍留足余量。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "op_app.h"
#include "op_store.h"

static const char *TAG = "octop";

#define INPUT_QUEUE_DEPTH 8
#define INPUT_TASK_STACK  8192
#define INPUT_TASK_PRIO   5

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 按键回调运行在共享的 esp_timer 任务上：只做一次非阻塞入队，立刻返回。
// 任何慢操作（提示音、NVS、LVGL）都不允许出现在这里。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = event };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static bool translate(const input_event_t *input, op_key_t *out)
{
    // 按住说话要的是「按下/松开」这一对事件。CLICK 只在短按后出现，长按被
    // LONG 吃掉后再也没有收尾事件——录音会一直顶到 30s 上限。所以 OK 的
    // 起录用 PRESS、停录用 RELEASE。
    if (input->btn == BSP_BTN_OK) {
        if (input->event == BSP_BTN_PRESS)   { *out = OP_KEY_OK_PRESS; return true; }
        if (input->event == BSP_BTN_RELEASE) { *out = OP_KEY_OK;       return true; }
    }
    // P-M1：双击不做事；确定长按暂不映射（原型的返回键没有对位页面）。
    if (input->event == BSP_BTN_LONG) {
        switch (input->btn) {
            case BSP_BTN_DOWN: *out = OP_KEY_DOWN_LONG; return true;
            case BSP_BTN_OK:   *out = OP_KEY_OK_LONG;   return true;
            default:           return false;
        }
    }
    if (input->event != BSP_BTN_CLICK) return false;

    switch (input->btn) {
        case BSP_BTN_UP:   *out = OP_KEY_UP;   return true;
        case BSP_BTN_DOWN: *out = OP_KEY_DOWN; return true;
        default:           return false;
    }
}

static void input_task(void *argument)
{
    (void)argument;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) != pdTRUE) continue;
        op_key_t key;
        if (translate(&input, &key)) op_app_key(key);
    }
}

static esp_err_t input_start(void)
{
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;

    if (xTaskCreate(input_task, "op_input", INPUT_TASK_STACK, NULL,
                    INPUT_TASK_PRIO, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        vTaskDelete(s_input_task);
        s_input_task = NULL;
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return err;
    }
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Octop Passport 启动");

    // 自发重启排查：把复位原因打出来（值含义见 esp_reset_reason() 头文件注释；
    // 1=panic 2=int_wdt 3=task_wdt 5=brownout 6=sw 7=sw_cpu ...）。
    ESP_LOGI(TAG, "复位原因: %d", esp_reset_reason());

    const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "I2C 初始化失败: %s", esp_err_to_name(err));
    (void)bsp_i2c_scan();

    // 屏幕是这个应用的主要出口，点不亮就没得玩 —— 打清楚日志后停在这里。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败。检查 SPI 接线"
                      "(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 通关进度存档。存不了也照常能玩，只是重启后进度从头开始。
    err = op_store_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "进度存储不可用: %s", esp_err_to_name(err));

    // P-M1 不用音频（无 TTS/音效）；BSP 音频留待 P-M2 录音时再初始化。
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，顶栏会显示 --");
    }

    const esp_err_t input_err = input_start();
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(input_err));
    }

    if (bsp_lvgl_lock(1000)) {
        op_app_start();
        bsp_lvgl_unlock();
        // 界面就绪之后才放行按键，避免开机瞬间的按键落到还不存在的页面上。
        s_input_ready = true;
    }

    ESP_LOGI(TAG, "就绪:显示=OK 按键=%s", input_err == ESP_OK ? "OK" : "FAIL");

    // USJ 控制台在主机未打开串口时写入会忙等（FIFO 永远满），任何日志都可能
    // 冻死调用任务——真机「按 OK 后死机」的根因。诊断走屏幕页脚（op_app），
    // 运行期关闭全部日志；需要串口调试时用 JTAG 或临时移除这一行。
    esp_log_level_set("*", ESP_LOG_NONE);
}
