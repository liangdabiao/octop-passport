<p align="right">
  <a href="level-bank-pipeline.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Turning a level bank into generated C tables

Recorded while building the Weiqi Quest app, a Go puzzle
ladder whose 154 levels were curated out of 3564 in the web version. The pipeline
is small enough to describe in one page, and every rule in it exists because
breaking it produced a specific failure.

## The problem: the content is a board position, the device wants a table

A level is not a record with a fixed shape. It carries up to 43 stones in two
colours, a set of lettered marks, up to four solution paths, an instruction that
runs to 240 characters, and — for the 32 quiz levels — a question with options.

The shape that matters most is the one that is easy to miss: **a level is a
tree, not a record.** Up to four solution paths can start from the same position,
and the opponent's reply depends on which path the player is still matching. A
flat row of strings cannot express that, so the data model is a struct with a
small array of paths hanging off it:

```c
typedef struct {
    uint8_t len;
    const uint8_t *moves;   /* point indices, len of them */
} wq_path_t;
```

Everything else stays boring on purpose: 154 levels of plain pointers is about
6 KB and any reader can see what it says.

## One file, one block per level, and a format that is boring on purpose

The source of truth is `tools/weiqi/levels.txt`. One block per level, fixed key
order, one value per line:

```
== 001 fundamentals-rules-intro-3
chapter: fundamentals
kind: puzzle
title: <level title shown in the top bar>
player: white
black: cg
white: bgchcf
mark: cross:dg
correct: dg
instruction: <the level text, up to 240 characters>
```

Coordinates are two-letter goban codes inside the 9x9 window (`aa` top-left,
`ii` bottom-right). Stones are written as two lines, one per colour. Solution
paths are separated by `|` and each path is a run of two-letter codes starting
with the player's move.

The format is deliberately dull so that every failure is a loud one. A missing
key, an unknown key, a repeated key, a coordinate outside the window, a solution
path that does not start with the player, an odd number of stones in a line —
all abort the generator rather than quietly shipping a level with a default.

**Why one file and not one file per level.** The previous app in this repository
used one file per chapter, because chapters were hundreds of lines each. Here a
level is nine or ten short lines, so 154 files would be 154 diff noise sources
and a directory listing nobody reads. One block per level in one file still gives
a clean diff — a change shows as one line in one block — while keeping the whole
bank reviewable in a single scroll (1703 lines).

## The extractor runs once; after that the file is the source

`extract_levels.mjs` holds a real lesson from the previous app: **solution paths
are data, never scrape them with regexes.** It *evaluates* the web game's
TypeScript chapters with esbuild and reads the resulting objects, rather than
pattern-matching the source text. That also lets it handle the two coordinate
dialects the web game mixes (goban letter pairs and human coordinates like `b3`
with the *i* column skipped).

It is a **one-shot bootstrap tool**: it refuses to overwrite an existing
`levels.txt` unless given `--force`, and its header says so. Otherwise there
would be two sources of truth, and editing the bank would be silently undone the
next time someone re-ran the extractor.

## Cropping is where a level can quietly become unsolvable

The web game is 19x19; the device is always 9x9 — one board size means one
renderer and one set of layout arithmetic. So every candidate level is cropped
by the bounding box of its stones, solution moves and marks, sliding a 9x9 window
over the original (centre first).

**Cropping is not a pure reframing.** Cutting the board can sever a group from
its liberties, or remove the stones that made a snapback work. So the extractor
**re-validates every solution path under the full rules** — liberties, captures,
suicide, simple ko — on the cropped board, and drops any level that stops being
solvable. The window search then rescues most of them.

The numbers this produced: 94% of candidate levels (2696 of 2860) fit inside a
9x9 window once cropped and re-validated. That is the only reason the 9x9
decision was allowed to stand.

## Generator constraints are arithmetic, not taste

The same rule as the previous app, with the numbers re-derived for this content:

| Budget | Value | Where it comes from |
| --- | --- | --- |
| Level title | 13 characters | one 16px line: 210 px / 16 px |
| Instruction | 240 characters | the scrolling brief page, three screens of 13-char lines |
| Question | 190 characters | the quiz brief page |
| Option text | 10 characters | three options must fit the ask page's rows |

These live in `tools/weiqi/content.py` as constants, and the generator **aborts**
if any level exceeds them. The interface asserts the same numbers from the other
side (`main/wq_layout.h`), so a level that no longer fits fails at generation time
rather than rendering off-screen on the device.

## `--check` is what turns "remember to regenerate" into a rule

`gen_content.py --check` and `gen_font.py --check` compare the generated files
against `levels.txt` and the interface strings. `tools/validate.sh` runs them
first, so **editing a level and forgetting to regenerate fails the build** instead
of shipping a firmware that compiles fine and plays the old content.

The font check has a second half that the previous app taught us to add: it does
not merely compare code point *counts*, it parses the generated `.c` and
reconstructs the set of code points the device can actually render, then takes
the set difference against the current inventory. A count that matches while the
code points have drifted still produces empty boxes on screen — and a `--check`
that only prints `STALE` with exit code 0 is not a check at all.

## One module owns the content facts

`tools/weiqi/content.py` is imported by everything that needs to know about the
content: the C table generator, the font inventory generator, and the host tests.
There is exactly one place where "how many levels are there" and "what can a
level contain" is written down.

The generated `main/wq_content.h` carries the measured maxima back into C:

```c
#define WQ_LEVEL_COUNT 154
#define WQ_CHAPTER_COUNT 7
#define WQ_STONES_MAX 43
#define WQ_CORRECT_PATHS_MAX 4
#define WQ_CORRECT_MOVES_MAX 9
```

These are **measured from the real bank**, not guessed: the largest starting
position in the selected set really does carry 43 stones, and the longest
solution path really is 9 moves (the average is 2.5). The interface uses them to
size its buffers, so the buffers follow the content instead of being
hand-tuned and then silently overrun.

## What the parser refuses

Every one of these aborts with a message naming the level:

- a block with a missing or unknown key, or a key repeated
- a `kind` outside the five known kinds
- coordinates that fall outside the 9x9 window
- a `black:` or `white:` line with an odd number of letters
- a solution path whose first move is not the declared player's
- a mark whose glyph is not one of the known mark shapes
- a title, instruction, question or option text over its budget
- a quiz level whose answer index is out of range, or whose correct option does
  not appear in the question text
- an endgame level with no removal targets where targets are required

Zero-tolerance here is cheap. Every one of these would otherwise surface as a
level that looks playable and cannot be completed.

## Related

- [Seven Bridges application record](seven-bridges/README.md) — this repository's record; the same techniques applied to the Seven Bridges puzzle.
- [Subsetting a CJK font for LVGL](cjk-font-subsetting-for-lvgl.md) — the other
  consumer of this module.
- [Keeping application logic on the host](host-testable-app-logic.md) — how the
  engine and session that read these tables are tested.
