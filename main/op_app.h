// main/op_app.h —— 应用总控：初始化次序、按键语义、网络回调 → UI 的转发。
#pragma once

#include <stdbool.h>

typedef enum {
    OP_KEY_UP = 0,
    OP_KEY_DOWN,
    OP_KEY_OK,
    OP_KEY_OK_PRESS,  // OK 按下瞬间（P-M2 语音：开始录音）
    OP_KEY_OK_LONG,   // 长按确定（保留）
    OP_KEY_DOWN_LONG, // 长按下 = 打开配网门户
} op_key_t;

void op_app_start(void);
void op_app_key(op_key_t key);
