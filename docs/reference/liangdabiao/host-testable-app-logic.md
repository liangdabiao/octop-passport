<p align="right">
  <a href="host-testable-app-logic.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Keeping application logic on the host

Recorded while building the Weiqi Quest app. The most
valuable structural decision in this app was not about the device at all — it was
about which modules are allowed to know the device exists.

## Where the line is drawn

Six modules in `main/` compile **without ESP-IDF and without LVGL**:

| Module | What it owns |
| --- | --- |
| `wq_engine` | Go rules: liberties, captures, suicide, snapback, simple ko |
| `wq_session` | the level state machine: brief, solving, answering, finishing |
| `wq_progress` | the save blob: star packing, unlock chain, checksum |
| `wq_content` | the generated level tables |
| `wq_wrap` | CJK line breaking |
| `wq_volume` | the volume step table and its arithmetic |

Everything above that line — pages, widgets, buttons, storage, sound — includes
ESP-IDF or LVGL and is only tested on the device.

The test for "is this module on the right side of the line" is mechanical: **does
it compile with the host compiler and nothing else?** If yes, it can have a fast
test. If it needs a board to be interesting, it belongs above the line.

What this buys: `test_wq_session` plays **all 154 levels to the end**, and
`test_wq_engine` replays **all 134 solution paths (280 moves)**, in a test that
finishes in well under a second. Waiting for a firmware build to find out that
level 87 is unsolvable would cost minutes per iteration.

## Make the two implementations agree by construction, then prove it

The level data crosses two languages: `extract_levels.mjs` (JavaScript, evaluating
the web game's TypeScript) produces `levels.txt`, and `tools/weiqi/content.py`
(Python) re-parses and re-validates it before generating the C tables. Two
independent implementations of the same validation rules will drift.

Two mechanisms keep them honest:

- **The rules are mirrored deliberately, and the file in between is the contract.**
  `levels.txt` is the single source of truth; neither side may invent data. The
  extractor writes it once and then refuses to overwrite it without `--force`.
- **The hard part is re-validated where it matters.** Cropping a 19x19 position
  into a 9x9 window can turn a solvable level unsolvable, so the extractor replays
  every solution path under the full rules on the *cropped* board. `content.py`
  then checks the same structural invariants again on the way to C, and
  `test_wq_engine` replays the same paths a third time through the **device's own
  C engine**. Three passes, each catching a different class of error.

The third pass is the one that matters most: it is the only one that runs the rules
code the device will actually execute.

## Test the rejection paths, not just the happy one

A save format is where this pays off. `wq_progress` packs stars into two bits per
level plus a Fletcher checksum, and its test asserts every way that data can be
wrong:

- a blob with the wrong magic
- a blob whose version is from the future
- a blob whose checksum does not match its payload
- a blob truncated mid-record
- a level index past the end of the bank
- a star value outside its packed range

Each of these must produce a specific rejection, not a silently-clamped value.
"On failure, do not modify the output" is a contract the test can check, and it is
worth checking: a half-written progress blob that the device reads back is far
worse than a clear failure.

The same idea appears in the content parser, which aborts on a missing key, an
unknown key, a duplicated key, a coordinate outside the window, or a solution path
that does not start with the declared player.

## Tests should link the real data, not a copy of it

The wrap test does not carry a handful of sample strings. It walks **every
instruction and every question in the generated tables** and asserts, for each
one, that it wraps into the buffer at 13 characters per line, that no line exceeds
the budget, and that no line begins with a closing punctuation mark.

The difference is not thoroughness for its own sake. A copied sample proves the
algorithm works on the cases you already thought of. The real bank proves it works
on level 154, which someone else wrote.

## Make the pipeline reproducible, so a rerun is a diff and not a surprise

There is no randomness in this app's build, and that is deliberate. The 154 levels
are a deterministic selection out of 3564, the extraction is a deterministic
evaluation of the web game's data, and every generator is a pure function of
`levels.txt`.

That property is worth protecting, because it is what makes `--check` meaningful:
if `gen_content.py --check` fails, the *only* possible explanation is that someone
edited the bank without regenerating — not that a random seed moved. A pipeline
that cannot reproduce its own output cannot tell you whether a change was intended.

## Related

- [Letting font metrics drive the layout](font-metrics-driven-layout.md) — the
  arithmetic these tests assert against.
- [Turning a level bank into generated C tables](level-bank-pipeline.md) — the data
  the pure modules consume.
- [Building ESP-IDF firmware from Git Bash on Windows](windows-git-bash-esp-idf.md)
  — how the host tests are actually built and run on this machine.
