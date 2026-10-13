<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# Octop Passport

把 AI Passport 变成**你自己部署的 Octop** 的随身语音终端：按住 OK 说话，
松开后设备把录音转成文字发给你自己的 Octop Agent，回复逐字流式显示在
240×320 屏上，超长自动分页，上下键翻页阅读。

基于 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 硬件
（ESP32-C3、240×320 屏、麦克风、扬声器、三键、520mAh 电池）与
[TencentCloud/Octop](https://github.com/TencentCloud/Octop)（MIT，自托管
多用户 AI 助手平台）。与 Meta、Muse 或 FoloToy 官方没有隶属或背书关系。

```mermaid
flowchart LR
    P[随身设备：按住 OK 说话] <-- BLE 配对 --> A["手机（Octop App 或网页控制台）"]
    A <-- 手机网络 --> S[你的 VPS / 家服：自托管 Octop]
```

## 它会做什么

| 你 | 设备 |
| --- | --- |
| **按住 OK 说话，松开** | 录音自动上传语音识别（按住即传，无需等待），屏幕显示「录音中→识别中」 |
| 等几秒 | 你说的话变成文字自动发给 Agent，回复**逐字流式**上屏 |
| 长回复 | 自动分页，页脚显示「第 x/y 页」；上下键翻页，翻到哪读到哪 |
| 标点 | GB2312 汉字+常用符号全覆盖，中文标点无缺字 |
| 连续对话 | 每台设备一个专属会话线程，电脑控制台可随时看全文 |

识别中文≠设备听得懂一切：识别准确率由你在 Octop 后台配置的语音识别服务
决定（详见下文「语音识别配置」）。

## 你需要准备

| 项 | 要求 |
| --- | --- |
| 硬件 | FoloToy AI Passport（8MB Flash 版），USB 数据线 |
| 服务器 | 自托管 [Octop](https://github.com/TencentCloud/Octop)（`http://IP:8088`），手机/电脑能访问 |
| 账号 | Octop 里建一个专用用户 + 一个 Agent |
| 语音识别 | Octop 后台「语音设置」里启用任意 STT（MiMo / 腾讯云 / OpenAI 均可，带 Key） |
| 网络 | 设备与手机/电脑同一 2.4G Wi-Fi，且能访问你的 Octop 地址 |

## 三步开始用

1. **长按下键约 1 秒**——设备打开配网热点 `OctopPassport`
2. **手机连接该热点**，浏览器打开 `192.168.4.1`
3. 按表单填写：**家里 Wi-Fi**（选 SSID 输密码）→ **Octop 服务器地址**
   （如 `http://150.109.81.50:8088`，带不带 `http://` 都行）→ **用户名/密码**
   （建议专用账号）→ 点 Connect

设备验证通过后自动重启并连接：状态栏显示「Octop 已连接」即可使用。
建议同时给这个账号在 Octop 后台「语音设置」里启用一个语音识别服务
（MiMo STT / 腾讯云 ASR / OpenAI Whisper 任意一种，按提示填 Key）。

## 按键

| 操作 | 效果 |
| --- | --- |
| **按住 OK 说话，松开** | 录音 → 识别 → 自动发问（最长 30 秒，<0.4 秒视为误触） |
| 短按 OK（回复进行中） | 取消当前回复 |
| 上 / 下键 | 翻页阅读（流式中随时可翻，翻到哪读到哪） |
| 长按下键 1 秒 | 重新配网（热点方式同上） |

## 语音识别配置（二选一）

**方式 A：Octop 内置（推荐，设备零配置）**
在 Octop 控制台 → 语音设置 → 选择并启用一个 STT 服务并填好 Key。
设备自动使用它；**以后在后台换识别厂商，设备无需任何操作**。

**方式 B：自定义 STT 接口**
配网页暂未开放自定义 STT 字段时，可在 Octop 侧用支持 OpenAI
`/v1/audio/transcriptions` 形状的网关中转，或将 `stt_url`/`stt_key`
写入设备（后续版本开放门户表单字段）。

## 常见问题

- **状态栏「连接失败 · 连不上服务器」**：设备在自动重试。先确认手机/电脑
  能打开你的 Octop 地址；服务器恢复后设备自动连上。
- **「识别失败」**：检查 Octop 语音设置里的 STT 服务与 Key；服务器收到音频
  但识别失败时屏幕不显示转录。
- **翻页按了不动**：当前回复不足一页（末页钳位是正常行为）；长回复才会
  出现多页。
- **忘了 Wi-Fi 密码 / 想换服务器**：长按下键重新进入配网。

## 更新固件

- **完整镜像**（`FoloToy-AI-Passport-full.bin`，从 `0x0` 写入）：会清空设备
  已存配置（Wi-Fi/Octop 账号），刷完需重新配网。首次安装用这种方式。
- **应用升级**（只写 `0x10000` 应用区）：保留已存配置，适合已配网设备的
  版本升级；要求新旧版本分区布局一致（本项目内升级均满足）。

## 隐私与安全

- 录音与文字只发送到**你自己部署的 Octop**（及其语音识别服务），无任何
  第三方中转。
- Octop 账号密码保存在设备 NVS，**未加密**：转交设备前请长按下键重新配网
  覆盖账号，或在 Octop 后台改密。
- 每台设备在 Octop 里是独立用户/会话，控制台可随时查看与删除历史。

## 从源码构建

ESP-IDF **5.5.3**，目标 `esp32c3`：

```sh
git clone <本仓库> && cd octop-passport
source <你的 ESP-IDF>/export.sh
idf.py set-target esp32c3
idf.py build                # 或 ./tools/validate.sh 走完整门禁并产出发布镜像
```

中文字库由 `tools/octop-passport/gen_font.py` 从源码字符串与 GB2312 全集
推导生成（需 Node 的 lv_font_conv 与 fontTools）。

## 许可与致谢

- 本项目代码沿用上游 [Apache-2.0](LICENSE)；BSP 与工程骨架来自
  FoloToy AI Passport 家族各仓库。
- Wi-Fi 配网组件 [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect)
  （Apache-2.0），含本仓库的门户字段扩展补丁。
- 中文字库：Noto Sans CJK SC（SIL Open Font License 1.1）子集。
- [Octop](https://github.com/TencentCloud/Octop) 为腾讯开源（MIT）项目；
  本项目与其无隶属关系。
