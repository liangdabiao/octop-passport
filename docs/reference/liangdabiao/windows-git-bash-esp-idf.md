<p align="right">
  <a href="windows-git-bash-esp-idf.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Building ESP-IDF Firmware from Git Bash on Windows

Recorded while building the Weiqi Quest app, on a Windows
machine where the shell is Git Bash and there is no MSVC and no MinGW. Four things
on this setup fail in ways that look like code problems and are not.

## Blocker 1: ESP-IDF refuses to start when `MSYSTEM` is set

Git Bash exports `MSYSTEM=MINGW64`. ESP-IDF's launcher looks at it, decides it is
running under MSYS and exits — **silently, with no output and no non-zero status**.

The trap is that unsetting it inside the same script does not help: the value was
already exported into the environment IDF's own shell reads. The fix is to pass a
clean environment at the point of invocation:

```sh
env -u MSYSTEM -u MSYSCON idf.py -B "$BUILD_DIR" build
```

or to launch IDF from a plain `cmd.exe` wrapper. Either way, **verify the symptom
before fixing it**: "idf.py produces no output at all" is the signature, and it
looks nothing like a build error.

## Blocker 2: the IDF virtualenv and the interpreter on `PATH`

ESP-IDF ships its own Python virtualenv (`.espressif/python_env/...`). If another
`python` earlier on `PATH` gets picked up instead, the failure is a confusing
import error from inside IDF's own scripts rather than a message about Python.

Two rules that avoid the whole class:

- Always go through the IDF export script so the virtualenv is activated for that
  shell, and never assume the ambient `python3` is the right one.
- When a helper script *must* run outside IDF, give it the interpreter explicitly
  with an absolute path. On this machine the `fontTools`-capable interpreter lives
  in a WorkBuddy-managed environment, and the system `python3` does **not** have
  `fontTools` — a fact worth writing down, because the failure is a bare
  `ModuleNotFoundError` in the middle of a font build.

## Getting a host C compiler without MSVC or MinGW

The host tests need a C compiler that is **not** the RISC-V cross-compiler. On a
machine with neither MSVC nor MinGW, `zig cc` works — it ships its own libc and
needs no SDK. The wrapper is three lines:

```sh
#!/bin/sh
# A host C compiler that is not the cross-compiler.
exec zig cc "$@"
```

Two details that cost time otherwise:

- Path arguments must be given as `D:/...`, not `/d/...`. Zig on Windows does not
  resolve the MSYS-style paths.
- `MSYS2_ARG_CONV_EXCL='*'` must be set for the invocation, or MSYS will rewrite
  the arguments (turning `/I` into a path, for instance) before zig sees them.

## What still does not work: the demo runtime tests

Some of the inherited host tests build a demo's runtime behaviour and are expected
to fail here — they need `-Wl,--gc-sections`, and the linker behind `zig cc` on PE
does not implement it. The failure shows up as a link error naming missing symbols,
which reads like a code problem and is an environment one.

**The way to tell the difference is to reproduce the failure on a clean baseline.**
Check out the parent commit into a separate worktree and run the same test:

```sh
git worktree add --detach /d/esp/wq-baseline <parent-commit>
# run the same build there
git worktree remove --force /d/esp/wq-baseline && git worktree prune
```

If the same test fails identically on the untouched baseline, it is the
environment. Without that comparison, every inherited failure looks like something
you introduced. Write the result down next to the test, so the next person does not
repeat the investigation.

Two further host tests hang rather than fail: anything that deletes a temporary
directory. On this machine removing a directory can block indefinitely, so
`tempfile.TemporaryDirectory`-based tests never return. They are skipped knowingly,
and that is recorded rather than hidden.

## The embedded version string is decided at configure time

The firmware descriptor carries a version string that defaults to the short commit
of the project directory, with `-dirty` appended when the tree is dirty. It is
written in during **CMake configure**, not at link time. Three consequences:

- **An incremental build does not refresh it.** Recompiling and relinking keeps the
  string from the last configure. Use `idf.py reconfigure` (41 seconds on this
  machine) before measuring it.
- **Editing files during a build pollutes it.** A cold build takes minutes; any
  write in that window — including a commit from another session — makes the
  configure step see a dirty tree and bake in a hash that never existed as a commit.
- **A merged image is not byte-reproducible.** The descriptor embeds a build
  timestamp, so two builds of the same source produce the same size and a different
  SHA-256. "Is this the image I verified?" can only be answered by the hash recorded
  at build time.

**A derived repository needs one extra step.** This app was derived from another
repository in the family by a full copy that included `.git`. Until the new
repository has a commit of its own, `git describe` resolves to the **parent's**
HEAD — so the first firmware built here carried the parent app's commit hash with a
`-dirty` suffix. Commit first, then reconfigure. The symptom is subtle enough that
it is worth checking the descriptor on every first build of a new derived app.

## Check list

- `MSYSTEM` is unset for the invocation, not just in the shell.
- The IDF virtualenv is the interpreter in use; helper scripts get absolute paths.
- Host builds use the wrapper compiler with `MSYS2_ARG_CONV_EXCL='*'`.
- Every inherited test failure has a baseline worktree result next to it.
- The version string in `project_description.json` is the commit you think it is.
- The hash in `build/` matches the hash recorded when the image was verified.

## Related documents

- [Keeping application logic on the host](host-testable-app-logic.md) — what these
  tests cover.
- [Seven Bridges application record](seven-bridges/README.md) — this repository's record; the same techniques applied to the Seven Bridges puzzle.
  produces.
