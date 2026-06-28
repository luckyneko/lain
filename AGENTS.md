# Global rules

This repository is **lain**, a set of small, self-contained **C++17** libraries
for rapidly prototyping ideas. Each library is independent: it builds standalone,
vendors or is consumed by the others, and carries its own architecture doc.
Project-specific architecture, build commands, and naming/style conventions live
in that library's **`CLAUDE.md`** (and its build plan in **`WORK.md`**) — those
files are authoritative; read them first and defer to them wherever they are more
specific than this one.

The rules below are general working discipline that applies to every change in
every library here.

## Top 10 non-negotiables

1. **Ask before planning when uncertainty matters.** If an answer could change
   architecture, ownership, production call path, performance, or test strategy,
   ask first. Do not guess through decision-changing ambiguity.
2. **Do not create duplicate or parallel implementations.** No `Foo2`, no
   `FooNew`, no alternative helpers, no benchmark/test/prototype copy of
   production logic.
3. **New code must be used by the real production path.** Trace the call path
   from production entry point to changed implementation before calling the work
   done.
4. **Tests and benchmarks must exercise production code.** Do not validate copied
   logic, test-only implementations, or isolated scaffolding the real system does
   not use.
5. **Read before you write.** Read the whole function, owner, nearby code, and
   callers before editing. No line patches without context.
6. **Hunt down local idioms before editing.** Inspect nearby files, call sites,
   naming, allocation/error-handling patterns, test style, ownership boundaries,
   and formatting. Match them closely enough that the change leaves no stylistic
   evidence of a different author or tool. Each library here has a distinct house
   style (`multi`'s free-function/`details::` split, `archimedes`' `acm::`
   handle pattern) — match the one you are editing.
7. **Put behavior where it belongs.** Prefer modifying the right class over
   wrappers, adapters, sibling classes, free functions, helper files, or
   caller-side workarounds.
8. **Never weaken tests to get green.** Diagnose whether the code is wrong or the
   test was wrong from the start. Justify any test expectation change before
   making it.
9. **Verify through the real caller before claiming completion.** Run the
   relevant test or command, grep/read the symbols involved, and prove production
   reaches the new behavior.
10. **Keep the diff coherent.** No drive-by reformatting, unrelated edits,
    dangling stubs, dead code, or half-done branches. Delete what you replace.

## Clarifying questions

- Ask before planning when the answer could materially change architecture,
  ownership, production call path, performance, or test strategy. Stop once the
  remaining uncertainty is not decision-changing — state a reasonable assumption
  and proceed.
- Before implementing, check the plan against the user's answers. If their
  answers invalidate it, revise before editing.

## Architecture and reuse

- Before writing a new class, search for an existing owner and extend or
  generalize it. Duplicate or "alternative" implementations are unacceptable —
  including in benchmarks, tests, prototypes, examples, and temporary
  scaffolding.
- **Reuse across libraries, don't fork.** A `lain` library that needs threading
  uses `multi`; one that needs rendering uses `archimedes`. Do not copy their
  code or re-solve a problem one of them already owns.
- **Separation of concerns.** Code lives where it logically belongs. If outside
  code must reach into a class's internals, the method probably belongs on the
  class.
- When adding a feature, refactor the nearby code it needs as part of the same
  change. If the refactor is large, surface the trade-off before proceeding.

## Before editing

Identify:

- the class/function that should own the change;
- the production call path that will reach it;
- overlapping tests, benchmarks, demos, or prototypes;
- local idioms, naming, style, ownership, and formatting patterns of the library
  you are in.

If overlapping code exists, modify, move, or delete it. Do not create another
path.

## Testing and integration

- Exercise the production implementation through the closest practical real
  caller — test behavior real callers depend on, not scaffolding or
  implementation details — and add or update coverage for every change. Each
  library uses a **Catch2** suite run via `ctest`.
- Where a library wraps a system resource (e.g. `archimedes` over a live Vulkan
  driver), the meaningful coverage is integration-level; keep such tests
  capability-aware (`SKIP`, not fail) so a CI host lacking the resource stays
  green.
- Do not edit a failing test just to pass. If the test is genuinely wrong, say
  why before changing it.
- Do not leave features, prototypes, or test-only implementations disconnected
  from the system they were built for. Integrate a prototype into the production
  path or delete it so it cannot be mistaken for the real thing.
- Before claiming completion, state which production file/function owns the
  behavior and which command (configure, build, or run) proves that path is
  exercised.

## Build & verify

- **Build out-of-source only.** Each library's top-level `CMakeLists.txt`
  hard-errors on in-source builds; use a separate `build/` dir. (Exact commands
  and CMake options live in that library's `CLAUDE.md`.)
- **A library builds standalone and as a subdirectory.** Tests/benchmarks/demos
  build only when the library is the top-level CMake project; consumed via
  `add_subdirectory` they stay off.
- **No new warnings in `lain`'s own code** on any supported compiler — strict
  flags are on (`/WX /W4` on MSVC, `-Werror -Wall -Wextra` elsewhere). A change
  that makes our sources warn is not done. Third-party warnings from vendored
  deps are out of scope.
- **Keep the docs honest.** When you change a documented contract — a public API
  surface, an ownership/handle rule, a vendored-dependency model, or the
  build/layout — update that library's `CLAUDE.md` (and `WORK.md` if the plan
  shifts) in the same change.
- **Verify, don't assume.** "Should work" doesn't count. Run the test, read the
  file, grep the symbol, trace the production path.

## Craft and discipline

- **Names carry the architecture.** If a name is awkward, the abstraction is
  wrong. Fix the shape, not the name length or comment.
- **Match surrounding style.** New code should look native. Follow the nearest
  `.clang-format` from the file's directory up to the repo root (Allman braces,
  tabs, no column limit).
- **C++ structure.** Prefer clear ownership and composition. Use `friend` only
  when genuinely required (e.g. a pImpl handle's parent-factory) — not as a
  shortcut around an awkward boundary. Do **not** introduce a `detail` / `details`
  namespace, nor anonymous `namespace {}` blocks; give internal types and helpers
  real names in the library's namespace, or use file-local `static` linkage for a
  single translation unit.
- **Finish before you start.** If the task is bigger than expected, surface it.
  Do not leave dangling stubs or silent partial work.
- **Stop after 2–3 failed attempts.** Repeated failure means the premise may be
  wrong. Re-read, ask, or change approach.
- **Surface real trade-offs.** When facing perf vs. clarity, generality vs.
  YAGNI, or refactor now vs. later, name the decision briefly before committing.

## Plans

- **Be concise.** Every line must earn its place. Revise before presenting: cut
  filler, merge overlap, remove the obvious, and sharpen vague phrasing.
- **State intent and decisions, not narration.** Use concrete steps, named
  files/functions where they matter, and real choices.
- Reflect the user's answers and constraints; remove any step that contradicts
  them. State assumptions, and if one could materially change the
  implementation, ask before proceeding.

## Session

- **Warn at 100K tokens of context.** Offer to compact before continuing so
  quality does not degrade or auto-compact mid-task.
