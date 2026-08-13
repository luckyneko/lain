# Group nodes are flattened by the scheduler into one execution plan

---
Status: accepted
---

> **Revisit (2026-08-01).** [ADR-0012](0012-definition-and-evaluation.md) keeps the flattening but
> separates definition from runtime state. A step is consequently addressed by
> `{const Graph*, Evaluation*, NodeId}`, not `{Graph*, NodeId}`; staleness and boundary publication
> are read/written in that Evaluation. The topology of the flat plan is unchanged.
>
> **Revisit (2026-08-11).** [ADR-0014](0014-map-nodes-staged-planning.md) keeps every rule here and
> adds one: a **map**'s arity is only known mid-run, so *one invocation builds several plans*.
> `expand()` stops at a map of unknown arity and the run re-plans after executing what it has. Each
> **stage** is still exactly the flat DAG described below, still needs only `emplace`/`precede`/`run`,
> and still nests nothing at runtime — which is precisely why staging was chosen over a subflow.

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
  is either "run node `{const Graph*, Evaluation*, NodeId}`" or one of a group's two boundary steps:
  an **entry step** (copy the group's outer evaluated inputs into its child evaluation's
  `GroupInputNode`) and an **exit step** (copy the child evaluation's `GroupOutputNode` values to the
  group's evaluated outputs). Expansion recurses through matching definition/evaluation children, so
  a plan spans every level at once. Two steps may name the same shared definition with different
  Evaluation pointers.
- `SerialScheduler` walks the plan in order. `ParallelScheduler` emplaces one task per step and one
  `precede` per plan edge. **Nothing is nested at runtime** — one flat task DAG, N levels deep.
- **Both `run()` and `evaluate()` build plans** (stale-closure mode and upstream-cone mode). There is
  one expansion mechanism, so `GroupNode::compute()` is a documented no-op — a group is never
  executed as a node.
- **`Node` gains `virtual Graph* innerGraph()`** (null by default). The scheduler asks the structural
  question it actually has — *does this node contain a graph?* — rather than `dynamic_cast`-ing for a
  class identity. (`flow::serialize` keeps RTTI: there the question really is *which kind is this*.)
- **Planning is evaluation-aware.** `runOrder(definition, evaluation)` selects a group when the group
  itself needs recompute or anything in its matching child Evaluation does, recursively — otherwise an
  edit inside a collapsed group would never reach the outer closure. `Node::dirty()` and
  `GroupNode::dirty()` disappear with runtime state on the definition.
- **The entry step gates its publish.** Republishing into the child boundary would request a run of
  the inner input path, so entry does it only when the group itself or an outer predecessor changed.
  A group selected solely because an inner node is stale still expands, but does not republish
  unchanged inputs. The request goes to that child Evaluation's boundary input, never to the inner
  definition.

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

- **The plan is rebuilt every scheduler invocation** — a recursive walk plus an evaluation-staleness
  scan, the same order of work as the original `runOrder`. Caching a plan is a deferred optimisation,
  not a requirement.
- **A suppressed group's inner *sources* still fire** (a node with no required inputs is always ready).
  Harmless recompute; noted so it isn't mistaken for a bug.
- **Crossing a boundary costs `PortValue` copies** (~4 in, ~3 out, against 1 for a plain edge). This is
  why **shared, immutable `PortValue` payloads** are a prerequisite slice: with a refcounted payload,
  boundary crossings cost nothing and the boundary nodes can stay fully honest and *visible* in the
  UI. The alternative — eliding the boundary nodes in the plan to dodge copies — was rejected because
  it would blank their pins inside the group and cost inner-graph inspectability.
- **Serial and parallel must agree.** The equivalence test (same port values over a nested graph under
  both schedulers) replaces the reference implementation that shape (1) would have provided for free.
