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
  This is `flow`'s **only** dependency.

`flow` itself is **payload-agnostic and GPU-free** — a port carries any copyable
value — so the GPU/UI pieces live in `flow`'s *consumers*, not in `flow`:

- **`archimedes`** (`acm::`) — Vulkan renderer. A GPU node and the viewer share
  one `VkDevice` so an `acm::Texture` output previews zero-copy; the handle just
  rides in the generic port slot (`flow` never names `acm::`).
- **`lain::app`** (`libs/app`, GLFW 3.4 + CLI11) — reusable app harness: a
  multi-window shared-device runner + input + a `View` plugin with both windowed
  hooks and a headless `run()` (cli-mode). Generalizes archimedes' testbed.
- **`lain::gui`** (`libs/gui`, Dear ImGui + imnodes) — the inspector toolkit over
  archimedes' raw handles + a `lain::app` window.
- **`lain::math`** (`libs/math`, GLM) — typed vector/matrix names, and the
  `ImVec2`/`ImVec4` ↔ `Vec2f`/`Vec4f` bridge `lain::gui` uses.

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
3. **`flow` is payload-agnostic (decoupled from archimedes).** A `PortValue` is a
   thin `std::any` slot carrying any copyable value — a CPU payload or a GPU handle
   (`acm::Texture`/`acm::Buffer` are just copyable `shared_ptr` handles), so `flow`
   core links only `lain::task`. "Is this a texture, preview it" is the viewer's
   job: it compares `PortValue::type()` against `typeid(acm::Texture)` (it already
   links archimedes). Supersedes the earlier dual-tagged-PortValue plan.
4. **Hybrid execution.** Taskflow's executor drives a **push** run (every node
   fires once its inputs are ready). On top of that we own a **pull** path,
   `evaluate(NodeId)`, that recomputes just one node's upstream subgraph on
   demand. Pull is what serves nodes that don't fit a single full run:
   - **`constant` nodes** — compute once, then stay clean; pulled, not pushed.
   - **on-request sources** — fire afresh each time they're asked, e.g. a
     `CameraCapture` node that grabs a frame per pull.
5. **Milestone 1 ships the viewer.** Core engine, the reusable app stack
   (`lain::app`/`lain::gui`/`lain::math`), and the inspector land together so
   graphs are visible from day one.
6. **App stack: GLFW 3.4 + CLI11; ImGui in a separate `libs/gui`.** GLFW sits
   behind a `lain::app` seam (SDL3 could swap in later); CLI11 lives inside
   `lain::app` (only apps consume it). ImGui + imnodes are wrapped in `lain::gui`,
   kept out of `lain::app` so the harness stays GUI-agnostic. cli-mode runs a graph
   headless and dumps output.
7. **`lain::math` wraps GLM with typed names** (`Vec<N,T>`, `Vec2f`/`Vec2i`/…,
   `Mat4f`). `lain::gui` re-exposes ImGui via `using namespace ImGui` and bridges
   ImGui's global `ImVec2`/`ImVec4` to `lain::math::Vec2f`/`Vec4f` through
   `IM_VEC{2,4}_CLASS_EXTRA` in an `IMGUI_USER_CONFIG` header.

Names are settled (committed): engine `lain::flow` (`libs/flow`), viewer `flowview`
(`apps/flowview`), internals `lain::flow::detail`. New libs: `lain::math`
(`libs/math`), `lain::app` (`libs/app`), `lain::gui` (`libs/gui`).

## Repo layout (proposed)

```
lain/
  CMakeLists.txt              # umbrella; add_subdirectory each libs/* + apps/* + extern/
  cmake/
    lainWarnings.cmake        # shared strict-flag INTERFACE target
    addTaskflow.cmake         # FetchContent, mirrors archimedes' addXXX.cmake
    addGLM.cmake  addGLFW.cmake  addCLI11.cmake  addImGui.cmake  addImnodes.cmake
  extern/
    archimedes/               # submodule (owned), tracks develop
    thorax/                   # owned — deferred until a consumer needs it
  libs/                       # libraries (lain::<name>)
    task/                     # lain::task — thin wrapper over Taskflow
    flow/                     # lain::flow — the node-graph engine (GPU-free core)
      include/lain/flow/
      src/
      test/
    math/                     # lain::math — typed GLM wrapper
    app/                      # lain::app — GLFW 3.4 + CLI11 app harness
    gui/                      # lain::gui — Dear ImGui + imnodes wrapper
  apps/                       # executables
    flowview/                 # the inspector (lain::app + lain::gui client)
```

Libraries live under `libs/` (a `lain::<name>` each); runnable executables under
`apps/`. Consumed-as-subdirectory rule (`archimedes` already honors it): a lib's
tests build, and `apps/` build, only when `lain` is the top-level CMake project.

## Milestone 1 — engine + app stack + viewer

**Status:** the engine core (steps 1–4 below, plus the engine test suite + example
GPU node) is built, tested, and committed. The remaining M1 work — decouple `flow`
from archimedes, then `lain::math` / `lain::app` / `lain::gui` / `flowview` — is in
"Remaining work" after the engine steps. Steps 1–4 record what was built.

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

### 2. Port value — the type-erased slot  (revised; see "decouple" below)

`PortValue` (`include/lain/flow/portvalue.h`) — a thin `std::any` holder for any
copyable payload (a CPU value **or** a GPU handle, which is just a copyable
`shared_ptr`). Carries a `std::type_index` for connection type-checking.
**Persistent**: a port owns its slot and it is overwritten on recompute, never
moved/consumed downstream — this is what makes every stage inspectable after a run.
(It originally shipped with dedicated `acm::Texture`/`acm::Buffer` variant arms +
a `PortKind` tag; the "decouple" step removes them so `flow` has no GPU dependency.)

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

### Engine tests + example graph  (built)

Catch2 suite exercising the production path: graph build, type-checked connect /
cycle rejection, push run correctness + ordering, pull subgraph eval (constant +
on-request refire), and a CPU + GPU-handle port round-trip. A small example graph
(a `flow-example` GPU node writing a `Texture`) doubles as the viewer's smoke
scene — `[gpu]` tests `SKIP`-aware so GPU-less CI stays green, matching
`archimedes`. New libs add their own per-step tests below.

## Remaining work — decouple + app stack + viewer

Build order. Each lib builds standalone + as a subdirectory, strict-clean.

### 5. Decouple `flow` from archimedes

Drop the `acm::Texture`/`acm::Buffer` arms from `PortValue`: `m_value` becomes a
plain `std::any` (empty when value-less). `set<T>` / `holds<T>` / `get<T>` /
`type()` / `clear()` collapse onto it; `PortKind` / `texture()` / `buffer()` are
removed. Update `flow-example`'s `GradientNode` to `set<acm::Texture>` and the
`[gpu]` test to `get<acm::Texture>`. After this `libs/flow` links only
`lain::task`; archimedes moves to `flow-example`, `lain::gui`, and `flowview`.

### 6. `libs/math` — typed GLM wrapper

`cmake/addGLM.cmake` (FetchContent, header-only, re-exposed SYSTEM). A typed header
(`include/lain/math/types.h`): generic `Vec<N,T>` / `Mat<C,R,T>`, per-dimension
`Vec2<T>` / `Vec3<T>` / `Vec4<T>`, and named concretes across scalar types
(`Vec2f` / `Vec2d` / `Vec2i` / `Vec2u` / sized int/uint, ×2/3/4; `Mat3f` / `Mat4f`
/ `Quatf`); `using namespace glm` re-exposes the free functions. Small
alias-sanity test. Lands first — the ImGui↔GLM bridge depends on it.

### 7. `libs/app` — GLFW + CLI11 app harness

`cmake/addGLFW.cmake` (copy archimedes') + `cmake/addCLI11.cmake`. Generalize
archimedes' testbed (`App` / `RenderContext` / `RenderWorker` / `selectSettings`)
into a library: a `Window` (GLFW + `acm::Surface`), an `Input` layer (held /
pressed / released keys, cursor pos+delta, buttons, scroll), the multi-window
shared-device runner (one `acm::Device` presenting to every window, fork-join
render thread per window, device-before-surface teardown), and a `View` plugin —
windowed hooks (`config` / `onInit` / `onUpdate` / `onRenderView` / `onShutdown`,
lifted from `Example`) plus a headless `run()` for cli-mode. CLI11 dispatches gui
vs cli mode. Tests: a headless run builds a small `Graph`, runs it, checks the
dumped output; a frame-capped window smoke (`TESTBED_FRAME_CAP`-style). Verify both
modes on the live driver.

### 8. `libs/gui` — ImGui + imnodes wrapper

`cmake/addImGui.cmake` (ImGui core + `imgui_impl_glfw` + `imgui_impl_vulkan` as a
small static lib; `IMGUI_USER_CONFIG` = an `imconfig_lain.h` that pulls `lain::math`
and defines `IM_VEC{2,4}_CLASS_EXTRA`; the `imgui` target links `lain::math`) +
`cmake/addImnodes.cmake`. `lain::gui` owns the integration seam: backend init from
archimedes' raw handles + a `lain::app` window, the ImGui descriptor pool,
`ImGui_ImplVulkan_SetMinImageCount` on swapchain-recreate, per-frame begin/end +
`RenderDrawData` inside `acm::Renderer::render(record)`, and an
`acm::Texture`→`ImGui::Image` preview helper. `using namespace ImGui` re-exposes
the widget API as `lain::gui::`; do **not** hand-write a backend. imnodes lives
here. Real verification comes with flowview (step 9).

### 9. Viewer (`apps/flowview`)

A thin `lain::app` client: one window + `lain::gui` panels. The **inspector** reads
each port's persistent `PortValue` — CPU values as text/tables; a port whose
`type()` is `typeid(acm::Texture)` previews via the `lain::gui` texture helper
(`Texture::vkImageView` + an `acm::Sampler`). A **node canvas** (imnodes, start
simple; defer imgui-node-editor and docking/multi-viewport) shows the graph from
`Graph` nodes/edges, read-only first. Reuse `flow-example`'s gradient graph as the
smoke scene. Threading: all ImGui on the main/render thread; `lain::task` workers
run `compute()` and funnel GPU submits through `Device::deviceMutex()`; workers
never touch ImGui.

### Verify M1

Every lib compiles warning-clean under strict flags; `ctest` passes; a `flowview`
**cli-mode** run evaluates the example graph headless and dumps its output; and
`flowview` **gui-mode** launches against the live driver and renders the example
graph with a GPU node's texture visible in the inspector. Do not claim the viewer
works without running it.

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
- Port type set: **resolved** — `PortValue` is a plain open `std::any` (no
  `PortKind` tag, no GPU arms), keeping `flow` payload-agnostic and dependency-free.
  Could lean to a closed `std::variant` later for speed once the node set is known,
  but only if profiling a real graph shows the type-erasure cost matters.
- `lain::task` is scoped minimal: an executor + the small DAG-build surface
  `flow`'s scheduler needs (a flow that emplaces node-tasks and adds edges), and
  nothing more. Subflows / `tf::Pipeline` stay unexposed until a caller needs
  them.
- Resolved for now: the `archimedes` submodule tracks `develop` (no tagged
  release exists; its real code isn't on `master`), and Taskflow FetchContent
  pins `v3.7.0`. Revisit if either gains a release worth pinning. `thorax` isn't
  vendored yet — added when something consumes it.
