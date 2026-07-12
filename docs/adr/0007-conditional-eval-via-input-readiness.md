# Conditional / gated eval via input readiness (a required input needs a value)

---
Status: accepted
---

`flow`'s scheduler is *push*: every node in the (incremental) run set fires once its inputs are
ready. To let a graph turn a branch off — a `Gate` bypassing an expensive subtree, a `Select`
choosing one of several — we need a way for a node to **suppress** its downstream, which a pure push
DAG can't express. The decisive framing: **in a push DAG you save computation by gating *upstream*,
not by selecting *downstream*** — a `Select` after two branches has already paid for both; only a
gate *before* an expensive subtree, whose "off" propagates *forward*, avoids the work.

## Decision

Model conditional eval as **input readiness**, unifying it with input optionality:

- **A node is READY (computes) iff every *required* input carries a value.** If a required input is
  empty, the scheduler does **not** call `compute()`; it **clears the node's outputs**, and that
  emptiness propagates — the next node's required input is now empty, so it isn't ready either.
- **Per-input `Presence { Required, Optional }`** (default `Required`) on `addInput`. An **Optional**
  input (a `Select`/`Merge` branch) does *not* block readiness; the node's `compute()` checks
  presence (`Port::ready()`) and picks a live one. `Presence` is input-only — a node's readiness is
  gated by what it *consumes*, so `addOutput` has no such flag.
- **A `Gate` suppresses by producing no value** (`output.clear()` when off). There is **no separate
  "skip" state** — "skip" is just *absence of a value*, and the existing `Port` empty/has-value
  distinction (`ready()`, `clear()`) carries it. `Scheduler::runNode` is the one "execute a node"
  primitive (shared by both run strategies and the pull walk) that applies the readiness gate.

## Why

- **It unifies two concepts that are the same thing.** "This input tolerates a suppressed upstream"
  and "this input is optional" are identical — an input either blocks the node when it has no value,
  or it doesn't. Collapsing them removes a whole port state and a parallel vocabulary.
- **It respects push + fire-and-join.** Suppression is forward-propagating emptiness the scheduler
  reads; no node blocks on a nested evaluation (why a pull-lazy "only evaluate the taken branch" model
  was rejected — it would make `compute()` trigger a graph run).
- **Topo order removes ambiguity.** A node's upstream is always processed first, so an empty input at
  compute-time means *genuinely no value* (unconnected, or an upstream that produced nothing) — never
  "not computed yet." So empty-means-suppress is safe.
- **It composes with incremental eval for free.** A subtree's readiness is determined by its gate's
  output, which changes only when the gate **recomputes**, which happens only when it is **dirty** —
  and the dirty closure ([WORK.md Tier A #3](../../WORK.md)) already includes everything downstream of
  a dirty node. So a gate gaining a value is dirty → the subtree is in the closure → each node is
  re-evaluated → now ready → it computes (resurrected). Off and stable-off fall out the same way.
  The only requirement is that a not-ready node is still **processed** (its outputs cleared, its
  dirty flag cleared) rather than silently omitted, so a stable-off subtree goes clean and drops out
  of future closures — keeping it cheap.

## Consequences

- **Behaviour change:** a node with an empty *required* input no longer computes (it used to run with
  an empty input and defend against it). This is arguably *more* correct — a node shouldn't compute
  garbage from a missing required input — and existing nodes are unaffected because their inputs are
  connected + produced in a running graph.
- **`Port` gains one bit** (`m_required`, input-only) and `runNode` one input scan. No `PortValue`
  change, so **params** (which reuse `PortValue` but aren't dataflow) are untouched.
- **"Gated off" vs "unconnected" is derivable, not stored** — a suppressed port has an incoming edge
  and an empty value; an unconnected one has no edge — so a debugging UI loses nothing.
- **Concrete `Gate` / `Select` / `Merge` nodes are a separate slice** — this ADR is the mechanism. A
  host (flowview, the cli `run`) treats an empty boundary output as "no output this run" (don't write
  it) — also a later slice.
- **Considered + rejected:** an explicit third `Port` "skipped" state distinct from empty (more
  machinery for a distinction that's derivable); a scheduler-computed "live set" from gate predicates
  (fiddly ordering); and pull-based laziness (violates fire-and-join).
