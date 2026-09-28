# A run reads a clone of the document, and the gui never waits on graph work

---
Status: accepted (designed 2026-09-28; slices 1 — clone + lineage — and 2 — the published evaluation
+ payload identity — built 2026-09-28, the rest not)
Amends [ADR-0012](0012-definition-and-evaluation.md) — the pairing guard moves from address identity
to **lineage**, and a *published evaluation* is the "different concept" it set aside. Amends
[ADR-0024](0024-one-process-task-pool.md) — gui-mode stops being serial, and the coordinator thread is
the pool's participating "+1". Supersedes the `start()`/`poll()` sketch in WORK.md's process-pool
*Owed* list.
---

gui-mode runs `SerialScheduler::run` synchronously inside the frame loop, on every edit — including
every frame of a param drag — so a slow graph freezes the window, and `ParallelScheduler` has no gui
caller because blocking the frame loop on a parallel run is no better. ADR-0024 named the blocker as
panes reading an evaluation mid-rebind. That is half of it. The other half is worse: a run reads
`const Graph&` on worker threads for its whole length — `populateInputs` walks `definition.edges()`,
plan steps hold `const Graph*` into group interiors — while the canvas, the Inspector, the Interface
pane and `syncPathGroups` mutate that same Graph in place. Moving `run()` onto another thread without
deciding what an edit does meanwhile is undefined behaviour, not a torn preview.

## Decision

**A run reads a clone of the document; the host drives it from a dedicated coordinator thread; panes
read a published copy of the evaluation. The frame loop never waits on graph work.**

### A run reads a clone

At run start the host takes `Graph::clone()` of the document. A clone preserves NodeIds, every
per-node **version**, and the definition's **lineage**, so an Evaluation computed against one clone is
still incremental against the next: a node the user did not touch has the same version in both. Edits
keep landing on the document in place, exactly as today, and never race a run.

This gives the glossary a third way to get a graph, beside *load* (preserves identity; versions
restart, so it needs a new Evaluation) and *paste* (mints identity): a **clone** preserves identity
**and** history — the same recipe at the same point in its version history.

Linked-group definitions are already `shared_ptr<const Graph>` (ADR-0013) and are shared by the clone,
never copied; inline, map and loop interiors are deep-copied. Params are `PortValue`s, so copying one
is a refcount bump (M5 slice 1).

### Lineage replaces address identity

ADR-0012's `prepare`-time guard compares the recorded definition's **address**, which it concedes
*"cannot distinguish a definition rebuilt where the old one stood"*. With a clone per run it would also
reject every legitimate run — and the same address check guards child evaluations
(`child->m_definition != inner`), so every cloned interior would fail it too.

A **lineage** identifies one definition's version history. It is minted when a Graph is constructed or
loaded — so New, Open and an undo restore each start one — carried by `clone()`, and recorded by an
Evaluation, whose `prepare` refuses a definition of another lineage, at the root and for every child.
That is the property the address check was standing in for: *these versions are comparable*. It closes
ADR-0012's recycled-address hole rather than working around it, and it is what makes asking "is this
node stale?" across the document and an evaluation computed against a clone legitimate.

*Amended 2026-09-28, the day slice 1 landed.* Lineage stopped being what prevents a **false match**,
because a version stopped being able to produce one. Building slice 1 found that a per-node counter was
ambiguous *within* a lineage: `edit::replaceGroup` re-seats a different node at the same NodeId, and
its restarted counter reached exactly the version its predecessor was computed at (3 == 3), so the
group looked clean over a fresh interior and delivered nothing. Versions are now drawn from one
process-wide sequence, so no two node objects share one unless one is a clone of the other. Every
version comparison is therefore sound across any two definitions. Lineage is the **pairing rule**
layered on top: it refuses an evaluation of one document run against another, a host bug, loudly
rather than as a silent full recompute. It still accepts the clone the address check rejected. At a
child it still decides that a replaced interior gets a fresh child evaluation instead of an error.

### Supersede: cancel at step boundaries, plus an opt-in check inside compute

A newer trigger cancels the in-flight run. Every step checks the cancel flag before it starts; a long
`compute()` may poll `NodeEvaluation::cancelled()` and return early. The rule for what survives:

- **A step that finishes normally is kept.** Its `computedAt` is against the clone's versions, which
  equal the document's for every node the edit did not touch, so the next run finds it clean. A slow
  load finished before a tint was tweaked is not recomputed.
- **A step that stopped because of the cancel stays stale**, whatever it wrote.
- **A cancelled run leaves every node it did not reach still stale.** Today's staleness assumes a run
  finishes its closure: `runOrder` propagates *downstream of a recomputed node* only within one run, so
  a run cancelled after U but before its consumer D would leave both looking clean and D holding a
  value computed from U's old output. The planned closure is therefore persisted as recompute requests
  when the plan is built, and each request is cleared as its step runs.

Loops cancel between iterations for free, since an iteration boundary is a stage boundary. In Manual
(below) an edit never touches a run; only Stop cancels.

### A dedicated coordinator thread; `Scheduler::run` stays blocking

The host hands a coordinator thread the clone, the evaluation and a cancel token; it calls the chosen
scheduler's `run()`; the frame loop polls atomics for completion and progress. The staging loop keeps
running at full speed off the UI thread. `SerialScheduler` runs *on* the coordinator and `--threads 0`
runs the parallel plan inline *there*, so both stay asynchronous to the gui. The scheduler's surface
grows only by the cancel token, a progress read and (later) a per-step completion hook; the headless
`flowview run` path is untouched.

Under `ParallelScheduler` the coordinator is a participating waiter — exactly the "+1" ADR-0024 sizes
the pool for (`hw - 1` workers). ADR-0024's caveat that a participating waiter can be caught inside
unrelated work now delays only when the coordinator *notices* completion, never the UI.

### Panes read a published evaluation

The panes read a host-owned **published evaluation**: a copy of the working evaluation — a refcount
bump per port — that keeps its run's clone alive, because `Evaluation::ready()` and `describe()`
consult the definition an evaluation recorded. It is published at stage boundaries and at run end,
where the coordinator is quiescent, and then per node as steps finish: a finished step's worker copies
that node's outputs into a completion record (race-free — a finished node has only readers left) and
the UI folds them in each frame. The ten pane files keep taking `const Evaluation&` and do not change.

*Built in slice 2 (2026-09-28).* The type is **`flow::PublishedEvaluation`**: the copy and the
`shared_ptr<const Graph>` it reads through, travelling together. `Evaluation`'s copy constructor is
private and this is its one caller, so no copy exists without its definition — holding the root clone
keeps every interior alive, since the clone's nodes own them and a linked group shares its definition.
Publishing refuses a definition other than the one the evaluation was last prepared against, by
address: the question there is lifetime, not history. It hands out only `const Evaluation&`, so a
published copy can never be passed to `run()` — the *evaluation fork* below stays deferred by
construction. The mutators a host needs (a pending binding, the fold) arrive with the slices that need
them.

ADR-0012 said *"a historical snapshot of one execution, if a caller ever needs one, is a different
concept."* This is that concept, and it is not a snapshot of one execution either: it is the host's
most recent *view* of a working evaluation it cannot read while a run holds it. It is deliberately not
called a snapshot — that word means the undo document.

### Bindings are queued, and shown at once

A boundary binding is a host operation on the working evaluation, which the run owns while in flight.
The host keeps **pending bindings** — the latest per pin — and the coordinator applies them at the
next run's start, inside the run lease, after `prepare`. The UI writes the same value into the
published evaluation immediately, because the Interface pane reads the bound value back every frame
and would otherwise snap a scalar drag back until the run caught up. A binding triggers a run like an
edit does, and is still not a document change.

A **document swap during a run** needs no rule of its own: New, Open and undo replace the host's pair
as they do today and cancel the in-flight run; the job shares ownership of its clone and evaluation, so
the old pair drains and is destroyed with it, and nothing waits.

### Triggers, and what a run computes

Three **run triggers**, chosen per document: **Live** (every change — today's behaviour, minus the
blocking), **On commit** (when a gesture ends — the boundary undo already coalesces on, so a slider
drag is one run on release), **Manual** (an explicit Run). No timer-based debounce: the delay worth
waiting depends on the run's cost, which cannot be known before it runs. A run pushes the whole stale
closure, as today; Manual adds **Run Selection**, the upstream cone of the selected nodes, planned like
the pull path but multi-target and executed in parallel.

### The staleness rule is stated once, in flow

The viewer shows each node's **freshness** — Stale, Queued, Computing, Current, Failed — and *Stale*
is `runOrder`'s own closure rule: a node is stale if its version differs from what was recorded, a
recompute request is pending, **or anything upstream is stale**. Per-node version comparison alone
misses the last clause. flow exposes the closure as a public const query, so the viewer asks the
engine instead of re-implementing it.

## Alternatives rejected

- **Drain before mutating** — an edit cancels the run and the UI thread waits for the in-flight steps
  before touching the graph. The stall is bounded by the longest single step, which is seconds for a
  footage open, a frame decode or a large still, and later a solver. It also needs a choke point before
  every mutation, where today four panes mutate through a raw `Graph*` — the "remember to call it
  first" shape M7 slice 1 deleted from `editableAt`.
- **A reader/writer lock on the definition.** The same stall, plus a run that observes a half-edited
  recipe across steps unless it is also cancelled — at which point it is the option above.
- **Edits as queued commands, applied between runs.** Rewrites every gesture.
- **`start()`/`poll()`, with the frame loop driving the staging loop** — WORK.md's earlier sketch.
  Staging becomes frame-quantised: a loop raises one frontier per iteration, so a 100-iteration loop
  of trivial work costs at least 100 frames. `SerialScheduler` would have to become a chain on the
  pool, and with the pool inactive (`--threads 0`) `task::async` runs inline, so `start()` would do
  the whole stage on the UI thread. And the between-stage coordinator work lands on the thread this is
  meant to keep clear.
- **The coordinator as a pool task.** Entering a scheduler from a task — with one pool, a deadlock.
- **Forking the evaluation**, so a new run starts at once while a superseded one drains. It needs a
  consistent copy of an evaluation workers are still writing, it puts two graphs' worth of work in
  contention, and it only buys latency while a *non-cooperative* long step is in flight.
- **Pulling only what the viewer shows** (Nuke's model). The stale closure already skips unrelated
  branches, so this saves only a branch downstream of routine edits that nobody is looking at — and
  "what is shown" is wide: the Inspector, every boundary pin, every image port on the active level,
  and Issues' readiness checks.

## Costs

- **Every node kind implements `clone()`** — about forty, mechanical because `Node`'s members are
  copyable, and a census test is what stops a subclass inheriting its parent's and slicing silently.
- **A clone per run is O(nodes) on the UI thread.** Cheap by construction (refcounted params, shared
  linked definitions), but not free for a very large document edited in Live mode.
- **Drain latency.** A non-cooperative long step delays the next run by whatever it has left. The UI
  stays responsive meanwhile.
- **A superseded run's payloads are destroyed on the coordinator thread.** Fine for today's CPU nodes;
  a node that owns GPU resources must not assume the render thread releases them.
- **New, Open and undo start a new lineage, so they recompute everything.** Pre-existing — a rebuilt
  graph's nodes are new objects with new versions — but in Live mode on a slow graph, every undo is a
  full run.

## What this does NOT buy

- **The UI never hitches.** The promise is narrower and exact: *the frame loop never waits on graph
  work.* Viewer work stays on the UI thread — a synchronous texture upload, a sequence poster's
  first-frame decode, the player's playback decode — and is made proportional to what changed (the
  preview cache skips a port whose payload is unchanged, `PortValue::samePayload`, built in slice 2)
  rather than moved.
- **A reusable runner.** The coordinator, the triggers and the policy of *when and what to publish*
  live in flowview until a second gui host exists; only the engine pieces are in `libs/flow`. *(Amended
  2026-09-28, slice 2: the published evaluation's TYPE is one of those engine pieces — it states flow's
  own invariant, that an evaluation's reads go through the definition it was prepared against, so it
  belongs beside `Evaluation`.)*

## Deliberately unsettled

- **Evaluation fork** — trigger: a long non-cooperative step whose drain latency matters.
- **View-driven pull** — trigger: an expensive branch downstream of routine edits that is not being
  looked at.
- **Keeping an evaluation across an undo** — trigger: undo on a slow graph. It needs a way to say a
  restored node is the same recipe as the current one, which a freshly minted version cannot.
- **Asynchronous texture upload and off-thread poster production** — trigger: an upload or a poster
  decode that visibly hitches.
