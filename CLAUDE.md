# CLAUDE.md

Guidance for Claude Code working in this repository. [AGENTS.md](AGENTS.md) holds
the general working discipline and applies in full; this file is more specific
and overrides it where they differ.

## Handoff status (2026-06-27)

`lain` is a collection of small C++17 prototyping libraries. The current focus is
a **node-graph engine** (`flow`) plus its ImGui inspector (`flowview`). The
authoritative build plan is **[WORK.md](WORK.md)** — it owns *what* to build and
in *what order*. This file owns *how* to build it so the result looks native to
`lain`.

**M1 progress — engine core + GPU path are built, tested, and committed; the
viewer is the one piece left:**

- ✅ Build skeleton (umbrella CMake, `lain::task` wrapping Taskflow), `PortValue`,
  `Port`/`Node`/`Graph` (type-checked + cycle-rejecting), and the `Scheduler`
  (push `Graph::run` via Taskflow, pull `Graph::evaluate`). All in `libs/flow`.
- ✅ GPU-port path proven: `lain::flow-example`'s `GradientNode` emits an
  `acm::Texture` through a port, verified by a headless `[gpu]` test on a real
  driver (`cmake/addVulkanRuntime.cmake` builds the loader from source).
- ⬜ **`flowview`** (WORK.md step 5) — the ImGui/archimedes inspector. Not started.
  Needs GLFW + Dear ImGui + imnodes (FetchContent), the `imgui_impl_vulkan`
  backend over archimedes' raw handles, and live-driver *visual* verification.

Keep this section current as work lands. Once `flow` is fuller, this file is its
standing architecture reference (the role `CLAUDE.md` plays in the sibling repos).

## What `lain` and `flow` are

`lain` is a **cumulative set** — an umbrella of small libraries under `libs/`,
each either owned `lain` code or a thin wrapper giving an external library a
`lain::` face (`libs/task` → `lain::task` over Taskflow; `libs/math` →
`lain::math` over GLM, when needed).

`flow` is a fast, threadable node-graph engine: typed-port nodes connect into a
DAG, the graph evaluates across worker threads, and every intermediate result
stays inspectable in a live ImGui viewer — including GPU image outputs shown as
thumbnails with no copy. It **composes** the set; it does not re-implement it:

- **Taskflow** (via **`lain::task`**, `libs/task`) — the execution substrate. It
  *is* a task-graph executor with a work-stealing pool, so the push scheduler is
  largely its job; our owned scheduler code just lowers our DAG onto it. We wrap
  it thin so `flow` sees `lain::task`, never `tf::`.
- **`archimedes`** (Vulkan renderer, `acm::` handles) — shares one `VkDevice`
  with the graph so a node's `acm::Texture`/`acm::Buffer` output renders in the
  viewer zero-copy.

(`multi`, `lain`'s own pool, is **not** a dependency for now — Taskflow replaces
it. It stays a sibling lib and can return behind the `lain::task` seam later.)

## Locked decisions (from planning — do not relitigate without asking)

1. **Cumulative-set layout.** Every library lives under `libs/`. External deps
   enter as thin wrapper libs with matching namespaces (`libs/task`/`lain::task`,
   `libs/math`/`lain::math`). Owned `lain` libs (`archimedes`, `thorax`) are git
   submodules under `extern/`; third-party libs come via `FetchContent`, exactly
   as `archimedes`' `cmake/addXXX.cmake` modules do.
2. **Taskflow is the substrate (drops `multi` for now).** `libs/task` wraps it;
   `flow`'s scheduler lowers the DAG onto a `tf::Taskflow` run on a `lain::task`
   executor.
3. **Dual CPU + GPU ports.** A port value holds a plain CPU value **or** a GPU
   resource (`acm::Texture` / `acm::Buffer`). Graph and renderer share one device.
4. **Hybrid execution.** Taskflow drives the **push** run (node fires when inputs
   ready); we own a **pull** path (`evaluate(NodeId)`) for nodes that don't fit a
   full run — `constant` nodes (compute once, then clean) and on-request sources
   (e.g. a `CameraCapture` that refires each pull).
5. **Milestone 1 ships the viewer** — engine and inspector land together.

Names `flow` / `flowview` / namespace `lain::flow` (internals `lain::flow::detail`)
are placeholders; confirm before the first commit. See WORK.md "Open questions".

## Intended architecture (target — build toward this)

Three layers, public → private, mirroring `multi`'s layering discipline:

1. **`Graph`** — owns nodes (`add<T>(...)`), `connect`/`disconnect` with
   type-checking + cycle rejection, holds the edge list + topo order, and the
   `run()` (push) / `evaluate(NodeId)` (pull) entry points.
2. **`Node` / `Port` / `PortValue`** — `Node` is an abstract base with a
   polymorphic `compute()`; declares ports in its ctor. `Port` owns a
   **persistent** `PortValue` (overwritten on recompute, never consumed
   downstream — this is what makes stages inspectable). `PortValue` is a tagged
   type-erased slot over a closed kind set (CPU payload | `acm::Texture` |
   `acm::Buffer`) carrying a `std::type_index` for connection checks. A node's
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

`flowview` uses ImGui's **official** `imgui_impl_glfw` + `imgui_impl_vulkan`
backends driven by `archimedes`' raw handles (`vkInstance`/`vkDevice`/`vkQueue`/
`getQueueIdx`/`SwapChain::vkRenderPass`/`maxSampleCount`); record ImGui draw data
inside `acm::Renderer::render(record)`. GPU port previews go through
`ImGui_ImplVulkan_AddTexture` → `ImGui::Image` (from `Texture::vkImageView` +
`acm::Sampler`). Do **not** hand-write an ImGui backend.

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

Follow WORK.md "Milestone 1" in order: (1) build skeleton + submodules, (2)
`PortValue`, (3) `Port`/`Node`/`Graph`, (4) `Scheduler` push+pull, (5) `flowview`,
(6) tests + example graph. Don't build the backlog (Tier A/B) speculatively.

## Build & verify (intended — CMake doesn't exist yet)

Out-of-source, standard for `lain`:

```sh
git submodule update --init --recursive   # pull extern/ (archimedes; tracks develop)
cmake -B build                            # FetchContent pulls Taskflow, ImGui, imnodes, ...
cmake --build build
ctest --test-dir build --output-on-failure
./build/flowview                          # launches the inspector on the live driver
```

**M1 is done** when: the library compiles warning-clean under strict flags;
`ctest` passes; and `flowview` launches against the live driver and renders the
example graph with a GPU node's texture visible in the inspector. Per AGENTS.md
rule 9 — do not claim the viewer works without having run it.
