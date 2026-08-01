# Definition and evaluation — a graph's recipe apart from its runs

---
Status: accepted
Supersedes part of ADR-0010 (see Consequences)
---

`Port` owns a `PortValue` and `Node` owns `m_dirty`, so a `Graph` is simultaneously a document, an
evaluation cache and dirty bookkeeping. The split already exists in practice — `toValue`'s own comment
says it serializes *"a graph's RECIPE (node kinds + params + name-addressed edges), never computed
port values"* — but it is enforced by omission in one function, with no boundary anywhere in the type
system.

Three things pushed on that. Identity kept needing composition, because ids are per-container and a
container is both recipe and instance. Undo needed positional ordinals, because a load produces a new
instance with no way to say "the same recipe node". And the intended workload does not fit at all: a
variable set of video streams, each processed by the same subgraph, combined for reconstruction — plus
a Loop node running a graph N times. **SplitGroup, Loop and linked-group instances are one shape: one
definition, N evaluations.**

## Decision

**Per-run state moves off the structure into an `Evaluation`.**

- **Values and per-run bookkeeping live in the `Evaluation`**, not on `Port`/`Node`. The definition is
  structure: node kinds, params, edges, port declarations, names.
- **An `Evaluation` is a value the host owns**, passed to the scheduler: `run(definition, eval)`.
  Retention is therefore *ownership*, not policy — the cli extracts its boundary outputs and drops the
  evaluation; the gui keeps one because the Inspector reads it, and a bounded pool is simply "how many
  do I retain". No policy enum, no per-port retention flags, no engine-side budget.
- **An `Evaluation` is a tree.** It holds this graph's values plus **named children**: one per group
  node, N per map element. A run is named by an `EvalPath` — a sequence of `{NodeId, index}` steps —
  which is a **coordinate composed of things that already exist**, not a minted id.
- **`compute()` receives its evaluation context.** A node can no longer write through
  `output(i).set(v)`; it is handed a per-node view of the evaluation.
- **Staleness is a version comparison.** The definition carries a per-node version, bumped exactly
  where `markDirty()` is called today; an evaluation records the version it computed each node at.
  `needsRecompute(n) = localDirty(n) || def.version(n) != eval.computedAt(n)`. When an evaluation
  notices a mismatch it **drops the stale values**.

## Why

**Parallel map makes an implicit context impossible.** SplitGroup runs one definition N times
concurrently, so anything stored on the node is shared across all N — a "current evaluation" pointer
on the node is a data race between evaluations of the same node, the exact hazard ADR-0009 flattened
the scheduler to avoid. A `thread_local` would hide it rather than fix it, and would break the moment
one evaluation spanned two tasks. Passing the context explicitly is not the tasteful option; it is the
only one that survives the target workload.

**Invalidation must be pulled, not pushed.** Evaluations are host-owned, so the definition has no list
of them — deliberately. An edit therefore *cannot* walk them to set flags or drop data; it can only
bump a version and let each evaluation notice when it next runs. This also keeps an edit O(1) in the
number of evaluations, which matters when there is one per video stream.

**Per-node versions, not a definition-wide one.** A single counter would make any edit invalidate every
evaluation wholly, so tweaking a blur radius would recompute every node of every stream. Per-node
versions preserve the dirty-closure incrementality built in Tier A #3 exactly — `runOrder` is unchanged;
only the predicate it asks is different.

**Versions rather than "absence means stale".** Deriving staleness from missing values is appealing —
one source of truth, no flag to drift — but it breaks two existing designs. ADR-0007 relies on a
suppressed node being **clean and empty at the same time**, *"so a stable-off subtree goes clean and
drops out of future closures — keeping it cheap"*; if absence meant "needs computing", a gated-off
subtree would be re-examined forever. And an on-request source (a camera capture) holds a value after
its first run, so it would be skipped rather than refiring, unless it unpublished its own output — a
flag in disguise. Versions also keep "evicted for memory" and "invalidated by an edit"
distinguishable, without which a gui pool evicting a preview would silently force recomputation of a
result that was still valid.

**Coordinates rather than minted run ids.** A minted `EvaluationId` would be process-unique runtime
identity in core again — the thing ADR-0011 rejects. It is also wrong for a live viewer: a preview
pinned to run #47 goes stale the instant run #48 happens. A coordinate keeps pointing at *"element 3
of the map in group X"* and shows the newest values there, which is what an inspector wants.

## Consequences

- **The `compute()` contract changes at ~41 sites** (about ten production nodes, the rest test
  fixtures). Mechanical — `input(i)` → `e.get<T>(i)`, `output(i).set(v)` → `e.set(i, v)` — with no
  logic changes, and best done as its own commit with the suite green either side. **Params do not
  move**: a param is serialized, so it is recipe, and `param(i).get<T>()` is untouched.
- **`GroupInputNode::m_bound` moves into the evaluation.** Bound values are per-run — that is the
  point, since each stream binds its own input — so the boundary seam (`BoundaryInput::setValue`, the
  cli binders, the Interface panel) gains an evaluation parameter.
- **`PinKey` becomes `{EvalPath, NodeId, PortId}`.** Same arity as today, but every field is a real
  axis — *which run*, *which node*, *which port* — rather than a field papering over scoping.
- **ADR-0010's "a link references a recipe, never a runtime share" no longer holds.** That was argued
  on correctness — *"a `Port` holds a persistent value, so two instances sharing one inner graph would
  stomp each other's intermediates"* — and it was a consequence of exactly the mixing removed here.
  With values in evaluations, **N linked groups can share one definition**, which is what makes a
  template edit propagate to every instance live rather than on reload.
- **Inspectability becomes a retention decision.** *"Every intermediate result stays inspectable"* was
  free when a port held one value; across N evaluations it is N× memory, and these are images. It is
  now bounded by who holds evaluations and how many.
- **Peak memory during a run is untouched** by that. A 20-node image chain still materialises 20
  images before the cli drops them. Cutting it needs in-run liveness — release a value once every
  consumer has read it — which is separable, and cheap to add later precisely because `PortValue`
  payloads are already shared and immutable, so releasing is a refcount decrement.
- **"Release it and re-run on inspection" cannot replace retention.** It works for a pure graph at a
  latency cost, but an on-request source re-marks itself dirty each pull, so re-running yields a
  *different frame* — not the one being inspected.

## Deliberately unsettled

These need a concrete SplitGroup or Loop in front of them; guessing produces a mechanism fitted to
imagined requirements.

- Whether a map's child index is positional or something stabler. If the input collection reorders
  between runs, *"element 3"* follows the position, not the stream, and a pinned preview quietly
  changes subject.
- Whether Loop carries state between iterations or starts clean.
- How suppression (ADR-0007) crosses a map — does an unready element suppress the whole map or just
  its own child?
- How the execution plan (ADR-0009) lowers a map: N children expanded into one flat plan, or a
  nested-but-joined shape.
