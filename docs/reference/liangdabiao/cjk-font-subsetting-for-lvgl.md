<p align="right">
  <a href="cjk-font-subsetting-for-lvgl.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Subsetting a CJK font for LVGL

Recorded while building the Weiqi Quest app. The device
carries three font subsets (16 / 24 / 32 px) holding **603 code points** — 480 CJK
ideographs plus 123 others. That is the smallest budget in this repository family,
and it is small only because the inventory is derived rather than maintained.

## Derive the inventory from the sources, never maintain it by hand

`tools/weiqi/gen_font.py` builds the list from two places:

1. every non-ASCII character inside a **string literal** of `main/*.c|*.h`
   (comments are stripped first, so Chinese comments cost no flash), and
2. the character set that `tools/weiqi/content.py` says the level bank uses.

A hand-maintained list drifts the first time someone edits the interface and
forgets. Deriving it means the list cannot be stale by construction — and the
generated `assets/fonts/charset.txt` records exactly what went in:

```
# total code points: 603  (CJK 480, other 123)
```

The space character deserves a warning of its own: it is trivially lost by any
step that trims the inventory, and losing it makes every space in the interface
render as nothing.

## Verify every code point before converting

Before calling the converter, the script loads the parent font with `fontTools`
and checks every code point against its cmap. A missing glyph becomes a
**build-time error naming the character**, not an empty box discovered on a
device three days later. The parent font is Noto Sans CJK SC Regular (SIL Open
Font License 1.1), and its SHA-256 is pinned in the script.

## Three sizes, uncompressed, and why the large format is required

The subsets are generated at 16 / 24 / 32 px, 4 bits per pixel, `--no-compress`,
`--format lvgl`. Uncompressed because the device reads the bitmaps straight out of
mapped flash; there is no reason to decode them at runtime.

`CONFIG_LV_FONT_FMT_TXT_LARGE=y` is mandatory here, and the reason is a hard
16-bit limit: the default LVGL text format stores the bitmap offset as a 16-bit
field, which caps a font's bitmaps at 65,536 bytes. The 32 px subset alone is
**233,660 bytes**. The flag switches that field to 32 bits.

Note the shape of this decision: it depends on the *current* glyph count, so it is
worth re-deriving rather than inheriting the previous app's answer. A smaller
subset can flip the conclusion back.

## The size of the generated `.c` file is not the flash cost

The three generated files are **2,857,006 bytes of text** but cost **431,919 bytes
of flash**:

| Subset | `.c` source | `glyph_bitmap` in flash |
| --- | --- | --- |
| 16 px | 464,759 B | 61,805 B |
| 24 px | 906,674 B | 136,454 B |
| 32 px | 1,485,573 B | 233,660 B |
| **total** | **2,857,006 B** | **431,919 B** |

Six and a half times smaller, because every byte is written as `0xAB,` — six
characters of source per byte of data. So never size a partition, or judge a font
budget, from the `.c` files.

The only trustworthy measurement is the **`glyph_bitmap` section in the link-time
`.map` file**:

```
grep -A2 "^ \.rodata\.glyph_bitmap" build/firmware/<sha>/FoloToy-AI-Passport.map
```

Do not try to count `0xXX` occurrences in the source either: the converter writes
values below 16 as a **single** hex digit (`0x0,`, `0xf,`), so a two-digit pattern
undercounts by nearly half.

## A stale font is the easiest mistake to miss

Two things make this failure mode nasty:

- a font that no longer matches the interface renders as a row of empty boxes,
  which looks like a rendering bug rather than a build problem, and
- the obvious check does not catch it: the first version of `--check` here only
  compared the inventory file, printed `STALE` to stderr, **and still exited 0**.
  A gate that passes on stale data is not a gate.

So `gen_font.py --check` now does two things, and both are non-zero on failure:

1. the inventory file must match what the sources derive, and
2. the generated `.c` must actually contain every code point in that inventory.

For (2) the script parses the generated file and reconstructs the renderable set.
Two details are worth writing down, because getting either wrong produces a
**catastrophic-looking** result (every character reported missing):

- The converter emits **two** cmap entries, and **both** must be counted. One is a
  contiguous range (`.unicode_list = NULL`, `range_start = 32, range_length = 95`
  for ASCII); the other is a sparse table (`.unicode_list = unicode_list_N`) whose
  code points are its *own* `range_start` plus the list values. Counting only the
  sparse one reports all of ASCII as missing; counting only the contiguous one
  loses every CJK glyph. The reliable discriminator is whether `.unicode_list` is
  `NULL`, not the numeric value of `range_start`.
- If the reconstruction reports that *everything* is missing, the parser is wrong,
  not the font. That symptom has exactly one cause.

## Related

- [Seven Bridges application record](seven-bridges/README.md) — this repository's record; the same techniques applied to the Seven Bridges puzzle.
  serve.
- [Turning a level bank into generated C tables](level-bank-pipeline.md) — the
  other half of the inventory.
- [Letting font metrics drive the layout](font-metrics-driven-layout.md) — what the
  generated sizes are used for.
