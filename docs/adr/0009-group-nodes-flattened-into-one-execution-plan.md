# Group nodes are flattened by the scheduler into one execution plan

---
Status: accepted
---

A **group node** contains its own `Graph` and exposes selected inner ports as its own — the
subgraph mechanism, with the top-level graph as the outermost group. The question is *who runs
the inner graph*. Three shapes were live:

1. **The node runs it** — `GroupNode::compute()` drives an inner `SerialScheduler`.
2. **The scheduler flattens it** — the whole nesting tree becomes one task DAG.
3. **The scheduler recurses at runtime** — a Taskflow subflow per group, joined.

Threading and performance are a top design priority for `flow` (its reason for being is a
volumetric reconstruction pipeline over N streams), and (1) confines a group's entire contents to
one thread — a group is exactly where the parallel work will live.

## Decision

**The `Scheduler` expands the whole nesting tree into a single, flat execution plan.**

- A **plan** is a topologically ordered list of **steps** with step-index dependency edges. A step
  is either "run node `{Graph*, NodeId}`" or one of a group's two boundary steps: an **entry step**
  (copy the group's outer input port values into its inner `GroupInputNode`) and an **exit step**
  (copy the inner `GroupOutputNode`'s delivered values out to the group's outer output ports).
  Expansion recurses, so a plan spans every level at once.
- `SerialScheduler` walks the plan in order. `ParallelScheduler` emplaces one task per step and one
  `precede` per plan edge. **Nothing is nested at runtime** — one flat task DAG, N levels deep.
- **Both `run()` and `evaluate()` build plans** (dirty-closure mode and upstream-cone mode). There is
  one expansion mechanism, so `GroupNode::compute()` is a documented no-op — a group is never
  executed as a node.
- **`Node` gains `virtual Graph* innerGraph()`** (null by default). The scheduler asks the structural
  question it actually has — *does this node contain a graph?* — rather than `dynamic_cast`-ing for a
  class identity. (`flow::serialize` keeps RTTI: there the question really is *which kind is this*.)
- **`Node::dirty()` becomes virtual.** A group is dirty if its own flag is set *or* anything inside it
  is, recursively — otherwise an edit inside a collapsed group would never reach the outer dirty
  closure.
- **The entry step gates its publish.** Publishing into the inner `GroupInputNode` marks it dirty,
  which would re-run the whole inner graph; so it publishes only when the group's inputs can have
  changed (`group.dirty()` own-flag, or an outer predecessor was selected into the run). Both are known
  at plan-build time. Without this gate, any inner param edit destroys inner incrementality.

## Why

- **It is the only shape that parallelises across group boundaries.** Inner nodes of two sibling
  groups interleave freely on the pool, and an inner node starts the instant its own dependencies are
  met — there is no per-group join barrier. (1) serialises a whole group; (3) at best matches
  flattening while adding a runtime nesting concept.
- **It keeps the fire-and-join contract literally true.** "A node body never blocks on a nested graph
  run" holds by construction, because no group ever runs a scheduler. (3) relies on Taskflow's corun
  semantics; (1) makes a `compute()` whose safety depends on which thread called it — the kind of
  by-convention invariant this codebase has consistently refused (`Graph::removePort` *refuses* rather
  than trusting callers; `connect` *rejects* cycles rather than documenting them).
- **It needs no new substrate surface.** Only `emplace` + `precede` + `run`, which `lain::task`
  already exposes and which `multi`'s `Recipe::step` / `Recipe::order` / `Context::async` match 1:1 —
  so the planned Taskflow → `multi` swap stays a `lain::task` internal change with no `flow` edits.
  (3) would have forced subflows through the wrapper, the one Taskflow concept most likely to leak.
- **Conditional eval crosses group boundaries for free.** An unready group publishes empty values into
  its inner boundary; inner nodes see empty required inputs and suppress themselves through the
  existing readiness gate (ADR-0007); the emptiness reaches the inner `GroupOutputNode` and the exit
  step copies out nothing. No "skip this subtree" mechanism is needed in the plan.

## Consequences

- **The plan is rebuilt every run** — a recursive walk plus a dirty scan, the same order of work as
  today's `runOrder`. Caching a plan across runs is a deferred optimisation, not a requirement.
- **A suppressed group's inner *sources* still fire** (a node with no required inputs is always ready).
  Harmless recompute; noted so it isn't mistaken for a bug.
- **Crossing a boundary costs `PortValue` copies** (~4 in, ~3 out, against 1 for a plain edge). This is
  why **shared, immutable `PortValue` payloads** are a prerequisite slice: with a refcounted payload,
  boundary crossings cost nothing and the boundary nodes can stay fully honest and *visible* in the
  UI. The alternative — eliding the boundary nodes in the plan to dodge copies — was rejected because
  it would blank their pins inside the group and cost inner-graph inspectability.
- **Serial and parallel must agree.** The equivalence test (same port values over a nested graph under
  both schedulers) replaces the reference implementation that shape (1) would have provided for free.
