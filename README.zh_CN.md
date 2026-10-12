<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# Octop Passport —— 把 AI Passport 变成自托管 Octop 的随身语音终端

按住 OK 说话，松开后读到自己这句话的文字回复：设备把录音转成文字，发给
**你自己部署的** [Octop](https://github.com/TencentCloud/Octop) Agent，流式
回复逐段显示在 240×320 屏上，长文自动分页、上下键翻页。整条链路不需要手机、
不需要安装任何 App——配网与绑定 Octop 账号都在设备自己的热点门户里完成。

> 状态：P-M1 骨架开发中（协议链路已在 PC 模拟器上对真实 Octop 实例验证，
> 语音录入 STT 与文章净化渲染在 P-M2）。当前按键：OK 短按发送一条预设测试句。

## 它会做什么（目标形态）

| 你 | 设备 |
| --- | --- |
| 按住 OK 说话，松开 | 录音流式上传语音识别 → 转录文字回显（可回看）|
| 等待 | 文字发给你绑定的 Octop Agent；屏幕显示连接状态 |
| 阅读 | 回复逐 token 流式渲染：Markdown 精简（代码块/图片/链接按阅读需求降级）、满页自动跟随，上下键翻页，OK 短按取消本轮 |
| 长按下键 | 打开配网门户：Wi-Fi、Octop 服务器地址与账号、绑定 Agent，一次提交 |

对话存在你自己的 Octop 里（每台设备一条专属 thread），网页控制台可以看全文；
设备端只保留最近一轮的阅读缓冲（约 48 页），超长内容提示去控制台看全文。

## 安全边界

- 设备直连你自己的 Octop（局域网 http/ws，或自备 HTTPS 反代）；语音与对话
  不经过任何第三方中间服务。
- Octop 账号密码保存在设备 NVS，仅用于登录换取 JWT（24h，自动续登）。
  固件不含任何预置凭据。
- 设备 NVS 无静态加密：转交设备前请先在门户改绑账号。

## 刷进去 / 自己编

ESP-IDF 5.5.3，目标 `esp32c3`：

```bash
idf.py set-target esp32c3
idf.py build                      # 日常
./tools/validate.sh               # 完整门禁 + 逐字节校验的完整镜像
```

完整镜像 `build/FoloToy-AI-Passport-full.bin` 从 `0x0` 写入（网页刷机或
`esptool.py --chip esp32c3 write_flash 0x0 ...`）。首次开机会进入配网热点
`OctopPassport-XXXX`，手机浏览器打开 `192.168.4.1` 按表单填写即可。

## 往下读

- [`docs/development/baby-toy` 式交接文档尚待建立；当前设计文档见工作区
  `octop-passport-方案v2-纯固件直连.md`（含 P-M0 实测协议事实）]
- [`docs/development/ai-guide.zh_CN.md`](docs/development/ai-guide.zh_CN.md)：AI 协作工作流
- [`AGENTS.zh_CN.md`](AGENTS.zh_CN.md)：仓库规范与任务路由

## 许可与致谢

代码沿用家族基线的 [Apache-2.0](LICENSE)。BSP、构建门禁与工程文档来自
FoloToy AI Passport 家族各仓库；Wi-Fi 配网组件
[esp-wifi-connect](https://github.com/78/esp-wifi-connect)（Apache-2.0）含
本仓库的门户字段扩展补丁；中文字库为思源黑体（SIL OFL 1.1）子集。
Octop 为腾讯开源（MIT）项目，与本项目无隶属关系。
