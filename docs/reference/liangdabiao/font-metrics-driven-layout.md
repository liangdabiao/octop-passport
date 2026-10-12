<p align="right">
  <a href="font-metrics-driven-layout.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Letting font metrics decide the layout

Recorded while building the Weiqi Quest app. Every layout
bug in this app came from a number that was guessed instead of measured. The fix
is to measure once, write the arithmetic down, and then let the compiler check it
forever.

## Measure the faces before placing anything

"How many characters fit on one line" is not a feeling, it is a division — but only
if glyph advance equals font size. For Noto Sans CJK SC the ideographs are
full-width, so the advance really does equal the point size, and the division is
exact:

```c
#define WQ_CHARS_HEADLINE 6   /* 32 px: 210 / 32 */
#define WQ_CHARS_BODY     8   /* 24 px: 210 / 24 */
#define WQ_CHARS_SMALL   13   /* 16 px: 210 / 16 */
```

(line heights are measured the same way: 16 px -> 20, 24 px -> 29, 32 px -> 38.)

**Measure it, do not assume it.** The claim "advance equals size" is a property of
this font, and it was verified before being trusted. A proportional face would make
this whole file wrong.

## Two forms of the same text, and both need a cap

The same level text appears in two places with different budgets, and only one of
them is the one you are looking at while writing the generator:

- the **brief page** carries the instruction, inside a scroll container, and is
  capped at **240 characters**;
- the **quiz ask page** carries the question at **190 characters** and each option
  at **10**.

Capping only the first one means a long quiz question passes generation and then
overflows on the device. Both are constants in `tools/weiqi/content.py`, and the
generator refuses to emit a level that exceeds either.

## The wrap buffer is sized from the worst caller, not the average one

The line-breaking buffer must hold the longest input **after** wrapping, and
wrapping adds a newline per line. Sizing it from the typical instruction is how you
get a buffer that works for 153 levels and silently truncates the 154th.

The same rule applies to the byte-vs-character trap: a UTF-8 CJK character is
**3 bytes** but **1 character**. A buffer sized as "240 characters" is 720 bytes
before the newlines and the terminator are counted. Getting this wrong shows up as
text that stops mid-sentence.

## Size every `snprintf` buffer for the widest conversion, and say so

The rule that is easy to skip because the failure is rare and ugly: size the buffer
for the widest value the conversion can produce, not for the widest value you
expect. `%d` can emit 11 characters including the sign; `%u` can emit 10. Write the
derivation in a comment next to the declaration, because "why is this 16 and not
8" is otherwise unanswerable a year later.

## Bind the generated budgets to the layout at compile time

The content limits live in generated headers; the layout constants live in
`main/wq_layout.h`. Those are two ends of the same number, and the only way to keep
them honest is to make the compiler compare them:

```c
_Static_assert(WQ_OPTION_TEXT_MAX_CHARS <= WQ_CHARS_SMALL,
               "option text no longer fits a 16px row");
```

Now changing a font size, a body width or a content cap without updating the other
side **fails the build**. That is the whole point: the relationship is stated once,
in a place the compiler reads.

## Measure the page budgets in one place

`main/wq_layout.h` deliberately does not include LVGL. The page code and the host
tests both include it, so both are looking at the same numbers:

```c
#define WQ_BODY_TOP     WQ_BAR_H                 /* 36  */
#define WQ_BODY_BOTTOM  (WQ_PAGE_H - WQ_HINT_H)  /* 284 */
#define WQ_BODY_H       (WQ_BODY_BOTTOM - WQ_BODY_TOP)  /* 248 */
```

A copy of these numbers inside a page file is how the two drift apart. Since the
constant header has no dependencies, there is no excuse for the copy.

## The vertical budget needs asserting too — and "scrollable" is not "reachable"

Two failures that screenshots and code review both miss:

**Vertical arithmetic needs its own assertions.** Horizontal budgets get caught by
the generator and the wrap tests. Vertical ones are usually only written in a
comment — and the symptom of a wrong `y` is that **the element is simply not
there**. The world-map page of this app laid out 7 rows at 32 px with a 4 px gap:
`7 * 32 + 6 * 4 = 248`, which is exactly the body height, and the first version
came to 252. A compile-time assertion caught it:

```c
_Static_assert(WQ_WORLD_ROWS * WQ_WORLD_ROW_H + (WQ_WORLD_ROWS - 1) * WQ_WORLD_GAP
               <= WQ_BODY_H, "the last chapter row will be clipped");
```

**And a scrollable container must have a key that scrolls it.** This device has
three buttons and no touch, so every scrollable region needs a button that moves
it. The check is one question per screen: *while this region is on screen, what
does each of the three physical keys do?* A page that forwards every key to the
state machine, in a state that only consumes OK, has a scroll container nobody can
reach — the second half of the content is invisible on the device and perfect in
the code.

The reverse constraint follows from it: **a page whose up/down keys are already
taken by something else cannot fall back on scrolling** — its content limit has to
be a hard cap that genuinely fits.

## Related

- [Turning a level bank into generated C tables](level-bank-pipeline.md) — where the
  content-side caps come from.
- [Subsetting a CJK font for LVGL](cjk-font-subsetting-for-lvgl.md) — the three
  sizes this arithmetic depends on.
- [Keeping application logic on the host](host-testable-app-logic.md) — the tests
  that assert the wrap results against the real content.
