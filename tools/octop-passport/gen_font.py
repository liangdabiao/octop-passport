#!/usr/bin/env python3
"""生成本应用的中文 LVGL 字库子集。

字符清单是**推导出来的，从不手工维护**，来源有两处：

  * ``main/*.c|*.h`` 的字符串字面量里的每一个非 ASCII 字符（先剥掉注释，所以
    中文注释不占 Flash）。基线 demo（``demo_*`` / ``ui_pixel_*``）不参与固件
    构建，它们的字符串也不收 —— 收进来只会白白放大字库。
  * 可打印 ASCII 与界面自己绘制的标点

每个码点在转换前都会对着母字体核对一次，所以「漏字」是构建期错误，而不是设备上
的一个空白框。

依赖：``lv_font_conv``（npm，下面锁了版本）与 ``fonttools``。
母字体：Noto Sans CJK SC Regular（SIL Open Font License 1.1）。

用法（在仓库根目录）：
    python3 tools/astock-quote/gen_font.py            # 生成三档字库
    python3 tools/astock-quote/gen_font.py --check    # 只核对清单与母字体覆盖，不转换
    python3 tools/astock-quote/gen_font.py --download # 先下载母字体

环境变量覆盖：
    SB_SOURCE_FONT   母字体 OTF 路径
    LV_FONT_CONV      lv_font_conv 入口脚本路径
    NODE_EXE          用来运行转换器的 node 可执行文件
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "main"
FONT_DIR = ROOT / "assets" / "fonts"
CHARSET = FONT_DIR / "charset.txt"

# 基线 demo 的源码留在 main/ 里作参考但不参与构建 —— 它们的字符串不需要字形。
def _is_demo(path: Path) -> bool:
    return path.name.startswith(("demo_", "ui_pixel"))


SOURCE_FONT_NAME = "NotoSansCJKsc-Regular.otf"
SOURCE_FONT_URL = (
    "https://raw.githubusercontent.com/notofonts/noto-cjk/main/"
    "Sans/OTF/SimplifiedChinese/" + SOURCE_FONT_NAME
)
SOURCE_FONT_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
SOURCE_FONT_LICENSE = "SIL Open Font License 1.1"

DEFAULT_SOURCE_FONT = Path("D:/esp/fontsrc") / SOURCE_FONT_NAME
LV_FONT_CONV_VERSION = "1.5.3"

# 三档字号。16 = 正文阅读 + GB2312 全部汉字（Agent 回复是动态中文文章，
# 必须全覆盖）；24/32 = UI 子集（标题与说明文字）。
SIZES = (16, 24, 32)
BPP = 4

# 可打印 ASCII，加上界面字符串里出现的中文标点。界面标点本来会从源码字符串
# 收到，但这里再列一遍：文案改动若恰好删掉某个标点，字库不会因此悄悄缩水。
BASE_RANGES = ((0x20, 0x7E),)
EXTRA_SYMBOLS = "·，。！：…—"


def strip_comments(text: str) -> str:
    """去掉 // 与 /* */ 注释，同时保持字符串字面量原样。"""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"':
            out.append(ch)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if ch == "'":
            out.append(ch)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == "'":
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def source_characters() -> set[str]:
    """应用字符串字面量里用到的非 ASCII 字符。"""
    chars: set[str] = set()
    for path in sorted(list(MAIN.glob("*.c")) + list(MAIN.glob("*.h"))):
        if _is_demo(path):
            continue
        text = strip_comments(path.read_text(encoding="utf-8"))
        for literal in STRING_LITERAL_RE.findall(text):
            chars.update(c for c in literal if ord(c) > 0x7F)
    return chars


def gb2312_all() -> set[str]:
    """GB2312 全部汉字（一二级 6763 字）+ 符号区（、。《》「」全角数字等）。

    本应用正文是 Octop Agent 返回的动态中文文章，与 astock 的 ASCII 股名
    不同——没有字库全集就读不了文章；中文标点在符号区（A1 行），漏收的
    后果是真机上标点全变方框（真机踩过）。16px 一档收全集；24/32 仅 UI
    子集。"""
    chars: set[str] = set()
    for hi in range(0xA1, 0xF8):  # A1-A9 符号区 + B0-F7 汉字区
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars




def build_inventory(with_gb2312: bool) -> list[str]:
    chars: set[str] = set()
    chars.update(source_characters())
    if with_gb2312:
        chars.update(gb2312_all())  # 动态股名的字形全集（16px 用，2bpp）
    for start, end in BASE_RANGES:
        chars.update(chr(c) for c in range(start, end + 1))
    chars.update(EXTRA_SYMBOLS)
    chars.discard("\n")
    chars.discard("\r")
    chars.discard("\t")
    return sorted(chars)


UNICODE_LIST_RE = re.compile(r"static const uint16_t unicode_list_1\[\] = \{(.*?)\};", re.S)
SPARSE_CMAP_RE = re.compile(r"\{[^{}]*\.unicode_list = unicode_list_1[^{}]*\}")

ASCII_FIRST, ASCII_LAST = 0x20, 0x7E


def font_codepoints(path: Path) -> set[int]:
    """从生成好的 lv_font_conv 输出反推「设备真能渲染的码点」。

    稀疏段 ``unicode_list`` 里存的是相对该段 ``range_start`` 的偏移，所以必须先
    找到挂着 ``unicode_list_1`` 的那条 cmap **自己的** ``range_start`` —— 文件里
    还有一条 ASCII 的 cmap，取错它会把所有偏移算到错误的码点上。
    """
    text = path.read_text(encoding="utf-8")
    block = SPARSE_CMAP_RE.search(text)
    if block is None:
        raise ValueError(f"{path.name} 里找不到 unicode_list_1 对应的 cmap")
    start = int(re.search(r"\.range_start = (\d+)", block.group(0)).group(1))
    offsets = UNICODE_LIST_RE.search(text)
    if offsets is None:
        raise ValueError(f"{path.name} 里找不到 unicode_list_1")
    numbers = [int(token.strip(), 0)
               for token in offsets.group(1).replace("\n", " ").split(",") if token.strip()]
    return set(range(ASCII_FIRST, ASCII_LAST + 1)) | {start + n for n in numbers}


def load_cmap(path: Path) -> set[int]:
    from fontTools.ttLib import TTFont

    font = TTFont(str(path), fontNumber=0, lazy=True)
    cmap: set[int] = set()
    for table in font["cmap"].tables:
        cmap |= set(table.cmap.keys())
    font.close()
    return cmap


def describe(chars: list[str]) -> str:
    cjk = [c for c in chars if 0x4E00 <= ord(c) <= 0x9FFF]
    others = [c for c in chars if not (0x4E00 <= ord(c) <= 0x9FFF)]
    lines = [
        "# A股大行情（astock-quote）的字符清单。",
        "# 由 tools/astock-quote/gen_font.py 生成，请勿手改。",
        f"# 母字体：{SOURCE_FONT_NAME}（{SOURCE_FONT_LICENSE}）",
        f"# 码点总数：{len(chars)}（汉字 {len(cjk)}，其它 {len(others)}）",
        "",
        "# --- 汉字 ---",
        "".join(cjk),
        "",
        "# --- 其它（ASCII 与界面用到的中文标点）---",
        "".join(others),
        "",
    ]
    return "\n".join(lines)


def download_source_font(target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    print(f"下载 {SOURCE_FONT_URL}")
    with urllib.request.urlopen(SOURCE_FONT_URL, timeout=180) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_FONT_SHA256:
        raise SystemExit(f"母字体哈希不符：得到 {digest}，期望 {SOURCE_FONT_SHA256}")
    target.write_bytes(data)
    print(f"wrote {target.relative_to(ROOT)}（{len(data)} 字节）")


def resolve_source_font(explicit: str | None, download: bool) -> Path:
    path = Path(explicit or os.environ.get("SB_SOURCE_FONT") or DEFAULT_SOURCE_FONT)
    if download or not path.is_file():
        if not download:
            raise SystemExit(
                f"找不到母字体：{path}\n"
                f"用 --download 下载，或用 --font <path>，或设 SB_SOURCE_FONT。\n"
                f"期望 SHA-256：{SOURCE_FONT_SHA256}\n"
                f"期望许可：{SOURCE_FONT_LICENSE}"
            )
        download_source_font(path)
    return path


def resolve_converter() -> list[str]:
    explicit = os.environ.get("LV_FONT_CONV")
    if explicit:
        return [explicit]
    candidates = [
        Path("D:/esp/fonttools/node_modules/lv_font_conv/lv_font_conv.js"),
        ROOT / "node_modules" / "lv_font_conv" / "lv_font_conv.js",
    ]
    node = os.environ.get("NODE_EXE", "node")
    for candidate in candidates:
        if candidate.is_file():
            return [node, str(candidate)]
    raise SystemExit(
        "找不到 lv_font_conv。安装方式：\n"
        f"  npm install lv_font_conv@{LV_FONT_CONV_VERSION}\n"
        "然后用 LV_FONT_CONV 环境变量把它的路径传进来。"
    )


def run_converter(command: list[str], source_font: Path, symbols: str, size: int,
                  output: Path, bpp: int) -> None:
    args = command + [
        "--font", str(source_font),
        "--symbols", symbols,
        "--size", str(size),
        "--bpp", str(bpp),
        "--format", "lvgl",
        "--no-compress",
        "--lv-include", "lvgl.h",
        "--lv-font-name", f"op_font_{size}",
        "-o", str(output),
    ]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-4000:], file=sys.stderr)
        print(result.stderr[-4000:], file=sys.stderr)
        raise SystemExit(f"lv_font_conv 在字号 {size} 上失败")
    if result.stdout.strip():
        print(result.stdout.strip())


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", help="母字体 OTF 路径")
    parser.add_argument("--download", action="store_true", help="先下载母字体")
    parser.add_argument("--check", action="store_true",
                        help="只核对清单与母字体覆盖，不转换")
    args = parser.parse_args()

    ui_chars = build_inventory(with_gb2312=False)
    full_chars = build_inventory(with_gb2312=True)
    print(f"字符清单：UI {len(ui_chars)}；16px 全集 {len(full_chars)}")

    FONT_DIR.mkdir(parents=True, exist_ok=True)
    content_text = describe(full_chars)
    up_to_date = CHARSET.is_file() and CHARSET.read_text(encoding="utf-8") == content_text
    if up_to_date:
        print(f"unchanged: {CHARSET.relative_to(ROOT)}")
    elif args.check:
        print(f"STALE: {CHARSET.relative_to(ROOT)}", file=sys.stderr)
    else:
        CHARSET.write_text(content_text, encoding="utf-8", newline="\n")
        print(f"wrote: {CHARSET.relative_to(ROOT)}")

    source_font = resolve_source_font(args.font, args.download)
    cmap = load_cmap(source_font)
    missing = [c for c in full_chars if ord(c) not in cmap]
    if missing:
        preview = "".join(missing[:40])
        raise SystemExit(
            f"{len(missing)} 个码点在 {source_font.name} 里没有：{preview}\n"
            "要么是界面文案用了母字体不提供的字，要么是 EXTRA_SYMBOLS 引入了生僻符号。"
        )
    print(f"覆盖：{len(full_chars)}/{len(full_chars)} 个码点都在 {source_font.name} 里")

    if args.check:
        # 清单/字库与当前源码脱节是**硬错误**，不是提示：设备上缺字形会直接显示成
        # 方框，而这一条曾经只往 stderr 打印 STALE 就放行（社区审核就是这么退回的）。
        problems: list[str] = []
        if not up_to_date:
            problems.append(f"{CHARSET.relative_to(ROOT)} 与当前源码不一致（清单已过期）")
        for size in SIZES:
            generated = FONT_DIR / f"op_font_{size}.c"
            if not generated.is_file():
                problems.append(f"{generated.relative_to(ROOT)} 不存在")
                continue
            expect = full_chars if size == 16 else ui_chars
            absent = sorted(c for c in expect if ord(c) not in font_codepoints(generated))
            if absent:
                problems.append(
                    f"{generated.relative_to(ROOT)} 缺 {len(absent)} 个码点的字形："
                    + "".join(absent[:40]))
        if problems:
            for item in problems:
                print(f"FAIL: {item}", file=sys.stderr)
            print("重跑 tools/astock-quote/gen_font.py 生成字库后重试。", file=sys.stderr)
            return 1
        return 0

    command = resolve_converter()
    for size in SIZES:
        symbols = "".join(full_chars if size == 16 else ui_chars)
        bpp = 2 if size == 16 else BPP  # 16px 全集用 2bpp 控 Flash（约 1.4MB）
        output = FONT_DIR / f"op_font_{size}.c"
        run_converter(command, source_font, symbols, size, output, bpp)
        print(f"wrote: {output.relative_to(ROOT)}（{output.stat().st_size} 字节）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
