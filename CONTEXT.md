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

## Value display — two purposes, two seams

Rendering a port's value splits by *purpose*; don't conflate them.

- **Text** *(cli dump, debug, inspector labels)* — **`meta::toString<T>(const T&)`**
  (`lain::meta`, fmt-free): best-effort stringify — `bool`→true/false, a member
  `toString()`, else an ostream operator, else the type name. Unlike
  `lain::string::format` (which needs a *formattable* type), it always produces
  something. A type opts into a nice text form just by exposing `toString()` — there
  is **no central `holds<int>/holds<float>/…` ladder**.

- **PortType** *(the per-type flyweight)* — `flow`'s **`PortType`** (`porttype.h`)
  bundles a declared type's reflective facts — `type_index`, human `typeName`, and a
  `describe(PortValue)` bridge over `meta::toString` — into **one static instance per
  type**. A `Port` holds a single `const PortType*` (not a copy of each fact), captured
  at `addInput/addOutput<T>`. `Port::type()` / `typeName()` / `describe()` forward
  through it. This is **the extension point for per-type facilities**: a new one becomes
  a field on `PortType`, never another functor on every `Port`.

- **GUI view** *(deferred)* — showing a value richly (a texture *thumbnail*, not the
  string `"acm::Texture 64x64"`) is a **separate, larger seam**: a registry of viewers
  keyed by type, living in `lain::gui` or the app. Not built yet. Today each adapter
  keeps its own texture branch (cli reads back pixels, the inspector draws a thumbnail)
  and falls through to `Port::describe()` for everything else.
