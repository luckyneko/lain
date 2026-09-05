# Definition and evaluation — a graph's recipe apart from its runtime state

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

**Runtime state moves off the structure into an `Evaluation`.**

- **Values and evaluation bookkeeping live in the `Evaluation`**, not on `Port`/`Node`. The definition is
  structure: node kinds, params, edges, port declarations, names.
- **An `Evaluation` is one graph's runtime state** — its values and bookkeeping — owned by the host
  and passed repeatedly to the scheduler: `run(const Graph& definition, Evaluation& evaluation)`.
  Re-running *updates* it; it is not a snapshot of one execution. Retention is therefore *ownership*,
  not policy — the cli extracts its boundary outputs and drops the evaluation; the gui keeps updating
  one because the Inspector reads it, and a bounded pool is simply how many it retains. No policy
  enum, no per-port retention flags, no engine-side budget.
- **A host owns a definition and its Evaluation together.** An Evaluation spans in-place edits of its
  definition — that is where version comparison earns its incrementality — but never transfers to a
  *reconstructed* Graph, even one whose node UUIDs match, because per-node versions are runtime
  counters that restart from zero. The rule is **ownership, not vigilance**: a host holds the two as
  one replaceable unit, so New/Open/undo-redo replace both or neither and no path can pair an old
  evaluation with a new definition. `Evaluation{graph}` records its definition and `prepare` compares
  it — a guard rail that catches a mispaired call in practice, though not a definition rebuilt at a
  recycled address. Retained document/UI identity such as `CanvasIds` is a separate concern.
- **An `Evaluation` is a tree.** It holds this graph's values plus **named children**: one child
  Evaluation per group node, N per map element. One is located by an `EvalPath` — a sequence of
  `{NodeId, index}` steps — a **coordinate composed of things that already exist**, not a minted id.
  Reusing the path reaches that Evaluation's newest values after each scheduler invocation.
- **Execution is read-only over the definition.** The scheduler consumes a `const Graph&`, and
  `Node::compute(NodeEvaluation&) const` receives a per-node view of the evaluation. A node can no
  longer write through a definition `Port` or mutate itself to request another run; all run mutation,
  including an on-request source rearming itself, goes through that view into the evaluation. `const`
  here carries its full weight: **a `const Graph&` is concurrently readable**, so the definition holds
  no lazy caches — `topoOrder()` is recomputed by the mutators, not on first query.
- **Preparation precedes dispatch.** `Evaluation` is the deep owner of values, readiness,
  `computedAt`, recompute requests and boundary bindings. Before scheduling work, the coordinator
  calls `evaluation.prepare(definition)`: it creates or prunes graph-shaped storage and stabilises it.
  Worker tasks receive a non-owning `NodeEvaluation&` for an already-existing node; they never lazily
  insert into shared containers. The invariant is *no worker task grows shared evaluation storage; a
  coordinator does* — which for graph-shaped children (everything M6 builds) means one pass before
  dispatch. A **data-dependent** child count cannot be known that early, so where a map prepares its
  children is settled together with how the plan lowers one (see *Deliberately unsettled*).
- **One Evaluation has one scheduler invocation at a time.** Scheduling the same `Evaluation`
  concurrently is invalid. Scheduler entry takes a non-blocking RAII run lease; if it is already
  leased the call immediately throws `std::logic_error`, and unwinding releases the lease. It never
  waits on a mutex, which could turn accidental recursive use into a deadlock. Distinct Evaluations —
  including two over the *same* definition — are independent and may execute concurrently. Each task
  reads and writes only its assigned node view.
- **Staleness is a version comparison.** The definition carries a per-node version, bumped exactly
  when its recipe changes; an evaluation records the version it computed each node at.
  `needsRecompute(n) = recomputeRequested(n) || def.version(n) != eval.computedAt(n)`. A boundary
  rebind, group-entry publication, or on-request rearm sets the evaluation-local recompute request;
  it does not change the definition version. When an evaluation notices a version mismatch it
  **drops the stale values**.
- **Recipe mutation and versioning are one operation.** Mutable `Param&` is not exposed to hosts.
  `Node::setParam(index, value)` validates and commits a parameter value, then advances that Node's
  definition version; failure changes neither. GUI editors and deserializers edit/decode a copy and
  commit through this seam, and concrete recipe setters use it. Graph topology/port primitives bump
  the affected node internally. `setName` remains display-only and does not force computation. There
  is no public "mutate, then remember to bump" protocol.
- **Force refresh is an Evaluation operation.** `requestRecompute(node)` demands one node (the
  ordinary closure carries it downstream); `requestRecomputeAll()` demands that Evaluation's prepared
  subtree. A host reaches a nested one through `EvalPath`. One verb spans both surfaces —
  `NodeEvaluation::requestRecompute()` is the same concept from inside `compute` — so runtime demand
  reads the same wherever it appears. `Graph::markAllDirty()` disappears: it cannot express which of
  several evaluations to refresh. Constructing a fresh Evaluation is the stronger operation that
  forgets all retained state.
- **Boundary handles describe; Evaluation binds and reads.** `Graph::boundaryInputs()` /
  `boundaryOutputs()` are const and return immutable recipe handles carrying `PortAddress`, name and
  declared type. A host calls `evaluation.bind(input, value)`, runs the scheduler, then
  `evaluation.value(output)`. Binding stores the value and requests recompute of that Evaluation's
  boundary input. Group entry uses the same operation against its child Evaluation; boundary Nodes
  hold no bound or delivered runtime value.
- **Readiness follows the values.** ADR-0007's gate is *"every Required input carries a value"* — a
  join of declaration (`Port::required()`) and runtime (is it empty?), so it belongs to the side that
  owns values and already knows its definition: `evaluation.ready(node)` for hosts,
  `nodeEvaluation.ready()` inside compute. Port-level presence is `hasValue(PortAddress)` /
  `hasValue(PortId)`, which also retires a name collision — today `Port::ready()` means *has a value*
  while `Node::ready()` means *all required inputs do*. Rendering a value as text stays a `PortType`
  capability (`describe`) applied to an evaluation value, so a type still describes itself with no
  central ladder.

## Why

**Parallel map makes an implicit context impossible.** SplitGroup runs one definition N times
concurrently, so anything stored on the node is shared across all N — a "current evaluation" pointer
on the node is a data race between evaluations of the same node, the exact hazard ADR-0009 flattened
the scheduler to avoid. A `thread_local` would hide it rather than fix it, and would break the moment
one evaluation spanned two tasks. Passing the context explicitly is not the tasteful option; it is the
only one that survives the target workload.

**Const execution makes that boundary enforceable.** Passing a context while leaving `Graph` and
`Node::compute` mutable would express the split by convention while still permitting a node to write
shared run state into the definition. A const definition plus const compute makes the evaluation the
only ordinary destination for runtime writes; definition edits remain explicit operations outside a
run.

**`const` has to mean *concurrently readable*, or it means nothing here.** N evaluations over one
definition is the whole point, and this ADR permits them to run at once — so the const-ness of the
definition is a threading claim, not just a mutation claim. `Graph::topoOrder()` today is a lazy cache
into `mutable` members, which two concurrent runs would rebuild simultaneously after an edit: a data
race that a `const&` signature would advertise as safe. Making the mutators maintain the order turns
the promise into a property. Editing is human-paced and runs are hot, so the cost lands in the right
place — and stating the rule matters more than the fix, because the next lazy cache added to a
definition would reopen it silently.

**Preparation makes the parallel boundary enforceable.** A const definition does not make a mutable
evaluation container safe if worker tasks can concurrently grow maps or vectors. Performing all
structural reconciliation before dispatch gives tasks stable references and keeps allocation,
pruning and child creation on the coordinator side. Parallelism then operates over disjoint
prepared state rather than depending on container implementation details.

**Reusing one Evaluation fails rather than races or blocks.** A documented precondition alone would leave a
release build with a data race, while a blocking mutex would hide the caller error and can deadlock on
recursive scheduler entry. A non-blocking run lease makes the invalid use deterministic, preserves
parallelism across independent Evaluations, and releases correctly when node computation throws.

**Invalidation must be pulled, not pushed.** Evaluations are host-owned, so the definition has no list
of them — deliberately. An edit therefore *cannot* walk them to set flags or drop data; it can only
bump a version and let each evaluation notice when it next runs. This also keeps an edit O(1) in the
number of evaluations, which matters when there is one per video stream.

**Per-node versions, not a definition-wide one.** A single counter would make any edit invalidate every
evaluation wholly, so tweaking a blur radius would recompute every node of every stream. Per-node
versions preserve the closure incrementality built in Tier A #3. The propagation algorithm remains,
but its signature and predicate become evaluation-aware: `runOrder(definition, evaluation)` asks the
matching Evaluation, and a group recursively asks its child rather than scanning mutable state on the
inner definition.

**Versions rather than "absence means stale".** Deriving staleness from missing values is appealing —
one source of truth, no flag to drift — but it breaks two existing designs. ADR-0007 relies on a
suppressed node being **clean and empty at the same time**, *"so a stable-off subtree goes clean and
drops out of future closures — keeping it cheap"*; if absence meant "needs computing", a gated-off
subtree would be re-examined forever. And an on-request source (a camera capture) holds a value after
its first run, so it would be skipped rather than refiring, unless it unpublished its own output — a
flag in disguise. Versions also keep "evicted for memory" and "invalidated by an edit"
distinguishable, without which a gui pool evicting a preview would silently force recomputation of a
result that was still valid.

**Ownership, not a pointer, keeps versions meaningful.** Per-node versions are runtime counters, not
serialized history. A graph reconstructed by load or undo can carry the same UUIDs over different
recipes while its counters restart at values an old Evaluation has already recorded — so reusing that
Evaluation would declare changed nodes clean. The fix is structural: a host that owns the definition
and its Evaluation as one unit cannot produce the mispairing, and needs no `GraphId` or global
revision registry to avoid it. The `prepare`-time comparison of the recorded definition is worth
having as a cheap guard rail, but it is *address* identity — it cannot distinguish a definition
rebuilt where the old one stood, so it must not be mistaken for the mechanism. This is the same
lesson as ADR-0011: a discriminator bolted onto an ambiguous key is weaker than removing the
ambiguity.

**Coordinates rather than minted run ids.** A minted `EvaluationId` would be process-unique runtime
identity in core again — the thing ADR-0011 rejects. It is also wrong for a live viewer: a preview
pinned to run #47 goes stale the instant run #48 happens. A coordinate keeps pointing at *"element 3
of the map in group X"* and shows whatever that Evaluation holds now, which is what an inspector
wants. A historical snapshot of one execution, if a caller ever needs one, is a different concept.

## Consequences

- **The `compute()` contract changes at ~41 sites** (about ten production nodes, the rest test
  fixtures). `compute()` becomes `compute(NodeEvaluation&) const`, and fixed-port nodes retain named
  `PortId` members returned by `addInput` / `addOutput`: `evaluation.input(m_image)` and
  `evaluation.output(m_result)`. A literal positional index is not a compute-time address; live port
  reorder would retarget it. A positional index remains only for deliberate ordered iteration (such
  as a dynamic Select's branches), resolving each iterated definition port's id into the Evaluation.
  **Params do not move**: a param is serialized recipe, and the const node still reads it through the
  handle its declaration returned.
- **`Port` becomes pure declaration** — `{id, name, direction, PortType, presence}`. `value()`,
  `set` / `get` / `holds`, `ready()` and `describe()` all leave it, so the type that most obviously
  mixed recipe with run state stops doing so, and the mixing cannot creep back in through a
  convenience accessor. `PortType` keeps `describe`, since rendering a value is a property of the
  declared type, not of the value's storage.
- **Parameter access becomes const/read plus atomic commit.** `Node::param(id) const` inspects and
  `Node::setParam(id, value)` performs
  the type-checked mutation plus definition-version bump, returning `bool` (as `removeNode` /
  `disconnect` / `removePort` do) and changing neither on failure. The Inspector's ParamEditor and
  `flow::serialize` decode into a temporary `PortValue` and commit through the same Node operation;
  direct mutable `Param&` access is removed.

  **Revised 2026-08-02, while building step 2.** This originally read "params keep positional indices
  rather than gaining `PortId`-style handles because nothing declares one dynamically and the on-disk
  key is the name" — and named a `ParamIndex` type for the cursor. Both are dropped. A param is now
  declared with a `PortId` from the node's one declaration counter, so `setParam` takes that id: the
  argument for indices only held while params can never be declared dynamically, and buying that
  future-proofing cost one field on `Param`. Positional access stays for iteration, as a plain
  `std::size_t` — the `PortIndex` alias is retired rather than renamed, because naming a *position*
  invited storing one, which is the mistake the whole contract exists to prevent.
- **Static declaration helpers finish the PortId contract.** `addInput`, `addOutput`, `addInputLike`
  and `addOutputLike` return the minted `PortId`, matching dynamic/boundary declarations. Definition
  access by position remains for iteration; durable access and NodeEvaluation use `PortId`. Port
  names remain the on-disk edge schema, so constructor or live display reordering changes neither
  compute binding nor serialized connections.
- **The scheduler prepares before either backend runs.** Serial and parallel execution share the same
  `Evaluation::prepare(const Graph&)` path, which checks the recorded definition. The parallel backend
  captures stable per-node views; no task calls `operator[]`, resizes evaluation storage or creates a
  child. Hosts inspect the owning `Evaluation` through `EvalPath` + `PortAddress`, while
  `NodeEvaluation` remains the narrow execution capability passed only to compute. `Evaluation` is a
  move-only value in `flow` core (`evaluation.h`) — it needs `PortValue` and nothing else.
- **`Graph::topoOrder()` stops being lazy.** The mutators (`add` / `removeNode` / `connect` /
  `disconnect` / `removePort`) maintain the order, so the accessor is a plain const read of
  non-`mutable` state and two concurrent runs cannot rebuild it at once. Seeding switches from map
  order to the new insertion-order vector — with UUID keys, map order is arbitrary, and topo order
  drives the canvas's default column layout as well as serial execution order.
- **Scheduler entry owns a run lease.** Both `run` and pull `evaluate` acquire the Evaluation's
  non-blocking RAII lease before preparation and throw `std::logic_error` if it is already held. A
  production-path concurrency test blocks one real scheduler invocation inside a test node, verifies
  a second invocation on the same Evaluation fails immediately, then releases the first; a separate
  Evaluation over the same Graph remains runnable.
- **The permitted concurrency is tested, not just promised.** *"Distinct Evaluations over one
  definition may run at once"* is the milestone's headline claim, and the first thing it broke was the
  lazy topo cache. Beside the readable sequential isolation test, N Evaluations run **simultaneously**
  over one `const Graph` on the parallel scheduler's executor, repeated in the style of `[group]`'s
  100× race sweep, asserting values and recompute requests never cross. That is what keeps the const
  rule above from decaying back into a signature.
- **Every plan step names both halves.** The flattened-plan address becomes
  `{const Graph*, Evaluation*, NodeId}`. A group selected because only its child is stale expands
  without republishing unchanged inputs; a local group change or selected outer predecessor sets a
  recompute request on that child Evaluation's boundary input. `Node::dirty`, `selfDirty` and the
  recursive `GroupNode::dirty` override disappear.
- **On-request rearming becomes evaluation-local.** A source calls the per-node evaluation view's
  recompute-request operation instead of `Node::markDirty()`, so running one stream cannot make every
  other evaluation of the shared definition refire.
- **The old dirty verbs split by meaning.** Definition mutations advance the affected node version;
  runtime refresh is `requestRecompute` — `Evaluation::requestRecompute` / `requestRecomputeAll` for a
  host, `NodeEvaluation::requestRecompute` from inside compute. There is no `Graph::markAllDirty` and
  no runtime request that mutates a definition version.
- **Graph replacement replaces runtime state.** A host that owns the two together gets this for free:
  replacing the definition — New, Open, undo/redo restoration — replaces the Evaluation with it.
  UUID-preserving host keys and CanvasIds may remain; computed values and version observations do not
  cross that boundary. So identity preservation buys *editor* continuity (path, selection, canvas ids,
  undo without ordinals), never evaluation continuity: a load or undo recomputes from scratch, exactly
  as it does today. Reusing an evaluation across a document reload is a separate question, and one
  UUID identity makes askable for the first time.
- **The boundary API follows the runtime owner.** `GroupInputNode::m_bound`,
  `BoundaryInput::setValue` and `GroupOutputNode::value` disappear. `BoundaryInput` /
  `BoundaryOutput` are immutable definition handles; `Evaluation::bind(input, value)` and
  `Evaluation::value(output)` are the host operations. CLI, Interface and group entry all use that
  same production path, so there is no node-local binding cache beside the Evaluation.
- **`PinKey` becomes `{EvalPath, NodeId, PortId}`.** Same arity as today, but every field is a real
  axis — *which evaluation*, *which node*, *which port* — rather than a field papering over scoping.
- **ADR-0010's "a link references a recipe, never a runtime share" no longer holds.** That was argued
  on correctness — *"a `Port` holds a persistent value, so two instances sharing one inner graph would
  stomp each other's intermediates"* — and it was a consequence of exactly the mixing removed here.
  With values in evaluations, **N linked groups can safely share one definition**. M6 proves that
  capability by running one real Graph through independent Evaluations (including the parallel
  scheduler), but does not change LinkedGroupNode ownership. A later vertical introduces shared
  template-definition caching/ownership, reload propagation and file-watch policy together.
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

> **Settled for the map case (2026-08-11) by [ADR-0014](0014-map-nodes-staged-planning.md)**, once a
> concrete caller existed — a folder of images listed in-graph. In short: the child index is
> **positional**; an unready element suppresses the **whole** map output; and the plan lowers a map by
> **staging** — `expand()` stops at a map of unknown arity and the run re-plans, so the second
> coordinator point is the gap between stages and the invariant below survives unchanged. **Loop
> remains unsettled**: a map's children are independent by construction, a loop's are not.
>
> **Loop settled in turn (2026-09-05) by
> [ADR-0021](0021-loop-nodes-carried-state-per-iteration-staging.md)** — with, unusually, no caller:
> the feature exists to test the node model itself, which the ADR states rather than hides. It
> **carries**, through paired inner boundary pins identified by `PortId`. One retained child
> evaluation, reused per iteration; the same staging machinery, but a frontier raised **per
> iteration** rather than once — which is why a loop is bounded by construction, since the bound
> replaces the termination argument ADR-0014 got from "a map is deferred at most once". All four
> questions here are now answered.

- Whether a map's child index is positional or something stabler. If the input collection reorders
  between runs, *"element 3"* follows the position, not the stream, and a pinned preview quietly
  changes subject.
- ~~Whether Loop carries state between iterations or starts clean.~~ **Settled by ADR-0021: it
  carries**, through paired inner boundary pins. A count loop with no carry would just be a map over a
  range, so the carry is the whole reason the node kind exists.
- How suppression (ADR-0007) crosses a map — does an unready element suppress the whole map or just
  its own child?
- How the execution plan (ADR-0009) lowers a map: N children expanded into one flat plan, or a
  nested-but-joined shape — **and with it, where a map prepares its children.** These are one
  question, not two: a map's arity comes from a collection computed *during* the run, so its child
  Evaluations cannot exist when the plan is built. Whatever shape the plan takes decides where the
  second coordinator point sits. The invariant that must survive either answer is the one stated
  above — a coordinator grows evaluation storage, never a worker task.
