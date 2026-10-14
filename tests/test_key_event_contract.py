#!/usr/bin/env python3
"""Static contracts for the push-to-talk key chain (press → record → release).

P-M2 的「按住说话」合进来时就是死线路：op_app 处理 OP_KEY_OK_PRESS，但 main.c
的 translate() 只映射 CLICK/LONG，BSP_BTN_PRESS 从未被翻译出来 —— 录音永远不
开始，松开的 release_req 也没有配对的 press。这条链横跨三层（BSP 事件注册 →
按键翻译 → 应用语义），任何一层掉链子，屏上的表现都是「按了没反应」，所以用
静态断言钉住每一层的对位关系。
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    """函数体（不含外层花括号）；用于把断言限定在真正的相关代码里。"""
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class ButtonLayer(unittest.TestCase):
    def test_press_and_release_both_reach_the_app(self) -> None:
        source = read("components/bsp/src/bsp_button.c")
        body = function_body(source, "register_callbacks")
        self.assertIn("BUTTON_PRESS_DOWN, NULL, cb_press", body)
        self.assertIn("BUTTON_PRESS_UP, NULL, cb_release", body)

    def test_release_callback_carries_the_release_event(self) -> None:
        source = read("components/bsp/src/bsp_button.c")
        self.assertIn("on_event(a, u, BSP_BTN_RELEASE)", source)
        self.assertIn("BSP_BTN_RELEASE", read("components/bsp/include/bsp_button.h"))


class KeyTranslation(unittest.TestCase):
    def test_ok_press_and_release_are_translated(self) -> None:
        body = function_body(read("main/main.c"), "translate")
        self.assertIn("BSP_BTN_PRESS", body)
        self.assertIn("OP_KEY_OK_PRESS", body)
        self.assertIn("BSP_BTN_RELEASE", body)
        # \bOP_KEY_OK\b 不会命中 OP_KEY_OK_PRESS / OP_KEY_OK_LONG 的前缀。
        self.assertRegex(body, r"\bOP_KEY_OK\b")

    def test_no_key_enum_value_is_unnamed(self) -> None:
        """NAMES[] 与 op_key_t 必须一一对位：越界读在页脚上打的是垃圾。"""
        header = read("main/op_app.h")
        enum = re.search(r"typedef enum \{(.*?\} op_key_t;)", header, re.S)
        self.assertIsNotNone(enum, "op_key_t enum not found")
        keys = re.findall(r"\b(OP_KEY_\w+)", enum.group(1))  # type: ignore[union-attr]
        names = re.search(r"NAMES\[\] = \{([^}]*)\}", read("main/op_app.c"))
        self.assertIsNotNone(names, "NAMES[] not found")
        self.assertEqual(len(re.findall(r'"[^"]*"', names.group(1))), len(keys))  # type: ignore[union-attr]


class VoiceGuards(unittest.TestCase):
    def test_short_tap_is_dropped(self) -> None:
        source = read("main/op_voice.c")
        self.assertIn("#define REC_MIN_FRAMES", source)
        self.assertIn("s_v.frames < REC_MIN_FRAMES", source)

    def test_transcript_needs_two_codepoints_before_submit(self) -> None:
        """MiMo 对静音会幻觉出「嗯。」，放过去就是一次白跑的 Agent 轮次。"""
        source = read("main/op_voice.c")
        submit = source.index("op_octop_submit_transcript(text)")
        self.assertLess(source.index("codepoints(text) >= 2"), submit)

    def test_release_is_consumed_after_the_take(self) -> None:
        """快速点按时 release 常先于 voice_task 醒来（任务 50ms 一轮）。一开始就
        清 release 等于吞掉这一轮的松手，录音会一路顶到 30s 上限。"""
        body = function_body(read("main/op_voice.c"), "voice_task")
        self.assertLess(body.index("char *text = rec_and_transcribe()"),
                        body.index("s_v.release_req = false"))


if __name__ == "__main__":
    unittest.main()
