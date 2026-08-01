# Subgraph reuse: inline vs linked groups, with no prefab overrides

---
Status: accepted; the sharing constraint superseded by ADR-0012
---

> **Revisit (2026-08-01).** The rule below that "a link references a recipe, never a running graph"
> was argued on correctness — a `Port` holds a persistent value, so two instances sharing one inner
> graph would stomp each other's intermediates. That was true, and it was a consequence of `flow`
> mixing structure with per-run state. **[ADR-0012](0012-definition-and-evaluation.md)** removes the
> mixing, so N linked groups *can* share one definition, each with its own evaluation — which is what
> makes a template edit reach every instance live rather than on reload. Everything else here (inline
> vs linked, the interface cache, read-only-in-place, overrides deferred) stands unchanged.

A subgraph's *recipe* can live in the parent document or in its own file, and the second raises the
prefab question: if many nodes are built from one template, may an instance diverge from it, and how
do template edits reach the instances? This decides the document model for group nodes; ADR-0009
decides how they execute.

## Decision

**Two node kinds, one implementation.**

- **Inline group** — the recipe is stored *inside the parent document* (`"graph": {nodes, edges,
  editor}`, the same body shape the root uses, recursively). Editable in place: its edits mark the
  parent dirty and ride the parent's undo history, because an inline subgraph is literally part of the
  parent document that the undo snapshot already captures.
- **Linked group** — the recipe lives in its own document, the **template**, referenced by a path
  stored **relative to the parent document** (`"source": "subs/denoise.json"`). **Read-only in place.**
  It is exactly what the template says, so a template edit reaches every linked group the next time
  they load, with **no per-instance diff**. Editing the template is an explicit **Edit Template…**
  gesture that opens it as its own edit session.

Port mirroring, plan expansion, dirty propagation and click-through navigation are shared — the kinds
differ only in what they serialize and whether in-place editing is refused.

**A link references a recipe, never a running graph.** Every group node owns its own inner `Graph`,
always. This is not a preference: a `Port` holds a *persistent* value (the inspectability contract),
so two group nodes sharing one inner graph would stomp each other's intermediates — and under
ADR-0009's flattened plan they would do so *concurrently*, which is a data race.

**The parent document caches a linked group's interface** (pin names + port-type keys + direction)
beside the source path. The template always wins when present; the cache exists so that a **missing**
template degrades to a *repairable placeholder* — correct pins, wiring intact, visibly marked
unresolved, and lossless to save — and so a **changed** interface can be diffed and reported rather
than silently dropping edges.

**Per-instance variation is expressed by exposing the varying value as a boundary pin**, not by
overriding inner values. Prefab-style **overrides are deferred** (see Consequences).

**Every `Graph` has exactly one `GroupInputNode` and one `GroupOutputNode`, enforced by `Graph`
itself** — created on construction, undeletable, and a second refused. A graph without them has no
path in or out, and several are redundant since one boundary node already grows dynamic pins. This is
a totality guarantee of the same class as acyclicity, not editor policy, so it belongs on the
primitives; `boundaryInputNode()` / `boundaryOutputNode()` consequently never return null.

Because `flow` core does no file I/O, **the template resolver is injected**: `fromValue` takes a
`source → {canonical key, document}` callback that the app supplies (flowview's `graphio`), and
recursion is held inside `flow::serialize` so it can refuse a **recursive template** by canonical key.

## Why

- **Read-only linked groups deliver the update-propagation requirement with zero machinery.** Because
  instances hold no local edits, rebuilding from the template *is* the update. Diffing is needed only
  to answer "the template changed *and* I had local edits — merge them," which is precisely the
  feature being deferred.
- **Parameterise, don't override.** If a value varies per instance, that is a *declared input of the
  subgraph* — visible on the node's face as a pin — rather than a hidden mutation you must open the
  node to discover. It also costs nothing: the boundary-pin machinery already exists, and host-injected
  boundary values are never serialized, so "same structure, different live data" is free.
- **The interface cache prevents the worst failure mode.** Edges serialize by *port name*, and a linked
  group's ports come from the template — so without a cache, a moved or missing template leaves the
  loader unable to reconstruct the node's ports, dropping every edge into and out of it, and the next
  save makes that permanent. That failure lands hardest in exactly the situation instancing exists for.

## Consequences

- **A pin renamed in a template breaks the parent's edges to it.** Within a live session a rename
  preserves wiring (the outer↔inner mapping is by `PortId`), but *across documents* the only identity
  is the port name. Rectification on load reports it as an Issue against the cached interface; it
  cannot be silently repaired.
- **Editing a template is a document swap**, guarded by the unsaved-changes modal with the parent
  pushed on a return stack, and it should state its blast radius up front ("denoise.json is used by 3
  linked groups in this document").
- **Overrides, if ever built,** add an `"overrides"` key beside `"source"` — but they first need a
  template-stable address for an inner node and rules for an override targeting a node the template
  deleted. Nothing here forecloses them.
- **The loader must *reuse* the boundary pair, not add it.** Boundary nodes are factory-registered
  (`"groupInput"` / `"groupOutput"`) and appear in documents as ordinary nodes — so with the graph
  auto-creating a pair, `fromValue` would otherwise produce duplicates. Rule: on meeting a boundary
  kind, route it onto the graph's **existing** node (replaying its dynamic pins onto that node) and
  point the file-id → live-id remap at the existing id. This is the one place the invariant and the
  serializer actively fight, so it is stated rather than left to be rediscovered.
- **A fresh `Graph` has `nodeCount() == 2`**, which churns the existing node-count assertions, and
  `buildNewScene`'s explicit pair-add becomes redundant. `Graph` also gains a compile-time dependency
  on the concrete boundary node types (already core, so no boundary-rule violation) — it is no longer
  content-agnostic, accepted on the same grounds as it already enforcing acyclicity.
- **Third-party structural node kinds** would want a registered node-serializer seam keyed by factory
  key (the `ValueCodecs` pattern) instead of the RTTI branch in `flow::serialize`. Judged unlikely
  near-term; the branch is small and localised if that changes.
