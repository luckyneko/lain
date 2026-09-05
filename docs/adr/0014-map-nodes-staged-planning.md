# A map node runs one definition N times, planned in stages

---
Status: accepted
Settles the four questions [ADR-0012](0012-definition-and-evaluation.md) left deliberately unsettled
---

ADR-0012 moved runtime state off the definition so that **one definition could back N evaluations**,
and named four questions it refused to guess at — *"these need a concrete SplitGroup or Loop in front
of them; guessing produces a mechanism fitted to imagined requirements"*. It now has one. The first
vertical is a **folder of images, listed in-graph**: a node lists a directory, a map runs the same
subgraph per element, and a combine node folds the results to one output.

That caller decides all four at once, because it makes **arity data-dependent**: the collection is
computed by a node *during* the run, so a map's child Evaluations cannot exist when the plan is built.
Every decision below falls out of that one fact meeting two rules already in force — ADR-0012's *"a
coordinator grows evaluation storage, a worker task never does"* and ADR-0009's *"it needs no new
substrate surface"*.

## Decision

**A map is a group whose interior runs once per element of a collection, and whose arity is discovered
mid-run — so the scheduler plans in stages rather than once.**

- **A collection is read through a `PortType` capability.** `PortType` gains three fields beside
  `describe`: `element` (the element type's `PortType`, null unless T is a collection), `size(const
  PortValue&)` and `at(const PortValue&, size_t)`. `portType<T>()` fills them when
  `meta::traits::is_vector_v<T>`, through the same per-type function-pointer bridge `describe` already
  uses — so **flow core still names no payload type**. A fourth, `gather(span<const PortValue>)`,
  builds the vector on the way out. The payload stays a natural `std::vector<T>`.
- **Element access is zero-copy.** `at()` returns a `PortValue` built with `shared_ptr`'s **aliasing
  constructor**: it shares the producer's control block and points at element *i*. No image is copied
  on the way in, and the producer's vector cannot be freed while a child still holds an element.
- **The plan stops at a frontier and the run re-plans.** `expand()` refuses to descend into a map
  whose arity is not yet known, leaving it and everything downstream out of this stage. `run()` becomes
  a loop — build a plan, execute it flat, prepare the children of every map whose input has now
  arrived, build again — until a stage plans nothing. Each stage is one flat DAG needing only
  `emplace`/`precede`/`run`, and **the second coordinator point is the gap between stages**, on the
  coordinator thread. `evaluate()` (pull) stages identically.
- **A map's child is identified by position.** `EvalPath` finally becomes what ADR-0012 wrote — a
  sequence of `{NodeId, index}` steps — with a group being index 0. `Evaluation`'s children become
  N-per-node. Position *is* a vector's identity; a reorder rebinds each child and recomputes it.
- **An input splits or broadcasts according to its own declared type.** Mirroring offers each inner
  input pin of type `T` as either `vector<T>` (split) or `T` (broadcast); the entry step decides by
  comparing the outer port's type against the inner pin it maps to — element-of-inner means split,
  equal-to-inner means broadcast. **Nothing is stored, and there is no flag.** Outputs always gather.
- **A hole suppresses the whole output.** If any child's inner `GroupOutput` is empty, the map clears
  its output port and ADR-0007's existing emptiness-propagates rule carries it downstream unchanged.
  `N == 0` is *not* that case: an empty collection in yields an empty vector out, which is a value.
  Split inputs of **ragged lengths** suppress and report, as does a map whose split input is itself
  empty (that one needs no new rule — it is the ordinary readiness gate).
- **`MapNode` is a third `GroupNode` subclass, inline only.** It owns its `Graph` beside
  `InlineGroupNode` and overrides mirroring to lift. A **linked** map — one shared template mapped
  over N streams — is deferred until the video workload asks; `LinkedGroupNode::adoptInterior` is
  already the seam it would reuse.
- **A map serializes its interface; a group still does not.** The map writes its outer port list
  (name + type) beside its body, and load reconciles it against the rebuilt inner boundary with the
  same rectification pass linked groups already use.
- **The viewer selects an element through the path it already holds.** Descending defaults to element
  0; the breadcrumb renders and edits the index; an Issues row for a failed element navigates to it.

## Why

**Staging is the only shape that keeps both existing rules.** Dynamic spawning — a map task creating
its children through a Taskflow subflow — is the obvious alternative and fails twice over: it makes a
**worker** grow shared evaluation storage, which is precisely the invariant that makes concurrent node
tasks safe beyond convention, and it forces subflows through `lain::task`, the one Taskflow concept
ADR-0009 singled out as most likely to leak and the thing that would break the planned `multi` swap.
Staging needs no substrate surface at all: a stage is the flat DAG the backends already run. Reading
arity from the *previous* run's value would keep the plan static and single-stage, but it is wrong on
the first run of a fresh graph — which is exactly what `flowview run` does every time.

**The frontier is the real data dependency, not a join.** Everything not downstream of an unexpanded
map still plans into the current stage, so a map's independent siblings keep running beside it. A
global two-phase split would have been simpler to state and would have reintroduced the per-group join
barrier ADR-0009 chose flattening to avoid.

**A natural `std::vector<T>` is what a producer already has.** The alternative — a core-owned
`Collection` of `PortValue`s — is zero-copy in both directions, but it makes every producer erase its
elements by hand, leaves the element type knowable only at runtime (so an inner boundary pin cannot be
statically typed or `connect`-checked), and adds a second aggregate concept beside the vector-valued
port CONTEXT.md already names. Worse, the two would never interconnect: `std::vector<Image>` and
`Collection` are different `type_index`es, so every consumer node would have to pick a side. The
capability is an interface, so the cheaper payload can arrive behind it later without touching a node.

**Position is the only identity a vector has.** A value-derived key would keep a reordered list
incremental and a pinned preview on its subject, but it needs a per-type `key()` capability that is
unimplementable for the very type the first vertical maps over — a path has a natural key, an image
does not, and hashing pixels to identify a frame is absurd. An explicit parallel key input would work
and is what the video workload will probably want; building it now means inventing rules for
mismatched lengths, duplicate keys and empty keys that nothing is asking for.

**The mode belongs in the port type because a remembered bool is what M7 deleted.** A per-pin
split/broadcast flag can disagree with the port it describes, gives `edit::syncGroupPorts` a third
thing to reconcile, and is the same shape as the `editable` bool M7 slice 1 removed on the grounds
that *"read-only-in-place stops being a `bool` every pane must remember and becomes something the type
refuses"*. Deriving the mode from the declaration means there is no second source to disagree with.
The remaining alternative — lifting everything and repeating a shared value N times upstream — is
circular: the repeat node would need N, which is only known downstream of it.

**A hole cannot be represented, so it must not be hidden.** `std::vector<Image>` has no hole, and
silently gathering the survivors into a shorter vector would break the positional correspondence
between the input list and the output list, so a second map over the same input would no longer line
up with this one — a quiet wrong answer. `vector<optional<U>>` is honest but turns the map's output
into something no ordinary node accepts. Clearing the output is the same call the image encoders and
the group gestures' `UnnamedPinType` refusal already make: reject rather than degrade, loudly.

**A map's interface is stored because it genuinely is not derivable.** M5's rule that a group's own
ports are never serialized rests on derivation being *total* — sync rebuilds them from the inner
boundary, which is what lets the parent's name-addressed edges land. A map's mirroring is
under-determined by one bit per input pin, so the honest response is to store the port list and
reconcile it, exactly as `LinkedGroupNode` stores and rectifies its cached interface. Storing the
*ports* rather than a *flag* keeps the decision above intact: what is written is the type, and the
type is the mode.

## Consequences

- **Per-element incrementality is not available**, and this is the sharpest cost. Any change to a
  `std::vector<T>` rebuilds the whole payload, so every element's address changes and all N children
  recompute — one new frame on one stream would recompute all N. The escape is the `Collection`
  payload behind the same `size`/`at`/`gather` interface, which is why that seam exists. The entry
  step therefore binds unconditionally rather than comparing, since there is nothing a comparison
  could save.
- **Child state is retained per element** — N× the subgraph's values, and these are images. That
  follows ADR-0012's *"retention is ownership"* with no policy type, and Q9's element stepper requires
  it: you cannot inspect element 7 if element 7's state was dropped. The preview cache is unaffected —
  it still holds one level and clears on navigation, and switching element *is* a navigation.
- **The gather costs N element copies per run.** `at()` can alias; building a vector cannot.
- **A stage barrier exists at each map frontier**, and the number of stages is 1 + the maximum map
  nesting depth. For the target workload that is 2.
- **List payload types need port-type registry keys** (`registerPortType<std::vector<image::Image>>
  ("ListOfImage")`), or the stored interface cannot name them. The group gestures' `UnnamedPinType`
  refusal is the existing precedent for that shape.
- **`Evaluation`'s child container changes** from `map<NodeId, unique_ptr<Evaluation>>` to N-per-node,
  and `prepare` gains a second call point (between stages) that creates a map's children now that its
  arity is known. Pruning when N shrinks is a truncate.
- **`GraphPath` stops being a sequence of NodeIds**, which reaches every flowview pane through
  `PinKey`, `resolvePath` / `resolveEvaluation`, the breadcrumb, the layout tree and the Issues rows.
  M5's ten bugs all lived in that surface, which is why it is the last slice and gets its own live
  verification.
- **`Scheduler::run` and `evaluate` become loops.** The run lease is held across every stage of one
  invocation, so the concurrency contract is unchanged: one Evaluation still has one scheduler
  invocation at a time, and a second entry still throws rather than waiting.
- **A map is never run as a node**, exactly as a group is not — `MapNode::compute` is the same
  documented no-op, and the entry/exit steps gain per-element variants.

## Deliberately unsettled

- ~~**Loop, and whether it carries state between iterations.**~~ **Settled 2026-09-05 by
  [ADR-0021](0021-loop-nodes-carried-state-per-iteration-staging.md)** — it carries, through paired
  inner boundary pins. The prediction here held: a loop is a different execution shape rather than a
  variant, and the difference lands in the staging loop. A map raises a frontier **once**, which is
  what bounds the number of stages; a loop raises one per iteration, so ADR-0021 makes a loop bounded
  by construction and that bound *is* the replacement for this ADR's termination argument. Two things
  built here paid forward unchanged: the frontier machinery needed no new substrate, and `at()`'s
  aliasing sibling — M5's shared, immutable `PortValue` payload — is what lets a loop read a carry out
  of a child evaluation and bind it straight back into the same one.
- **A linked map.** One shared template mapped over N streams is what the video workload wants, and
  M7's ownership model already permits it; it needs the workload in front of it to decide how an
  instance's interface reconciles against a shared template's.
- **The `Collection` payload.** Named here as the escape from the two costs above, but not built: it
  buys nothing the first vertical can measure.
- **Keyed elements.** The parallel key input, for when "stream 3" must survive a reorder.
- **A cli binder for a collection boundary input.** `BoundaryBinders` maps one string to one
  `PortValue`; binding `--files a.png b.png c.png` needs a multi-value arm. Not needed by the first
  vertical, whose list is built in-graph by `listDir`.
