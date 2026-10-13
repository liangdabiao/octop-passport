// main/op_voice.h —— 按住 OK 说话：录音 → 流式上传 STT → 转录文本交给 op_octop。
//
// 交互（P-M2）：
//   按下 OK      开始录音（立即向 STT 发起 chunked POST，边录边传）
//   松开 OK      结束录音 → 等 STT 返回转录 → 转录自动作为一轮发问发出
//   按住 <0.4s   视为误触，丢弃
//   最长 30s     自动截断
//
// STT 两态（op_store.stt_url）：
//   空    → Octop 内置 POST {octop}/api/voice/stt（Bearer JWT，字段 audio/language）
//   有值  → 自定义端点（OpenAI 兼容形：POST stt_url，字段 file，Bearer stt_key）
//
// 本模块自带任务与麦克风读取；完成后通过 op_octop_submit_transcript() 把
// 文本交给 op_octop 排队发送。状态经 op_voice_status_text() 供状态栏显示。
#pragma once

void op_voice_init(void);

// 按键路径调用（输入任务上下文）：只置标志，毫秒级返回。
void op_voice_press(void);   // OK 按下：开始录音
void op_voice_release(void); // OK 松开：结束录音 → STT → 发问

// 状态栏文案（当前动作），op_app 的 ui_timer 轮询。
const char *op_voice_status_text(void);
