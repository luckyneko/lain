# A run reads a clone of the document, and the gui never waits on graph work

---
Status: accepted (designed 2026-09-28; slices 1 — clone + lineage — 2 — the published evaluation
+ payload identity — and 3 — cancellation — built 2026-09-28, 4 — the async vertical — and 5 —
triggers + persistence — 2026-09-29, the rest not)
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

*Amended 2026-09-28, the day slice 3 landed.* Building it settled four things this section left open:

- **Only COMPUTE is cancelled; the crossings between levels always run.** "Every step checks the
  cancel flag" was wrong for a group's entry and exit, a map's gather and a loop's fold. A skipped exit
  leaves nothing to say its owner is owed: the entry already cleared the group's request, and the
  inner GroupOutput's own step cleared that node's. A skipped entry is worse, since the group is
  selected next run only for its stale interior, does not republish, and its interior computes the
  old input for good. Saying so would take a per-node "exit pending" flag, plus a republish that throws
  away the interior's incrementality after every supersede. A crossing only copies values, so running
  it keeps its owner consistent with whatever its interior currently holds, and the interior's own
  requests keep the owner stale. That needs no new state.
- **The closure requests are exact.** A stage persists a request for three kinds of node: each node
  STEP it emits, each node deferred because something upstream of it was, and each FRONTIER it raises.
  It does *not* persist one for a group or map expanded without republishing, an owner deferred only
  because its interior was, or an exit. A request on any of those would make it republish next stage,
  which is a change to an uncancelled run, and its interior's entries already keep it owed. The same
  requests cover a THROW: a serial walk stops where it is, abandoning branches that have nothing to do
  with the thrower.
- **Staleness asks every child of a map, not the first.** A map's children share a definition and
  are run together, so asking child 0 used to be enough. A cancel can stop a map part-way, and its
  gather still runs, so element 2 can be owed while element 0 has finished.
- **The surface is one `flow::RunControl` per run**, holding the cancel flag and the progress read. It
  counts NODE computes only, summed across stages (a map's elements and a loop's iterations are
  planned only once the stage before them has run), on the scheduler's own counters.
  `RecipeHandle::finishedCount()` lives inside `ParallelScheduler::executePlan` and the serial
  strategy has no handle. `Scheduler::evaluate` takes no control yet: the gui's cancellable pull is
  Run Selection, many targets at once, and arrives with it (slice 8).

A compute that finishes after the cancel is kept, as above. One that asks
`NodeEvaluation::cancelled()` and hears yes has **given up**, so it is not recorded and stays stale,
whatever it wrote.

### A dedicated coordinator thread; `Scheduler::run` stays blocking

The host hands a coordinator thread the clone, the evaluation and a cancel token (built as
`flow::RunControl` in slice 3); it calls the chosen
scheduler's `run()`; the frame loop polls atomics for completion and progress. The staging loop keeps
running at full speed off the UI thread. `SerialScheduler` runs *on* the coordinator and `--threads 0`
runs the parallel plan inline *there*, so both stay asynchronous to the gui. The scheduler's surface
grows only by the cancel token, a progress read, a between-stages observer and (later) a per-step
completion hook; the headless `flowview run` path is untouched.

*Built in slice 4 (2026-09-29)* as flowview's `Runner`, with one job slot: the host supersedes by
cancelling the run in flight and starting the next once it has drained, so it clones exactly once per
run that starts, always from the newest document, and never queues a second job behind a first — which
would need the fork below. The between-stages observer (`RunControl::setStageObserver`) is what
stage-boundary publication needed and this section had not listed: the staging loop lives inside the
blocking `run()`, so without a hook the host could publish only at the end. It is called on the thread
inside `run()`, after a stage has executed and before the next is prepared, never after the last. The
coordinator starts with the first job, so a headless process never has one.

A throw is caught on the coordinator and reported per RUN: the exception leaving `run()` does not say
which node threw, so per-node **Failed** needs either the per-step hook or flow naming the node on the
way out. Slice 6, which draws it, has to settle which.

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
next run's start, after `prepare`. The UI writes the same value into the
published evaluation immediately, because the Interface pane reads the bound value back every frame
and would otherwise snap a scalar drag back until the run caught up. A binding triggers a run like an
edit does, and is still not a document change.

*Amended 2026-09-29, the day slice 4 built it* (moved there from slice 5, since the Interface pane's
direct bind became a race the moment runs left the UI thread):

- **Applied after the runner's OWN prepare, before `run()` — not inside the run lease.** The lease is
  the scheduler's and a host cannot take it, and it does not need to: once a job holds the working
  evaluation, the coordinator is its only user. Prepare-then-bind is the load-bearing order, since
  `Evaluation::bind` ignores a pin it has no slot for and a pin added since the last run has none until
  prepare makes it. `run()` then prepares again against the same clone and finds nothing to do.
- **"Shown at once" has to survive a publication that is OLDER than the binding.** A run that ends
  between one frame's poll and its pump publishes into the next frame — after the pump has handed the
  queue to the next job. So each job has a serial, each publication carries the serial of the job that
  made it, and each binding remembers which job took it: a publication from job N has every binding
  taken after N, plus those still queued, laid over it (flowview's `PendingBindings`).
- **A binding onto a pin the published copy has no slot for is not shown** until a run publishes one —
  exactly what `Evaluation::bind` does with it. It still reaches the run.

A **document swap during a run** needs no rule of its own: New, Open and undo replace the host's pair
as they do today and cancel the in-flight run; the job shares ownership of its clone and evaluation, so
the old pair drains and is destroyed with it, and nothing waits. *(Slice 4: the cancel is an ABANDON,
which also drops whatever that run would still publish, and the panes read an empty published
evaluation until the new document's first run lands — the old values belong to a dead lineage, and a
lineage-checked freshness query would refuse them anyway.)*

### Triggers, and what a run computes

Three **run triggers**, chosen per document: **Live** (every change — today's behaviour, minus the
blocking), **On commit** (when a gesture ends — the boundary undo already coalesces on, so a slider
drag is one run on release), **Manual** (an explicit Run). No timer-based debounce: the delay worth
waiting depends on the run's cost, which cannot be known before it runs. A run pushes the whole stale
closure, as today; Manual adds **Run Selection**, the upstream cone of the selected nodes, planned like
the pull path but multi-target and executed in parallel.

*Amended 2026-09-29, the day slice 5 built the triggers.* Four things this section left open, each
settled with the repo owner:

- **The trigger is saved with the document, and is not an undo step.** It travels with the file for
  the reason above, and changing it marks the document dirty. But it stays out of every undo
  snapshot, and a restore keeps the current trigger: otherwise switching a slow graph to Manual and
  then undoing the param edits made before the switch would flip it back to Live and start the run
  the switch was for. It lives in the root's adapter blob (`flow::serialize`'s `EditorTree::graph`,
  one per level, written only when set — so no version bump, and a Live document is written
  byte-identically to one saved before this existed). flowview strips it from a load into one live
  owner, so no snapshot carries a copy.
- **Manual means only Run starts a run — a document swap included.** New, Open, an undo and a
  template reload each begin a new lineage, so the panes are empty until the next Run. That is the
  honest reading of Manual, and it lets a user undo several steps and run once; its cost is the
  deferred *keeping an evaluation across an undo* below, which Manual makes more pressing.
- **On commit is undo's boundary, not undo's flag.** A committed edit is "no widget is active" —
  asked once a frame, for both undo and the trigger. Not `pendingSnapshot`: a document swap and the
  startup request carry no snapshot, and must still run.
- **The scheduler is remembered per session**, beside the file dialogs' folder: it is a choice about
  this machine and this sitting, not about a graph.

Under Manual an edit never cancels a run in flight, since only Run can make one due; under On commit a
run in flight is superseded on the release, not on the first frame of the drag.

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
  restored node is the same recipe as the current one, which a freshly minted version cannot. Under
  **Manual** (slice 5) the cost is sharper than a recompute: an undo leaves the panes EMPTY until the
  next Run, since the swap drops the old lineage's values and a swap does not run.
- **Asynchronous texture upload and off-thread poster production** — trigger: an upload or a poster
  decode that visibly hitches.
