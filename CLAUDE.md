# CLAUDE.md

Guidance for Claude Code working in this repository. [AGENTS.md](AGENTS.md) holds
the general working discipline and applies in full; this file is more specific
and overrides it where they differ.

## Current status (2026-11-13)

- **Milestones.** M1 to M8 and M10 to M15 are complete. **M9** (camera calibration and fixed
  registration) is built to its slice plans and to its 2026-10-20 review, and awaits acceptance through
  its PR #1 to `develop`. What is left of it is owed by the repo owner (the real-camera capture, the
  targetless calibration judgements), gui-mode checks on a Metal session, and work waiting on real
  footage.
- **Branches.** `feature/M9` carries M9. `feature/M15` (rig design) was cut from it and had M9 merged
  in on 2026-11-10, so it holds both.
- **Last verified** on `feature/M15`, 2026-11-13: `ctest -j8` **1719/1719** MSVC Debug and
  **1727/1727** MSVC Release, with video, OpenCV and Ceres on. CI was last green on all eight jobs at
  `ea273a1` ([run 43136024980](https://github.com/luckyneko/lain/actions/runs/43136024980)); GCC and
  macOS are built only there. gui-mode is verified by the repo owner's eyeball on a Metal machine,
  never by CI.
- **What is owed** is WORK.md's *Outstanding work — one index*: every unfinished milestone item,
  deferred feature, known defect and standing refusal, each pointing at the section that owns it.
- **Where history lives:** WORK.md's milestone and dated sections hold every change's landing notes,
  and at the end, the handoff log this file carried until 2026-11-13. CONTEXT.md holds the vocabulary,
  `docs/adr/` the decisions, and git log the rest.
- **Recording work:** a change's notes go in the WORK.md section that owns it, and in CONTEXT.md or an
  ADR when it changes a term or a decision. This file changes when one of the sections below does, and
  its status lines when they stop being true.

## Libraries at a glance

One row per library, plugin family and app. Each library's CMakeLists says what it links and why.

| Path | Targets | What it is |
|---|---|---|
| `libs/core` | `lain::core` | std-only foundations: `Time`, `Length`, `Version`, `Uuid`, `Uri`, `Sha256`, `Factory`, `parse`, seeded random draws |
| `libs/memory` | `lain::memory` | `Buffer` and aligned allocation |
| `libs/log` | `lain::log` | logging over spdlog; renders any type with a `toString()` |
| `libs/string` | `lain::string` | `format` over fmt, and the `<key>` name pattern |
| `libs/meta` | `lain::meta` | enum reflection (magic_enum), `typeName`, type traits |
| `libs/math` | `lain::math` | typed GLM, `RigidTransform`, `AxisConvention`, angles, `isFinite` |
| `libs/image` | `lain::image` | CPU rasters, pixel formats, colour spaces and conversion, the procedural texture |
| `libs/mesh` | `lain::mesh` | CPU triangle meshes and mesh clips |
| `libs/media` | `lain::media`, `::serialize` | frame sequences: finite, lazy footage |
| `libs/io` | `lain::io`, `::image`, `::data`, `::sequence`, `::video`, `::mesh` | the transport, and a codec seam per medium |
| `libs/data` | `lain::data` | the format-neutral `Value` DOM and reflection |
| `libs/task` | `lain::task` | a namespace alias for `multi`, the process task pool |
| `libs/flow` | `lain::flow`, `::serialize`, `lain::flow-example` | the node-graph engine, its documents, example nodes |
| `libs/editor` | `lain::editor` | undo, recent documents, session files, the unsaved-changes guard, the playback clock |
| `libs/camera` | `lain::camera`, `::flow`, `::serialize`, `::testing` | camera models, boards, features, calibration, registration, comparison |
| `libs/rigplan` | `lain::rigplan`, `::serialize`, `::scene`, `::capture`, `::testing` | rig design: specifications, rig plans, realisations, synthetic captures |
| `libs/render` | `lain::render`, `::testing` | the raster renderer over archimedes: offscreen, viewport, capture pass, distance |
| `libs/app` | `lain::app` | the GLFW + CLI11 harness; owns the Vulkan instance, device and task pool |
| `libs/gui` | `lain::gui` | Dear ImGui, imnodes and ImGuizmo over archimedes |
| `libs/testing` | `lain::testing` | test support: scratch paths, a thread pool, `skipInDebug`, rooted paths |
| `plugins/io/*` | `lain::io::<medium>::<format>`, `::codecs` | png, jpeg, tiff, json, obj and FFmpeg codecs |
| `plugins/camera` | `lain::camera::opencv`, `::ceres`, `::backends` | OpenCV detection, features and estimation; Ceres refinement |
| `apps/flowview` | `flowview` | the graph editor, and the headless `run` / `list` |
| `apps/rigplan` | `rigplan` | the rig plan editor, and `rigplan render` |
| `extern/` | `archimedes`, `multi` | owned submodules: the Vulkan renderer, the work-stealing pool |

## What `lain` and `flow` are

`lain` is a **cumulative set** — an umbrella of small libraries under `libs/`,
each either owned `lain` code or a thin wrapper giving an external library a
`lain::` face (`libs/task` → `lain::task` over `multi`; `libs/math` →
`lain::math` over GLM; `libs/core` → `lain::core` std-only types; `libs/log` →
`lain::log` over spdlog; `libs/string` → `lain::string` over fmt; `libs/meta` →
`lain::meta` over magic_enum; `libs/app` → GLFW 3.4 + CLI11; `libs/gui` → Dear ImGui).

`flow` is a fast, threadable node-graph engine: typed-port nodes connect into a
DAG, the graph evaluates across worker threads, and every intermediate result
stays inspectable. It is **payload-agnostic** — a port carries any copyable value,
CPU or a GPU handle — so `flow` pulls in no GPU/UI deps; it links only two
featherweight libs:

- **`multi`** (via **`lain::task`**, `libs/task`) — the execution substrate, and
  lain's own work-stealing pool, a submodule under `extern/`. It *is* a task-graph
  executor (`Recipe` / `Step` / `Context`), so the push scheduler is largely its job;
  our owned scheduler code just lowers our DAG onto it. `lain::task` is a one-line namespace
  ALIAS for it — lain does not wrap what it owns — so `flow` spells it `lain::task` and has
  multi's whole surface available. *(Taskflow held this place from M1 until 2026-09-11, and the
  wrapper until 2026-09-26 — see the updates of those dates.)*
- **`lain::meta`** (`libs/meta`) — a `Port` captures `lain::meta::typeName<T>()` at
  declaration so the inspector can label pins by type. Compile-time, header-only — no
  GPU/UI coupling, in keeping with `flow`'s minimalism.

**`archimedes` is a dependency of `flow`'s *consumers*, not of `flow`.** A GPU node
(`flow-example`'s `GradientNode`) and the viewer (`lain::gui`/`flowview`) link
`archimedes` and share one `VkDevice` so an `acm::Texture` output previews
zero-copy; `flow` just stores the handle in its generic port slot and never names
`acm::`. (This used to read *"`multi`, lain's own pool, is not a dependency for now —
Taskflow replaces it. It stays a sibling lib and can return behind the `lain::task` seam
later."* **It returned on 2026-09-11**, and the seam held: `libs/flow` changed by zero
lines. The seam itself was then deleted on 2026-09-26 — having absorbed the one swap it
existed for, over the library lain owns.)

## Locked decisions (from planning — do not relitigate without asking)

1. **Cumulative-set layout.** Every library lives under `libs/`. **Third-party** deps
   enter as thin wrapper libs with matching namespaces (`libs/math`/`lain::math`,
   `libs/log`/`lain::log`). Owned `lain` libs (`archimedes`, `multi`, `thorax`) are git
   submodules under `extern/`; third-party libs come via `FetchContent`, exactly
   as `archimedes`' `cmake/addXXX.cmake` modules do.
   **OWNED libraries are not wrapped** — there is no upstream vocabulary to keep out, and a
   wrapper over one is a second spelling of a thing that already has a name. `acm::` appears
   raw in the public headers of `lain::app` and `lain::gui`; `libs/task`/`lain::task` is a
   one-line namespace alias for `multi`, not a wrapper. (It *was* a wrapper until 2026-09-26 —
   see [ADR-0024](docs/adr/0024-one-process-task-pool.md).)
2. **`multi` is the substrate, and `lain::task` is a namespace ALIAS for it.** `flow`'s
   scheduler lowers the DAG onto a `multi::Recipe` run on the PROCESS pool — one pool for
   the whole program, owned by `lain::app` and sized by `--threads`, reached through free
   functions with no executor in any signature. Taskflow held the substrate from M1 until
   2026-09-11; the decision as written said *"`multi` … can return behind the `lain::task`
   seam later"*, so the swap was taken rather than relitigated. **Amended 2026-09-26**
   ([ADR-0024](docs/adr/0024-one-process-task-pool.md)): the seam then went too, because a
   wrapper over an owned library is a second vocabulary for one thing, and an injected
   per-caller executor means the second subsystem to want threads oversubscribes the machine.
3. **`flow` is payload-agnostic (decoupled from archimedes).** A `PortValue` is a
   thin `std::any` slot holding any copyable value — a CPU payload **or** a GPU
   handle (`acm::Texture`/`acm::Buffer` are just copyable `shared_ptr` handles), so
   `flow` names no GPU types and links only `lain::task` + `lain::meta` (both
   featherweight; the latter for `Port::typeName()`). "Is this a texture, preview it"
   is the viewer's job: it compares `PortValue::type()` against `typeid(acm::Texture)`
   (it already links archimedes). Supersedes the earlier dual-tagged-PortValue plan.
4. **Hybrid execution.** The pool drives the **push** run (node fires when inputs
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

1. **`Graph`** — a pure data model: owns nodes (`add<T>(...)`, keyed by opaque
   `NodeId`), `connect`/`disconnect` with type-checking + cycle rejection, holds the
   edge list, insertion order and topo order. It does **not** execute — a `Scheduler`
   (layer 3) consumes it. It holds no lazy caches: a `const Graph&` is concurrently
   readable, so the mutators maintain the topo order rather than rebuilding it on
   first query.
2. **`Node` / `Port` / `Evaluation` / `PortValue`** — `Node` is an abstract base
   with a polymorphic `compute(NodeEvaluation&) const`; it and `Port` declare the
   recipe, while `Evaluation` owns one graph's runtime state — values and
   bookkeeping — across scheduler invocations. `Port` is pure declaration
   (`{id, name, direction, PortType, presence}`); readiness and value description
   follow the values into the Evaluation (`evaluation.ready(node)`, `hasValue`,
   `PortType::describe`).
   `PortValue` is the shared, immutable type-erased payload slot, so copying a
   value across an edge is a refcount bump. A node reads and writes only through
   its per-node evaluation view; an on-request source rearms that evaluation, not
   the shared definition. `Evaluation::prepare(const Graph&)` creates/prunes and
   stabilises storage before dispatch; compute never grows shared containers.
   Fixed-port nodes retain named `PortId`s returned by their declaration methods
   and use `evaluation.input(id)` / `output(id)`; a position is iteration-only.
   Force refresh is likewise evaluation-local, under one verb:
   `requestRecompute(node)` / `requestRecomputeAll()`; `Graph::markAllDirty()` is
   removed, and a fresh Evaluation is the full reset.
   A host owns a definition and its Evaluation as **one replaceable unit**, so a
   load/undo Graph replacement replaces both; `Evaluation{graph}` records its
   definition's **lineage** and `prepare` refuses a definition of another one — so an
   Evaluation spans in-place edits and `Graph::clone()`s alike, and never a rebuilt
   graph, wherever in memory it was rebuilt (ADR-0025, M14 slice 1).
   Boundary handles are immutable recipe metadata; hosts and group-entry steps use
   `evaluation.bind(input, value)` / `evaluation.value(output)`. Boundary Nodes
   retain no bound or delivered runtime cache.
   Parameter mutation is `Node::setParam(PortId, value)`: type-check, commit and
   definition-version bump as one operation. Hosts/serializers receive no mutable
   `Param&`; `setName` remains display-only and computation-neutral.
3. **`Scheduler`** (public, `scheduler.h`) — consumes a const definition and a
   host-owned evaluation; this is where execution lives, not on `Graph`. An
   abstract base exposes the varying full push `run(const Graph&, Evaluation&)`
   plus the shared, serial pull `evaluate(const Graph&, Evaluation&, NodeId)`.
   Two backends: `SerialScheduler` (one topo-order pass, no execution deps) and
   `ParallelScheduler` (lowers to a `multi::Recipe` — one task per node calling
   `compute()`, edges become `Recipe::order` — and runs it to completion on the
   **process task pool**, waiting by PARTICIPATING in that pool's stealing rather than
   sleeping on it, so one worker count is right for every primitive in the program).
   `lain::app` owns the pool and exposes `--threads`; `--threads 0` leaves it inactive and
   runs the same plan inline, in dependency order. `multi` owns the push scheduling, so
   `ParallelScheduler` stays thin.

Hard contracts:

- **A node reads only its evaluated inputs, writes only its own evaluated outputs.**
  Execution receives a `const Graph&`, and `compute(NodeEvaluation&) const` puts
  every runtime write in the host-owned Evaluation. No shared mutable definition
  state — this is what makes parallel evaluation safe. `const` here means
  **concurrently readable**, not merely unmodified: no `mutable` caches on a
  definition, or two evaluations racing after an edit corrupt one.
- **A coordinator grows Evaluation storage; a worker task never does.** For
  graph-shaped children that means one `prepare` pass before dispatch. A task
  receives a stable `NodeEvaluation&` and never inserts/resizes. (A data-dependent
  child count — a future map — cannot be prepared that early; where its second
  coordinator point sits is settled with how the plan lowers a map, ADR-0012.)
- **Reusing one Evaluation rejects immediately.** Scheduler entry takes a
  non-blocking RAII Evaluation lease and throws `std::logic_error` if already
  held; it never waits, and unwinding releases it. Distinct Evaluations remain
  concurrent even over the same definition — and that is tested, not assumed.
- **A plan step names definition and evaluation.** `{const Graph*, Evaluation*,
  NodeId}` is the runtime address; the same shared definition may appear with
  several Evaluation pointers. Recursive group staleness follows the matching
  child Evaluation, and entry republishes only into the selected child's boundary.
- **Fire-and-join only.** A node body never blocks on a nested graph run; the
  scheduler dispatches from the join, not from inside a task. (Blocking a worker on
  work that needs that same worker is the classic pool deadlock.) With ONE pool per
  process this is load-bearing program-wide rather than merely wasteful: a violation
  deadlocks rather than slowing down. Nested *dispatch* — `task::each` inside a
  `compute()` — is a different thing and is supported; what is forbidden is entering
  a SCHEDULER from a task, since that takes the evaluation's run lease and blocks.
- **ImGui is single-threaded.** All ImGui/`flowview` calls on the main render
  thread; `lain::task` workers run `compute()` and funnel GPU submits through
  `acm::Device::deviceMutex()`. Workers never touch ImGui.

`flowview` is a thin `lain::app` client: one window, plus `lain::gui` panels.
`lain::gui` drives ImGui's **official** `imgui_impl_glfw` + `imgui_impl_vulkan`
backends from `archimedes`' raw handles (`vkInstance`/`vkDevice`/`vkQueue`/
`getQueueIdx`/`SwapChain::vkRenderPass`/`maxSampleCount`) and a `lain::app` window,
and records ImGui draw data inside `acm::Renderer::render(record)`. GPU port
previews go through `Context::createTexture` → a `gui::Texture` (an `acm::Texture` plus an
`acm::DescriptorSet` built against ImGui's texture-set layout, so both are archimedes' to
destroy) → `lain::gui::Image`. Do **not** hand-write an ImGui backend.

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
- **Optional plugins: wanted, available, built.** Three separate questions, each with one owner.
  1. **The option says what is WANTED** (`LAIN_IO_IMAGE_PNG`, `LAIN_IO_VIDEO_FFMPEG`,
     `LAIN_CAMERA_OPENCV`, ...). Source-built plugins default ON; a fetched prebuilt defaults
     `${LAIN_NOT_SUBPROJECT}`, so a bundled lain never downloads a binary its consumer did not ask for.
  2. **`cmake/add<Dep>.cmake` says what is AVAILABLE**, in one file per dependency. Where availability
     varies by platform (the prebuilts) it sets `LAIN_<DEP>_FOUND`; FALSE comes with
     `LAIN_<DEP>_UNAVAILABLE`, fetches nothing and returns. It fails configure only for a BROKEN source
     (download, hash, manifest): unavailable is a fact about the platform, broken is a fault, and
     falling back to OFF on a fault would hide it. The plugin's CMakeLists checks `_FOUND` straight
     after the include and returns before it defines a target or joins its aggregator.
  3. **Whatever needs to know asks whether it was BUILT**, as `if(TARGET lain::camera::opencv)`, never
     the option, which is only a wish. Each aggregator directory reports through `add_feature_info`,
     and a top-level configure ends with the summary.

  And **a node kind is vocabulary, a backend is a capability**: a host registers every node kind
  whatever the plugins, so a document loads and saves whole in any build, and a node whose backend is
  missing says so when it runs. What a menu OFFERS may follow the backends.
- **Style.** `.clang-format` is already present (Allman braces, tabs width 4, no
  column limit, left pointer alignment). Format on save; run clang-format on
  touched files. **Do not go hunting for a `clang-format` on PATH** — the repo
  vendors a *pinned* one (`cmake/addclangformat.cmake`, FetchContent'd like any
  other dep, cached under `.cache/fetch/`), because clang-format output drifts
  between versions and the pin is what makes the check a meaningful gate. Use the
  targets, whose file set is `.clang-format-include`:

  ```sh
  cmake --build build --target format         # rewrite sources in place
  cmake --build build --target format-check   # dry-run, non-zero on diff (CI gate)
  ```

  Neither is part of `ALL`, so a normal build never reformats. Override the binary
  with `-DLAIN_CLANG_FORMAT=/path/to/clang-format` if you must.
- **Strict build.** `/WX /W4` (MSVC), `-Werror -Wall -Wextra` (else).
  Out-of-source enforced. Standalone-vs-subdirectory option gating like the
  siblings (`FLOW_BUILD_TESTING` etc., default ON top-level, OFF as subdir).
- **API shape.** Where a type owns a resource and is passed around, prefer
  `archimedes`' pImpl/`shared_ptr` handle + parent-factory pattern. Where it's a
  value algorithm surface, prefer `multi`'s thin free-functions-over-a-`Context`
  pattern. `Graph`/`Node` are owners (handle-ish); the scheduler is internal.
- **Tests.** Catch2 via `ctest`; `[gpu]` tests `SKIP`-aware (no driver → green),
  exactly as `archimedes` does. Tests exercise the production path, never a copy.
  A **TEST_CASE name is ASCII** — `catch_discover_tests` hands it back to the executable
  as an argv filter, and a non-ASCII byte does not survive that trip on Windows. Prose and
  SECTION names keep their em dashes.
  **A time or a memory peak is a measurement, not a check.** It belongs in a bench
  (`bench-camera-*`, behind `LAIN_BUILD_BENCHMARK`, never run by ctest), printed rather than
  bounded. The test beside it keeps what the code guarantees. A ctest bound on one fails when
  the machine is busier, which is how `RUN_SERIAL` came to exist and why it went.
  **A Release-only case calls `lain::testing::skipInDebug(reason)`**, never `SKIP` under
  `#ifndef NDEBUG`, so its body still compiles in Debug (Catch2's `FAIL` and `SKIP` do not return).
  **A sabotage proves a test only once it builds**: a sabotage that fails to compile leaves the old
  binary answering, and a source restored in the same second as its object stays compiled, so touch
  it again and check the object is newer. Check a smoke's exit code, not only its log.
- **Where a body lives.** Three homes, and the split must buy something.
  1. **In the class** — the default. Anything a reader takes in at a glance: a forwarder,
     a comparison, a one-expression accessor.
  2. **`details/<name>.inl`** — a template body long enough that the header would stop
     reading as an interface. Every `.inl` in the tree is included by its own header, so
     the split buys *readability only*; never claim it buys an include or a rebuild.
  3. **`src/<name>.cpp`** — a non-template body. The only home that buys a real dependency
     boundary, and the default for one. A ONE-LINE DELEGATION whose dependency is light and
     already part of what the type is about may stay inline in the header instead —
     `LAIN_CLI_OPTION` (core/clioption.h) is the standing case, where splitting bought a
     `.cpp` half that had to be kept in step with a signature in the header, and bought
     nothing else. That is a trade made deliberately, not a shortcut.

  **Never out of line in the same header.** It buys nothing — the reader scrolls past the
  body either way — and costs a second place to look. The one sanctioned exception is a
  body that *cannot* sit in its class because it needs a type declared later, and such a
  body says so in a comment, because moving it up is a build break rather than a tidy.
  There is currently **no example of it in the tree**: the standing one was
  `task::Task::precede`, which needed `Flow`, and ADR-0024 deleted both with the rest of the
  `lain::task` wrapper. The rule stays, because the shape recurs whenever two classes in one
  header refer to each other.
  This rule is about a body's HOME, and deliberately says nothing about whether an operator
  is a member or a free function. Making every comparison a hidden friend was considered and
  **refused** (2026-09-21): the only thing it buys over a member is converting the LEFT
  operand, which just enables `42 == value` — Yoda comparisons nobody here wants to read —
  and it is a C++17-only difference anyway, since C++20 synthesises the reversed candidate.
  The ADL-scoping argument was measured and came to nothing in this tree. Both spellings
  exist and both are fine; the body goes in the class either way.

## Where to start

Read WORK.md's *Outstanding work — one index* for what is open, then the section that owns whatever
you pick up: a milestone's build order, or a built piece's landing notes. Use CONTEXT.md's terms.
Don't build the backlog (Tier A/B/C) speculatively.

## Build & verify

Out-of-source, standard for `lain`:

```sh
git submodule update --init --recursive   # pull extern/ (archimedes, multi; both track develop)
cmake -B build                            # FetchContent pulls GLM, GLFW, CLI11, ImGui, imnodes, ...
cmake --build build
ctest --test-dir build --output-on-failure
./build/plugins/camera/opencv/bench/bench-camera-opencv   # measurements, by hand, in a Release build
./build/apps/flowview/flowview            # gui-mode: the inspector on the live driver
./build/apps/flowview/flowview --help     # cli-mode: run a graph headless, dump output
cmake --build build --target format-check # the pinned clang-format, as CI runs it
```

- **Optional plugins** (FFmpeg, OpenCV, Ceres: `LAIN_IO_VIDEO_FFMPEG`, `LAIN_CAMERA_OPENCV`,
  `LAIN_CAMERA_CERES`) default ON in a top-level build wherever the platform has them, and OFF as a
  subproject. An existing build directory keeps a cached option or pin (`IMGUI_REF` too) after its
  default changes, so reconfigure with `-D<option>=…` rather than trusting the new default.
- **On Windows with the Ninja generator**, `cmake --build` needs the MSVC environment
  (`vcvars64.bat`); configure, `ctest` and the format targets do not. The Visual Studio generator needs
  nothing.

A change is done when every target builds warning-clean under the strict flags, `ctest` passes, the
real binary was run where the change reaches one, and gui-mode was eyeballed where it reaches the gui.
Per AGENTS.md rule 9, do not claim the viewer works without having run it.
