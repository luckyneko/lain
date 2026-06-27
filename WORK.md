# WORK.md — node-graph library build plan

Forward build plan for `lain`'s node-graph library. See [AGENTS.md](AGENTS.md)
for working discipline. This plan states intent and decisions; it is not a
commitment to build past what a milestone needs. Discipline is "only the
necessary" — don't build ahead of a real caller.

## What this is

A fast, threadable node-graph engine for rapid prototyping: nodes with typed
ports connect into a DAG, the graph evaluates across worker threads, and every
intermediate result is inspectable in a live ImGui viewer — including GPU image
outputs rendered as thumbnails with no copy.

`lain` is a **cumulative set**: an umbrella of small libraries under `libs/`,
each either owned `lain` code or a thin wrapper that gives an external library a
`lain::` face. The node-graph engine composes the set rather than reinventing it:

- **Taskflow** (via **`lain::task`**, `libs/task`) — a header-only task-graph
  executor. It is the execution substrate: it *is* a DAG scheduler with a
  work-stealing executor, so the push side of evaluation is largely its job, not
  ours. We wrap it thin so the rest of the set sees `lain::task`, never `tf::`.
- **`archimedes`** (`acm::`) — Vulkan renderer. Shares one `VkDevice` with the
  graph so a node's `acm::Texture`/`acm::Buffer` output displays in the viewer
  zero-copy.

(`multi`, `lain`'s own work-stealing pool, is **not** used for now —
Taskflow replaces it. It stays a sibling library and can return later behind the
same `lain::task` seam; see Decisions.)

## Decisions locked (planning, 2026-06-27)

1. **Cumulative-set layout.** Every library lives under `libs/` (a `lain::<name>`
   each); runnable executables live under `apps/`. An external dependency is
   brought in as a thin wrapper lib with a matching namespace — `libs/task` →
   `lain::task` (Taskflow), `libs/math` → `lain::math` (GLM, when needed). Owned
   `lain` libraries (`archimedes`, `thorax`) are git submodules under `extern/`;
   third-party libraries come via `FetchContent`, exactly as `archimedes`'
   `cmake/addXXX.cmake` modules do.
2. **Taskflow is the substrate (drops `multi` for now).** `libs/task` wraps
   Taskflow; `flow`'s scheduler lowers the DAG onto a `tf::Taskflow` and runs it
   on a `lain::task` executor. `multi` is not a dependency of `flow` for M1.
3. **Dual CPU + GPU ports.** A port value carries either a plain CPU value or a
   GPU resource (`acm::Texture` / `acm::Buffer`). The graph and renderer share
   one device.
4. **Hybrid execution.** Taskflow's executor drives a **push** run (every node
   fires once its inputs are ready). On top of that we own a **pull** path,
   `evaluate(NodeId)`, that recomputes just one node's upstream subgraph on
   demand. Pull is what serves nodes that don't fit a single full run:
   - **`constant` nodes** — compute once, then stay clean; pulled, not pushed.
   - **on-request sources** — fire afresh each time they're asked, e.g. a
     `CameraCapture` node that grabs a frame per pull.
5. **Milestone 1 ships the viewer.** Core engine and the archimedes+ImGui
   inspector land together so graphs are visible from day one.

Open naming decision: engine library/namespace `lain::flow` (target `flow`,
`libs/flow`), viewer executable `flowview` (`apps/flowview`). Placeholders —
rename before first commit if preferred. Internals live in `lain::flow::detail`
(mirrors `multi::details` / `acm` pImpl conventions).

## Repo layout (proposed)

```
lain/
  CMakeLists.txt              # umbrella; add_subdirectory each libs/* + apps/* + extern/
  cmake/
    lainWarnings.cmake        # shared strict-flag INTERFACE target
    addTaskflow.cmake         # FetchContent, mirrors archimedes' addXXX.cmake
    addImGui.cmake  addImnodes.cmake  addJson.cmake
  extern/
    archimedes/               # submodule (owned), tracks develop
    thorax/                   # owned — deferred until a consumer needs it
  libs/                       # libraries (lain::<name>)
    task/                     # lain::task — thin wrapper over Taskflow
    flow/                     # lain::flow — the node-graph engine (this plan)
      include/lain/flow/
      src/
      test/
    math/                     # lain::math — GLM wrapper (later, when needed)
  apps/                       # executables
    flowview/                 # ImGui inspector
```

Libraries live under `libs/` (a `lain::<name>` each); runnable executables under
`apps/`. Consumed-as-subdirectory rule (`archimedes` already honors it): a lib's
tests build, and `apps/` build, only when `lain` is the top-level CMake project.

## Milestone 1 — engine + viewer

### 1. Build skeleton

Umbrella `CMakeLists.txt` aggregating `libs/*`, `apps/*`, and `extern/*`.
`extern/` submodules (`archimedes`, `thorax`) via `add_subdirectory` with their
testing/benchmark options OFF (default OFF when not top-level). Third-party deps
(Taskflow, ImGui, imnodes, json, Catch2) via `cmake/addXXX.cmake` FetchContent
modules patterned on `archimedes` (prefer system package, else pinned release,
download cached under `.cache/fetch/`, headers re-exposed `SYSTEM` so strict
flags don't fire on them). `libs/task` wraps Taskflow; `flow` links `lain::task`;
`apps/flowview` links `flow` + `archimedes` + ImGui. Strict flags come from a
shared `lain_warnings` INTERFACE target (`/WX /W4`, `-Werror -Wall -Wextra`),
out-of-source enforced.

### 2. Port value — the dual CPU/GPU slot

`PortValue` (`include/lain/flow/portvalue.h`) — a type-erased holder over a
closed set: a CPU `std::any`-style payload **or** an `acm::Texture` / `acm::Buffer`
handle, tagged so the viewer can dispatch on kind without RTTI. Carries a
`std::type_index` for connection type-checking. **Persistent**: a port owns its
buffer and it is overwritten on recompute, never moved/consumed downstream —
this is what makes every stage inspectable after a run.

### 3. Port / Node / Graph

- `Port` — name, `type_index`, owned `PortValue`, `ready()`. Typed `get<T>()` /
  `set<T>()`.
- `Node` (`node.h`) — abstract base, polymorphic `compute()` (templated
  dispatch is not worth it across heterogeneous user nodes; one virtual per node
  eval is noise next to the work). Declares ports in its constructor via
  `addInput`/`addOutput`. `dirty()` flag, optional `onInspect()` ImGui hook,
  `id()`/`name()`. `constant` and on-request sources express their behaviour
  through `dirty()` (a constant clears it after first compute; a `CameraCapture`
  stays dirty so each pull refires).
- `Graph` — owns nodes (`add<T>(...)`), `connect(from,out,to,in)` with
  type-check + cycle rejection, `disconnect`. Holds the edge list and computed
  topo order.

Contract that makes it threadable: a node reads only its inputs and writes only
its own outputs — no shared mutable state. Stated in the `Node` docs and
enforced by review, not the type system.

### 4. Scheduler — lower the DAG onto Taskflow

`Scheduler` (`detail/scheduler.*`) is the seam between our `Graph` and
`lain::task`. Taskflow already owns the push scheduling that `multi` would have
made us write by hand, so this layer is thin:

- **Push.** Lower the `Graph` to a `tf::Taskflow`: one task per node (its body
  calls `Node::compute()`), edges become `task.precede(...)`. Hand it to the
  `lain::task` executor and `run(...).wait()` once. The executor seeds in-degree-0
  nodes and dispatches successors as predecessors finish — work-stealing across
  threads. Rebuild the taskflow when topology changes; reuse it across runs
  otherwise.
- **Pull.** `evaluate(NodeId)` is our own code (Taskflow is push-oriented): walk
  dependencies, recompute only `dirty()` upstream nodes, then run the target.
  Same `Node::compute()` path. This is the constant / on-request (`CameraCapture`)
  entry point.

The scheduler takes an injected `lain::task` executor so a graph can target an
isolated pool; default to a shared process executor. Workers must not block on
nested graph runs — dispatch from the executor's join, never recursively from
inside a node body.

### 5. Viewer (`flowview`)

- ImGui's **official** `imgui_impl_glfw` + `imgui_impl_vulkan` backends driven by
  `archimedes`' raw handles (`Instance::vkInstance`, `Device::vkDevice/vkQueue/
  getQueueIdx`, GPU physical device, `SwapChain::vkRenderPass`,
  `maxSampleCount`). Do **not** hand-write an ImGui backend.
- Record ImGui draw data inside `acm::Renderer::render(record)` — the render pass
  is already begun there. Keep `MSAASamples` in lockstep with the swapchain;
  drive `ImGui_ImplVulkan_SetMinImageCount` from `archimedes`' swapchain-recreate
  path.
- Node editor via **imnodes** (start simple; defer imgui-node-editor and
  multi-viewport/docking until needed).
- **Inspector** reads each port's persistent `PortValue`. CPU values render as
  text/tables; a GPU `acm::Texture` output renders via
  `ImGui_ImplVulkan_AddTexture(sampler, view, layout)` → `ImGui::Image` (uses
  `Texture::vkImageView` + `acm::Sampler`).
- Threading rule: all ImGui calls on the main/render thread; `lain::task` workers
  run node `compute()` concurrently and funnel any GPU submits through
  `Device::deviceMutex()` (archimedes already serializes there). Workers never
  touch ImGui.

### 6. Tests + example graph

Catch2 suite exercising the production path: graph build, type-checked connect /
cycle rejection, push run correctness + ordering, pull subgraph eval (constant +
on-request refire), dual CPU/GPU port round-trip. A small example graph (a couple
CPU nodes + one `archimedes` compute node writing a `Texture`) doubles as the
viewer's smoke scene — `[gpu]` tests `SKIP`-aware so GPU-less CI stays green,
matching `archimedes`.

### Verify M1

Library compiles warning-clean under strict flags; `ctest` passes; `flowview`
launches against the live driver and renders the example graph with a GPU node's
texture visible in the inspector. Do not claim the viewer works without running
it.

## Backlog (deferred — don't build speculatively)

### Tier A — when a real graph demands it

1. **Graph serialization** (json save/load + viewer round-trip). Needed once
   graphs outlive a session; trivial to slot onto `Graph`.
2. **Conditional / gated nodes** — a node suppresses downstream eval. A scheduler
   extension (skip successors); the one piece a pure push DAG can't express.
3. **Incremental re-eval** — dirty-propagation so a single input change reruns
   only the affected subgraph (the pull path is half of this already).

### Tier B — speculative / large

4. **Streaming pipeline** — process a stream of inputs through the graph with
   stage overlap. Taskflow's `tf::Pipeline` is the natural fit; build only if
   streaming becomes real.
5. **Node hot-reload** and a node-type plugin registry.
6. **imgui-node-editor + docking/multi-viewport** upgrade from imnodes.

## Open questions

- Final names (`flow` / `flowview`, namespace).
- Exact CPU port type set: closed `std::variant` of known types vs open
  `std::any`. Closed is faster + viewer-friendly; open is more flexible.
  `PortValue` ships **open** (`std::any` CPU arm behind a closed `PortKind` tag)
  to avoid pinning a type set before real nodes exist — swappable behind the
  interface. Revisit (lean closed) once the node set is known.
- `lain::task` is scoped minimal: an executor + the small DAG-build surface
  `flow`'s scheduler needs (a flow that emplaces node-tasks and adds edges), and
  nothing more. Subflows / `tf::Pipeline` stay unexposed until a caller needs
  them.
- Resolved for now: the `archimedes` submodule tracks `develop` (no tagged
  release exists; its real code isn't on `master`), and Taskflow FetchContent
  pins `v3.7.0`. Revisit if either gains a release worth pinning. `thorax` isn't
  vendored yet — added when something consumes it.
