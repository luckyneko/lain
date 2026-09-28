# One process task pool, and `lain::task` is a namespace alias for `multi`

---
Status: accepted
Reverses the *"no default or hidden process-wide pool"* rule recorded in WORK.md alongside the
M1 scheduler reshape. Narrows [ADR-0004](0004-static-linking-service-shaped-seams-over-thorax.md)'s
refusal of a service registry to exactly what it refused. Amends
[ADR-0009](0009-group-nodes-flattened-into-one-execution-plan.md)'s statement of the substrate
surface a plan needs.
---

`lain::task` was written over Taskflow and moved to `multi` on 2026-09-11 with `libs/flow` unchanged
by a single line of production code. That proved the seam's one claim and, in the same stroke,
exhausted its reason to exist: `multi` is lain's **own** library, there is no third substrate coming,
and what remained was a second vocabulary — `Flow` for `Recipe`, `Task` for `Step`, `Executor` for
`Context` — paid for on every read.

The naming was the smaller half. `multi` is built around **one pool for the whole process**, sized to
the machine, reached through free functions over a global `Context`. `lain::task::Executor` was an
injected, caller-owned object threaded through sixteen construction sites, so the second subsystem in
lain to want threads would have got its own pool and the process would have oversubscribed by a
factor of however many subsystems there were. The recorded justification was one sentence — *"the
caller owns the executor (so worker count and lifetime stay explicit) — there is no default or hidden
process-wide pool"* — written when `flow` was the only thing in lain that wanted threads at all.

## Decision

**There is one task pool per process. `lain::task` is a namespace alias for `multi`, `lain::app` owns
the pool's lifetime and exposes `--threads`, and a scheduler run participates in the pool's stealing
rather than sleeping on it.**

### `lain::task` is an alias, not a wrapper

`namespace lain { namespace task = multi; }` and nothing else. lain does not wrap what it owns —
`acm::` appears raw in the public headers of `lain::app` and `lain::gui`, and archimedes is a
submodule on the same footing as multi. CLAUDE.md's *"external deps enter as thin wrapper libs with
matching namespaces"* was written about third-party code and never covered either.

A namespace alias **cannot be reopened**, so `lain::task` can hold no owned name. That is the
mechanism rather than a side effect: it is what forced every behaviour the wrapper carried to find a
real owner, and what makes it impossible to grow a second vocabulary back one name at a time.

The alias costs the claim that no `multi::` type reaches a consumer's translation unit. That claim
was never physically true — the wrapper included multi's headers — so what is lost is a sentence in a
CMakeLists, not a property.

### `multi` is unchanged

Two additions were designed and rejected.

- **A scoped-pool RAII.** `WorkerPool::~WorkerPool` is already `assert(m_workerCount == 0);` followed
  by a defensive `stop()`: it complains, then cleans up after a caller who forgets. The only real
  defect is **where** it complains — a static destructor after `main`, with no stack to say why — and
  that is fixed by giving the global an owner, not by a second mechanism for a job already done.
- **A synchronous `run(Recipe&&)`.** It would go against multi's intent, which is that a thread is
  never blocked unless the caller really means it: a blocking call reachable from a worker halts the
  system, so blocking must stay conspicuous. `async` + `waitAll` + `get` is already what multi's own
  README teaches, and a short safe spelling would make blocking look ordinary.

### `lain::app` owns the lifetime, and `--threads`

`Application` owns the pool across its lifecycle: **`initialise` starts it** past every early exit —
so `--version`, `--help` and `--licenses` answer without spawning a thread — and **`shutdown` stops
it** after the last `onShutdown`, so teardown may still dispatch.

**What guarantees the pair is `~Application`, not a bracket.** The first cut put the whole lifecycle
inside one private `runLifecycle()` call, so the start and the stop sat either side of a single
return and the pairing was structural. That was traded away deliberately when the lifecycle became
the public interface (`initialise` / `run` / `shutdown`): a caller can now leave between the two. The
destructor is the replacement — it asserts the stop happened and then does it, which is multi's own
`WorkerPool` contract one level up, and the `Application` lives in `main`, so it runs before static
destruction. That is the entire requirement, and it is the stronger mechanism of the two, because it
also covers a caller who never calls `shutdown` at all rather than only the returns of one function.

An escaping exception needs no cover either way: it reaches no handler, the process terminates, and
static destructors never run, so the pool is never destroyed to complain about.

`--threads` joins `--version` / `--licenses` / `-v` as a reserved framework flag, because one pool
per process makes worker count a process-wide knob of exactly that family. `0` is not degenerate: it
leaves the pool inactive, and multi then runs every dispatch **inline on the caller, in dependency
order** — a deterministic single-threaded execution of a parallel plan, with no second scheduler.

It inherits the reserved flags' existing constraint: **it must precede a subcommand.** An app whose
subcommand sets `allow_extras` — flowview's `run`, which collects `--<boundary>` bindings that way —
swallows anything typed after it, so `flowview run --threads 0` warns about an unmatched argument and
keeps the default. Pre-existing, and shared with `-v`. CLI11's `fallthrough()` is the obvious fix and
does not work here: it makes the subcommand hand unmatched options up to the parent, which refuses
them, breaking `--<boundary>` binding outright. Tried, measured, reverted.

### A scheduler run participates

One pool forces a choice that two pools hid, because multi's primitive families wait in opposite
ways:

| | workers | caller | runners |
|---|---|---|---|
| `parallel` / `each` / `range`, via `runQueueJob` | W | steals | W + 1 |
| `async(Recipe)` + `handle.wait()` | W | sleeps | W |

No single `W` is right for both. `W = hardware_concurrency() - 1` — multi's own default — **plus a
participating caller** is exactly one runner per core for every primitive in the process. So
`ParallelScheduler` waits with `waitAll(handle)`, which spins through `waitUntil` running stolen
tasks, and then `get()`, which is a non-blocking fetch that rethrows.

This also retires two undocumented deviations that happened to cancel out. The wrapper used
`handle.wait()`, the one wait that does *not* steal, and compensated by starting
`hardware_concurrency()` workers instead of `hw - 1`; neither was written down as deliberate, and
multi's README teaches `waitAll` on exactly this handle.

### The headless path runs parallel; gui-mode does not

`ParallelScheduler` had **no production caller** — flowview ran `SerialScheduler` in both gui-mode and
the headless `run` path — so it was built, heavily tested, and reached by no shipped binary. A cli is
where blocking the calling thread is free, so `flowview run` takes the parallel path and `--threads 0`
is the escape hatch. gui-mode stays serial: a run blocks until the whole plan finishes, and doing that
in the frame loop would stall the window for the length of the graph.

## What this does NOT buy, stated up front

- **It does not make graph execution asynchronous.** A run still blocks its caller. Making it not
  block is a separate milestone, and the work is not in the task layer: `Scheduler::run` is a staging
  loop, and ten flowview pane files make twenty-three direct reads of an evaluation's values every
  frame. Payloads are shared and immutable since M5 slice 1, so the pixels are safe, but the *slot* is
  rebound non-atomically and a pane reading mid-rebind tears.
- **It does not give `flow` a parallel-for.** `task::each` / `parallel` / `range` are now reachable
  from any lain code, but no node uses them.
- **It does not isolate pools.** Nothing can run a graph on a pool of its own. Nothing needs to:
  `catch_discover_tests` gives every TEST_CASE its own process, so the process pool is already
  per-case. The trigger for adding one is a caller that must not share, and it is recorded on
  `ParallelScheduler` itself rather than here, because a decision with nothing pointing at it from the
  code it governs stops being a decision (the lesson [ADR-0023](0023-uri-as-an-identity-not-a-path-algebra.md)
  opens with).

## Alternatives rejected

- **Keep the wrapper.** It earned its place once, absorbing the Taskflow → multi swap. But the thing
  it insures against is a third substrate, and the second one was the owned library that is now the
  destination.
- **A curated re-export** — per-name `using` declarations, so `lain::task` could omit `Context` and add
  owned names. Buys curation nobody needs and keeps a drift surface: a new multi primitive stays
  invisible to lain until someone adds a line here.
- **Delete `libs/task` and write `multi::` directly.** Consistent with archimedes and genuinely
  defensible. The alias is one line, keeps the lain-facing spelling every existing consumer already
  reads, and leaves one place to say why it is an alias.
- **A `Services` registry** with ordered teardown for process-wide singletons. Already refused, in
  writing: [ADR-0004](0004-static-linking-service-shaped-seams-over-thorax.md) names *"a shared service
  registry, one process-wide logger/executor/factory across DSOs"* as *"machinery to repair the
  global-state fragmentation that DSOs themselves introduce — a cost of going multi-DSO, not a
  pre-existing pain."* lain is one static binary, so there is nothing to repair, and the question it
  cannot answer — when is it safe to tear down? — is the second reason.

## Costs

- **A global is a global.** Two subsystems now contend for one pool, and a long task delays a short
  one. That is the trade being made deliberately: contention inside one correctly-sized pool is
  cheaper than context-switching between several oversized ones.
- **A participating wait executes foreign work.** The thread waiting on a graph run may run any task
  the pool holds. Harmless while `flow` is the only producer; once video decode or `lain::image` shares
  the pool, a waiting thread can be stuck inside a long unrelated task. That is a latency hazard, not a
  correctness one, and it is a second reason gui-mode does not wait on a graph run.
- **Caller-stack growth.** multi warns that steal participation grows the waiting thread's stack when a
  stolen task itself dispatches. Bounded today, because a plan step never dispatches.
- **The "fire-and-join" contract becomes load-bearing program-wide.** Nested *dispatch* — `task::each`
  inside a node's `compute()` — is fine and supported. A nested graph **run** is not: it takes the
  evaluation's run lease and blocks a worker. CLAUDE.md already forbids it; with one pool, a violation
  is a deadlock rather than a slowdown. A runtime guard would need multi to answer whether the calling
  thread is a worker. Trigger: anything that dispatches a graph run from inside `compute()`.
