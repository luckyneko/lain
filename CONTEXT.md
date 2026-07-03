# CONTEXT.md — domain vocabulary

The ubiquitous language for `lain`. Names here are load-bearing: use them exactly
in code, comments, and reviews. Architecture terms (module, interface, depth, seam,
adapter, leverage, locality) come from the `/codebase-design` skill; this file names
the *domain*. `WORK.md` owns the build plan; `CLAUDE.md` owns how to build it.

## The `flow` engine — three layers over one data model

`flow` is deliberately split into three peer layers, public → private. Keep them
apart: logic that belongs to one must not swell another.

- **Graph** *(data model)* — owns nodes + edges and the invariant-preserving
  **primitives** that mutate them: `connect` (type-checked, single-source,
  acyclic), `disconnect`, `removeNode`, `add`. Each primitive is total and keeps
  exactly one data-model invariant. Graph does **not** execute and does **not**
  carry editor policy. `connect` *reports* (`Connection::InputInUse` /
  `WouldCycle` / …) — it does not decide.

- **Scheduler** *(execution layer)* — consumes a Graph and evaluates it (push
  `run`, pull `evaluate`). Execution lives here, never on Graph. See `scheduler.h`.

- **edit** *(editing layer)* — `lain::flow::edit`, a **stateless free-function
  module** over Graph (`edit.h`), peer to Scheduler. Owns editor **policy and
  gesture orchestration** that composes Graph's primitives:
  - **connectReplacing** — replace-on-occupied-input. Because `connect` checks
    `InputInUse` *before* `WouldCycle`, a naive replace can destroy the existing
    edge and then fail the cycle check. `connectReplacing` is **atomic**: it
    captures the edge feeding the target input, disconnects, tries the new
    connect, and **restores the original if the new one is rejected**.
  - **remove** — batch delete-a-selection: removes a set of nodes and a set of
    edges as one gesture, one result. Edges are identified by **value / stable
    destination** `(to, inPort)`, never by list position, so the shifting-index
    hazard cannot arise inside the module.
  - **disconnect** / **addNode** — thin routes to Graph's primitives, kept so the
    adapter talks to a single mutation seam (a future undo/command log has one
    choke point).

  Policy varies across front-ends; primitives don't. That is why editing is its
  own layer and not methods on Graph.

## Adapter

A front-end (e.g. flowview's imnodes canvas) is an **adapter**: it decodes its own
input (pins, selection, cursor, key gestures) into calls on `edit::` and `Graph`.
It owns everything GUI — `pinId`/`decodePin`, gesture detection, selection
resolution, placement — and no graph-mutation policy. The editing layer is the
**test surface**: edit logic is tested against a plain Graph with GPU-free nodes,
never by driving a live GUI.

## Payload-agnostic

`flow` names no GPU/UI types. A **PortValue** is a type-erased slot (`std::any`)
carrying any copyable payload — a CPU value or a GPU handle (`acm::Texture`). A
consumer that cares compares `type()` against `typeid(...)`. See `CLAUDE.md`.
