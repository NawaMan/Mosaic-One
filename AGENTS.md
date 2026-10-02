# AGENTS.md — Working on this repository

Guidance for an AI agent (Claude Code, or similar) helping develop **OverflowableInt**: a C++17
library of signed integers that never overflow silently, in one file (`main.cpp`), built with
**Zig** (its bundled clang + libc++) inside **CodingBooth**. See `README.md` for what the library
does; this file is how to work on it.

> **Bootstrap phase (current):** `main` is the trunk. Instead of defaulting to a worktree,
> **ask** "on main, or in a worktree?" in each proposal. The user will say when this phase is
> over; then delete this note and Rule 1 applies as written.

---

## Design intent (don't undo these)

**Goal:** make it impossible to *accidentally* get a wrong number out of integer arithmetic.
"Airtight" here means: no accidental misuse compiles, and a wrapped (wrong) result never leaves
an `OverflowableInt` without an exception. Deliberate workarounds are out of scope and are the
caller's responsibility: math on a number after `value()`, overwriting a variable with `=`,
`reinterpret_cast`/`memcpy`, or turning the pragmas off.

**Invariants.** Keep them, and keep the tests that enforce them:

1. **Exactly four types:** `int8_t`, `int16_t`, `int32_t`, `int64_t` (`is_supported_int_v`).
2. **No arithmetic on plain numbers.** The operations are friends defined inside the class that
   take `OverflowableInt` only; the arithmetic helpers are private. Don't add `plus(T, T)`,
   `plus_wrap`/`plus_upgrade` (removed on purpose) or a constructor that converts from `T`.
3. **No implicit conversions.** `from_value` starts with `int&... ExplicitArgumentBarrier`, so its
   type always comes from the argument; `#pragma clang diagnostic error "-Wconversion"`.
4. **The overflow record can't be forged.** Private constructor; the only friends are the four
   real specializations and `from_value`.
5. **`value()` is the only way out, and it throws `OverflowError` on overflow.** No unchecked
   getter: `wrapped_value()` was rejected on purpose. Printing may show the wrapped number.
6. **The first overflow wins,** across all operations; a later overflow never replaces it.
7. **No in-place changes:** `+=`, `-=`, `*=` were removed on purpose. Plain `=` is kept so loops,
   containers and structs work.
8. **No ordering** (`<` etc.): a value that overflowed has no right answer.
9. **Results can't be ignored:** the class is `[[nodiscard]]`, and pragmas make `-Wunused-value`,
   `-Wunused-result` and `-Wunused-comparison` errors.

**Adding an operation** (e.g. divide): follow every invariant. Take and return `OverflowableInt`,
record the first overflow (add an `OverflowOp`), and check it against the exact `i128` reference
in `test_width`, `test_int8_exhaustive` and the long sweeps (plus *Verifying* below).

**Not done yet:** `divide`/`remainder` (`min / -1` overflows, `x / 0` is undefined behaviour), and
a checked `narrow` (open question: a value that doesn't fit can't be stored in the smaller type's
overflow record).

---

## Rules you must follow

0. **Proposal before code.** For anything that is not pure Q&A: research read-only, then post
   three short headings and **wait** for a reply before the first edit:
   - **Problem** — the request restated in plain words, so a mismatch surfaces early.
   - **Diagnostic** — what exists in the tree, which constraint bites; 1–2 questions if unclear.
   - **Approach** — the plan, deliberate non-goals, open choices, and the **checkout** (worktree
     or here). Any new dependency gets its own line:
     > **⚠ New dependency:** `<tool/lib>` — why, and the no-new-dependency alternative.

   Skip the gate for: pure questions, an approach already approved in this thread, the user
   saying "just do it", or the next step of an already-agreed plan. One approval covers that
   plan — if the approach has to change, re-propose the delta.
1. **Feature work runs in a linked worktree** (after bootstrap). From the main clone root:
   `mkdir -p worktree && git worktree add worktree/<name> -b <name>`, then edit only there.
   Not an agent CLI's own worktree feature (`EnterWorktree`, `isolation: "worktree"`) — those
   land outside `worktree/` and are invisible to GUIs. "Go ahead" approves the *work*, not
   editing main. Skip only for Q&A, docs nits the user wants here, or "here" / "on main".
2. **Smallest honest verification, never less.** Don't claim "done" without the check that
   would catch the bug class you touched (see *Verifying*).
3. **Docs follow behaviour.** A new rule, type, operation, flag or command updates `README.md`
   (the rules table, *Using It*, *Project Structure*) in the same change. No churn for refactors.
4. **No commit / push / merge unless asked.** Landing only on an explicit "land" / "merge".
5. **Never kill processes or sessions you did not start**; tear down your own.
6. **Never `--force`** `git worktree remove` or `git branch -d` — a refusal means unmerged or
   uncommitted work; stop and tell the user.

## How to run things

**First, check where you are — you may already be inside the booth:**

```bash
[[ -d /opt/codingbooth ]] && echo "in booth" || echo "on host"   # or: [[ -n $BOOTH_CONTAINER_NAME ]]
```

- **In the booth:** run commands directly (`zig`, `just`, `clang++` are installed). Do **not**
  wrap them in `./booth …` — there is no `booth` CLI in here, only `booth--*` helpers. Read
  `/opt/codingbooth/AGENT.md` before touching the environment. `.booth/Boothfile` and
  `config.toml` are generated: never hand-edit them; to add a tool, give the user the full
  host-side `booth config` command (extend the `# Configured by:` line, keep the whole `--select`).
- **On the host:** the toolchain is not installed; use the `just` recipes (they wrap
  themselves in `./booth exec`) or start a shell with `./booth`.

| Task | Command | Notes |
| --- | --- | --- |
| Run the example | `just run` | `zig build run` |
| **Tests** | `just test` | Runtime tests (UB traps on), `static_assert`s, `compile_fail/run.sh` |
| Long tests | `just test-long` | All int16 pairs + random samples; a few minutes |
| Cross-compile | `just build` | `build-all.sh` → `dist/` for 8 targets (Linux gnu/musl, macOS, Windows) |
| Host binary | `./run-overflowable.sh [--test]` | Runs the `dist/` binary for this machine |

## Verifying

- **Every change:** `just test` must pass; the build is `-Wall -Wextra -pedantic -Werror`.
- **Arithmetic / overflow detection changed:** also `just test-long`.
- **A new compile-time rule** (something must *not* compile) needs both: a `static_assert` in
  `main.cpp`'s compile-time checks **and** a `compile_fail/<case>.cpp` whose first line is
  `// expect-error: <text the compiler must print>`. A rule without a failing test is not done.
- **Portability-sensitive change** (types, headers, platform behaviour): `just build` — all 8
  targets must compile.
- Check `zig`, `just` exist before running; if a tool is missing, stop and tell the user — don't
  invent a fallback (e.g. switching to `g++`).

## C++ conventions (from `main.cpp`)

- **C++17, standard library only.** No third-party libraries, no OS-specific APIs — that is what
  lets one machine cross-compile for every target.
- **Clang is the compiler** (via `zig c++`; `compile_fail/run.sh` defaults to `clang++`). Don't
  target `g++`. The file relies on `#pragma clang diagnostic error` for `-Wconversion` and
  unused results.
- **Misuse is a compile error, not a runtime surprise:** exact-type checks
  (`is_supported_int_v`, SFINAE via `enable_if_supported_t` so a wrong call is "no matching
  function"), no implicit conversions, `[[nodiscard]]`-style results, no ordering operators.
  Loosening any of these is a design change → Rule 0.
- **No undefined behaviour.** Overflow is detected without performing it; tests compare against
  exact 128-bit math with UB traps on. Never "fix" a test by widening what's allowed to wrap.
- `long long` vs `int64_t` differs per platform (distinct types on Linux) — keep exact-type
  checks honest on both.
- Comments explain *why* (the constraint or the mistake prevented), not *what*. Match the
  existing density.
- Shell scripts (`build-all.sh`, `run-overflowable.sh`, `compile_fail/run.sh`) must work on
  Linux and macOS (bash 3.2, BSD tools): quote expansions, no GNU-only flags.

## Landing a worktree branch

Only on explicit request. Preflight read-only first, and report **gaps** only (missing tests,
README not updated, unconcluded experiments, accidental changes) — or say `No gaps.` — then wait.
Then: stash uncommitted work in main → `git rebase main` in the worktree, re-run checks →
from main `git merge --no-ff <branch>` (**never squash**; a single-commit branch may
fast-forward) → restore the stash → `git worktree remove worktree/<name>` and
`git branch -d <name>`. **Never push** as part of landing.

## Project layout

| Path | What's there |
| --- | --- |
| `main.cpp` | Library, compile-time checks, runtime tests (`--test`, `--test-long`), example |
| `build.zig` | Steps `run`, `test`, `test-long`; flags and UB-trap settings |
| `compile_fail/` | Files that must fail to compile + `run.sh` |
| `build-all.sh`, `run-overflowable.sh`, `Justfile` | Cross-compile, run host binary, shortcuts |
| `booth`, `.booth/` | Vendored CodingBooth (source: `../CodingBooth`) — don't edit `booth` |
| `dist/`, `zig-out/`, `.zig-cache/` | Build output, git-ignored |
