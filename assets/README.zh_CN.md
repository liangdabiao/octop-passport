<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 素材

固件里代码之外的一切。本应用只带**字体** —— 没有音频素材（音效与两段背景乐都是
运行时 RTTTL 合成的，见 `main/sb_sfx.c`），也没有位图（桥图由控件画出，见
`main/sb_ui.c`）。

| 文件 | 是什么 | 来源 |
| --- | --- | --- |
| [`fonts/charset.txt`](fonts/charset.txt) | 推导出来的字符清单：一段注释头 + 全部码点 | `tools/seven-bridges/gen_font.py` 生成；请勿手改 |
| [`fonts/op_font_16.c`](fonts/op_font_16.c) | 16px LVGL 字库子集 | 生成物；Noto Sans CJK SC，SIL OFL 1.1 |
| [`fonts/op_font_24.c`](fonts/op_font_24.c) | 24px LVGL 字库子集 | 生成物；Noto Sans CJK SC，SIL OFL 1.1 |
| [`fonts/op_font_32.c`](fonts/op_font_32.c) | 32px LVGL 字库子集 | 生成物；Noto Sans CJK SC，SIL OFL 1.1 |

## 清单怎么来的

`tools/seven-bridges/gen_font.py` 把两处来源并在一起，所以字库永远不会与界面文案
脱节：`main/` 下每个字符串字面量里的非 ASCII 字符（先剥注释；基线 demo 源码不收
—— 它们不参与固件构建），加上固定基底的 ASCII 与界面标点（含星评用的 ★☆）。
转换前每个码点都会对一遍母字体的 cmap —— 缺字是构建期错误，不是设备上的空框。

`gen_font.py --check` 会重新推导清单并反解析生成好的 `.c`（读稀疏 cmap 表），
断言每个码点真的在字库里；`tools/validate.sh` 在静态门禁里跑它。

## 母字体

Noto Sans CJK SC Regular，SHA-256
`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`，
SIL Open Font License 1.1 —— 允许子集化并随固件分发。下载地址与转换命令见
`tools/seven-bridges/gen_font.py`。

## 没有内容管线

与家族里的问答/闯关应用不同，七桥谜题**没有外部关卡库和内容表**：十二关全部是
`main/sb_level.c` 里的手工图，其图论事实（奇度、可解性、终章「拆任意一桥必可解」）
由宿主测试 `tests/test_sb_level.c` 钉死。音频是两段 RTTTL 循环加十道短音效，
全部在 `main/sb_sfx.c` 里合成。
