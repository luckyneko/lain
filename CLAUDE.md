# CLAUDE.md

Guidance for Claude Code working in this repository. [AGENTS.md](AGENTS.md) holds
the general working discipline and applies in full; this file is more specific
and overrides it where they differ.

## Handoff status (2026-06-28)

`lain` is a collection of small C++17 prototyping libraries. The current focus is
a **node-graph engine** (`flow`) and the **reusable app stack** that lets it (and
future apps) run on a live Vulkan driver. The authoritative build plan is
**[WORK.md](WORK.md)** — it owns *what* to build and in *what order*. This file
owns *how* to build it so the result looks native to `lain`.

**Engine core is built, tested, committed. The remaining M1 work is one decoupling
refactor of `flow` plus the app stack + viewer:**

- ✅ Build skeleton (umbrella CMake, `lain::task` wrapping Taskflow), `PortValue`,
  `Port`/`Node`/`Graph` (type-checked + cycle-rejecting), and the `Scheduler`
  (push `Graph::run` via Taskflow, pull `Graph::evaluate`). All in `libs/flow`.
- ✅ GPU-port path proven: `lain::flow-example`'s `GradientNode` emits an
  `acm::Texture` through a port, verified by a headless `[gpu]` test on a real
  driver (archimedes' `acm_require_vulkan_runtime()` builds the loader from source).
- ✅ **`flow` decouple** — `PortValue` is now a thin `std::any` slot; `flow` core
  links only `lain::task` (archimedes moved to `flow-example`). Verified: warning-
  clean build + all 23 flow tests pass, incl. the live-driver texture round-trip.
- ✅ **`libs/math`** (`lain::math`) — typed GLM wrapper (generic `Vec<N,T>`/`Mat`/
  `Quat`, per-dim `Vec2/3/4<T>`, named concretes; GLM free fns re-exposed). GLM
  1.0.3 via `cmake/addGLM.cmake` (SYSTEM). Builds warning-clean; 3 tests pass.
- ⬜ **`libs/app`** (`lain::app`) — GLFW 3.4 + CLI11 reusable app harness:
  multi-window shared-device runner + input + a `View` plugin (windowed hooks +
  headless `run()` for cli-mode). Generalizes archimedes' testbed into a library.
- ⬜ **`libs/gui`** (`lain::gui`) — Dear ImGui + imnodes wrapper over archimedes'
  raw handles + a `lain::app` window; re-exposes `ImGui::` as `lain::gui::`.
- ⬜ **`apps/flowview`** — the inspector: a `lain::app` window + `lain::gui` panels,
  reusing the example graph. Needs live-driver *visual* verification.

Keep this section current as work lands. Once `flow` is fuller, this file is its
standing architecture reference (the role `CLAUDE.md` plays in the sibling repos).

## What `lain` and `flow` are

`lain` is a **cumulative set** — an umbrella of small libraries under `libs/`,
each either owned `lain` code or a thin wrapper giving an external library a
`lain::` face (`libs/task` → `lain::task` over Taskflow; `libs/math` →
`lain::math` over GLM; `libs/app` → GLFW 3.4 + CLI11; `libs/gui` → Dear ImGui).

`flow` is a fast, threadable node-graph engine: typed-port nodes connect into a
DAG, the graph evaluates across worker threads, and every intermediate result
stays inspectable. It is **payload-agnostic** — a port carries any copyable value,
CPU or a GPU handle — so `flow` itself depends on nothing but `lain::task`:

- **Taskflow** (via **`lain::task`**, `libs/task`) — the execution substrate. It
  *is* a task-graph executor with a work-stealing pool, so the push scheduler is
  largely its job; our owned scheduler code just lowers our DAG onto it. We wrap
  it thin so `flow` sees `lain::task`, never `tf::`. This is `flow`'s **only**
  dependency.

**`archimedes` is a dependency of `flow`'s *consumers*, not of `flow`.** A GPU node
(`flow-example`'s `GradientNode`) and the viewer (`lain::gui`/`flowview`) link
`archimedes` and share one `VkDevice` so an `acm::Texture` output previews
zero-copy; `flow` just stores the handle in its generic port slot and never names
`acm::`. (`multi`, `lain`'s own pool, is **not** a dependency for now — Taskflow
replaces it. It stays a sibling lib and can return behind the `lain::task` seam
later.)

## Locked decisions (from planning — do not relitigate without asking)

1. **Cumulative-set layout.** Every library lives under `libs/`. External deps
   enter as thin wrapper libs with matching namespaces (`libs/task`/`lain::task`,
   `libs/math`/`lain::math`). Owned `lain` libs (`archimedes`, `thorax`) are git
   submodules under `extern/`; third-party libs come via `FetchContent`, exactly
   as `archimedes`' `cmake/addXXX.cmake` modules do.
2. **Taskflow is the substrate (drops `multi` for now).** `libs/task` wraps it;
   `flow`'s scheduler lowers the DAG onto a `tf::Taskflow` run on a `lain::task`
   executor.
3. **`flow` is payload-agnostic (decoupled from archimedes).** A `PortValue` is a
   thin `std::any` slot holding any copyable value — a CPU payload **or** a GPU
   handle (`acm::Texture`/`acm::Buffer` are just copyable `shared_ptr` handles), so
   `flow` core links **only** `lain::task`. "Is this a texture, preview it" is the
   viewer's job: it compares `PortValue::type()` against `typeid(acm::Texture)` (it
   already links archimedes). Supersedes the earlier dual-tagged-PortValue plan.
4. **Hybrid execution.** Taskflow drives the **push** run (node fires when inputs
   ready); we own a **pull** path (`evaluate(NodeId)`) for nodes that don't fit a
   full run — `constant` nodes (compute once, then clean) and on-request sources
   (e.g. a `CameraCapture` that refires each pull).
5. **Milestone 1 ships the viewer** — engine, the reusable app stack
   (`lain::app`/`lain::gui`/`lain::math`), and the inspector land together.
6. **App stack: GLFW 3.4 + CLI11; ImGui in a separate `libs/gui`.** GLFW sits
   behind a `lain::app` seam (SDL3 could swap in later); CLI11 lives inside
   `lain::app` (only apps consume it). ImGui + imnodes are wrapped in `lain::gui`,
   kept out of `lain::app` so the harness stays GUI-agnostic. cli-mode runs a graph
   headless and dumps output.
7. **`lain::math` wraps GLM with typed names** (`Vec<N,T>`, `Vec2f`/`Vec2i`/…,
   `Mat4f`). `lain::gui` re-exposes ImGui via `using namespace ImGui` (functions
   read as `lain::gui::`) and bridges ImGui's global `ImVec2`/`ImVec4` to
   `lain::math::Vec2f`/`Vec4f` via `IM_VEC{2,4}_CLASS_EXTRA` in an
   `IMGUI_USER_CONFIG` header.

Names `flow` / `flowview` / namespace `lain::flow` (internals `lain::flow::detail`)
are settled (committed). New libs: `lain::math`, `lain::app`, `lain::gui`.

## Intended architecture (target — build toward this)

Three layers, public → private, mirroring `multi`'s layering discipline:

1. **`Graph`** — owns nodes (`add<T>(...)`), `connect`/`disconnect` with
   type-checking + cycle rejection, holds the edge list + topo order, and the
   `run()` (push) / `evaluate(NodeId)` (pull) entry points.
2. **`Node` / `Port` / `PortValue`** — `Node` is an abstract base with a
   polymorphic `compute()`; declares ports in its ctor. `Port` owns a
   **persistent** `PortValue` (overwritten on recompute, never consumed
   downstream — this is what makes stages inspectable). `PortValue` is a thin
   `std::any` slot carrying a `std::type_index` for connection checks — it holds
   any copyable payload (CPU value or GPU handle), with no GPU types in `flow`. A node's
   `constant` / on-request character is expressed through `dirty()`: a constant
   clears it after first compute; a `CameraCapture` stays dirty so each pull
   refires.
3. **`Scheduler`** (`detail/`) — the seam between `Graph` and `lain::task`.
   Taskflow owns the push scheduling, so this is thin. Push: lower the `Graph` to
   a `tf::Taskflow` (one task per node calling `compute()`, edges become
   `precede`), `run(...).wait()` once on the injected `lain::task` executor;
   rebuild only when topology changes. Pull: `evaluate(NodeId)` is our code
   (Taskflow is push-oriented) — walk upstream, recompute only `dirty()` nodes,
   run the target.

Hard contracts:

- **A node reads only its inputs, writes only its own outputs.** No shared
  mutable state — this is what makes parallel eval safe. State it in `Node`'s
  docs.
- **Fire-and-join only.** A node body never blocks on a nested graph run; the
  scheduler dispatches from the executor's join, not from inside a task. (Blocking
  a worker on work that needs that same worker is the classic pool deadlock.)
- **ImGui is single-threaded.** All ImGui/`flowview` calls on the main render
  thread; `lain::task` workers run `compute()` and funnel GPU submits through
  `acm::Device::deviceMutex()`. Workers never touch ImGui.

`flowview` is a thin `lain::app` client: one window, plus `lain::gui` panels.
`lain::gui` drives ImGui's **official** `imgui_impl_glfw` + `imgui_impl_vulkan`
backends from `archimedes`' raw handles (`vkInstance`/`vkDevice`/`vkQueue`/
`getQueueIdx`/`SwapChain::vkRenderPass`/`maxSampleCount`) and a `lain::app` window,
and records ImGui draw data inside `acm::Renderer::render(record)`. GPU port
previews go through `ImGui_ImplVulkan_AddTexture` → `lain::gui::Image` (from
`Texture::vkImageView` + `acm::Sampler`). Do **not** hand-write an ImGui backend.

## Conventions to inherit (so new code looks native)

Match the sibling repos — study `../archimedes` and `../multi` (both checked out
beside this repo) before writing:

- **Layout.** Each lib under `libs/<name>/`; runnable executables under
  `apps/<name>/` (so `flowview` is `apps/flowview`). Public headers under
  `include/lain/<name>/`; templates in `.inl` beside the header; non-template
  bodies in `src/`. Internals in `lain::<name>::detail` (mirrors `multi::details`).
- **Wrapping externals.** An external dep gets a thin `libs/<name>` lib + matching
  `lain::<name>` namespace that fronts it; don't leak the upstream namespace (no
  `tf::`, no `glm::`) past the wrapper. Pull it with a `cmake/addXXX.cmake`
  FetchContent module patterned on `archimedes`.
- **Style.** `.clang-format` is already present (Allman braces, tabs width 4, no
  column limit, left pointer alignment). Format on save; run clang-format on
  touched files.
- **Strict build.** `/WX /W4` (MSVC), `-Werror -Wall -Wextra` (else).
  Out-of-source enforced. Standalone-vs-subdirectory option gating like the
  siblings (`FLOW_BUILD_TESTING` etc., default ON top-level, OFF as subdir).
- **API shape.** Where a type owns a resource and is passed around, prefer
  `archimedes`' pImpl/`shared_ptr` handle + parent-factory pattern. Where it's a
  value algorithm surface, prefer `multi`'s thin free-functions-over-a-`Context`
  pattern. `Graph`/`Node` are owners (handle-ish); the scheduler is internal.
- **Tests.** Catch2 via `ctest`; `[gpu]` tests `SKIP`-aware (no driver → green),
  exactly as `archimedes` does. Tests exercise the production path, never a copy.

## Where to start

The engine core (skeleton, `PortValue`, `Port`/`Node`/`Graph`, `Scheduler`,
example node + tests) is already built. Follow WORK.md's "Remaining work" in order:
(1) decouple `flow` from archimedes (`PortValue` → `std::any`), (2) `libs/math`
(typed GLM), (3) `libs/app` (GLFW + CLI11 harness), (4) `libs/gui` (ImGui + imnodes
wrapper), (5) `apps/flowview`. Don't build the backlog (Tier A/B) speculatively.

## Build & verify

Out-of-source, standard for `lain`:

```sh
git submodule update --init --recursive   # pull extern/ (archimedes; tracks develop)
cmake -B build                            # FetchContent pulls Taskflow, GLM, GLFW, CLI11, ImGui, imnodes, ...
cmake --build build
ctest --test-dir build --output-on-failure
./build/apps/flowview/flowview            # gui-mode: the inspector on the live driver
./build/apps/flowview/flowview --help     # cli-mode: run a graph headless, dump output
```

**M1 is done** when: every lib compiles warning-clean under strict flags; `ctest`
passes; a `flowview` cli-mode run evaluates the example graph headless and dumps
its output; and `flowview` gui-mode launches against the live driver and renders
the example graph with a GPU node's texture visible in the inspector. Per AGENTS.md
rule 9 — do not claim the viewer works without having run it.
