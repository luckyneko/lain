# WORK.md — node-graph library build plan

Forward build plan for `lain`'s node-graph library. See [AGENTS.md](AGENTS.md)
for working discipline. This plan states intent and decisions; it is not a
commitment to build past what a milestone needs. Discipline is "only the
necessary" — don't build ahead of a real caller.

**What is still open lives in one place: [Outstanding work — one index](#outstanding-work--one-index),
just above the backlog.** Every deferred item, known defect and standing refusal, each pointing at
the section that owns its reasoning.

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
  Alongside it `flow` links only **`lain::meta`** (a `Port` captures
  `lain::meta::typeName<T>()` for inspector pin labels) — both featherweight, no
  GPU/UI coupling.

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
   names no GPU types and links only `lain::task` + `lain::meta`. "Is this a texture,
   preview it" is the viewer's
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

## Milestone 1 — engine + app stack + viewer (**COMPLETE** 2026-06-30)

**Status:** complete. The engine core (steps 1–4 below, plus the engine test suite +
example GPU node), the decoupling of `flow` from archimedes, and `lain::math` /
`lain::app` / `lain::gui` / `flowview` all landed by 2026-06-30, with gui-mode
verified on the live driver. Steps 1–4 record the engine; the "Remaining work"
section after them records the rest, and is likewise done.

**Foundational additions (landed, beyond the original M1 steps):**
`lain::core::Version` (semver-style identity type, sibling to `Time`); **`libs/log`**
(`lain::log`, a thin spdlog wrapper with a hidden backend + shared external fmt, default
stderr sink); **`libs/string`** (`lain::string`, an fmt-backed `format()` plus a generic
formatter for any type exposing `toString()` — detected via a trait, so `core` stays
format-unaware); **`libs/meta`** (`lain::meta`, enum reflection over magic_enum in the
`lain::meta::enums` sub-namespace — `name`/`fromString`/`values`/`entries`/`nameValueMap`
— feeding an idiomatic CLI11 enum option and an ImGui enum-combo in `lain::gui`, plus
`typeName`/`typeNameShort` (an owned parse of the compiler signature intrinsic — no
external dep) and constexpr type traits (`has_to_string`, `has_ostream`) in `lain::meta`
— `lain::string`'s formatter now uses `lain::meta::has_to_string`); and an
`AppInfo{name, version}` on
`lain::app::Application` driving the CLI program name, `--version` (and reserved
`-v/--verbose`), the Vulkan instance name, and a startup log line. `lain::app`
diagnostics now route through `lain::log`.

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
- `Graph` — owns nodes (`add<T>(...)`, keyed by an opaque `NodeId`: a monotonic
  `uint64` handle, not a list index, so an id survives other nodes' removal),
  `connect(from,out,to,in)` with type-check + cycle rejection, `disconnect`. Holds
  the edge list and computed topo order. A pure data model — it does not execute
  (see step 4).

Contract that makes it threadable: a node reads only its inputs and writes only
its own outputs — no shared mutable state. Stated in the `Node` docs and
enforced by review, not the type system.

### 4. Scheduler — the execution layer over a Graph

The `Scheduler` (public, `scheduler.h`) consumes a `Graph` and evaluates it, so
execution never lives on the data model. An abstract base exposes the varying full
push `run(Graph&)` plus the shared, serial pull `evaluate(Graph&, NodeId)`; two
concrete backends implement `run`:

- **`SerialScheduler`.** One topo-order pass (per node: `clearDirty` → populate
  inputs from edges → `Node::compute()`). No execution dependency, so it is the
  natural choice for cli / headless runs and tests.
- **`ParallelScheduler`.** Lowers the `Graph` to a `tf::Taskflow`: one task per node
  (its body calls `Node::compute()`), edges become `task.precede(...)`. Runs it to
  completion on an **injected, caller-owned** `lain::task` executor — which seeds
  in-degree-0 nodes and dispatches successors as predecessors finish (work-stealing
  across threads). Taskflow already owns this push scheduling that `multi` would have
  made us write by hand, so this stays thin.
- **Pull (shared, on the base).** `evaluate(Graph&, NodeId)` is our own code
  (Taskflow is push-oriented): walk dependencies, recompute only `dirty()` upstream
  nodes, then run the target — same `Node::compute()` path. This is the constant /
  on-request (`CameraCapture`) entry point, and is serial for either backend.

The caller owns the executor (so worker count and lifetime stay explicit) — there is
no default or hidden process-wide pool. Workers must not block on nested graph runs —
dispatch from the executor's join, never recursively from inside a node body.

### Engine tests + example graph  (built)

Catch2 suite exercising the production path: graph build, type-checked connect /
cycle rejection, push run correctness + ordering, pull subgraph eval (constant +
on-request refire), and a CPU + GPU-handle port round-trip. A small example graph
(a `flow-example` GPU node writing a `Texture`) doubles as the viewer's smoke
scene — `[gpu]` tests `SKIP`-aware so GPU-less CI stays green, matching
`archimedes`. New libs add their own per-step tests below.

## Remaining work — decouple + app stack + viewer (**all built** by 2026-06-30)

Retained as the record of M1's build order and the decisions inside it. Build order. Each lib builds standalone + as a subdirectory, strict-clean.

### 5. Decouple `flow` from archimedes

Drop the `acm::Texture`/`acm::Buffer` arms from `PortValue`: `m_value` becomes a
plain `std::any` (empty when value-less). `set<T>` / `holds<T>` / `get<T>` /
`type()` / `clear()` collapse onto it; `PortKind` / `texture()` / `buffer()` are
removed. Update `flow-example`'s `GradientNode` to `set<acm::Texture>` and the
`[gpu]` test to `get<acm::Texture>`. After this `libs/flow` names no GPU types
(links `lain::task` + `lain::meta`, both featherweight); archimedes moves to
`flow-example`, `lain::gui`, and `flowview`.

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

## Milestone 2 — interactive graph editing (**COMPLETE** 2026-07-02)

The viewer became an editor. Landed in order:

1. **Opaque `NodeId` + id-keyed storage** — `NodeId` is a monotonic `uint64` handle
   (not a list index), nodes live in a `std::map<NodeId, unique_ptr<Node>>`, so an id
   survives other nodes' removal. (Also drove a scheduler reshape — see step 4 / the
   `CLAUDE.md` handoff.)
2. **`Graph::removeNode` + `add(unique_ptr<Node>)` adopt overload** — the node lifecycle
   editing needs; the template `add<T>` forwards to the adopt overload.
3. **`lain::core::Factory<Base>`** — a generic string-keyed registry (the node palette /
   future deserialiser seam). Keys are explicit strings, not `meta::typeName<T>()` (see
   Backlog Tier B item 7 for a type-owned-key refinement).
4. **`flow-example::TintNode`** — a connectable `acm::Texture` transform (readback → tint
   → re-upload), so a graph edge carries data; the smoke scene is `gradient → tint`.
5. **flowview editable canvas** — a `Factory<flow::Node>` palette (right-click add), drag
   to connect (replace on an occupied input), drag-off to detach/move a link, Delete to
   remove selected nodes/links; a `SerialScheduler` re-runs the graph per edit. The
   inspector previews every texture port via a register-once `VkImageView`→`ImTextureID`
   cache.

Preview descriptors for deleted nodes are reclaimed via `lain::gui::Context::releaseImage`
(the inspector prunes its texture cache after each edit).

## Milestone 3 — file loading (**COMPLETE** 2026-07-14)

**Status:** the **read vertical is done and cli-verified** — `lain::memory` (Buffer) → `lain::io`
(`read → Buffer`) → `lain::io::image` (reader registry + `load` facade) → the **png / tiff / jpeg**
codec plugins (build-discovered aggregator) → `flow-example::LoadImageNode`. `flowview --headless
--image <file>` loads a real PNG/JPEG/TIFF through the node and dumps its extent + format + corner
pixels. The **write library is also done** — `io::write` + the `ImageWriter` seam (`save` facade +
`canEncode`) + png / tiff / jpeg encoders (see the write pass below) — and so is its **interim save
consumer**: flowview saves any image output through a native file dialog (Mac-verified). The M3
write pass is **complete**; the `ImageWriteNode` is **superseded** — see the M4 reframe below. The
gui-mode thumbnail follow-on is **also done** (closed 2026-07-14 by the RGBA8 normalisation fix in
`lain::gui`'s texture bridge — a CPU `image::Image` port previews through `PreviewCache` like any
other): **M3 has no loose ends.**

**Driving consumer:** a `flow` node that loads a *real* image file off disk and shows it in
`flowview` — the first source that isn't synthetic. That one goal is what pulls FileIO and an
image codec into existence for a real reason; nothing else is built ahead of it.

**Locked decisions** (see [ADR-0004](docs/adr/0004-static-linking-service-shaped-seams-over-thorax.md)
and the "Loading" section of `CONTEXT.md`):

- **thorax deferred; static linking now.** Keep `core::Factory<Base>` as the registration seam
  so a `thorax::service` can back it later. The one or two things that truly need runtime loading
  (camera drivers) can `dlopen` directly, without the framework. Revisit only if `lain` starts
  *distributing binaries* to machines with library sets it didn't build on.
- **Service-shaped seams** (free functions over a hidden singleton, the `lain::log` shape) for
  every new subsystem, so each stays thorax-swappable with no caller change.
- **Payload is `Buffer`, not `std::vector<uint8_t>`** — aligned, fixed-size, non-resizable, no
  byte indexing (its refusals are the point). The *type* lands now; the allocator *engine* is
  deferred behind a `memory::alloc/dealloc` seam.
- **Memory manager deferred**, chosen by profiling (mimalloc-as-global-malloc first — one CMake
  line, zero code — then a pool behind the seam if churn is measured; marv is a candidate
  backend, not a from-scratch rebuild).
- **Transport is separate from codec.** `lain::io` moves bytes and names no format; per-media
  loaders decode. Order **png → tiff → jpeg** (reordered from the original jpeg-first): png/tiff
  need *real* codecs because stb can't honor the `U16` `ChannelType`, and both FetchContent
  cleanly (libpng + zlib, libtiff), so they prove the plugin + aggregator machinery on cooperative
  libs. **libjpeg-turbo refuses `add_subdirectory`** (it hard-errors, demanding
  `ExternalProject_Add`), so jpeg came last and uses **stb_image** (resolved): jpeg is 8-bit so
  the U16 objection doesn't apply, and stb vendors trivially (single header, no build system)
  where turbojpeg would need the repo's only `ExternalProject_Add`. Speed/quality is below
  turbojpeg but adequate; a turbojpeg swap can replace `JpegReader` behind the same seam if ever
  needed (turbojpeg does support 16-bit in lossless mode — niche). Prebuilt binaries rejected
  (cross-platform×arch fights from-source reproducibility).
- **Codec format policy: documented lossless expansion, honest tags.** A reader PRESERVES any
  source format lain represents natively (RGB/RGBA/Gray/**GrayAlpha** at 8/16-bit — GrayAlpha was
  added to `lain::image` for this) and never silently changes it. Formats lain can't hold are
  expanded *losslessly in pixel value* — as the reader's documented contract, not a silent surprise.
  **`ColorSpace` is read from the file, `Unspecified` when untagged — corrected 2026-09-03, because
  the claim was false when written**: only the PNG reader did this. JPEG asserted `sRGB` without
  consulting a single marker and TIFF asserted `Unspecified` without reading one, and no still-image
  writer recorded a colour tag at all, so lain's own round trip lost it. The rule is now stated and
  enforced by **[ADR-0020](docs/adr/0020-codec-colour-tag-policy.md)** — a reader states only what
  the file states; a writer records the tag when the format can state it and refuses when it cannot —
  with the two remaining conventional arms (PNG's gAMA window, JPEG's JFIF arm) named there rather
  than passed off as readings. **lain
  format gaps still filled by expansion** (candidates to add as native formats later): indexed /
  palette color; sub-byte channel depths (1/2/4-bit); colourkey transparency (tRNS, vs. a full
  alpha channel); and colour spaces beyond `Unspecified`/`Linear`/`sRGB`/`BT709` (arbitrary gamma,
  ICC profiles, non-sRGB primaries — collapsed to `Unspecified`, which ADR-0020 keeps: a space lain
  cannot hold is not the same as no space). A codec may instead
  **reject** a layout it can't represent faithfully rather than expand it — the TIFF reader
  rejects tiled / planar / float / palette / exotic-photometric loudly (logged, invalid Image);
  either way the response is loud, never a silent wrong result.
- **Codecs are plugins, and live like plugins.** The `lain::io::image` interface + `Factory` +
  facade is the codec-free **seam** and stays in `libs/`; each format is a **separate satellite
  target** (`JpegReader` + `JpegWriter` together) under a top-level **`plugins/`** root (peer to
  `libs/`/`apps/`, matching `thorax`'s own layout) so the swappable parts don't get lost among the
  interfaces. Dependency inverts: a codec depends on the interface, never the reverse.
- **Registration is explicit and build-determined.** No self-registering static-init (the linker
  strips it). Each codec exposes `registerCodec(registry)`; the build collects the *enabled*
  codecs into a generated `lain::io::image::codecs` aggregator emitting one
  `registerImageCodecs(registry)`, which the app calls once (mirroring `registerExampleNodes`).
  Membership is discovered from a global CMake list, so a new `plugins/io/image/<fmt>/` folder
  joins the group with no edits elsewhere; `if(TARGET lain::io::image::<fmt>)` is available for
  finer per-codec control.
  - **Layout corrected 2026-09-02 (repo owner):** each codec's `registerCodec()` used to sit at the
    bottom of `<fmt>reader.cpp`, with the writer's registration reached through a hand-written
    forward declaration across translation units. **One header, one translation unit** — so
    `register.h` is now implemented by `register.cpp`, and `<fmt>reader.h` / `<fmt>writer.h` declare
    the single thing their own `.cpp` exports (its registration; the codec classes stay private to
    their TU, since nothing constructs them elsewhere). The cross-TU forward declarations are gone,
    and each half owns the format keys it claims. Behaviour is unchanged. The same defect had been
    copied into the FFmpeg plugin and `io::sequence`, and was fixed there first.
- **Third-party codecs via `cmake/addXXX.cmake` FetchContent**, not `find_package` (house
  convention). The per-codec guard is therefore an **opt-out `option`**, not an availability probe
  (sources are always fetchable — "absent" means "disabled"). png/tiff pull transitive deps
  (zlib; tiff optionally jpeg) resolved in the `addXXX.cmake` chain, as `archimedes` does.

**Build order** (each a small static lib behind a service-shaped seam; each strict-clean,
standalone + as a subdirectory):

1. **`lain::memory`** (`libs/memory`) — the minimal `Buffer` only: aligned, fixed-size,
   move-only, `data()`/`size()`, no `resize`, no `operator[]`. It allocates through
   `memory::alloc(size, align = 64)` / `dealloc(ptr, size, align)` (sized + aligned so a pool is
   a drop-in); v1 backing is `std::aligned_alloc` / aligned `operator new`. **No pool/arena yet.**
2. **`lain::io`** (`libs/io`) — `io::read(uri) → memory::Buffer`, the FileIO front seam;
   `local` scheme only, dispatch behind it for `remote`/`s3` later. **Codec-dependency-free.**
3. **`lain::io::image`** (`libs/io/image`) — the codec-free seam: the `ImageReader`/`ImageWriter`
   interface, a hidden `core::Factory<ImageReader>`, and the `io::image::load(uri) → image::Image`
   facade. **No third-party codec deps.**
4. **First codec plugin** (`plugins/io/image/png` → `lain::io::image::png`, via `addLibPNG.cmake`
   + `addZlib.cmake`) + the generated `lain::io::image::codecs` aggregator. Then `tiff`, then
   `jpeg` (its own integration decision — see the codec bullet above) as further satellites. Each
   depends on the `lain::io::image` interface + its fetched codec; codec deps never reach core
   `io`. png first validates the **U16** round-trip and the aggregator on a FetchContent-clean lib.
5. **`LoadImageNode`** in `flow-example` — emits a **CPU `image::Image`** through a port (no GPU:
   `acm` upload waits for Compute Nodes). `flowview` **cli-mode** dumps the loaded image (extent +
   corner pixels) — the true file→node→output proof, no driver needed.

**Verify M3:** every lib strict-clean; `ctest` passes; `flowview --headless` loads a real image
file through `LoadImageNode` and dumps it. The **gui-mode thumbnail** of a CPU `image::Image` port
is a **follow-on** — it realizes the deferred "GUI view" seam (a per-type viewer that uploads the
CPU image *for display only*; not a node/compute op), and needs the live driver (AGENTS rule 9).
`Image`'s internal `std::vector` → `Buffer` swap is an internal follow-on (callers use
`data()`/`size()`), not a blocker.

**Write path — a dedicated pass after reads.** M3's consumer only *loads*, so the read vertical
(memory → io → io::image → codec plugins → `LoadImageNode`) lands first. Writing is the symmetric
pass, taken up once an `ImageWriteNode` gives it a caller: `io::write(uri, Buffer)` + an
`ImageWriter` interface + a `Factory<ImageWriter>` registry + a `save(uri, Image)` facade +
per-codec encoders (added to each *existing* plugin — the codec dep + target are already there, so
only the encode code is new; encode is a separate API surface from decode regardless of timing).
Deferring it keeps the milestone honest for a modest re-entry cost. The `ImageWriteNode` needs the
**node-parameter** surface below (an output path + quality are params), so that lands first.

**M3 follow-on plan (2 → 1 → node-config → 3).** After the read vertical:
- ✅ **Step 2 — `Image` → `memory::Buffer`** (done): aligned, alloc-seam-backed, pool-ready storage;
  `Image` stays copyable via a hand-written deep copy.
- **Node parameters** (designed — [ADR-0005](docs/adr/0005-node-parameters-distinct-typed-slots.md),
  `CONTEXT.md`): params are distinct, non-connectable, typed `PortValue`-style slots
  (`addParam<T>` / `param(idx).get<T>()`); widgets chosen by type via an adapter-side editor registry
  (prefer existing types — `std::filesystem::path`, `image::ColorRGBf`; bespoke `Choice`/`Range<T>` only where none fits); `Node::onInspect` removed. Needed *now*
  to set `LoadImageNode`'s path in the gui, and a prerequisite for `ImageWriteNode`.
- **flowview gui pass** (one Metal-verified batch): the CPU-image **thumbnail** (step 1) +
  **node-param editing** (properties panel) + a **persistent palette add-panel** (left-click node
  types from `Factory::keys()`; right-click kept secondary — a MacBook trackpad handles right-click
  poorly). Keyboard-search add deferred.
- ✅ **Step 3 — the write library** (done): `io::write(uri, Buffer)` + the codec-free `ImageWriter`
  seam (`save(uri, Image)` facade) + **png / tiff / jpeg** encoders. Encoders **reject rather than
  degrade**: `ImageWriter::canEncode(image)` gates the seam so a lossy silent conversion never
  happens (JPEG refuses alpha / 16-bit; png/tiff refuse float) — the caller converts explicitly
  (ADR-0003). Config knobs (png level / tiff compression / jpeg quality) use fixed defaults; a
  config mechanism is **deferred** (the three surfaces are heterogeneous — decide with a real
  caller, i.e. the M4 output binding).
- ✅ **Step 4 — interim save consumer** (done, Mac-verified). `ImageWriteNode` was the planned
  caller, but it's the **wrong model** (see M4): a save *node* is a side-effecting sink in a
  pure-compute engine, and "output to one file" reads oddly in a gui. Instead, saving is a **host
  action on an image port** — flowview shows, under each image *output*, an inline format dropdown
  (only formats that `canEncode` the image losslessly, per-pin) + a **Save…** button →
  `gui::saveFile` (native panel) → `io::image::save`. The dialog seam (`lain::gui`, over
  portable-file-dialogs; **Browse…** retired the LoadImageNode path field too) remembers the last
  folder within the session. This ties up the write library with a real end-to-end consumer *and*
  is **forward-compatible**: it is precisely the M4 gui host's "display the output, write it on
  Save/Record" binding.

**Deferred:** a read-only ("Debug") param kind; graph serialization of params (Tier A item 1).

**Deferred behind these seams:** `remote`/`s3` IO schemes; ~~a **`Stream` transport**~~ and
~~`lain::io::video`~~ — **both claimed by Milestone 10**, which is the caller that unblocks them
(`read → Buffer` is a whole-asset slurp that video can't use); `lain::io::audio` + `Timecode`
(Tier C item 9 — M10 needs neither, since a frame is addressed by ordinal and carries a plain
timestamp); the memory pool/arena +
`BufferView`/`SharedBuffer`; magic-byte format sniffing (extension-keyed for now); thorax
adoption. **TIFF reader breadth ("one day", not now):** float sample format → `F32` Images
(intermediary/HDR files — lain already has the `*32F` formats), `CIELab`/`YCbCr` photometrics
(need a colour conversion), and tiled / planar / multi-page (currently rejected / first-page).

## Milestone 4 — pipeline I/O: graph boundary + host binding (**COMPLETE** 2026-07-10)

**Both verticals built and verified.** This began as a rough sketch captured so M3 could close
cleanly — *"not yet designed… grill + ADR before building"* — and was then designed and built:
vertical **(a)** (the boundary seam, the cli round-trip, the Interface panel) and vertical **(b)**
(dynamic boundary pins on stable `PortId`s) both landed 2026-07-10, and the cli named-binding
deferred here landed 2026-07-13 with Tier A #1's `run` subcommand. **Two things stay deferred from
this milestone:** the Merge / fan-in (multi-connectable) port, and port reorder.

`flow`'s reason for being is a **volumetric reconstruction pipeline**: N streams of frames (images)
→ voxels, live (real-time) and offline (cli). That reframes I/O away from per-file load/save
*nodes* toward a **graph with a declared boundary interface that a host binds**:

- **Graph interface (boundary ports)** — a Graph declares typed **inputs** (what it needs, e.g. "N
  image streams") and **outputs** (what it produces, e.g. "voxel volume"); internal nodes wire
  between them. This *is* the pipeline's signature.
- **Host binding** — the host supplies inputs and consumes outputs, differently per host:
  - **gui**: inputs ← a **live** stream *or* a **static** image (two input modes); outputs →
    displayed, and **written only on Save/Record** (the interim flowview "Save…" action is exactly
    this output binding, arriving early).
  - **cli**: inputs ← files parsed/synced from args into the input slots; outputs → a real **write**
    step. A cli run reads the graph's input list to know what to ask for, and its output list to
    know what to write.
- **Group node = subgraph** — a node that contains its own nodes and exposes selected inner ports as
  its own. The **top-level Graph is the outermost group**; boundary ports are its interface. One
  mechanism at every level.

**Boundary representation — LOCKED: A.** Boundary ports are **designated Input/Output nodes** inside
the graph (Blender group style) — a `GroupInput` node whose *output* pins are the graph's inputs, a
`GroupOutput` node whose *input* pins are its outputs; the host enumerates them to bind. Reuses the
existing node/port machinery and unifies with group nodes for free (vs. a second port system living
on the `Graph` object).

**First vertical — LOCKED: (a) static single-I/O boundary.** One host-bound image input, one output;
gui binds the input by file-pick + displays/saves the output, a minimal cli binds an input path arg
+ writes the output path. Proves the core new idea — a graph declares a boundary interface, cli vs
gui bind it differently — reusing the image load/save library. The **host-binding mechanism is a
loop over a boundary node's pins, so it generalizes 1→N for free**: dynamic ports (the N-input case)
layer on next with no rework of the binding / cli / gui. Streams, sync, real-time, nested groups all
deferred past this vertical.

**Seam design (LOCKED for vertical a) — a generic, payload-agnostic boundary seam, not typed
app nodes.** "Generic" has two independent axes: *type*-genericity (the boundary carries any `T`) is
handled at **compile time** by templates — no dynamic ports; *count/runtime-typing* (variable pin
count, runtime-chosen type) is dynamic ports, i.e. **(b)**. The seam is axis-independent (it's the
enumerate + bind contract), so (a) uses it with fixed single-port templated nodes, and (b) later
swaps in dynamic-port nodes **without changing the seam, cli, or gui**.

- **Single node, multiple pins (Blender model), not one node per pin.** flow's ports are
  individually typed (`addOutput<T>` per pin), so *one* node declares a fixed set of
  differently-typed pins at construction — **no dynamic ports, no runtime-typed ports** (those are
  only for *runtime* add/remove, i.e. (b)). So a graph's whole interface is two nodes, and N image
  streams later are one Input node with N pins, not N nodes.
- **flow core** gains two concrete nodes: `GroupInputNode` (`addBoundary<T>(name)` declares a
  host-bound output pin; holds a per-pin `std::vector<PortValue>`; `setValue(pin, PortValue)`;
  `compute()` publishes each pin's value) and `GroupOutputNode` (`addBoundary<T>(name)` declares an
  input pin; `compute()` no-op; `value(pin)` returns its input's value). All crossing is through the
  type-erased `PortValue`, so core names no payload type. (Core's first concrete *node facility*,
  justified: boundary nodes are graph *structure*.)
- **Pin-centric seam.** A bindable input/output is a **pin**, not a node: `BoundaryInput` /
  `BoundaryOutput` handles (`{node, pin}` with `name()` / `type()` / `setValue()` or `value()`).
  `Graph::boundaryInputs()` / `boundaryOutputs()` **flatten** every boundary node's pins into one
  list — the RTTI/`dynamic_cast` lives here, and the host binds pins without caring how many nodes
  host them.
- **Value crossing.** Input: host `setValue(pin, ...)` stores the value + `markDirty()`; `compute()`
  publishes it, then it is **constant until re-set** (the Node docs' "constant clears dirty, re-set
  refires" pattern; a live streaming input in (b) simply stays dirty). Output: after the run, host
  reads `value(pin)`. The host loops the flat pin lists — *setValue each input → run → read each
  output* — identical for 1 or N pins, images now or voxels later.

**Host consumers (a).** The graph is still built in code (`buildExampleScene`) — graph
*serialization* is deferred (Tier A) — so neither host runs an arbitrary graph file yet.
- **cli** = extend flowview's existing **`--headless`**, kept deliberately *testy*: `--input <path>`
  → `io::image::load` → `setValue` the sole boundary input → run → read the sole output `value()` →
  `io::image::save` to `--output <path>`. `dumpGraph` stays for inspection. A proper **`run` mode**
  (arbitrary serialized graphs + cli helpers to *list* a graph's inputs/outputs, named-arg binding
  `source=foo.png`) waits for serialization — don't build it until the data structure is solid.
- **gui** = a dedicated **"Interface" panel** (Inputs & Outputs), driven by
  `Graph::boundaryInputs()/outputs()` — the *same enumeration loop the cli runs*. Inputs: name,
  type, **Bind file…** (`openFile` → `io::image::load` → `setValue` → `reevaluate`). Outputs: name,
  type, thumbnail, **Save…** (reads `value()` → the format-dropdown save flow). The boundary nodes
  still appear in the canvas/inspector as nodes; this panel is the host-binding surface, and the
  natural home for **boundary-only controls** that don't fit a normal node — save (outputs),
  play/pause (live inputs, (b)), a re-run. It is a *persistent* surface (independent of canvas
  selection — which matters given the planned "inspector shows only the selected node" change).

### Vertical (b) — dynamic ports (grilled; identity settled, mutation API next)

Ports are declared in a `Node`'s ctor and fixed for life today, and **edges reference ports by
`PortIndex`** — so removing a middle pin shifts every higher index and silently corrupts edges +
`BoundaryInput` handles. Two concepts were hiding in "N inputs" and must stay apart (see CONTEXT.md
"Port arity"): a **vector-valued port** (`Port<std::vector<T>>`, one pin, aggregate payload — needs
*no* engine work) vs **dynamic ports** (variadic pins — the feature here). The driver wants *arbitrary*
add/remove (and reorder later), so:

- **LOCKED: stable `PortId` + `PortAddress`.** `PortId` is an opaque per-node handle (the port-level
  `NodeId`; `0` sentinel). `PortAddress {NodeId, PortId}` is the durable address of a port — the unit
  edges and handles reference, and the serialization primitive. An `Edge` becomes two PortAddresses
  `{from, to}`; a `PortIndex` is demoted to a positional *iteration cursor* only.
- **Storage:** keep the ordered `std::vector<Port>` (compact, display-ordered, reorderable by
  permuting), each `Port` carrying its `PortId`; `portById(PortId)` resolves by scan (few ports).
  Removal compacts, reorder permutes — ids ride along, so edges (holding ids) are untouched, exactly
  as `Graph` treats `NodeId`. `GroupInputNode`'s `m_bound` moves to `map<PortId, PortValue>`.

**Build order (b):**
1. ✅ **`PortId` / `PortAddress` refactor** — the "NodeId-ification of ports": `Edge` → two
   PortAddresses; `connect`/`disconnect`/`populateInputs`/scheduler/`edit`/`dump`/imnodes pin
   encoding/boundary handles move `PortIndex` → `PortId`; dynamic/boundary add paths return `PortId`.
   **Known gap found during M6 grilling:** the protected static `addInput` / `addOutput` / `add*Like`
   helpers still return `PortIndex`, so fixed nodes retain positions despite the iteration-only
   contract. **M6 step 2** closed it, and retired the `PortIndex` alias itself — a position is now a
   plain `std::size_t`, so nothing invites storing one. **No behaviour
   change** — a standalone commit kept fully green on the original edge/handle refactor.
2. ✅ **Mutation engine** — `DynamicPortsNode` + `addDynamicPort<T>` + `Node` raw erase +
   `Graph::removePort` primitive (refuses a connected pin) + `edit::removePort`. Driver-free tests
   incl. the mid-pin-removal proof.
3. ✅ **Port-type registry + boundary nodes go dynamic** — `registerPortType<T>`; `GroupInputNode`/
   `GroupOutputNode` are `DynamicPortsNode`s (`addBoundary` routes through `addDynamicPort`);
   `edit::addPort`; `Port::setName`.
4. ✅ **gui** (Mac-verified) — the Interface panel is node-grouped: per-pin editable name + bind/save
   + a "×" with a "Remove 'name'? N link(s)" confirm, and a per-node "+ add pin" (registry type menu
   filtered by `acceptsPortType`). `Graph::boundaryInputNode()`/`boundaryOutputNode()` (single —
   one Group Input, one Group Output).

**Vertical (b) is complete** — dynamic ports work end-to-end: add / rename / remove typed boundary
pins in the gui, on stable `PortId`s so a mid-list removal never corrupts sibling edges. Deferred as
planned: Merge / fan-in port and port reorder. (**cli named-binding was deferred here but has since
landed** — Tier A #1's `run` subcommand binds boundary inputs by name, `--<boundary> <value>`.)

**Mutation design (grilled, settled):**
- **`DynamicPortsNode`** marker base (the gui `dynamic_cast`s to show ±): `dynamicSide()` (which side
  grows), `acceptsPortType(key)` (default: all — a swappable predicate), `addDynamicPort<T>(name)`
  (adds on the dynamic side). Opt-in, so fixed nodes are unaffected.
- **`Graph::removePort(PortAddress)`** — a *primitive* that **refuses if the port has incident
  edges** (keeps the no-dangling-edge invariant total, like `connect` refusing a cycle); the node's
  raw port-erase is Graph-friend. **`edit::addPort` / `edit::removePort`** are the *safe gestures*
  (removePort disconnects incident edges, then the primitive).
- **Port-type registry** (see CONTEXT.md "Port arity") — `registerPortType<T>(name)`, app-registered;
  the ± menu is its keys filtered by `acceptsPortType`. No hardcoded pipeline types.
- **Driver = the Boundary nodes, not Merge.** `GroupInputNode`/`GroupOutputNode` become
  `DynamicPortsNode`s — the genuine, irreducible dynamic-ports case (Merge's natural form is the
  deferred **fan-in / multi-connectable port**; building Merge-via-N-pins is a strawman for it).
  Pins auto-named (`input0`…); boundary pins additionally **renameable** (`Port::setName` + an
  editable field — edges reference `PortId`, so a rename touches only the display string).
- **gui:** the Interface panel gains a per-node **"+"** (registry type menu) and a per-pin **"×"**
  that **always confirms** ("Remove 'name'? N link(s)"), then `edit::removePort`.
- **Deferred:** Merge / fan-in port; port **reorder**. (cli **named-binding** was listed here too;
  it landed with Tier A #1's `run` subcommand — `run --<boundary> <value>`, keyed on the pin name.)

**Build order (vertical a)** — three commits, each strict-clean + tested:
1. ✅ **flow core — the boundary seam** (built, tested). `GroupInputNode` / `GroupOutputNode`
   (multi-pin, `addBoundary<T>`) + the pin-centric `BoundaryInput` / `BoundaryOutput` handles +
   `Graph::boundaryInputs()` / `boundaryOutputs()` (flatten pins). Tested **driver-free** on trivial
   payloads (`int`/`float`): `setValue` → run (SerialScheduler) → downstream → `value()`; a single
   node with two differently-typed pins; re-bind refires; unbound → empty; enumeration flattens +
   excludes ordinary nodes. Fixed pins (no dynamic ports). In **flow core** (payload-agnostic,
   structural — core's first node facility).
2. ✅ **Example scene reshape + cli** (built, cli-verified). `buildExampleScene` is
   `GroupInputNode("source")` → tint → blur → `GroupOutputNode("result")`. flowview `--headless
   --input <path> --output <path>` binds the boundary via `io::image::load` → `setValue`, runs,
   dumps, then `save`s the output boundary — the write library's caller. A default gradient stands
   in for an unbound input (bare `--headless` + gui-mode until the panel). `dumpGraph` stays.
3. ✅ **gui Interface panel** (built, Mac-verified). A dedicated "Interface" window driven by
   `Graph::boundaryInputs()/outputs()` — Inputs (name/type, bound thumbnail, **Bind file…** →
   `openFile`→`load`→`setValue`→`reevaluate`) + Outputs (name/type, result thumbnail, the
   format-dropdown **Save…**). The save controls are a shared `renderImageSave` reused by the
   inspector's output ports. (UI polish deferred to a later pass once more features land.)

**Vertical (a) is complete** — the boundary model works end-to-end in both hosts (cli round-trip +
gui bind/save), on the pin-centric multi-port seam shaped so (b)'s dynamic ports slot in unchanged.

**Also implicates** (each its own effort): the deferred **`Stream` transport** (video / live
frames, incremental vs. `read`'s whole-asset slurp); **multi-stream sync** (temporal alignment of N
streams); and a **live / real-time execution** model (Taskflow `tf::Pipeline`, Tier B item 4). The
**side-effecting-sink** concept a true write-in-graph node would need is deferred with it — the host
binding sidesteps it for now.

## flowview UI pass (grilled 2026-07-13)

> **Live-verified 2026-07-23.** Everything from here through the window/panel layout pass and the
> pane-split refactor had been built and suite-tested but never driven on the Metal driver; that
> backlog was cleared in one session by the repo owner exercising the gui. The per-slice markers below
> are dated accordingly.

A readability + usability pass on the viewer, grilled into three slices (A/B/C) plus later passes.
New module **`canvasstyle.{h,cpp}`** owns just the palette: a `CanvasStyle` with a `type_index → colour`
port palette (+ hash-of-typeName fallback) and a `factory-key → colour` node-title palette (categories
emergent from shared colour, no `Category` enum) + the muted tones. Two supporting pieces landed in their
proper homes, not as viewer free functions: **readiness is `flow::Node::ready()`** (every Required input
carries a value — ADR-0007), a node-local member the **scheduler** gates on *and* the viewer reads
post-run for the dim (one definition, no duplication); and the colour pack is
**`lain::gui::packColor(image::ColorRGBA8) → ImU32`** (`gui/color.h`, the GUI seam to the toolkit's
packed-colour form — `gui` already depends on `image`, now PUBLIC). The palette speaks `image::ColorRGBA8`
**end-to-end**; the ImU32 is produced only by `gui::packColor` at the **imnodes boundary** (the draw
loop's `PushColorStyle` calls) — no colour converted in a middle layer (imnodes' colour API is a raw
ImU32, so no `ImVec2`-style struct hook for auto-conversion). The hash fallback uses a **new reusable
`image::ColorHSVf` + a `convert()` overload** (HSV→RGB) added to `lain::image`. The future theme.json /
colour editor loads into this.

- ✅ **Slice A — readability BUILT** (2026-07-13, live-verified 2026-07-23).
  Inactive node (a Required input empty) → whole node muted (title+bg); dead edge (source output empty)
  → muted link. Pins + links coloured by value type (Image=blue, Int/Float=green, Bool=purple,
  String=amber; hash fallback otherwise). Node titles tinted by kind (source/filter/control/boundary).
  Pin **shape = value-presence** (all circles), the same rule both directions: filled = carries a value,
  hollow = empty (an input's upstream produced nothing / it's unconnected; a suppressed node's outputs) —
  connectedness is read from the wire, not the fill. Pins **always show their type colour** (the dim lives
  on the node title/background + muted dead links, *not* the pins — muting them would hide the type you
  need to wire an incomplete node; refined 2026-07-14 after eyeballing). Inline `: type` text **dropped**
  (pin shows name only); outputs **right-aligned** to a stable text-derived column width (a rendered-width
  target fed back → runaway node growth). Precedence for a pin: the only mute is the drag-incompatible one
  (slice C). Full suite 287/287; headless unaffected.
- ✅ **Slice B — selection-driven inspector BUILT** (2026-07-13, live-verified 2026-07-23).
  The Inspector panel shows only the node(s) selected on the canvas (`selectedNodes()`), stacked and
  walked in topo order for a stable layout, each with its params + port values + previews; nothing
  selected → a "Select a node…" hint; the preview-size combo stays pinned at the top. Replaces the
  all-nodes dump. The Interface panel is untouched (its merge is the window-interaction pass).
- ✅ **Slice C — interaction feedback BUILT** (2026-07-13, live-verified 2026-07-23). During a
  link drag, every pin that isn't a compatible drop target (opposite direction + same type) greys out —
  the drag source is captured on `IsLinkStarted` (after `EndNodeEditor`) and consumed by the next frame's
  pin draw (so the grey shows one frame in, invisible mid-drag), cleared on mouse-release. This is the
  only thing that mutes a pin (an inactive node no longer greys its pins — see slice A). Pin **tooltips**
  on `IsPinHovered` show the full type +
  current value (`describe()`), recovering the detail the terse name-only labels dropped. Missing-required
  stays **emergent** (a dim node with hollow input pins — slice A), with actionable validation deferred to
  the "Graph Issues" panel. A guarded `findNode` protects the decoded hovered/dragged pin against a node
  deleted the same frame. **The A/B/C readability + usability pass is complete.**
- ✅ **Menu bar BUILT** (2026-07-14, live-verified 2026-07-23). An in-app `BeginMainMenuBar`
  (not a native platform menu — that'd be per-OS Cocoa/Win32 outside ImGui+GLFW): **File** (New / Open... /
  Save / Save As... / Quit) tracking a remembered current path (plain Save writes it, Save As sets it, New
  clears it); **Add ▸ category ▸ kind** grouped by a new viewer-side `scene::nodeCatalog()` (Sources /
  Filters / Control — boundary nodes are deliberately **not** addable, being a one-each-per-graph fixture;
  the right-click + Nodes panel source the same catalog). Shortcuts are **displayed and wired** via
  `Shortcut()` + `RouteGlobal`, using **`ImGuiMod_Ctrl` always** (ImGui remaps it to Cmd on macOS — an
  explicit `ImGuiMod_Super` does NOT match, the bug in the first cut). **New defaults to a blank
  Input/Output graph** (`buildNewScene` — one empty GroupInput + GroupOutput, laid out Input-left /
  Output-right), also the fresh-gui-session default; the example is opt-in via **`--example`**. **New
  guards unsaved changes** with a Save/Discard/Cancel modal (a `m_dirty` flag set on any edit, cleared on
  save/load/new). Menu labels use ASCII `...` (the default ImGui font has no `…`/`•` glyph → they'd render
  `?`). Settings menu skipped (not needed yet).
- ✅ **Boundary input value editors BUILT** (2026-07-14, live-verified 2026-07-23). You can now
  set a graph input's value in the gui by type: image inputs keep the **Bind file...** picker, and every
  other type gets a type editor (int → drag, float → drag, bool → checkbox, string → text, path → field +
  Browse). `ParamEditors` was generalised from `render(Param&)` to **`render(label, type, PortValue&)`** —
  a bare value slot — so it now serves node params **and** boundary inputs from one registry (an empty
  slot edits from the type's default). The Interface panel reads the boundary's currently-published value
  (`pin.value()`), edits a copy, and `setValue`s on change; the edit marks the graph dirty so New guards
  a bound image (loaded data) from silent loss. **Preview format fix:** `lain::gui`'s texture bridge
  sampled RGBA8 only, so a loaded RGB/Gray file (or a direct copy of it) previewed blank while a
  node-computed RGBA8 image worked; `Context::createTexture` + `Texture::upload` now **normalise any
  image to RGBA8** (`image::convert`), so a `gui::Texture` is always drawable — robust for every consumer.

### Window/panel layout pass (grilled 2026-07-16)

Docking + a Preview pane + an Issues panel, so panels tile with draggable splitters (no more Interface
panel lost behind the Graph). Grilled design: **docking-only** (multi-viewport deferred — the real
"pop-out" is a future `lain::app` multi-window for rich viewers like a 3D Voxel or zoom/pan image, not
ImGui viewports); Interface + Inspector stay **separate**, both docked (boundary nodes suppressed from
the Inspector); a **Preview** pane tabbed with the Graph (click an asset thumbnail → it opens full-size
there); an **Issues** panel = live validation (missing-required ⚠, unread-output ℹ, rejected-connect ⚠
transient, load-issue ⚠/⛔), rows click-to-locate (`SelectNode` + `EditorContextMoveToNode`). Default
layout: Graph/Preview tabs centre, Inspector/Nodes tabs right-top, Interface right-bottom, Issues under
the centre (independent splits). Built in 3 slices.

- ✅ **Slice 1 — docking infra BUILT** (2026-07-16, live-verified 2026-07-23). **ImGui switched
  to the `docking` branch** (release tarball → `github.com/ocornut/imgui/archive/docking.tar.gz`, now
  `1.92.9 WIP` with `DockSpace`/`DockBuilder`; the release branch has no docking) — build + `ctest`
  clean on it (289/289). *(Reproducibility: **resolved 2026-07-23** — `IMGUI_REF` is pinned to commit
  `162ce49` (the 1.92.9 WIP this was developed against), like imnodes and the submodules, instead of the
  moving branch tip. Note `IMGUI_REF` is a CMake **cache** variable: an existing build dir keeps its old
  value until reconfigured with `-DIMGUI_REF=…`, so bumping the pin needs that or a fresh build dir.)*
  The DockBuilder API (ImGui-**internal**) is fronted by
  a new **`lain::gui` docking seam** (`gui/dock.{h,cpp}`: `DockNode` / `DockDir` / `dockSpaceOverViewport`
  / `dockReset` / `dockSplit` / `dockWindow` / `dockFinish`) so flowview never includes `imgui_internal`.
  `lain::core` gains **`configDir("flowview")`** → `<home>/.flowview` (created on demand) built on
  **`homeDir()`** + **`core::envVar`** (new `core/platform.h`, MSVC-safe env read via `_dupenv_s`).
  `gui::Context` now takes a **`ContextConfig{ iniFilename, docking }`** struct (named fields, no opaque
  trailing bool). flowview submits the dockspace, stamps the default layout on first run (no
  `~/.flowview/imgui.ini`) or reset — saved layouts win — with **View ▸ Reset Layout** and a
  **`--reset-layout`** flag as recovery. Stub **Preview** + **Issues** panels exist (empty) so the layout
  docks them; filled in slices 2/3.
- ✅ **Slice 2 — Preview pane + click-to-preview BUILT** (2026-07-16, live-verified 2026-07-23).
  Every image thumbnail (Inspector ports + Interface inputs/outputs) is now clickable → routes that asset
  (by stable `PinKey`) to the **Preview** pane and flips the Graph/Preview tab group to it. Tab activation
  is a new gui seam **`gui::activateWindowTab`** driving the dock tab bar's `NextSelectedTabId` — reliable
  for a docked background tab where `SetWindowFocus`/`SetNextWindowFocus` did *not* switch it. The Preview
  shows a **header** (which node's which pin + image size — a future version may float it over the image),
  then the image **fit-to-pane** (aspect-preserving, centred); it resolves the target each frame and clears
  to a hint if the source port/node is gone. Zoom/pan is a future refinement (the `lain::app` viewer).
- ✅ **Slice 3 — Issues panel + validation BUILT** (2026-07-16, live-verified 2026-07-23). The
  Issues panel is a **live validation** view: **missing-required inputs** (⚠, a Required input with no
  incoming edge) and **unused outputs** (ℹ, an active node's output with no outgoing edge) recomputed
  from the graph each frame; **load issues** (from Open, mapped from `LoadResult`, persist until the graph
  is next edited — the **load modal is retired**); and a **transient rejected-connect** row (⚠, ~4 s frame
  countdown) surfacing the previously-silent `tryConnect` failure. Each row is **click-to-locate**:
  select the node + **centre it** on the canvas (deferred `EditorContextResetPanning` computed from the
  node's drawn pos/size + the canvas size — `EditorContextMoveToNode` parks it top-right, not centred) +
  `activateWindowTab("Graph")`. Severity drives the row colour. **Boundary nodes are suppressed from the
  Inspector** (a selected GroupInput/GroupOutput shows an "edit in the Interface panel" hint).
  **Canvas navigation** (raised by the locate work): a bottom-right **MiniMap** (overview +
  click-to-navigate) and **Alt+left-drag panning** (`EmulateThreeButtonMouse`, trackpad-friendly vs
  imnodes' middle-mouse default). imnodes has **no zoom** — the mitigation is minimap + pan + centre;
  real zoom would need the **imgui-node-editor migration** (Tier B #6). **The window/panel layout pass is
  complete.**

**The pane-split refactor is complete** ([ADR-0008](docs/adr/0008-flowview-pane-architecture.md);
`inspectorwindow.{h,cpp}` → `mainwindow.{h,cpp}` + `src/panes/*`):
the 1369-line delegate became a ~130-line `MainWindow` that orchestrates one pane per file — `MenuBarPane`,
`IssuesPane`, `PreviewPane`, `InterfacePane`, `InspectorPane`, `GraphPane` — over a shared `AppContext` model
(graph-adjacent metadata + cross-pane signals + document/pending-load state), a `PreviewCache` (the per-pin
thumbnail cache), and small shared helpers (`pinkey.h`, `panes/canvasids.{h,cpp}` = `pinId`/`selectedNodes`,
`panes/imagesave.{h,cpp}` = the format+Save widget). Each pane is a plain struct with a `draw(...refs...)`
called explicitly from `onRender`; the window owns the GUI resources (gui `Context`, `PreviewCache`,
`ParamEditors`) and hands panes references. Behaviour unchanged; built warning-clean, `ctest` 289/289,
and **live-verified 2026-07-23** (the refactor's whole point being that nothing changed on screen).

### Session state — reopen the last graph, Open Recent, persistent dialog folder (2026-07-23)

The document-app conveniences, built on the serialization spine that was already there. New
**`session.{h,cpp}`** in flowview owns a `Session { lastGraph, recentGraphs, lastDialogDir }` persisted as
**`~/.flowview/session.json`** (`core::configDir`, beside the layout's `imgui.ini`) through `data::toValue` /
`fromValue` + `io::data` — i.e. flowview dogfoods its own `LAIN_SERIALIZE` + json codec for its settings,
not just for graphs. It is **convenience state, not document data**: a missing / unreadable / malformed
file is never an error, every field falls back to its default (data's `member()` is tolerant of absent
keys, so the file is forward-compatible), and writes are best-effort + logged, never surfaced.

- **Reopen on launch.** `MainWindow::onInit` restores the last graph through the *same*
  `MenuBarPane::openGraphPath` the menu uses (deferred end-of-frame swap, canvas layout replay, load
  issues into the Issues panel — one load path, no parallel implementation). A file that has since
  vanished logs once, is pruned from `lastGraph` + the recents, and leaves the blank scene standing.
  **`--example` suppresses the restore** (an explicit ask for the demo scene wins).
- **File ▸ Open Recent** — up to `maxRecentGraphs` (10) entries, most-recent-first, deduplicated, greyed
  out when empty, with a **Clear Menu** item. Labels are filenames (full path on hover); an index
  `PushID` keeps two same-named files in different folders distinct. Open *and* Save both record.
- **Dialog folder persists across runs.** `lain::gui`'s dialog seam already remembered the last folder
  within a session; it now exposes **`lastDirectory()` / `setLastDirectory()`** so an app can persist it
  (the seam itself stays settings-free — where settings live is the app's business). flowview restores it
  at startup and writes it back on shutdown, so *every* dialog (graph open/save, image bind, image save)
  resumes where the last session ended.
- **Registration order:** `FlowviewApp::onStart` now registers the node factory + codecs **before**
  `createWindow`, since the window's restore needs both.

Verified on the live driver via `--frames` runs, reading the written `session.json` back: a seeded
`lastGraph` is reopened (proved by it appearing in the recents, which only a successful load writes), a
vanished one is pruned without disturbing the rest, a malformed file and a wrong-shaped root both start
clean (the latter warns). `ctest` 289/289.

### Editable node names (2026-07-23)

A node's title is now the user's to set, defaulting to the name its type gives it — the boundary-pin
rename pattern, one level up.

- **flow core: `Node::setName`** — display only, documented as such alongside `Port::setName`. A node's
  identity is its `NodeId` (edges, handles, and the editor layout all reference that); nothing addresses
  a node by name, and core imposes no uniqueness.
- **Serialization now reads the name back.** `nodeToValue` already wrote `"name"` as informational —
  a load applies it (absent / blank leaves the constructor's name), so a title survives a round-trip.
  The node is still rebuilt from its `kind`, never its name. Save→load→save stays byte-idempotent.
- **flowview: Inspector ▸ Name** — an `InputText` on the selected node, committed on Enter / focus loss
  (`IsItemDeactivatedAfterEdit` — the field's per-keystroke return is *not* the commit signal) and only
  when the text actually changed; blank is refused. A rename arms the unsaved-changes guard but
  triggers **no re-run** and no preview refresh: it changes no value.
- **The `[id]` prefix drops from the canvas title** (as the parked note intended), leaving the user's
  name alone; the Inspector header keeps `name [id]`, since that is the detail surface and the id is
  what the cli dump names. To keep the canvas readable without it, the palette **uniquifies a new
  node's name** ("tint", "tint 2", …) in `AppContext::addCatalogNode` — the *viewer's* readability
  policy, not a core rule. A user rename may still collide; that's their call.
- **Boundary nodes are excluded** (they're a fixture, edited in the Interface panel) — consistent with
  the Inspector already declining to show their pins.

`ctest` 290/290 (a new flow-serialize case renames a node and proves it comes back renamed while an
untouched sibling keeps its ctor name); verified through flowview's own load path (a hand-renamed
`scene.json` dumps as `[2] warm tint`) and both gui modes smoke-run clean. Typing in the Name field
itself — the one part with no test coverage — was **live-verified 2026-07-26**.

### Document guard + rename correctness (2026-07-23)

Three gaps the two features above brought into view, all in the "an edit is a document change" family.

- **Open is guarded like New.** Only New asked before discarding unsaved work; **Open... and Open
  Recent replace the graph just as destructively** and did it silently. Both now funnel through one
  request path: `AppContext` carries a **`PendingSwap { DocumentSwap kind, path }`** (New, or Open with
  a path — empty meaning "ask with the dialog"), `requestNew`/`requestOpen` → `requestSwap` decide
  whether the guard is needed, and **`performSwap`** is the single place a swap is carried out. Adding a
  third destructive action later means one more `DocumentSwap` arm, not another parallel guard.
  (The startup session-restore calls `openGraphPath` directly and is deliberately unguarded — a
  just-launched document has nothing to lose.)
- **A cancelled save no longer discards the graph.** The modal's **Save** ran `saveToCurrentPath` and
  then swapped regardless — so cancelling the Save-As panel (or a failed write) threw the work away,
  the exact outcome the guard exists to prevent. `saveToCurrentPath` / `saveAsDialog` now **return
  whether the save happened**, and an unsaved Save cancels the whole swap (the macOS convention).
- **Renaming is a document change, and a boundary-pin rename is now guarded for uniqueness.** Neither
  rename path marked the document dirty (so a rename could be lost silently); both now do, without a
  re-run — a name changes no value. And `renderPinName` checked `validPortName` but **not** uniqueness,
  though `node.h` documents that "the editing layer guards a boundary-pin rename with `hasPortNamed`"
  and `addDynamicPort` enforces it at creation. Since **serialization addresses edges by port name**,
  renaming a pin onto its sibling's name made an edge ambiguous on load. The rename now applies only
  when the name is valid *and* unused on that side of the node.

### Text-surface cleanup: lain::string::format + imgui_stdlib (2026-07-25)

Retiring the last hand-rolled string plumbing in flowview, which was two different jobs wearing one
`snprintf` face:

- **Display strings → `lain::string::format`.** The Issues-panel messages (`char[192]`) and the
  Inspector node header now format through `lain::string::format` — compile-time-checked format
  strings, no fixed buffer (the `char[192]` could truncate a long node+port name), and the manual
  `static_cast<unsigned long long>` for `%llu` is gone (`{}` takes a `uint64` directly). flowview now
  links `lain::string` and dogfoods it.
- **Edit buffers → `imgui_stdlib`.** The three `char[]`-backed `InputText` sites (string/path params,
  boundary-pin rename, node-name rename) use ImGui's official **`std::string` overload**
  (`misc/cpp/imgui_stdlib.cpp`, now compiled into the `imgui` target — ships in the tarball, no new
  dep; surfaced through `lain::gui` because its overloads land in `namespace ImGui`). This removes the
  fixed 128/512-char caps (a long name silently truncated on edit) and collapses each site's
  seed-scratch-compare-copy dance to a local `std::string`. See the `lain::gui` bullet in CLAUDE.md.

After this, flowview has **no `snprintf`** — CPU text is `lain::string::format`, edit fields are
`std::string`, and ImGui's own `Text("%s", …)` variadics (its API, not ours) stay as they are.
`ctest` 290/290; both gui modes smoke-run clean.

### Undo / redo (2026-07-25)

Snapshot-based history, reusing the serialization spine — a snapshot is Save-to-RAM, a restore is
Load-from-RAM. New **`undo.{h,cpp}`** (`UndoStack`): a `std::vector<data::Value>` of document states
with a cursor, `reset`/`push`/`undo`/`redo`, bounded to `maxDepth` (100). Driver-free and unit-tested
(new **`apps/flowview/test`** target — 8 cases over the real `undo.cpp`, the app's first unit tests).

- **Snapshot = `graphio::snapshotGraph`** (`toValue` with `sceneCodecs()` + `collectLayout`), the same
  document Save writes — so it captures structure / params / node+pin names / dynamic pins / layout,
  and **nothing runtime-only** (bound boundary values aren't serialized). `push()` ignores a snapshot
  equal to the current one, so a bind-only edit records no history step (its document is unchanged).
- **Restore reuses the deferred-load path.** `MenuBarPane::undo/redo` rebuild via `restoreGraph`
  (`fromValue`) and feed the existing `loadedGraph`/`pendingLayout`/`loadRequested` swap — the one place
  the graph is replaced safely (after all panes drew). A **`pendingBaseline`** on `AppContext`
  distinguishes the two swap kinds: a New/Open carries one (→ the swap **resets** the history: undo
  doesn't cross a document change, per the locked default), an Undo/Redo carries none (→ history kept,
  cursor already moved).
- **Coalescing without per-editor plumbing.** Edits route through a new `AppContext::markChanged()`
  (dirty + a `pendingSnapshot` flag); `MainWindow` snapshots at end of frame only when
  `pendingSnapshot && !gui::IsAnyItemActive()` — so a continuous param/colour **drag** (which marks
  changed every frame) collapses to a single history entry on release, and live preview during the drag
  is untouched. This is the cheap alternative to threading a `committed` signal through every editor.
- **Baselining.** The startup graph baselines lazily on the first frame (positions live after the
  canvas draws); New/Open baseline from their document `Value` (timing-independent — a freshly swapped
  graph's imnodes positions aren't seeded until the next frame, so a `collectLayout` baseline there
  would be wrong).
- **Wiring:** an **Edit menu** (Undo/Redo, greyed by `canUndo`/`canRedo`) + **Ctrl+Z / Ctrl+Shift+Z**
  (mods match exactly, no collision). `collectLayout` moved from a menubar static to the shared
  `panes/canvasids` (Save + snapshot both use it).

**Defaults (as agreed):** node *moves* are **not** undoable (a pure reposition fires no edit signal;
snapshots still record positions, so undo/redo of a real edit restores layout); **Open resets** the
stack; drag coalescing via `IsAnyItemActive`.

`ctest` 297/297 (8 new); snapshot format stays byte-idempotent with Save; both gui modes smoke-run.
**Live-verified 2026-07-26** — the interactive behaviour (Ctrl+Z restoring, a drag coalescing to one
step, edit-after-undo truncating the redo tail) was eyeballed on the live driver; there is no test
harness at the gui layer, so this was the only way to confirm it.

**Selection survives a restore** (fix, 2026-07-26). A restore rebuilds the graph via `fromValue`, which
mints fresh `NodeId`s, and the swap's `onGraphReplaced()` clears the imnodes selection — so a param-drag
undo left the just-edited node deselected and the selection-driven Inspector empty. The selected nodes
are now captured **as ordinals into `nodeIds()`** before the restore (stable across the id remap: both
the current and restored graphs enumerate in insertion order) and re-selected after the swap. For a
structure-preserving edit the ordinal maps back to the same node exactly; New/Open still clear the
selection (they carry a `pendingBaseline`, which distinguishes the two swap kinds).

**Parked for later passes:** user-configurable / theme.json
colours (the registries are the load target) · pin **shape** encodes presence (square=required, circle=optional,
triangle=conditional — deferred: mostly-required → mostly-square is visually sharp) · **node-move undo**
(snapshot on an imnodes position-delta, coalesced on drag release) · the **window-interaction pass**
and a **`lain::gui::nodes` wrapper pass**, both described below.

**Window-interaction pass** *(placeholder — not grilled; scope is what has been deferred into it)*. The
window/panel **layout** pass settled *where* panes live (docking, splitters, tab groups, the default
layout, reset/recovery). The **interaction** pass is the sequel about *whole-layout purpose* — what each
pane is for and how panes hand off to each other, so the set reads as one app rather than six
independently-grown panels. Deferred into it so far:
- the **Interface↔Inspector merge** (deferred at UI-pass slice B, and again at the layout pass's
  "Interface + Inspector stay separate, both docked"). The tell that it is unresolved: slice 3 suppresses
  boundary nodes from the Inspector, so selecting a GroupInput shows an "edit in the Interface panel"
  hint that bounces the user to another pane.
- the **Preview header** floating over the image rather than sitting above it (layout slice 2).
- ~~**Preview zoom/pan**~~ — built in-pane 2026-07-31 (see "Preview sizing" below); doing it here does
  not preclude the rich viewer, it just meant the pane stopped being a fixed fit-to-size view. Pane
  **pop-out** remains deferred to a future `lain::app` **multi-window** rich viewer (a 3D voxel view),
  explicitly *not* ImGui multi-viewport.
Needs its own grill before building, as the layout pass got.

**`lain::gui::nodes` wrapper pass** — front the raw `Im*` surface the `namespace nodes = ImNodes` alias
leaks (`ImNodesCol_*`, `ImNodesPinShape_*`, `PushColorStyle(ImU32)`, attribute flags) with lain-typed
calls, so a client passes lain colours/enums and never touches `ImU32` — which retires `gui::packColor`
from flowview's call sites. **Not to be confused with the colour discipline, which is done:**
`gui::packColor(image::ColorRGBA8) → ImU32` exists, the palette speaks `image::ColorRGBA8` end-to-end,
and the pack happens only at the imnodes boundary. What remains is the *typed surface* — `nodes.h` is
still a bare `namespace nodes = ImNodes;`, and `panes/graphpane.cpp` still names `ImNodesCol_*` (lines
~219–232), `ImNodesPinShape*` (~261, ~278), and `ImNodesAttributeFlags_*` (~254) directly.

## Milestone 5 — group nodes / subgraphs (grilled 2026-07-29, **COMPLETE** 2026-08-11)

**All six slices built and live-verified.** A **group node** contains its own graph and exposes
selected inner ports as its own, through the *same* boundary mechanism the top-level graph uses —
"the top-level Graph is the outermost group", made real. Two kinds: an **inline group** (recipe stored in the parent document, editable in place)
and a **linked group** (recipe in an external **template** document, read-only in place, rebuilt
from the template on every load). Design locked in **[ADR-0009](docs/adr/0009-group-nodes-flattened-into-one-execution-plan.md)**
(execution) and **[ADR-0010](docs/adr/0010-inline-vs-linked-groups-no-prefab-overrides.md)**
(document model); vocabulary in [CONTEXT.md](CONTEXT.md).

**The decisions, in one place:**

- **Execution: the scheduler flattens.** One **execution plan** spans every nesting level — steps
  (`{Graph*, NodeId}` + a group **entry**/**exit** step pair) with step-index dependencies.
  `SerialScheduler` walks it; `ParallelScheduler` emplaces a task per step + a `precede` per plan
  edge. No nested runs, no subflows, so it needs nothing beyond `lain::task`'s existing
  `emplace`/`precede`/`run` — and survives the planned Taskflow → `multi` swap untouched.
  `evaluate` builds a plan too (upstream-cone mode), so there is **one** expansion mechanism and
  `GroupNode::compute()` is a no-op.
- **`Node` gains `virtual Graph* innerGraph()`** (null by default) — the scheduler asks the
  structural question, not an RTTI identity question, and it's on the hot path. `Node::dirty()`
  becomes virtual (a group is dirty if anything inside it is). The entry step **gates its publish**
  (`own dirty || an outer predecessor ran`), else an inner param edit would destroy inner
  incrementality.
- **Ports mirror inner boundary pins by identity, not name** — the group holds real `Port`s plus an
  `outerPortId → innerPortId` map, so an inner pin *rename* keeps the outer wiring. Reconciliation is
  **`edit::syncGroupPorts(Graph& parent, NodeId group)`**, not a node-local method: a vanished pin may
  have a wired outer port, and `Graph::removePort` refuses connected pins by design — so the gesture
  disconnects first, exactly like `edit::removePort`.
- **A link is a recipe reference, never a runtime share** — every group owns its own inner `Graph`
  (a `Port` holds a persistent value; sharing would race under the flat plan).
- **Format:** a **graph body** is `{nodes, edges, editor}`; a **document** is `{version, <body>}`.
  Inline embeds a body under `"graph"`; linked writes `"source"` (relative to the parent document) +
  an **interface cache** `{inputs:[{name,type}], outputs:[…]}`. `flow::serialize` recurses (RTTI
  there — the question really is *which kind*), takes an injected `source → {canonical key, document}`
  **resolver** (core does no I/O), and refuses recursive templates by canonical key.
  `LoadResult::editor` becomes an `EditorTree { EditorData nodes; map<NodeId, EditorTree> groups; }`.
  Document `version` stays 1 — the encoding is unchanged and an unknown kind already degrades.
- **Navigation is uniform:** double-click descends into *either* kind; the host tracks an **active
  graph** path (as *ordinals* into `nodeIds()`, since an undo restore remaps every id — the same fix
  the selection-preservation change used) with a breadcrumb and Back. A linked group is navigable and
  inspectable but **read-only**; an explicit **Edit Template…** does the guarded document swap (parent
  on a return stack) and states its blast radius. Inline edits mark the parent dirty and ride the
  parent's undo history for free — an inline body is part of the parent snapshot.

### Prerequisite slice — shared, immutable `PortValue` payloads ✅ (2026-07-29)

`image::Image`'s copy is a **deep pixel copy** and `populateInputs` copies a `PortValue` **per edge,
per run** — so a 10-node image chain did 10 full image copies per run, and a group boundary would
multiply it (~4 in, ~3 out, vs 1 for a plain edge). Fixed first, as a standalone no-behaviour-change
commit (the shape of the `PortId` rename commit).

**What landed:** the slot stores `std::shared_ptr<const void>` + a `std::type_index` (rather than a
`std::any` holding a `shared_ptr<const T>` — one indirection instead of two; the shared_ptr keeps `T`'s
deleter, the type_index restores the identity `shared_ptr<void>` loses). `set<T>` moves the value into
a fresh shared const allocation and **rebinds** the slot, so an earlier copy keeps the old payload and
a recompute never disturbs a value someone else is reading. `get<T>()` still returns `const T&` and
still throws `std::bad_any_cast` on a mismatch — the exception type is *deliberately* retained from the
`std::any` era so the documented contract and every caller are unchanged. Zero call-site changes across
`flow`, `flow-example`, `flow::serialize` and flowview: every existing use was already a const read, and
`TintNode`'s `image::Image src = input(m_in).get<image::Image>()` still takes its deliberate copy.

**Verified:** warning-clean strict build; `ctest` **299/299**; two new guards — a `PortValue`-level test
that copies/assignment/vector-fill share one payload address and that `set` rebinds, and a
scheduler-level test that a fan-out graph run twice performs **zero** payload copies and that each
consumer's input is *the producer's own object*. Headless `run` still flows source→tint→blur→result with
each stage's value distinct (proving no shared payload is mutated through), and save→load→save is still
byte-idempotent.

### Build order

1. **`PortValue` shared payloads** (above) — standalone, green on the existing suite + a copy-counting
   test.
2. ✅ **flow core — the group seam** (2026-07-29). `flow/group.h`: `GroupNode` / `LinkedGroupNode`
   (a thin subclass — the kinds differ only in what they serialize and whether in-place editing is
   refused, so port mirroring / plan expansion / dirty propagation are written once), the
   outer↔inner `PortId` map, and the interface cache as `PinSpec {name, typeKey}` lists (typeKey is
   a port-type-registry key — already core — so an unresolved link builds real placeholder pins).
   `Node::innerGraph()` (virtual, null by default) + virtual `Node::dirty()`.
   **Boundary-pair invariant**: `Graph`'s constructor mints one `GroupInputNode` + one
   `GroupOutputNode` through a private `adopt()`, `removeNode` refuses either, `add` refuses a
   second (returning the null `NodeId`, as `addDynamicPort` does), and `boundaryInputNode()` /
   `boundaryOutputNode()` now return **references** — which also retired the RTTI scan in all four
   boundary accessors (the ids are cached members) and the null checks in `interfacepane.cpp`.
   `buildNewScene` was **deleted** rather than left as a no-op: a fresh `Graph` *is* a blank
   document, and a do-nothing builder callers must remember to call would imply otherwise.

   **The loader-reuse rule moved here from slice 5** — it isn't optional once the invariant exists.
   Boundary nodes are factory-registered and appear in documents as ordinary nodes, so `fromValue`
   now **adopts** a document's `groupInput`/`groupOutput` onto the graph's existing pair (the pair is
   pinless, so the document's dynamic pins replay onto it exactly as onto a fresh node) instead of
   adding a duplicate that `add` would refuse — which would have dropped every edge touching it. A
   second of either kind in one document warns and merges.

   Verified: warning-clean; format-clean; `ctest` **306/306** (+7: the invariant's refusals, and
   `test_group.cpp` over ownership / non-sharing / dirty propagation incl. a nested group / the id
   map surviving a rename / the linked variant's source + cache). Headless `run` still flows
   source→tint→blur→result, `list` reports the boundary, and save→load→save is byte-idempotent with
   the loaded graph holding **4** nodes, not 6. Churn was as predicted: `nodeCount()`/`topoOrder`
   assertions now read `kBoundaryNodes + N`, and tests that added their own boundary nodes use
   `graph.boundaryInputNode()`.
3. ✅ **Scheduler — the execution plan** (2026-07-29). A `Plan` is `{steps, edges}`: a `Step` is
   `Node` / `GroupEntry` / `GroupExit`, each carrying the `Graph*` it belongs to, and edges are
   step-index pairs. `expand()` flattens one level and recurses through anything answering
   `Node::innerGraph()`, so a group becomes *entry → its inner graph's steps → exit*; the level's
   own edges are then wired between each node's **consuming end** (entry, for a group) and
   **producing end** (exit). `SerialScheduler` walks `plan.steps` (already a valid serial order —
   every dependency points backwards); `ParallelScheduler` emplaces a task per step and a `precede`
   per edge, across **every level at once**. `buildRunPlan` = the dirty closure; `buildEvalPlan` =
   the dirty *upstream cone* (deliberately not a closure — that is the long-standing pull
   semantic), so `evaluate` crosses groups through the same one mechanism.

   Two subtleties, both proven load-bearing by deliberately breaking them and watching a test fail:
   - **Publish gating.** The entry step republishes into the inner boundary only when the group's
     inputs can have changed (`selfDirty()`, or an outer predecessor was selected). Without it, an
     edit inside a group re-ran the *whole* inner graph (`fedByBoundary == 2`, want 1) — inner
     incrementality gone. `Node::selfDirty()` was added to keep "my own flag" apart from the
     virtual aggregate `dirty()`.
   - **Pre-marking the inner boundary dirty at plan time.** When publishing, the inner
     `GroupInputNode` must be marked dirty *before* the inner closure is computed, or it is planned
     out and the published values land on a node that never republishes them — a re-bind then
     silently served the stale result (`105`, want `107`).

   `Node::innerPin(PortId)` joins `innerGraph()` as the second half of the structural seam, so the
   entry/exit steps map outer ports to inner pins **without casting to a concrete group class** —
   which is what keeps ADR-0009's "a future graph-containing node needs no scheduler change" true.
   `GroupNode::exposeInput/exposeOutput<T>` are the mirroring **primitive** (slice 4's
   `edit::syncGroupPorts` is the gesture over them).

   Verified: warning-clean; format-clean; `ctest` **312/312** (+6 in `test_nestedrun.cpp`: value
   crossing in/out + re-bind, two-level nesting, **serial/parallel equivalence** over sibling
   groups, suppression crossing the boundary with no extra machinery, the publish-gate incremental
   cases, and the pull path crossing a group). The `[group]` tests ran **100×** with zero failures
   to shake out ordering races. Flat graphs are unchanged: the whole pre-existing suite passes and
   the headless `run` / idempotence checks are identical.
4. ✅ **`edit::syncGroupPorts`** (2026-07-29) — reconcile a group's outer ports against its inner
   boundary pins, in the order **remove → rename → add** (so a name freed by a removal or a rename
   is available to a new pin in the same pass). Returns a `GroupSync {added, removed, renamed,
   disconnected}`; `disconnected` is the count a host must surface — those are the *parent's* edges
   a vanished pin took with it. Identity is the outer↔inner `PortId` map, so a renamed inner pin
   keeps its outer port **and its wiring** and is merely retitled.

   **Mirroring needs no port-type registry and no `T`.** A `Port`'s type is a shared `PortType`
   flyweight, so `Node::addInputLike/addOutputLike(name, const PortType&)` + `Port::portType()` copy
   the pointer and `GroupNode::exposePort(side, innerPin)` mirrors *any* type — including one
   registered nowhere. That is the right rule: a group's ports are **derived**, not user-chosen, so
   they must not be limited to the registered set the way an addable dynamic pin is. (This retired
   the worry that `GroupNode` would have to become a `DynamicPortsNode` — it can't, since
   `dynamicSide()` names one side and a group grows both.)

   **Hazard found and pinned down:** a `PortId` is minted **per node**, so the inner GroupInput's
   first pin and the inner GroupOutput's first pin are *both* `PortId{1}`. The map's key (an outer
   id) is unique, but its **value is not** — a value is only meaningful together with the outer
   port's *direction*, which says which boundary node to resolve it against. Every reader does that
   (the scheduler's entry/exit steps, sync's `innerPinFor`), and a test now wires an inner graph
   **crossed** (a→q, b→p) so a mix-up shows up as swapped values instead of passing by luck.

   Verified: warning-clean; format-clean; `ctest` **320/320** (+8 in `test_groupsync.cpp`: mirroring
   name+type, idempotence, rename-keeps-wiring, removal reporting the two edges it cut, same-name
   replacement, end-to-end run through a synced group, no-op on a non-group, an unregistered type,
   and the id-collision crossing). Sabotaging sync's direction resolution is caught by the
   idempotence assertion — the scheduler's own resolution is what the crossing test guards, so the
   two paths have separate guards.
5. ✅ **`flow::serialize` — recursion, resolver, cycle guard, `EditorTree`** (2026-07-29). The format
   splits as designed: a **body** is `{nodes, edges, editor}`, a **document** is a body plus
   `{version}`. `toValue`/`fromValue` are thin wrappers over `bodyToValue`/`loadBody`, which recurse.
   An inline group embeds a body under `"graph"`; a linked group writes `"source"` + `"interface"`.
   `LoadContext` carries factory/codecs/resolver/issues plus the in-flight canonical keys down the
   recursion. (Boundary-node reuse landed early, in slice 2, which forced it.)

   - **A group's own ports are never serialized.** They are re-derived by `edit::syncGroupPorts` from
     the rebuilt inner boundary, *before* the level's edges resolve — which is exactly what lets the
     parent's name-addressed edges land again. One source of truth, not two.
   - **`TemplateResolver`** = `source → optional<ResolvedTemplate{key, document}>`, injected because
     core does no file I/O. **No resolver is a legitimate mode, not an error**: every linked group
     then loads unresolved from its cache — which is what `flowview list` wants.
   - **Unresolved is a first-class state.** The cached pins are rebuilt onto the inner *boundary
     nodes* (they are `DynamicPortsNode`s) and mirrored outward by sync, so a placeholder is a real
     empty graph with the right face rather than a special case downstream. A test proves re-saving
     an unresolved link is **byte-lossless**, so a broken link is repairable rather than destructive.
   - **Rectification** diffs the cache against the resolved template and reports a vanished or
     retyped pin as a `LoadIssue` — the user is told, instead of finding wiring missing later.
   - **`EditorTree { nodes, groups }`** replaces the flat `EditorData` on `LoadResult`, re-keyed
     recursively. An implicit `EditorData → EditorTree` conversion kept every save-side call site
     unchanged; only two flowview lines and three test lines needed `.nodes`.

   Verified: warning-clean; format-clean; `ctest` **326/326** (+6 in `flow/serialize/test/test_group.cpp`:
   inline round-trip + byte-idempotence, two-level nesting, the nested editor tree, linked-with-resolver
   running the template's interior, unresolved-keeps-its-face-and-wiring + lossless re-save, rectification
   reporting, and the recursion refusal). **The cycle guard was verified by removing it: a self-linking
   template SIGSEGVs** (unbounded recursion), so the guard is load-bearing, not decorative. Flat
   documents are byte-identical to before — the real scene's `run` / `list` / idempotence are unchanged.
6. ✅ **flowview — navigation + the palette entries** (built 2026-07-29; **gui-mode NOT eyeballed —
   no Metal in this sandbox**). *(**Superseded** — that eyeball happened across 2026-07-29 → 07-31,
   and the last two items were confirmed 2026-08-11; every gui item in this slice is live-verified.
   See the end of this slice.)* New **`groupnav.{h,cpp}`**: a `GraphPath` (the group nodes descended
   from the root), `resolvePath` (tolerant — truncates a path that no longer resolves, so a deleted
   group degrades to its parent), `breadcrumb`, `editableAt` / `enclosingLinkedGroup`, and the
   `EditorTree` subtree accessors.

   **One retarget point:** `MainWindow` resolves the active graph once and hands it to every pane, so
   descending moves the canvas, Inspector, Interface, Preview and Issues together. Canvas: breadcrumb
   + Back, **double-click a group to descend** (the same gesture for both kinds), a
   `[linked - read only]` marker, and mutations gated **per gesture** rather than as one block — so
   the read-only affordances (pin tooltips, following links) keep working, which is the whole point
   of letting you navigate into a linked group. `Add ▸ Groups ▸ group` is a palette entry;
   `Add ▸ Linked Group…` picks a template (path stored **relative to the current document**),
   resolves it immediately so the node arrives with its interior and ports, and reports issues.
   `Group ▸ Edit Template…` reuses the existing guarded `PendingSwap` to open the template *as the
   document*. The Interface pane below the root edits the interface and **hides both binding arms**.

   **Two bugs found in this slice's own code, both fixed:** the layout capture used the *current*
   path, but a pane can navigate mid-frame — so positions are now captured into the path that was
   actually **drawn**; and `addCatalogNode` added to the **root**, so the menu/palette would have
   dropped nodes into the wrong graph while descended (it now targets the active graph and refuses
   inside a linked group, with callers respecting the refusal).

   **Layout is now a tree, kept in `AppContext::layout`.** imnodes only knows the level on screen, so
   each level's positions are captured as it is shown and retained while others are displayed; Save
   and the undo snapshot read the tree rather than imnodes. A navigation forces a re-seed and clears
   the selection, because **imnodes keys node state by the int of a NodeId and ids repeat across
   levels** — without it an inner node would inherit a same-numbered root node's position.

   **Verified headlessly, end-to-end through the real app** (hand-written documents, `flowview run`):
   a linked group **resolves its template, runs its interior, and delivers through the parent's
   boundary** (`TL=(0,0,64,255)`, matching the flat scene); the interface cache is **refreshed on
   save**; a **missing** template loads unresolved with its wiring intact and re-saves
   **byte-losslessly**; a **recursive** template is refused cleanly. Flat documents are unchanged and
   still idempotent. `ctest` **338/338**, warning-clean, format-clean.

   **Fixed after the first live click** (`Add ▸ Group` crashed): a node whose body submits **no
   item** trips ImGui's *"SetCursorPos used to extend window/parent boundaries"* assert
   (imgui.cpp:11675) — imnodes positions the body with `SetCursorPos`, and an empty group has nothing
   to grow it. A fresh group is the first node kind with **zero pins**; the pinless *boundary* nodes
   only escaped it because their `±` buttons happen to be items. The canvas now submits a placeholder
   (`(empty - double-click)`) for **any** pin-less node, and **`libs/gui/test` gained a headless
   regression test** — ImGui + imnodes run fine with no backend, so a real frame is drawn in-suite;
   remove the placeholder and it aborts with that exact assert.

   **Also found while checking what happens after that crash:** `syncGroupPorts` was only ever called
   when *adding* a linked group — so editing a group's interface from inside (the Interface panel's ±
   or a rename) would never have reached its outer ports, and the feature would have looked broken.
   `groupnav::syncPathGroups` now re-derives the ports of every group on the active path each frame;
   it is idempotent, so rather than enumerating the many paths that can mutate an inner boundary, the
   host simply reconciles the ones it is inside.

   **Third live bug — the id collision, striking exactly where it was documented.** Adding an image
   input *and* an image output inside a group left the OUTPUT port missing on the group's face.
   `syncGroupPorts`'s "already mirrored" set was a single, direction-agnostic `set<PortId>`, and since
   ids are minted **per node** the inner GroupInput's first pin and the inner GroupOutput's first pin
   are both `PortId{1}` — so the input's mapping masked the output pin and it was never exposed. The
   sets are now **per direction**. Note this is precisely the hazard written up on `portMap()` in slice
   4; documenting it did not prevent using it wrongly one function later.

   **Why the suite missed it, which is the more useful lesson:** every existing test added its pins
   *before* a single sync, so the pre-loop snapshot was empty for both sides and the collision never
   bit. The bug needs a sync **between** two adds — the interactive ordering, and exactly what the
   host's per-frame sync produces. There is now a test in that ordering (add → sync → add → sync),
   which also runs a value through both ports; it fails with the old single set.

   **Fourth live bug — navigation consumed its own signal.** A node inside a group jumped to the
   *group node's* position when navigating via the breadcrumb. `GraphPane::draw` drew the breadcrumb
   and then, ten lines later, consumed `pathChanged` — so a click was consumed **in the same frame it
   was made**: positions were re-seeded for the level being *left*, and the level being *entered* drew
   with `m_laidOut == true` and no seeding at all, inheriting imnodes state by node id (inner `tint`
   is id 3, the same imnodes id as the group at root). Double-click descent escaped it only because
   that gesture is detected *after* `EndNodeEditor`, so its flag survived to the next frame. The
   request is now consumed by `MainWindow` at the **start** of a frame, before the path is resolved
   (`GraphPane::onNavigated`), so every navigation source behaves identically.

   **`apps/flowview/test` gained `test_groupnav.cpp`** (7 cases over the real `groupnav.cpp`) — the
   navigation model had produced two bugs and had no tests: path resolution + the three ways a stale
   path truncates, breadcrumb labels/depths, linked-group editability (including below a link),
   per-level layout storage *with deliberately colliding ids*, and the per-frame sync carrying an
   inner interface out. The frame *ordering* itself still needs a live driver — it lives in
   `MainWindow`.

   **Fifth live report — undo ejected you to the root.** The swap handler ran `navigateTo({})` for
   *every* swap, so undoing an edit made inside a group threw the user out to the root, away from the
   thing they had just undone. A restore now carries the active path as **ordinals** (the same trick
   the selection already used, applied per level, since a restore mints fresh NodeIds):
   `groupnav::pathOrdinals` / `pathFromOrdinals`, held in `AppContext::pendingPath`. New/Open still
   land at the root — there the *document* changed — and clear it. The path is assigned directly
   rather than through `navigateTo`, because `onGraphReplaced` has already asked for the re-seed and
   raising `pathChanged` would clear the selection again next frame, wiping the reselect.

   **Two further bugs found while fixing it, neither reported:**
   - **The selection reselect used the wrong graph.** It captured ordinals into the **root's**
     `nodeIds()` while the canvas selection holds *active-graph* ids — and since ids repeat across
     levels, undoing inside a group would silently select whichever root node shared a number. Both
     capture and re-apply now resolve against the active graph.
   - **Loading discarded every nested level's layout.** `pendingLayout` became an `EditorTree`, but
     both load paths still assigned `result.editor.nodes` — which implicitly converts to a tree with
     *no* subtrees, so a document's inner positions were dropped on open and on every undo. Both now
     move the whole tree.

   `apps/flowview/test/test_groupnav.cpp` grew the remap case: two structurally identical graphs whose
   group has **different ids** (one churns a node first), proving ordinals track the logical node and
   not a coincidence — plus a group the restored document no longer has, and an ordinal naming a
   non-group. Sabotaged to confirm it bites.

   **Sixth report — `Add ▸ Linked Group…` and `Group ▸ Edit Template…` were never wired up.**
   `addLinkedGroup` / `editTemplate` were implemented and declared but **never called**: the edit
   meant to add the menu items failed to match its anchor and was silently dropped (a no-op
   `str.replace`), and nothing warned, because an out-of-line definition of a declared member is not
   "unused". Both menu entries now exist and are verified present in the source. Editing tooling
   lesson: assert that a scripted replacement matched, and grep for the call site afterwards — a
   feature can compile, link, and ship while being unreachable.

   **A real bug in that dead code, found on review before it ever ran:** `editTemplate` looked its
   node up in the **root** graph, but `enclosingLinkedGroup` can return a link nested inside an inline
   group. And ids restart per `Graph`, so the root lookup did not come up empty — it silently found a
   **different node** (the wrapper), whose `dynamic_cast` then failed and the menu did nothing.
   `enclosingLinkedGroup` now returns the **node**, not an id; a test pins the collision down
   explicitly (`root.contains(nestedId)` is *true*, and resolves to the wrong node).

   **Seventh report — the serious one: Save wrote the VIEW, not the document.** `MenuBarPane::draw`
   receives the *active* graph (slice 6 retargeted every pane to it) and Save / Save As / the
   unsaved-changes modal all wrote **that**. So descending into any group and saving — including
   answering "Save" to the guard that `Edit Template…` raises — overwrote the document with just that
   group's interior, flattened, with a single level of layout. **Data loss.** Saving is a *document*
   operation and must never follow the view: it now always writes `ctx.app->graph()` plus the whole
   `ctx.layout` tree, with the active graph used only to refresh the on-screen level's positions
   first. This is the direct cost of the one-line "retarget everything to the active graph" — the
   panes were right to follow the view; Save rode along and should not have.

   **Also reported: a linked group's nodes sat in default columns.** The template's own `editor`
   section was being discarded on load (`EditorTree ignored`). It now comes with the template, into
   that group's subtree — a linked group is read-only, so the template author's arrangement is simply
   the truthful view of it, and there is no divergence to manage. Covered by a serialize test
   (sabotage-checked).

   **Return stack added** (2026-07-31) — the piece ADR-0010 specified and slice 6 shipped without, which
   made `Edit Template…` a one-way door. `AppContext::returnStack` holds the documents to come back to
   (a stack, since a template may itself link one); the entry is pushed/popped when the swap is
   **carried out**, not requested, so a cancelled guard leaves it untouched *and* a Save answered in
   the guard has already given an untitled document a path. The return is itself a guarded Open, so
   unsaved template edits are protected. Two affordances: `Group ▸ Return to <file>` and a `< <file>`
   button on the breadcrumb line, where the rest of the navigation lives (the canvas raises a request;
   the menu bar, which owns document swaps, consumes it later in the same frame). An untitled document
   says so instead of silently having no route back, and an unrelated Open clears the stack.

   **This is also what delivers "edits reach every instance"** — the thing that looked like it needed
   inline editing. Returning re-opens the parent, which re-resolves every linked group from the
   template, so all instances update at once. Verified headlessly: editing a template's boundary pin
   and reopening the parent picks up the change, refreshes the interface cache, and — once a real
   cache exists — reports the rename through rectification.

   **Layout on first add** — `Add ▸ Linked Group…` resolved the template but kept only `loaded.graph`,
   dropping `loaded.editor`, so a freshly added group's interior sat in default columns until the
   document was saved and reopened (at which point the *load* path, which does carry the layout,
   appeared to "discover" the positions). It now writes the template's layout tree into that group's
   subtree, the same rule the load path follows.

   **A pattern worth naming, since it has produced three bugs here:** *two paths do the same job and
   only one gets updated.* `syncGroupPorts` was called when adding a linked group but not when editing
   an interface; `result.editor.nodes` was assigned into an `EditorTree` in both load paths after the
   type became a tree; and now the template layout was carried by `loadLinkedGroup` but not by
   `addLinkedGroup`. All three are the app-side and serialize-side halves of "resolve a template into a
   group" drifting apart. Worth collapsing into one shared routine before the next change to that rule.

   **Previews showed the wrong level's images.** Inside a linked group, one node had input/output
   thumbnails and its neighbour had none. Two compounding causes: a **`PinKey` carries no level**
   (`{node id, direction, port id}`, and node ids repeat across levels), and **navigation never marked
   the preview cache dirty** — only edits and graph swaps did. So descending left the ROOT's textures
   in the cache, and inner pins were looked up against them: a colliding key returned *another graph's
   image*, a non-colliding one returned nothing. The visible symptom was the missing thumbnail; the
   worse half was the thumbnail that *was* there being wrong. Navigation now clears the cache, marks it
   dirty, and drops `previewTarget` — done at the START of the frame, before `newFrame()`, which is
   also the safest point to release descriptors (no draw data references them yet).

   **Residual — SINCE FIXED by M6 step 5.** `ctx.saveFormat` was also keyed by `PinKey` and persisted
   across navigation, so a same-numbered pin at another level could inherit a format choice. The note
   said the key should gain a level rather than relying on "only one level is cached at a time"; that
   is exactly what M6 step 5 did — `PinKey` is now `{GraphPath path, PortAddress port}`, and
   `saveFormat` is keyed by it like every other consumer.

   **Breadcrumb bar reworked** (2026-07-31) — navigation and document identity now live in one strip,
   and the `Group` menu is retired:

       parent.json < boof.json * / denoise / sharpen                            [Edit boof.json]

   - **The first crumb names the DOCUMENT**, not `root` — which file is open was otherwise shown
     nowhere in the app — with `Untitled` when unsaved and a `*` for unsaved changes. That single
     change also gives flowview the document-status indicator it never had.
   - **Two separators, because the two halves cost different things.** `<` precedes a *document* crumb
     (not loaded; clicking it is a swap that may prompt to save); `/` precedes a *graph level within
     the open document* (already in memory; clicking it is a free view change). Clicking an outer
     document crumb unwinds several templates at once, so the return stack became depth-based.
   - **The parent crumb IS the back button**, so the separate `[back]` control is gone.
   - **`Edit <template>` is right-aligned on the same line**, appearing only inside a linked group —
     directly beside the `[linked - read only]` marker, so the strip states the constraint and offers
     the way out of it. Its tooltip carries the **blast radius** ("used by N linked group(s) here"),
     which the grill promised and the menu never delivered.
   - **`Group ▸ Edit Template… / Return to …` deleted.** Both actions were contextual, so a menu that
     is greyed out almost always was the worse discovery path.

   *Deviation, stated:* the separator is ASCII `<` rather than `‹` — the default ImGui font's glyph
   range stops at U+00FF, so `‹` would draw as a missing-glyph box. Adding a font range for one
   character wasn't worth it; say the word if the real glyph matters.

   **Read-only presentation reworked, and an enforcement hole closed** (2026-07-31). The
   `[linked - read only]` text left the breadcrumb — it was a *mode* statement in a *location* strip,
   competing for width with the Edit button — and the state is now shown three ways, each doing a
   distinct job:
   - **A muted `LINKED - READ ONLY` watermark** at the canvas' bottom-left: the state belongs where the
     gestures happen. Deliberately not a background tint — `canvasstyle` already uses dimming to mean
     "this node did not run", so a dimmed canvas would collide with an existing meaning.
   - **A transient message when an edit is actually attempted** ("This group is linked - edit its
     template to change it"), on the existing `recentIssue` fade. A refusal that silently does nothing
     reads as a bug; this fires exactly when the user's expectation is violated. Wired to all five
     canvas gestures (pin ±, link detach, link create, Delete, right-click add). The imnodes queries
     are now made *regardless* of editability and branched after — leaving them unread would surface a
     stale answer on the next graph that is editable.
   - **Disabled, not hidden, controls** in the Interface and Inspector panes.
   - **Node LAYOUT is read-only too** (`nodes::SetNodeDraggable(id, editable)`, set every frame since
     editability changes with navigation). imnodes drags nodes entirely inside itself, so gating our
     edit *gestures* never touched it — a drag was accepted, captured into the parent document, and
     then silently overwritten by the template's own positions on the next load. Attempting one raises
     the same transient message; Alt+drag (canvas panning) is excluded, since panning is a view action
     and stays available.

   **The hole:** read-only was enforced **only on the canvas**. The Interface pane gated *binding* on
   being at the root but not its ± / rename / ×, and the Inspector gated nothing — so inside a linked
   group you could add a boundary pin or edit a param, have `syncPathGroups` dutifully carry it to the
   group's face, and lose it silently on save (a linked group stores only `source` + the cache), or end
   up with a cache disagreeing with the template. Same family as the Save-wrote-the-view bug: a rule
   applied in one place and not the others. Both panes now wrap their editing widgets in
   `BeginDisabled(!editable)` — greyed out and inert, but still readable, which is the whole point of
   being allowed to look inside a linked group.

   **Live-driven by the repo owner (2026-07-29 → 07-31), and that is where every bug above came from.**
   Exercised on the Metal driver: `Add ▸ Group`, descending, adding boundary pins from inside, wiring
   an inner node, breadcrumb navigation in and out, undo across a group boundary,
   `Add ▸ Linked Group…`, `Edit Template…`, previews inside a linked group, and node dragging there.
   **Ten bugs in about that many sessions** — and the engine slices (1–5) produced none of them. Every
   one sat in a gui seam headless verification cannot reach: imnodes' assert contract, gui-frame
   ordering, the id-collision rule under interactive (rather than batch) ordering, a rule enforced in
   one pane and not its neighbours, and a menu item that compiled while being unreachable.

   **Each fix was driven as it landed**, not batched to the end — which is why the reports arrive in a
   chain (a crash, then the port it hid, then the positions, then undo, then the previews). The
   breadcrumb / read-only rework was exercised too: the document crumbs, `Untitled` + dirty marker, the
   right-aligned `Edit <template>` button, the return stack and the disabled Interface / Inspector
   controls were all in use before the watermark was reported as too pale.

   **The last two changes — the watermark's new tone + size (`CanvasStyle::readOnlyMark`, ~1.8x) and
   making node LAYOUT read-only (`SetNodeDraggable` + its transient message) — were confirmed
   2026-08-11.** With that, **every gui item in slice 6 is live-verified** and nothing from the M5 pass
   is outstanding.

### Preview sizing — thumbnails fit their pane (2026-07-31, live-verified)

The Inspector's **Preview size** dropdown is gone: with a Preview pane doing the full-size view, a
three-step thumbnail setting was redundant configuration. Thumbnails now fit the pane they are in.

- **A single `previewFit(extent, box)`** (appcontext) is the one place the arithmetic lives — a list
  thumbnail and the Preview pane's fit-to-pane are the same operation with a different box. The list
  box is `{pane width, kThumbnailMaxHeight}`: a **height** cap bounds the size in practice, and width
  follows from the aspect ratio except for a panoramic image, which the pane width then catches.
- **It fixes a long-standing distortion.** The Inspector and Interface drew `gui::Image(tex, {side,
  side})` — every non-square image was squashed into a square. Only the Preview pane preserved aspect,
  and its hand-rolled fit is now the shared helper.
- Scales **up** as well as down, so a small image still fills a list row and the rows keep an even
  rhythm (a `min(scale, 1.0)` away if that reads worse than it sounds).
- Removed with it: `PreviewSize`, `previewExtent`, `AppContext::previewSize`. Nothing persisted it.
  **Note:** this was the only *live* use of `lain::gui::enumCombo` — the rest is a headless smoke test
  — so the `lain::meta::enums` dogfooding goes with it. Deliberate: UI shouldn't be kept alive to
  exercise an API.

**Zoom / pan in the Preview pane** (2026-07-31, live-verified). The pane was a fixed fit-to-size view;
it now opens at fit and zooms.

- **`minZoom = min(fitScale, 1.0)`**, so ACTUAL SIZE is always reachable: an image larger than the pane
  fits below 1 and 1:1 is a zoom *in*; a smaller one is magnified by fit and zooming *out* bottoms out
  at 1:1. You can never zoom out past "fit or actual, whichever is smaller". Max 16x.
- **Fit is a MODE, not a value** — while it holds, the view re-fits as the pane resizes; it releases the
  moment the user zooms deliberately, and the `Fit` button re-enters it. Changing the previewed asset
  resets to fit (a 12x zoom means nothing on a different image).
- **The image lives in a child window** with scrollbars, so ImGui handles clipping, scroll range and the
  scrollbar affordance; panning is a drag translated into `SetScroll*`, and `NoScrollWithMouse` keeps
  the wheel for zoom. **Wheel zoom is anchored on the cursor** — without that, zooming in walks the view
  off whatever you were looking at.
- Toolbar **below the image**: `Fit`, `1:1`, and a **logarithmic** percent slider (zoom is
  multiplicative; a linear slider spends most of its travel in the high end). Under the content because
  the header already carries node/port/dimensions — one row of chrome before you see anything instead of
  two — and a full-width slider along the bottom reads as a scrubber. Its height is reserved out of the
  image area *before* either draws, so the placement is a reorder rather than a re-layout, and the
  percentage describes the area it is reporting on rather than lagging a frame. (A slider edit lands on
  the next frame's image; invisible during a continuous drag.)
- Pan is a plain left-drag, unlike the graph canvas' Alt+drag — there a plain drag is box-select, here
  nothing else wants it.
- **Known limit:** the texture keeps the app's sampler, so past 1:1 the magnification is filtered rather
  than blocky. Crisp pixel-peeping would need a nearest-sampled descriptor — a separate piece.

### Deferred (designed, not built)

- ✅ **`Group Selected`** / **`Ungroup`** / **`Save as Template`** / **`Make Local`** — designed here,
  **built 2026-08-11**; see "Group authoring gestures" after Milestone 7 for the landing notes.
- **Prefab overrides** — per-instance divergence from a template. Needs a template-stable inner-node
  address + conflict rules; ADR-0010 explains why parameterising via boundary pins is preferred.
- **Plan caching** across runs; **file-watch** on templates (the manual *Reload Linked Groups*
  gesture named here as the prerequisite shipped in M7 slice 3, 2026-08-11);
  a **registered node-serializer seam** if third-party structural node kinds ever appear.

## Milestone 6 — definition & evaluation (grilled 2026-07-31 → 08-01, **COMPLETE** 2026-08-10)

**All five steps built and live-verified.** `flow` kept a graph's *recipe* and its *run state* in the same
objects: `Port` owned a `PortValue`, `Node` owned `m_dirty`. Three pressures converged on that —
identity that kept needing composition, undo that needed positional ordinals, and a target workload
(N video streams through one subgraph; a Loop node) that the model cannot express at all. Decisions in
**[ADR-0011](docs/adr/0011-node-identity-is-a-uuid.md)** (identity) and
**[ADR-0012](docs/adr/0012-definition-and-evaluation.md)** (the split); vocabulary in
[CONTEXT.md](CONTEXT.md).

**What it is, in one line each:**

- A **definition** is the recipe (kinds, params, edges, declarations); an **evaluation** is one
  graph's runtime state — its values and bookkeeping. `run(const Graph& definition,
  Evaluation& evaluation)` updates it.
- An `Evaluation` is a **value the host owns**, so retention is ownership — cli drops it after its
  invocation, gui keeps updating one, and a bounded pool is simply how many it retains. No retention
  policy type.
- **A host owns a definition and its Evaluation as one replaceable unit.** An Evaluation survives
  in-place versioned edits; load/New/undo replaces both together, because per-node versions restart on
  a rebuilt Graph. Matching UUIDs preserve document/UI identity, never runtime version observations.
- An `Evaluation` is a **tree**; one is located by an **`EvalPath`** coordinate (`{NodeId, index}`
  steps), because a map retains one child per element. It is not a historical run id.
- **`compute(NodeEvaluation&) const` takes its evaluation context.** The scheduler reads a
  `const Graph&`; parallel map means anything mutable stored on the node would be shared across
  concurrent runs of it, so all run mutation goes into the evaluation. `const` means **concurrently
  readable**, so the definition keeps no lazy caches. `Port` is left as pure declaration — readiness
  and value description follow the values out of it.
- **Preparation precedes dispatch.** `Evaluation::prepare(definition)` creates/prunes and stabilises
  graph-shaped storage on the coordinator thread. Tasks receive disjoint, already-existing
  `NodeEvaluation` views. The invariant: *a coordinator grows evaluation storage, a worker task never
  does.* One Evaluation cannot be scheduled twice at once; distinct ones — including two over the same
  definition — may run concurrently.
- **Staleness is a version comparison** — per-node version on the definition vs what the evaluation
  computed. Invalidation is *pulled*, never *pushed*: an edit cannot reach host-owned evaluations, and
  must not cost anything per stream.
- **`NodeId` wraps a `core::Uuid` (v7)**, minted at creation. `PortId` stays a per-node counter.
  **Load preserves identity; paste mints it.**

### Build order

Each step stands alone and leaves the suite green.

1. ✅ **`core::Uuid` + `NodeId` — BUILT** (2026-08-02; **live-verified 2026-08-03**). New std-only `Uuid` in `lain::core` (generate v7, parse *any*
   well-formed UUID, format canonical, compare, hash). `NodeId` wraps one. Serialize as a string; drop
   the canonical `1..N` renumbering; document load **preserves** ids while paste/copy mints fresh ones,
   and a duplicate is re-minted + reported. This is schema **v2**. The public loader routes documents
   by version; a private `version1.cpp` migrates the old numeric-id `Value` DOM to v2 — nodes, edge
   endpoints, and editor keys in both halves of the `EditorTree` (node layout *and* group subtrees),
   recursively through inline bodies — then the sole v2 graph decoder runs.
   `toValue` writes only v2. Linked template documents enter the same router instead of bypassing the
   version gate. Fixed v1 fixtures prove migration; deleting v1 later means deleting that translation
   unit, its dispatch case and fixtures.
   Keep ids immutable after admission. The v2 decoder stages `{id, kind, node DOM}` headers, takes the
   first valid GroupInput/GroupOutput UUIDs, constructs `Graph{BoundaryIds}`, replays their definitions,
   then calls `add(node, requestedId)` for remaining nodes in array order; the ordinary overload mints.
   Duplicate later ids are re-minted + reported, while a second boundary is reported and skipped — no
   post-insertion re-key and no ambiguous merge into the canonical pair. A document missing either
   boundary node mints that id and reports an issue rather than failing: the pair is a graph invariant.
   Keep identity lookup and ordering separate: the existing id-keyed owner gains an explicit
   insertion-order `vector<NodeId>`; `nodeIds()` returns that order, and the JSON `nodes` array restores
   it without a second order field. Topological order stays independent but must now *seed* from that
   vector — map order was ascending-integer and becomes arbitrary, and topo order drives both serial
   execution and the canvas's default columns. Retire `pathOrdinals` /
   ordinal `pendingReselect` (a restore now preserves identity). Replace every imnodes encoding at once
   with a document-lifetime **`CanvasIds`** owned by `AppContext`: bidirectional, monotonically
   allocated `int`s, never recycled, so one int names one object for the document's life. It keys
   `NodeId`, `PortAddress` and edges — an edge needs no new identity, being its destination
   `PortAddress` (inputs are single-source). It survives edits, navigation and UUID-preserving
   undo/redo, and resets only for New/Open/document replacement. This replaces node
   casts, `pinId` / `decodePin`, edge-index link ids, selection, locate and layout calls through the
   production canvas path; canvas ids never enter JSON. **Keep the per-navigation position re-seed and
   selection clear**: imnodes destroys an unsubmitted node's data and frees selection pool indices
   without pruning them, so those hazards sit below the id layer and unique ids do not address them.

   **What landed, and the three places it deviates from the plan above:**
   - `libs/core/uuid.{h,cpp}` + 10 tests; `flow` now links `lain::core` PUBLIC (types.h names
     `core::Uuid`). `Graph` gains `BoundaryIds`, `add(node, requestedId)`, a private `usableId`
     (one rule for "null means mint" and "a duplicate is re-minted"), and `m_order`; `nodeIds()`
     returns that vector by const reference. `topoOrder` seeds from it, which reproduces the old
     ascending-counter order exactly rather than merely being deterministic.
   - `flow::serialize` splits into a **version router** (`loadDocument`) over the sole v2 body
     decoder (`loadBody`), which now **returns** a Graph instead of filling one — a graph's boundary
     ids are fixed at construction, so an inline group's inner graph is move-assigned from the load
     rather than re-keyed. `src/version1.{h,cpp}` is the private DOM migrator.
   - **Deviation 1 — `IdPolicy`.** The plan says "paste mints", without naming today's paste. It is
     the **linked template**: one file may back several linked groups in one document, so a resolved
     template is loaded with `IdPolicy::Mint` and everything else with `Preserve`. Without it,
     ADR-0011's "a duplicate is a certainty whenever one file is loaded twice" is exactly what
     flowview does on every load of a document with two links to one template.
   - **Deviation 2 — a missing `version` is fatal**, matching CONTEXT.md rather than the previous
     warn-and-assume-current. Guessing "current" reads a v1 file as v2, finds no node whose id is a
     string, drops every one — and a subsequent save writes that empty result over the original.
   - **Deviation 3 — `Uuid::shortString()` truncates from the TAIL**, not the head the ADR's
     `[019fbafb…]` illustration implied. Caught by running the cli dump: a v7 id leads with a
     millisecond timestamp, so all four nodes of the example scene printed as `[019fc0ab...]`. The
     trailing 32 bits are pure randomness. ADR-0011's consequence bullet is corrected.
   - flowview: `src/canvasids.{h,cpp}` (`CanvasIds`, 6 driver-free tests over the production class)
     + `panes/canvasstate.{h,cpp}` (the imnodes-reading half, renamed from `panes/canvasids`).
     `pinId`/`decodePin`/edge-index link ids/`NodeId`-to-`int` casts are gone; `groupnav`'s
     `pathOrdinals`/`pathFromOrdinals` are deleted and `pendingReselect`/`pendingPath` carry plain
     ids. `CanvasIds::reset()` hangs off `pendingBaseline`, which *is* "document identity changed".
   - Verified: warning-clean strict build, `format-check` clean, `ctest` **374/374**; the headless
     save ⇒ load ⇒ save round-trip is still **byte-idempotent** *and* now preserves node ids; a
     hand-written v1 document migrates with its params, names, edges and editor blobs intact and
     re-saves as a stable v2. **Not exercised: gui-mode** — the canvas, Inspector, Interface and
     Preview all changed id plumbing, and undo/redo's reselect + path restore changed mechanism.
     That needs a Metal session.
2. ✅ **Every declaration returns a `PortId`; `PortIndex` is retired — BUILT** (2026-08-02). The gap
   M4b slice 1 left, widened on review. `addInput` / `addOutput` / `addInputLike` / `addOutputLike`
   return the minted id instead of a position, each fixed node stores named `PortId` members, and
   `input(PortId)` / `output(PortId)` join the index accessors (a strong type beside `size_t`, so the
   overloads are unambiguous). Lifted out of the vertical because it touches every node class while
   interacting with nothing else in it.

   **Two decisions taken during the slice, both widening the plan:**
   - **Params get a `PortId` too.** The plan (and ADR-0012) had params keeping positional indices,
     on the grounds that nothing declares one dynamically and the on-disk key is the name. That
     argument only holds while params *can't* become dynamic — and future-proofing it cost one field
     on `Param`. `addParam` now returns a `PortId`, `param(PortId)` / `findParam(PortId)` join the
     positional accessors, and step 3's seam becomes `setParam(PortId, value)`. ADR-0012's bullet is
     amended in place with the reasoning.
   - **`PortIndex` is deleted, not renamed to `ParamIndex`.** With every durable handle a `PortId`,
     what was left was a *position*, used for three unrelated jobs — a count (`inputCount`), an
     iteration cursor, and (formerly) a param address. A plain `std::size_t` says all of that, and
     naming the type was what invited storing one. `grep PortIndex` over `libs/` and `apps/` now
     returns nothing.

   **Also as built:** the by-identity accessors are unchecked like the index ones (a node's named
   PortId always names a thing that node declared), asserting in debug through a shared
   `Node::checked` — flow core stays log-free. Ports and params draw from **one per-node counter**, so
   a param id can never equal a port id on that node: handing one to `input()` finds nothing rather
   than silently finding the wrong port, and a test pins that down. `DynamicPortsNode::addDynamicPort`
   and `GroupNode::exposeInput` / `exposeOutput` / `exposePort` each lost their
   declare-then-look-the-index-back-up dance. `flow-example`'s `imagePort()` / `inputPort()` were
   **deleted** — nothing in the tree called them, and an unexercised accessor is how a nested
   `Edit Template…` stayed broken through a whole slice.
   `ctest` **377/377**; a new `[dynamic]` test contrasts the two accessors directly (remove the first
   of three pins: `input(id)` still names its port, the same position now names a different one), and
   two new `[param]` tests cover id addressing and the shared counter.
3. ✅ **`Node::setParam` — one seam for recipe mutation — BUILT** (2026-08-02). `param(id) const`
   inspects; `setParam(PortId, value)` type-checks, commits and (from step 4) bumps the version as one
   operation, returning `bool` and changing nothing on failure. Mutable `Param&` leaves the public
   surface: the Inspector's ParamEditor, `flow::serialize`'s `readParams`, and concrete setters such
   as `ConstantNode::setValue` all decode into a temporary `PortValue` and commit through it. Green
   either side (the bump is still `markDirty` here), independently valuable, and also lifted because
   it is a wide mechanical sweep with no dependency on constness or on where values live.

   **As built:** `Param`'s write side is now `Node`-only — `set<T>` is private (it seeds the declared
   default from `addParam`) and the non-const `value()` is gone, so the seam cannot be routed around.
   `setParam` has two overloads: the type-erased `(PortId, PortValue)` primary for callers holding a
   runtime-typed value (the decoder, the Inspector), and a `template <typename T>` convenience that
   builds the erasure for a caller with a compile-time type (`ConstantNode::setValue`, tests). An
   empty `PortValue` is refused rather than treated as "clear the param": a param always holds a
   value. **`paramFromValue` changed shape** to match — it now takes a `const Param&` and returns
   `std::optional<PortValue>` rather than writing into the param, so decoding and committing are
   visibly separate and the commit goes through the node; `paramToValue` was already const-taking, so
   the pair is symmetric. The Inspector edits a **detached copy** of the value and commits only on
   change, which is free because a `PortValue` copy is a refcount bump. `markDirty` disappeared from
   three call sites (Inspector, `ConstantNode::setValue`, two node tests) — that is the point.
   `ctest` **384/384**, with four new `[param]` cases: the declared-type check (and that a refusal
   changes nothing *and* does not dirty the node), rejection of an id that names no param — including
   a *port's* id — the atomic commit-and-invalidate, and the type-erased overload.
4. ✅ **`Evaluation` + const compute + staleness — one vertical — BUILT** (2026-08-03;
   **live-verified 2026-08-03**). Values move off `Port`; ~41
   sites become `compute(NodeEvaluation&) const`, addressing ports through the ids from step 2:
   `evaluation.input(m_image)` / `evaluation.output(m_result)`. `Port` is left as pure declaration —
   `{id, name, direction, PortType, presence}` — so `value()`, `set`/`get`/`holds`, `ready()` and
   `describe()` all leave it. Readiness follows the values: `evaluation.ready(node)` /
   `nodeEvaluation.ready()` for ADR-0007's gate, `hasValue(PortAddress)` / `hasValue(PortId)` for port
   presence (retiring `Port::ready()`-means-one-thing / `Node::ready()`-means-another), and `describe`
   stays a `PortType` capability applied to an evaluation value. That reaches the canvas too — node
   dimming, pin shapes and link activity in `graphpane` all read readiness. The scheduler becomes
   `run(const Graph&, Evaluation&)`, and `const` means concurrently readable: `topoOrder()` stops being
   a lazy `mutable` cache and is maintained by the mutators, or two evaluations racing after an edit
   corrupt it. In the same change,
   `m_dirty` becomes a per-node definition version + evaluation-side `computedAt` and recompute
   request: recipe edits bump the version, while boundary rebinds, group-entry publication and
   on-request rearming affect only that evaluation. Boundary handles become immutable recipe metadata
   (`PortAddress`, name, type); remove `GroupInputNode::m_bound`, `BoundaryInput::setValue` and
   `GroupOutputNode::value`. CLI/Interface hosts and group entry use the same
   `evaluation.bind(input, value)` path, and outputs read through `evaluation.value(output)`; all
   value-reading panes, dump and tests follow it. Params do **not** move — they are recipe, and
   step 3's `setParam` now bumps the version instead of marking dirty. Keeping the rest together
   avoids exposing a multi-evaluation interface while dirtiness is still shared on the definition, and
   avoids sweeping for `const` twice. Both schedulers call the same `Evaluation::prepare(const Graph&)`
   before dispatch; it reconciles/prunes storage and returns stable per-node views, so worker tasks
   never insert, resize or create children. `Evaluation` is a move-only value in core
   (`flow/evaluation.h` — it needs only `PortValue`). Tests
   cover the concurrency contract through the production scheduler path: entry acquires a non-blocking
   RAII Evaluation run lease and throws `std::logic_error` immediately if already held; a blocking test
   node holds the first run while the second is rejected, then verifies unwinding/release. Never wait
   on a mutex for the same Evaluation; distinct ones over the same Graph remain concurrent.
   The flat `Step` address becomes `{const Graph*, Evaluation*, NodeId}` and `runOrder` takes both;
   recursive group staleness follows the matching child Evaluation. A group selected only for stale
   interior work does not republish its inputs; a local group change or selected outer predecessor
   requests recompute on that child's boundary input. Remove `dirty()`, `selfDirty()` and the group
   override rather than leaving a second invalidation path on the definition. Remove
   `Graph::markAllDirty`: `Evaluation::requestRecompute(node)` forces one node and its downstream
   closure, `requestRecomputeAll()` forces the prepared subtree at an EvalPath, and constructing a
   fresh Evaluation is the explicit full-state reset. One verb spans both surfaces —
   `NodeEvaluation::requestRecompute` is the same concept from inside compute. Migrate every existing
   `markDirty` call by meaning: recipe edit to a version bump, boundary/group/on-request runtime demand
   to the matching evaluation request. Make the pairing structural rather than remembered — every host
   owns its definition and Evaluation as **one replaceable unit**, so `FlowviewApp::replaceGraph` and
   the cli load swap both or neither; `Evaluation{graph}` records its definition and `prepare` compares
   it as a guard rail, which catches a mispaired call but cannot see a Graph rebuilt at a recycled
   address. Graph primitives bump versions internally, `setName` stays computation-neutral, and no
   public caller mutates then separately bumps.
   Prove the target isolation through production code: run one Graph with two Evaluations through
   SerialScheduler and ParallelScheduler, with different boundary bindings and on-request state, and
   assert their values/recompute requests never cross. Then prove the *permitted* concurrency the same
   way — N Evaluations running **simultaneously** over one `const Graph` on the executor, repeated in
   the style of `[group]`'s 100× sweep. Without it the milestone's headline contract ships unchecked
   and the `topoOrder` fix has no regression guard. This validates safe definition sharing without
   introducing a test-only evaluator or pulling linked-template ownership into M6.

   **As built.** `flow/evaluation.h` holds `Evaluation` (move-only, in core, needing only `PortValue`)
   and `NodeEvaluation`, the per-node view `compute(NodeEvaluation&) const` receives. Everything
   landed as planned; the notes below are where reality added detail.
   - **A bound value IS the boundary pin's output value in the evaluation.** `GroupInputNode::compute`
     became a no-op that carries what was bound rather than republishing from an `m_bound` map. That
     is what deleted the map, `setValue` and `GroupOutputNode::value` in one move, and it made group
     entry literally the same operation a host performs — `enterGroup` calls `child.bind(...)`.
   - **`BoundaryInput` / `BoundaryOutput` collapsed into one `BoundaryPin`** carrying
     `{PortAddress, name, type, typeName}` as plain fields. They were separate only because they
     wrapped different node pointers; as pure recipe metadata there is nothing to tell apart.
   - **The version bookkeeping is two halves, in this order**: clear the recompute request BEFORE
     `compute()` so an on-request source that rearms itself keeps its new request, and record
     `computedAt` AFTER it so a `compute()` that **throws** stays stale and is retried rather than
     being remembered as done. Found by the lease test, which expected the second run to throw again
     and got silence. A *suppressed* node still records — ADR-0007 relies on clean-and-empty together.
   - **`Scheduler::Session`** is the entry ritual as one RAII object (take the lease, then prepare),
     because `RunLease` is private to `Evaluation` and friendship is not inherited — so a backend
     cannot do half of it.
   - **`resolveEvaluation`** joins `resolvePath` in flowview's `groupnav`: an Evaluation is a tree
     with one child per group node, so the same `GraphPath` walks it, and `MainWindow` hands every
     pane a definition and its values in step.
   - `Graph::bumpNodeVersion` is public for the editing layer (`edit::syncGroupPorts` re-derives a
     group's ports in a way no single primitive covers). Documented as *not* a general invalidate
     hook: a caller that changed nothing must not call it.
   - **Known cost, accepted:** `prepare` rebuilds its node and port maps on every run rather than
     detecting that nothing changed. Correct and O(nodes); a graph revision counter would make it
     O(1) in the common case, and is the obvious follow-up if a large graph ever feels it.
   - `ctest` **394/394**, the flow suite repeated 5x clean; the new `[evaluation]` file drives the
     PRODUCTION schedulers for: two evaluations keeping values apart, keeping recompute requests
     apart, an on-request source rearming only its own, an edit invalidating both without touching
     either, **8 evaluations running simultaneously over one `const Graph`, 100x**, the parallel
     scheduler keeping two apart, the run lease refusing a second concurrent run and releasing on
     both normal and throwing unwind, the definition-mismatch guard rail, and `prepare` keeping
     surviving values. Headless round-trip still byte-identical; the reporter's v1 + linked-group
     document still migrates and computes.
   - **gui-mode live-verified by the repo owner 2026-08-03** — every value-reading pane changed where
     it reads from, the canvas's dimming / pin shapes / link activity now come from `evaluation`, and
     the Interface pane binds through it, so that was the slice's real risk. No bugs came out of it.

5. ✅ **Host-side keys and paths — BUILT** (2026-08-03; **live-verified 2026-08-10**). `PinKey` →
   `{EvalPath, NodeId, PortId}`; preview cache, Inspector,
   Interface, Preview pane, `saveFormat` follow. The clear-on-navigation scoping becomes a memory
   choice rather than a correctness one.

   **As built:** `PinKey` is `{GraphPath path, PortAddress port}` — the same three axes (which
   evaluation, which node, which port) in two fields, because a `PortAddress` already *is*
   `{NodeId, PortId}`. Two deviations worth naming:
   - **The direction bool is gone**, not carried through. A `PortId` is minted per NODE across both
     sides (step 2), so a `PortAddress` names one port unambiguously and the flag was saying nothing.
   - **`EvalPath` is spelled `GraphPath`, not introduced as its own type.** Today a group has exactly
     one child Evaluation, so the graph walk and the evaluation walk are the same sequence of group
     NodeIds; a parallel alias for an identical type would be two names for one thing. A **map node**
     (one child per element) is what makes them differ, and is where an `EvalPath` of its own earns
     its keep — recorded here and in `pinkey.h` so the next reader knows why it is absent.

   **Behaviour is deliberately unchanged.** The cache still holds only the level on screen, and the
   clear on navigation stays — but as an explicit memory choice (a texture per image port per visited
   level, for a cache only one level reads), no longer as the thing standing between the user and
   M5's bug nine. Retaining every level is now *available*: it would want `refreshIfDirty` to walk all
   levels rather than prune everything outside the active one. Four `[pinkey]` tests pin the
   invariant, including the case that actually motivates it — two evaluations of ONE definition share
   node and port ids by design, so the level has to be in the key however unique NodeIds become.
   `ctest` **398/398**.

**The hold on leftover group GUI work is lifted** — it was held until step 5 because it lives in the
panes steps 4 and 5 rewrote, and doing it twice is how the last four bugs happened. Those panes are
now settled.

### Not in this milestone

- **SplitGroup and Loop themselves.** M6 makes them expressible; building them is separate, and their
  open questions (map index stability, loop carry, suppression across a map, how ADR-0009's plan lowers
  a map) need a concrete feature in front of them.
- **Shared definitions for linked groups.** M6 makes and scheduler-tests it as *safe* — ADR-0010's
  sharing constraint falls — but LinkedGroupNode keeps its copied inner Graph in this milestone.
  Shared template ownership/cache and reload propagation are **Milestone 7** (grilled 2026-08-10,
  [ADR-0013](docs/adr/0013-shared-template-definitions.md)), **now built**; file watching stayed out of
  that too, so a template edit made outside the app reaches the document through the explicit
  Reload gesture (or a save + reopen), never live.
- **In-run liveness release.** M6 bounds retention *after* a run, not the peak *during* one. Releasing
  a value once every consumer has read it is separable, and cheap to add later precisely because
  `PortValue` payloads are already shared and immutable.

## Milestone 7 — shared template definitions (grilled 2026-08-10, **COMPLETE** 2026-08-11)

**All three slices built and live-verified.** M6 removed the reason every linked group owned a private copy of its
template: with values in an `Evaluation`, N instances can share one `const Graph`. M6 proved that
capability through the real schedulers but deliberately left `LinkedGroupNode` copying. This
milestone makes it real. Decisions in
**[ADR-0013](docs/adr/0013-shared-template-definitions.md)**; vocabulary in [CONTEXT.md](CONTEXT.md).

**What it is, in one line each:**

- One **definition per template**, held by a host-owned **`TemplateCache`** (canonical path →
  `{shared_ptr<const Graph>, EditorTree}`); instances hold the shared pointer.
- **`Node::innerGraph()` becomes `const`.** Mutable access to a contained graph belongs to the
  *inline* kind alone, so "a linked group is read-only in place" stops being a `bool` every pane must
  remember and becomes something the type refuses.
- The group types split to say that: abstract **`GroupNode`** (mirroring + pure-virtual
  `innerGraph()`), **`InlineGroupNode`** (owns a `Graph`, mutable `inner()`), **`LinkedGroupNode`**
  (holds `shared_ptr<const Graph>`) — which also brings the code onto ADR-0010's own *inline/linked*
  vocabulary.
- A template edit **replaces** the cached definition; it never mutates it. That is what keeps
  ADR-0012's "a `const Graph&` is concurrently readable" true once a definition is genuinely shared.
- **Reload is snapshot → invalidate → restore**, through the loader that already exists — not a
  second in-place patch path, which is the shape that produced M5's bugs six and eight.
- **Saving a document invalidates its own path.** Required, not a nicety: `Edit Template… → Save →
  Return` works today because the return re-reads the file.
- **`IdPolicy::Mint` is deleted.** It existed only because loading one template twice made duplicate
  ids certain; one definition means one set of ids.

### Build order

Each step stands alone and leaves the suite green.

1. ✅ **Hierarchy + constness — a pure refactor, no behaviour change.** *(built 2026-08-10;
   gui-mode live-verified 2026-08-10.)* Abstract `GroupNode` holds the mirroring and a pure-virtual `innerGraph()`;
   `InlineGroupNode` owns the `Graph` and is the only kind with a mutable `inner()`;
   `LinkedGroupNode` keeps its own `Graph` for now but exposes **no** mutable accessor at all — a
   loader establishes its interior through `adoptInterior(Graph)`, which is the seam slice 2's
   `shared_ptr<const Graph>` slides into without touching a caller. `Node::innerGraph()` returns
   `const Graph*` (the non-const overload is gone) — the engine already only used it that way, and
   `edit::syncGroupPorts` already only read the interior. flowview split `resolvePath` into a **const
   read resolution** and **`resolveEditable`**, a mutable one that returns `nullptr` at a linked
   group; `editableAt` is **deleted**, because "may I edit here?" is now answered by whether there is
   a graph to edit *through*. Panes take `const Graph& graph` plus a nullable `Graph* editable` and
   derive their disabled state from the pointer — the Inspector reads a node through the const graph
   and writes through the editable one, the Interface pane draws each pin name from the read side and
   commits through the write side. `MenuBarPane` takes only the const graph: Save writes the root, and
   `Add ▸ Linked Group…` resolves its own level. The factory key `"group"` is unchanged, so no
   document is affected. `ctest` **398/398**, warning-clean, format-check clean; headless `run` still
   flows the example graph and a real linked-group document still resolves and runs.
2. ✅ **The cache — the substance.** *(built 2026-08-10; gui-mode live-verified 2026-08-11.)* `TemplateCache`
   lives in `flow::serialize` and is owned by the host (`AppContext::templates`, beside `CanvasIds`
   and reset on the same document-identity event — `performSwap`, so opening a document re-reads its
   templates while an edit or an undo keeps them). `LinkedGroupNode` holds `shared_ptr<const Graph>`
   and exposes `definition()`; an unresolved link owns its placeholder alone behind the same pointer,
   so `innerGraph()` is uniform. A definition whose interior holds an unresolved link is **not**
   cached (a structural walk, not a reading of issue severities), so a missing-then-created template
   heals with no gesture. **`IdPolicy` is deleted whole** — a load always preserves identity now, and
   without a cache two instances simply hold separate equal copies, which is safe because ids need only
   be unique *within* a graph.
   - **Deviation from the ADR's stated ordering, amended there:** the injected resolver runs before
     the cache lookup, because the canonical key is the resolver's answer and `flow` must not
     interpret a `source` path itself. The file is therefore still read per instance; the cache shares
     the BUILD, which is what "never cache a partially built template" was protecting.
   - **Addition:** `serialize::resolveLinkedGroup` — resolving ONE link (source already set) is now a
     public routine the loader and the host share. `Add ▸ Linked Group…` had its own copy of that job,
     which is how it once dropped the template's layout, and would now have handed the added instance a
     private copy of a shared definition. The M5 note asking for these two halves to collapse is
     discharged.
   - Verified: `ctest` **404/404** (6 new: shared-by-pointer + independent parallel runs ×50, layout
     travels with a cached entry, a template holding a broken link is not cached, a self-link is still
     refused with nothing stored, invalidate → rebuild, and an undo keeping its definition);
     warning-clean; format-check clean. Live headless: a hand-built document with **two linked groups
     on one template** resolves, mirrors both faces, and pushes two DIFFERENT images through the one
     shared definition to two distinct results; save ⇒ load ⇒ save stays byte-identical.
3. ✅ **Reload.** *(built 2026-08-11; gui-mode live-verified 2026-08-11.)* **File ▸ Reload Linked Groups**
   clears the cache and rebuilds the document through snapshot → invalidate → restore — the loader
   that already exists, not a second in-place patch path. It keeps the user where they are (active
   path + canvas selection, like an undo), reports its issues into the Issues panel, pushes **no** undo
   entry, and marks the document dirty only when the rebuilt document actually differs (a `data::Value`
   comparison of the before/after snapshots — a template edit that leaves its interface alone changes
   nothing this document stores). The item is greyed out when the document links nothing
   (`groupnav::hasLinkedGroups`, which recurses — a link most often sits inside an inline group).
   - **Saving any document drops that path's cache entry**, in both Save and Save As. Required, not
     tidiness: `Edit Template… → Save → Return` works because the return re-reads the file.
   - **One canonical key, one function.** `graphio::templateKey` (weakly_canonical, so a not-yet-existing
     template still has a stable key) is used by the resolver *and* by save-invalidation. A key computed
     two ways eventually disagrees with itself, and the failure is silent — an invalidation that misses
     simply keeps serving the definition it was told to drop.
   - **Accepted consequence, noted in the code:** after a reload that changed the document, the undo
     cursor still sits on the pre-reload state, so the next edit's undo steps past the reload as well.
     What returns is the same graph (ports are re-derived from the templates on disk) with a stale
     interface cache, which rectification reports. Recording the reloaded document would tidy that at
     the cost of making the reload look like an undoable step, which it cannot be.
   - Verified: `ctest` **407/407** — one file has one key however spelled (and it is the key the
     resolver reports); a stale entry demonstrably keeps serving the old definition, while dropping
     that one key (Save) or clearing the cache (Reload) picks the edit up, group face and all; and
     `hasLinkedGroups` finds a link nested inside an inline group. The gesture itself needs the
     window — its pieces are what the tests pin down.

### Not in this milestone

- **File watching.** An external edit needs the explicit gesture. A watcher is a new dependency (or a
  poll) and would make definition swaps happen at arbitrary times rather than at a user gesture.
- **Prefab overrides / per-instance divergence.** ADR-0010's reasoning is unchanged, and sharing
  strengthens it: a template is one definition for every instance, and parameterising is what boundary
  pins are for.
- **In-place template editing.** `Edit Template…` stays the explicit act. Sharing raises the stakes of
  a template edit rather than lowering them, and step 1's const accessor is what enforces it.

## Group authoring gestures (built + live-verified 2026-08-11)

The four gestures M5 designed and deferred. The hold on them was lifted at M6 step 5 (they live in the
panes steps 4 and 5 rewrote), and M7 settled what a linked group's interior *is* — which is what makes
two of them expressible at all. Building a nested pipeline by hand was the friction they remove:
authoring a subgraph meant writing a separate document and pointing a link at it.

**The shape: flow does the surgery, the host does the canvas, the files and the layout.** Every graph
operation here is a `flow::edit` free function over a plain `Graph`, tested without a window (25 cases);
flowview's half is reading the selection, carrying positions across the change, and touching the disk.

- **`Graph::extract(NodeId) -> unique_ptr<Node>`** — the primitive both directions needed and neither
  had: removing a node while keeping it. `removeNode` is now this with the result dropped, so there is
  one removal, not two. **The node keeps its NodeId**, which is safe only because ADR-0011 made that a
  uuid — globally unique rather than unique within its graph — so identity survives the move and every
  host-side key built on it (layout entry, canvas int, preview key) still points at the right node.
- **`edit::groupSelected`** computes the cut-set exactly as designed: one boundary pin per distinct
  *source port* (a fan-out becomes one pin carrying one value, not a pin per consumer), names taken
  from the mirrored port and uniquified with `_2` (port names are identifiers — they double as cli
  flags and as the on-disk edge key), internal edges moved verbatim, outer wiring re-formed through
  the group.
- **Two refusals beyond the designed one**, both found by asking what a cut-set can contain:
  - **`WouldCycle`.** Contracting a selection a value LEAVES and RE-ENTERS needs the group to run both
    before and after the nodes in between. The graph was and stays acyclic — it is the contraction that
    is impossible. Without the check `Graph::connect` would refuse those edges one at a time, leaving a
    group built and silently unwired.
    Detected by walking forward from everything the selection feeds and asking whether it comes back.
  - **`UnnamedPinType`.** A boundary pin is replayed on load through its **port-type registry key**,
    and the serializer *skips a dynamic pin it cannot name* — so grouping across an unregistered type
    would give a group that works perfectly until saved and comes back missing that pin and every edge
    through it. Refusing is the same call the image encoders make: reject rather than degrade.
  - Every refusal is **atomic and checked up front**, before the first node moves. A half-formed group
    is not something a user can undo their way out of by hand.
- **`edit::ungroup`** is the inverse, resolving each boundary pin back to the direct edges it stood for
  (whatever fed the outer input now feeds every inner consumer of the matching pin, and so on). Inner
  nodes keep their ids here too. **Refuses a linked group** — its interior is the template's definition,
  shared with every other instance (ADR-0013), so there is nothing here this document owns to splice;
  Make Local first. Contrary to the M5 sketch it does **not** mint fresh ids: with uuids there is
  nothing to avoid colliding with, and preserving them is what keeps the canvas steady.
- **`edit::replaceGroup`** is the half Save as Template and Make Local share: swap what backs a group,
  keeping the group. **The replacement keeps the original NodeId** — not an optimisation but the honest
  answer, since from the document's point of view this is the same group, differently backed. Parent
  edges are carried across by **pin name** (the replacement's PortIds are its own, and both kinds derive
  their names from the same inner boundary); a pin the new interface lacks is **reported** in `dropped`
  rather than silently lost.
- **`Save as Template`** writes the interior out as a document, invalidates that path in the template
  cache (same rule as Save — an invalidation that is skipped keeps serving what it was told to drop),
  then re-points the group at it through `serialize::resolveLinkedGroup` — so the instance shares the
  cached definition with every other one built from that file, including ones added later.
- **`Make Local`** reads the template **from disk** rather than copying the definition in memory. That
  is the honest meaning of the gesture, and also the only way: a definition is a `shared_ptr<const
  Graph>` precisely so no instance can reach in and take it. Loaded with **no cache** — what is being
  built is a private body, not another sharer.
- **Layout migration is pure data, and lives in `groupnav`** (which owns the layout tree) rather than
  beside the gestures, so it is unit-tested in the driver-free flowview suite. `descendLayout` /
  `ascendLayout` move entries between levels under unchanged keys; `liftedPositions` translates a
  group's contents so their centre of mass lands on where the group sat — verbatim inner coordinates
  would fling them to a corner of the canvas, since an inner graph's grid space starts near the origin.
  Splitting them out was not tidiness: **a layout mistake here is silent** — a node whose entry did not
  travel just appears in a default column, which reads as a bug in something else entirely. It earned
  its keep immediately, catching a **nesting bug in the first cut**: a moved node may itself be a
  GROUP, and only its own position was travelling, so grouping a group (or ungrouping one that held
  one) discarded everything below it. Both functions now carry the whole subtree.
- **Wiring:** the **Edit menu** (Group Selected `Ctrl+G`, Ungroup `Ctrl+Shift+G`, Save Group as
  Template…, Make Group Local), each greyed by its own availability query, plus the two shortcuts. Not a
  Group menu: that one was retired in M5 precisely because a menu greyed out almost always is the worse
  discovery path. All four act on the canvas selection and resolve their own level, as
  `Add ▸ Linked Group…` does. A refusal reports through the transient-issue row (new generic
  `AppContext::noteMessage`, which `noteRejectedConnect` / `noteReadOnlyEdit` now go through) — a
  gesture that silently does nothing reads as a bug.

**Verified:** `ctest` **430/430** (+23), warning-clean, format-check clean, headless `run --example`
unchanged. The suite proves what matters rather than what is easy: most cases run the graph through the
**production SerialScheduler before and after** and compare values, because a cut-set wired plausibly
but wrongly passes a structural check. The integration case is in `flow-serialize`: a group *made by
groupSelected* round-trips through the document, re-runs to the same value, re-saves **byte-identically**,
and ungroups back to the original — which is the one property no unit test of `edit::` can reach, and
exactly what `UnnamedPinType` exists to protect.

**gui-mode live-verified by the repo owner 2026-08-11**, which was the real risk: every gesture reaches
the canvas, and all ten of M5's bugs came from gui seams headless verification cannot touch (imnodes'
assert contract, frame ordering, a rule applied in one pane and not its neighbours). This time the
engine-side split held — no bugs came out of it.

**Also fixed here (P0):** **`File ▸ Reload Linked Groups` had no menu item.** M7 slice 3 built
`reloadTemplates` and `hasLinkedGroups`, tested both, and never added the `MenuItem` — so the gesture
compiled, linked and was unreachable. Exactly M5's bug six ("an out-of-line member definition does not
warn as unused, so the feature compiled and linked while being unreachable"), and it says the *reviewing
dead code before it runs* habit is worth keeping: `grep` for a call site is what found it.

## Milestone 8 — map nodes (grilled 2026-08-11, **COMPLETE** 2026-08-15)

**All six slices built and live-verified.** M6 split definition from evaluation so one definition could back N
evaluations, and M7 made that sharing real for linked groups — but the thing both were built for has
never been expressible: **running one subgraph once per element of a collection**. ADR-0012 named four
questions and deliberately refused to guess at them without a caller. This milestone brings one.
Decisions in **[ADR-0014](docs/adr/0014-map-nodes-staged-planning.md)**; vocabulary in
[CONTEXT.md](CONTEXT.md).

**First vertical — LOCKED: a folder of images, listed in-graph.** `listDir` emits a
`std::vector<path>` → a map runs `load → tint` per element → `combine` folds the results to one
result. Runnable headless on the existing `io::image` with no new transport, and it is the *smallest*
caller that still makes arity **data-dependent** — the collection is computed by a node during the
run — which is the fact every decision below turns on.

**What it is, in one line each:**

- **A collection is read through a `PortType` capability**, not a payload type flow knows:
  `element` / `size` / `at` / `gather` beside `describe`, filled by `meta::traits::is_vector_v`. The
  payload stays a natural `std::vector<T>`.
- **Element access is zero-copy** — `at()` uses `shared_ptr`'s aliasing constructor to point at
  element *i* of the producer's own vector. The **gather** cannot alias, and costs N element copies.
- **The plan stops at a frontier and the run re-plans.** `run()` becomes plan → execute → prepare the
  now-known children → plan again, until a stage plans nothing. Each stage is one flat DAG, so
  `lain::task` is untouched and ADR-0009's "no new substrate surface" holds. **The gap between stages
  is the second coordinator point** ADR-0012 left open.
- **A child is identified by position**, so `EvalPath` finally becomes the `{NodeId, index}` sequence
  ADR-0012 wrote and a group is simply index 0.
- **An input splits or broadcasts according to its own declared type** — `vector<T>` against an inner
  `T` splits, `T` against `T` broadcasts. No flag, nothing stored; the declaration *is* the mode.
- **A hole suppresses the whole output** (a `vector<Image>` has no hole, and shortening it would break
  the positional correspondence). `N == 0` is different: empty in, empty vector out, which is a value.
- **`MapNode` is a third `GroupNode` subclass, inline only.** A linked map is deferred. *(**Refused**
  2026-09-09 — compose it: a map whose interior holds a linked group. See ADR-0014.)*
- **A map serializes its interface** — unlike a group, whose ports are re-derived, because a map's
  mirroring is under-determined by exactly one bit per input pin.

### Build order

Each slice stands alone and leaves the suite green. The two structural refactors land as
**no-behaviour-change** commits *before* the map exists — the shape that kept the shared-`PortValue`
and `PortId` changes green, and the shape that makes a regression unambiguous.

1. ✅ **`PortType` collection capability — BUILT** (2026-08-11). `element` / `size` / `at` / `gather`
   beside `describe`, filled by `if constexpr (meta::is_vector_v<T>)` through the same per-type
   function-pointer bridge, plus `PortValue::alias`. Core only — no scheduler, no node, no serializer
   touched. `ctest` **443/443** (+13), warning-clean, format-check clean, headless `run --example`
   unchanged.
   - **`meta::vector_element_t` was added** beside `is_vector`, which had only ever answered half the
     question — a consumer that detects a vector needs to name its element type next, and deriving
     that at each site is how the two drift apart. traits.h explicitly invites this ("add traits here
     as a real consumer appears"), and there is now one.
   - **`PortValue::alias(owner, member)`** is the static factory that makes the split free:
     `shared_ptr`'s aliasing constructor, so an element shares the collection's control block while
     pointing at a subobject of it. Public, with the precondition documented, because the per-type
     bridge in `details/porttype.inl` is not a friend.
   - **Found while building: a proxy container has no element to alias.** `std::vector<bool>` packs
     bits, so `operator[]` yields a *value*; aliasing it would take the address of a temporary and
     dangle. `at` now branches on `std::is_reference_v<decltype(items[index])>` and copies for a
     proxy — general rather than a `vector<bool>` special case, and cheap, since a container is only
     ever a proxy for something small. Refusing the type instead would have been a nasty surprise for
     whoever first registers a per-element flag list. A test covers it.
   - **`gather` reports a hole mechanically, and decides nothing.** An empty or wrongly-typed element
     yields an empty result; what a hole *means* (ADR-0014 has the map suppress its whole output and
     name the offending element) stays the map's decision in slice 4. `gather({})` is deliberately an
     empty *vector*, not an empty slot — the N == 0 rule, so a caller can tell "no elements" from
     "no collection".
   - **Both load-bearing properties were sabotage-verified**, not just asserted. Making `alias` copy
     instead of alias fails the zero-copy test on the address comparison; making it non-owning fails
     the outlives test by reading freed memory (`820706605` where `20` was expected) — deterministically,
     in both of its sections.
2. ✅ **Staged planning, with zero frontiers — BUILT** (2026-08-13). `run` / `evaluate` are now the
   plan → execute → re-plan loop, with nothing yet able to raise a frontier, so **behaviour is
   identical** and the whole existing suite is the regression test. `ctest` **444/444** (+1),
   warning-clean, format-check clean, headless `run --example` unchanged.
   - **`run` moved to the base and stopped being virtual.** The staging loop, the run lease and all
     planning are shared; a strategy now overrides only **`executePlan(const Plan&)`** — one stage,
     already built and ordered. So a backend never plans, never takes the lease and never decides
     when the run is over, which is the same reasoning that made `Session` one RAII object rather
     than a ritual each backend performs. Nothing held a `Scheduler&`, and only the two backends
     subclass it, so this changed no call site.
   - **`Plan` gained `frontiers`** — the deferred maps, addressed `{definition, evaluation, node}`
     exactly as a `Step` is, and for the same reason. Always empty until slice 4.
   - **The loop terminates on "nothing was deferred", NOT on "the next plan is empty"**, and that
     distinction is the whole slice. A mapless run must build exactly ONE plan, or every run pays a
     second full planning walk to discover there is nothing left to do — and worse, an on-request
     source re-arms itself inside `compute()`, so a freshly built plan is *never* empty and the
     invocation would never return.
   - **A new `[staging]` test pins that down** over both strategies: one `run()` fires a self-rearming
     source exactly once and leaves it armed for the next. Sabotage-verified — swapping the
     termination rule for the naive one makes it hang rather than fail, which is exactly the failure
     it exists to prevent.
   - `runSteps(plan)` is the extracted serial walk, shared by `SerialScheduler::executePlan` and by
     the pull path (serial for either strategy, as before).
3. ✅ **`Evaluation`: N children per node — BUILT** (2026-08-13). The child container is
   `map<NodeId, vector<unique_ptr<Evaluation>>>`; `child(node, index = 0)`, `hasChild(node, index = 0)`
   and a new `childCount(node)` address a child by *which node* **and** *which evaluation of it*.
   A group is index 0. `ctest` **446/446** (+2), warning-clean, format-check clean, headless
   `run --example` unchanged.
   - **All 25 call sites changed by zero lines.** The default index is what does that — a group's
     `child(id)` still reads as it did, in the scheduler, the tests and flowview's `groupnav`.
   - **`prepare` deliberately does NOT impose a count.** It keeps however many children are already
     there and guarantees at least one. This is the one part that is not a rename, and it matters:
     `prepare` runs at the top of *every* invocation, so "give a graph-containing node one child"
     would reset a map's N children — and every element's retained values and versions — on each
     run. A map's count is set between stages instead, once the collection that determines it
     exists. Sabotage-verified: imposing a count makes a group's interior recompute on the second
     run (`calls == 2`, want 1), which is inner incrementality gone.
   - **Children are held indirectly** (`unique_ptr` in a vector) because an `Evaluation` must not
     move when the vector grows: the scheduler holds child pointers in plan steps for a whole
     invocation, and slice 4 grows that vector between stages.
   - **Deviation — `GraphPath` did NOT gain an index here**, as this step originally said; it moves
     to slice 6. Because `child()` defaults to index 0, flowview resolves unchanged, so an index on
     `GraphPath` has no caller until the element stepper varies it. Adding one now would thread an
     always-zero field through `PinKey`, the layout tree, the breadcrumb and `resolvePath` — a wide
     sweep across the surface that produced M5's bugs, in exchange for nothing observable. It lands
     in slice 6 beside the first thing that reads it.
4. ✅ **`MapNode` + the map steps — BUILT** (2026-08-14). The class, lifted mirroring, the
   split/broadcast decision, per-element children sized between stages, the gathering exit, and the
   suppression / ragged / `N == 0` rules. `ctest` **456/456** (+10), warning-clean, format-check
   clean, headless `run --example` unchanged. Everything is driven through the **production
   schedulers**, serial and parallel, because a plan wired plausibly but wrongly still passes a
   structural assertion.
   - **The structural seam gained its third question: `Node::evaluatesPerElement()`.** The scheduler
     asks the fact, not the class — ADR-0009's rule, and the first draft of this slice broke it with
     a `dynamic_cast<MapNode*>` before it was corrected. *(Renamed `interiorEvaluation()`, returning
     an enum, in M11 slice 1.)*
   - **Addition the ADR did not anticipate: lifting needs the port-type REGISTRY.** A `PortType`
     knows its element type, but nothing can walk that backwards — naming `std::vector<T>` needs `T`
     at compile time, and mirroring has only a runtime type. So `registerPortType<std::vector<T>>`
     now also records itself as the list form of `T`, and `listTypeFor(element)` is what `MapNode`
     lifts through. **A type is mappable exactly when its list form is registered**, which ADR-0014
     already required for a collection pin to serialize. A type without one is refused rather than
     silently mirrored un-lifted.
   - **`GroupNode::exposePort` became virtual**, so `edit::syncGroupPorts` needs no idea which kind
     it is reconciling — one mirroring gesture, two faces.
   - **There is no MapEntry step.** Binding happens where the children are sized: between stages, on
     the coordinator thread. A map's plan is therefore N × its interior's steps plus a `MapExit`,
     and a map is never *consumed* within a stage it is expanded in (it would have been deferred if
     a predecessor were running), so it needs no consuming end.
   - **A map is deferred exactly when a group would republish** — `needsRecompute || a selected
     predecessor` — and additionally only if it has not already been prepared this invocation. The
     first half keeps the incremental case at ONE stage (an edit inside a map re-runs neither the
     producer nor the binding); the second half is what makes the loop terminate.
   - **`exitMap` re-asks whether the map could run**, rather than remembering it. That is what keeps
     zero children unambiguous: an **empty collection** gathers to an empty vector (a value), while a
     map that could not determine an arity produces nothing at all.
   - **`prepare` gives a map no default child**, since 0 is a legitimate count for it — the reason
     slice 3's "keep what is there" rule was written that way.
   - **Four sabotages, all caught:** disabling split detection breaks 7 of 9; never deferring breaks
     the same 7; making `gather` skip holes breaks the hole rule; and dropping the already-prepared
     guard **hangs** — a map defers itself forever.
   - **Nested maps verified rather than assumed.** A map inside a map delivers `{{2,3},{11}}` from
     `{{1,2},{10}}`, and each row is its own evaluation of the inner map with its own element count
     — which is precisely why a frontier is addressed `{definition, evaluation, node}`.
5. ✅ **Serialization — BUILT** (2026-08-14). A map writes its recipe as a nested body like an inline
   group, **plus its own `interface`** — the one group kind whose ports are stored, because mirroring
   is under-determined by one bit per input pin and derivation alone would turn every broadcast back
   into a split. `ctest` **460/460** (+4), warning-clean, format-check clean, headless unchanged.
   - **What is stored is the port's TYPE, not a flag.** On load the stored key is compared against
     the inner pin's own key and its list form's: the pin's type means broadcast, the list type means
     split. So there is no second field that can fall out of step with the port it describes — the
     rule ADR-0014 chose, carried onto disk unchanged.
   - **Restoration runs BEFORE `edit::syncGroupPorts`**, and that ordering is the mechanism: sync
     mirrors by `PortId`, so a pin already restored here is left alone, while a pin that has appeared
     since is added by sync at the default. What the document said wins; what the interior now offers
     fills the gaps; neither silently overrides the other.
   - **Rectification, as linked groups get it:** a stored port whose pin has vanished is dropped and
     *reported*; a stored type that matches neither form is re-mirrored at the default and reported.
     A bad entry costs its own port, never the node.
   - **A document with no `interface` mirrors at the default**, so a hand-written one still opens.
   - Verified: round-trip **runs to the same values** (the honest check — had the broadcast come back
     lifted, its edge would have failed to reconnect and the map would have suppressed), save ⇒ load
     ⇒ save is **byte-identical**, and ignoring the stored interface fails 3 of the 4 cases.
   - **Found by the tests, in the tests:** an empty `ValueCodecs` silently skips a param on save
     (best-effort, by design), which made the first round-trip read `{1,2,3}` instead of
     `{101,102,103}` — the offset had fallen back to its declared default. Worth noting because the
     failure looked exactly like a broken broadcast.
6. **flowview + the example nodes.** Split into three commits, as the note here recommended — the
   mechanical widening first, then the driver-free proof, then the gui.
   - ✅ **6a — `GraphPath` carries its element — BUILT** (2026-08-14). A path step is now
     `PathStep {NodeId node; std::size_t element = 0;}`, the `{NodeId, index}` step ADR-0012 named
     and CONTEXT.md had been holding a note for. Everything resolves at element 0, so **behaviour is
     unchanged** and the existing suite is the regression test. `ctest` **461/461** (+1),
     warning-clean, format-check clean, headless unchanged.
     - Deferred from slice 3 precisely so it would land beside its first caller; 16 files touch
       `GraphPath`, and mixing that sweep with the UI is what slice 2 exists to avoid.
     - **Only the EVALUATION walk uses the element.** The graph walk still reaches one definition
       however many elements are being evaluated over it, and the **layout tree stays keyed by node
       alone** — an arrangement describes the definition, and every element of a map shares one
       interior. Getting that wrong would give each element its own canvas positions.
     - `GraphPath` is runtime-only (the session persists file paths, not this), so nothing on disk
       changed.
   - ✅ **6b — the example nodes + the headless proof — BUILT** (2026-08-14). `flow-example` gains
     **`ListDirNode`** (a directory → one `std::vector<path>`, sorted, so element *i* means the same
     file twice running) and **`CombineNode`** (N images → their pixelwise mean). `ctest` **464/464**
     (+3), warning-clean, format-check clean. **The milestone's end-to-end claim is now proven**:
     `flowview run` over a real folder lists 2 files, maps `LoadImage` over them and averages to
     `(40+80)/2 = 60`, through the production save/load facade.
     - **`ListDir` is what makes the arity genuinely data-dependent** — nothing can know how many
       files a folder holds until it runs, which is the fact the whole staging design rests on. A
       missing folder yields NO value (suppression) while an empty one yields an empty list (a
       value); the difference is the same one ADR-0014 draws for `N == 0`.
     - **`LoadImageNode` gained an Optional `path` INPUT** that overrides its param. A param is
       per-node configuration and every element of a map shares one definition, so a per-element path
       has to arrive as a value — the same shape as a Select's connectable `selector`.
     - **`CombineNode` is an ORDINARY node**: it takes one vector-valued input and needed no engine
       support at all, which is exactly what CONTEXT.md's "Port arity" always said. Only looking
       *inside* a collection from the engine ever needed anything new.
     - **A collection now describes itself as "3 items"** rather than falling back to its bare type
       name. Small, but it is what the Inspector, the pin tooltips and the cli dump show for *every*
       port a map has, and the type label already says what the type is.
     - **A REAL BUG, found only by this end-to-end scene:** on a later run where only the *input*
       changed, a map whose new arity is **zero** was never selected in the next stage, so its exit
       step never ran and it kept serving the collection it gathered last time. `prepareMap` had
       assumed a recompute request was "still standing" — true on the first run of a fresh
       evaluation, false afterwards. Every unit test had masked it by dirtying everything
       (`requestRecomputeAll`) or running once; with elements left, binding them drags the map in as
       stale through the recursive check, so **only the drop to zero exposes it**. `prepareMap` now
       requests the map's recompute explicitly. Regression test added at that exact shape, and
       sabotage-verified.
   - ✅ **6c — the gui — BUILT** (2026-08-14; **gui-mode live-verified by the repo owner 2026-08-15**,
     together with the two fixes below and the input-default work that followed).
     `ctest` **466/466** (+2), warning-clean, format-check clean, both headless paths unchanged.
     - **Found first, and it would have made `Add ▸ Map` useless:** `resolveEditable` and
       `syncPathGroups` both `dynamic_cast<InlineGroupNode*>` to mean "owns a mutable interior". A
       map owns one too but is not an inline group, so **every pane would have been read-only inside
       a map** — a map you could add, descend into, and never build — and an interface edit inside one
       would never have reached its outer ports. That is M5's bug three in map-shaped form, and it
       gets the structural answer rather than a third cast: **`GroupNode::editableInner()`**, a
       virtual the host asks instead of testing for a class. The next kind that owns an interior needs
       no host change.
     - **`Add ▸ Groups ▸ map`** — added empty and grown by descending, exactly like a group.
     - **The breadcrumb carries an element stepper** on a map crumb: `< [3/12] >`. The path already
       held the choice (`PathStep::element`), so this only renders and edits it. Two facts from two
       owners: `Crumb::perElement` (the DEFINITION says it is a map) and `groupnav::pathElementCounts`
       (the EVALUATION says how many) — a map's arity is not in the recipe, so neither can answer the
       other. `MainWindow` fills the counts, being the one place holding the root evaluation.
     - **An Issues row for a map's hole, that navigates to it.** One element producing nothing clears
       the whole output, so the panel now says *"3 of 12 elements produced no 'image' — the whole
       output is cleared (first: element 2)"* and clicking it descends to that element. **One row per
       output, not per failed element**: a systematically broken folder would bury the panel under a
       row per file.
     - **`Issue` grew NAMED CONSTRUCTORS, and its general one went private.** Adding a fourth field to
       a plain aggregate made six positional `{…, {}, {}}` sites across four files, which was the
       symptom rather than the problem: an Issue has exactly three shapes — **`note`** (nothing to
       point at; seven of the ten sites), **`at`** (a node on this level), **`inside`** (another level,
       a map's failed element). The last two are ALTERNATIVES, and as an aggregate a caller could set
       both and have the node silently ignored. Now they cannot: the shapes are named, the general
       constructor is private, and adding a field touches no call site. `locatable()` replaces the
       `node != NodeId{}` check the click guard used, which was already the wrong question once a row
       could point at a level instead.
     - The engine reports no element index itself — flow core stays log-free and has no channel — but
       the child evaluations are right there, so the host simply looks.

**First live bug — a SEGFAULT leaving a map via the breadcrumb** (reported and fixed 2026-08-14).
Make a map, descend, add pins inside, click the document crumb to go back: crash. The strip computes
its crumbs from `ctx.activePath` once, then draws each one — but a crumb click calls `navigateTo`,
which **replaces the path mid-loop**. Clicking the document crumb empties it, and the map crumb behind
it then read `activePath[0]` of an empty vector.

- The stepper was the first thing in that loop to READthe live path rather than only slice it on
  click, which is why the pattern had never bitten: two crumbs cannot be clicked in one frame.
- Fixed structurally rather than with a bounds check: **`GraphPane::draw` snapshots the path once**
  and draws the entire strip — crumbs, stepper, `Edit <template>` button, layout seeding — from that.
  The pane can no longer read navigation state it may itself have changed. It is the same lesson
  `MainWindow` already learned one level up, where a navigation requested during a draw is consumed at
  the *start* of the next frame.
- **Found while auditing for the same shape: `MenuBarPane::documentToSave` captured the canvas layout
  into `ctx.activePath`** while `MainWindow`'s end-of-frame capture correctly used the drawn path —
  two paths doing one job, one updated. Navigating and saving in the same frame would have filed one
  level's node positions under another level's key. `drawnPath` is now on `AppContext` as the single
  answer to "which level is on screen", and both captures read it.

**Second live report — `ListDir` could not pick a folder, and its settings were params only**
(2026-08-14). Both fixed, and the first was worse than reported:

- **The path param editor hard-coded an IMAGE filter**, with a comment already admitting it — *"the
  only path value today is an image input; a per-value filter hint is a future refinement if a
  non-image path value appears (ADR-0005)"*. One had now appeared. A `std::filesystem::path` is
  legitimately either a file or a folder and nothing in the type says which, so the editor now offers
  **both `File...` and `Folder...`** rather than guessing. New **`gui::selectFolder`** in the dialog
  seam (pfd had `select_folder`; lain never exposed it), which remembers the folder ITSELF as the next
  start directory rather than its parent. The per-value hint ADR-0005 named is still the real
  refinement — it would replace both buttons with one that knows what it is picking.
- **`ListDir`'s `directory` and `extension` are now INPUT PINS too**, Optional and overriding their
  params. A param is per-node configuration and cannot be driven by the graph; "which folder" is
  exactly what a caller wants to supply from a boundary input or a Constant node. **`constPath` and
  `constString` joined the palette** (and a `string` codec joined `sceneCodecs`) so there is something
  to wire — without them the request had no driver.
- **The rule is now stated once**, in `flow/example/setting.h`: *wired wins, unconnected the param
  stands* — the Select-`selector` convention, shared by `LoadImage::path` and both of `ListDir`'s. It
  lives in flow-example rather than core: it is a node-authoring convenience over two accessors core
  already has.
- Tested end-to-end: a node configured with an empty folder and wired to a real one lists the WIRED
  one, and a stray `.txt` beside the images breaks the map until a wired `.png` filter excludes it —
  which is the same failure the round-trip fixture hit when a document sat beside its images.

**Input defaults — a node setting that is configured OR wired** (2026-08-15, prompted by review of the
`ListDir` fix). The hand-rolled `setting.h` helper that fix introduced was the symptom: a node wanting
both spellings declared a param and an Optional input of the same name and reconciled them by hand, in
three places that had to agree. It is now one declaration in core, **`addInput<T>(name,
Default{value})`**, with `Node::defaultOf(port)` pairing the two halves — amending
[ADR-0005](docs/adr/0005-node-parameters-distinct-typed-slots.md) in place, with the reason maps
supply: every element of a map evaluates ONE definition, so a setting that must differ per element
cannot be a param at all, while the same node outside a map still wants a configured value.

- **The default IS a param underneath**, because "editable and serialized" is exactly what a Param is.
  So it round-trips, the inspector edits it through the ordinary `setParam` seam, and **existing
  documents load unchanged** — the param keeps its name. Verified against the map document saved
  before the change.
- **It seeds an input with NO INCOMING EDGE, and such an input stays REQUIRED.** That pairing is the
  whole safety of the feature: if a default filled any empty slot, a Gate turned off upstream would
  feed the node its default instead of suppressing it, silently undoing ADR-0007. Sabotage-verified —
  the "fill any empty slot" version fails the readiness assertion.
- Chosen over a third noun (`addSetting`) or a connectable param, because it adds no vocabulary: the
  concept is *an input with a default*, which is the model people arrive with from Blender. CONTEXT.md
  had already listed **"setting"** among the words to avoid for this family, which settled the name.
- The gui follows: the Inspector shows **"driven by input"** in place of the editor while the pin is
  wired, since editing a value that has no effect is worse than not offering it.
- `LoadImage::path` and both of `ListDir`'s settings converted; `flow/example/setting.h` deleted.

### Milestone 8 is COMPLETE (2026-08-15)

All six slices built, and **gui-mode live-verified by the repo owner**: `Add ▸ Map`, descending into
one, building its interior, the breadcrumb element stepper, Issues rows navigating to a failed
element, the folder picker, and the Inspector's *driven by input*. The engine slices produced no bugs;
both live bugs came from the gui seam, which is the same distribution M5 saw (ten there, none in the
engine) and the reason that surface gets driven rather than assumed.

`ctest` **472/472**, warning-clean, format-check clean; `flowview run` over a real folder lists files,
maps `LoadImage` across them and averages to the expected pixel; save ⇒ load ⇒ save byte-identical.

**Landed with it, after the milestone proper:**

- **Input defaults** (below) — `addInput<T>(name, Default{value})`, amending ADR-0005.
- **`GateNode`'s `enable` now defaults to true.** An unwired gate is TRANSPARENT rather than a dead
  end: before defaults existed `enable` was a plain required input, so a gate dropped on the canvas
  was never ready and suppressed everything downstream until you found a `Constant<bool>` to feed it —
  it read as broken. Flipping the default in the inspector keeps a gate off with nothing attached, and
  a wired `enable` still wins. `value` deliberately has NO default: it is the data flowing through,
  and inventing one would let a gate produce a value nothing gave it. Sabotage-verified. **This does
  change an existing document**: a saved graph with an unwired gate now passes through instead of
  suppressing — accepted, since such a gate could not do anything else useful before.
- **`SelectNode`'s `selector` is now `Default{0}`**, which moves "unwired means branch 0" out of
  `compute()`'s presence check and into the declaration. One behaviour DID change, in the right
  direction: an Optional selector wired to something that produced nothing quietly fell back to
  branch 0, where a defaulted one leaves the node unready — so a suppressed selector suppresses,
  exactly as the Gate does.
- **`BlurNode`'s `radius` and `sigma` became defaulted inputs too**, and `constFloat` joined the
  palette so `sigma` has a driver. A blur strength that varies per element is what a map wants to
  express, and a param cannot do it. **The data input was moved to declare FIRST** — that is not
  cosmetic: an input's position is how tests and hand-built graphs address it, so declaring the
  settings first silently retargeted `connect(src, 0, blur, 0)` onto `radius`, which is how the suite
  caught it. Serialized documents were never at risk (edges are addressed by port NAME), and the
  example graph still produces a byte-identical blur.

**The full param audit** (asked 2026-08-15): four params existed in production nodes. **Two are now
defaulted inputs** (`BlurNode`'s `radius` and `sigma`); the other two stay params, for different
reasons; and one Optional input (`SelectNode`'s `selector`) was converted alongside them:
- **`ConstantNode<T>::value` stays a param, permanently.** A Constant's whole job is to BE a source;
  giving it an input would make it a pass-through that needs a source of its own.
- **`TintNode`'s `tint` (`image::ColorRGBf`) stays a param FOR NOW**, and the reason is concrete
  rather than aesthetic: `ColorRGBf` is not a registered port type, so a `tint` pin could not be
  wired by anything — no `ConstantNode<ColorRGBf>` exists, and it could not even be created as a
  boundary pin. It would be a dead pin. Converting it wants
  `registerPortType<image::ColorRGBf>("Color")` plus a colour Constant first; then it is a one-line
  change and per-element tinting works.
- **`MergeNode` / `SelectNode` branch pins keep `setRequired(false)`** and must: a Merge forwards the
  first LIVE branch, so a default on every branch would make them all live and defeat the node.

The rule that came out of it: **make a param a defaulted input when the value plausibly varies per
element or wants graph control — and when something can actually drive it.** It is not free; each one
adds a pin to the canvas.

### Not in this milestone

- ~~**Loop.**~~ A map's children are independent by construction; a loop's are not. *(**Built** by
  Milestone 11, 2026-09-06. The carry question this bullet left with ADR-0012 is settled by
  [ADR-0021](docs/adr/0021-loop-nodes-carried-state-per-iteration-staging.md): a loop **carries**,
  through paired inner boundary pins. It landed with no production caller, which the ADR states
  rather than hides — the feature exists to test the node model.)*
- **A linked map** (one shared template mapped over N streams) — what the video workload will want,
  deferred until it exists to shape the interface reconciliation. *(**Refused** 2026-09-09: the
  reconciliation question dissolves once you compose instead — a map whose interior holds a linked
  group. Argument in ADR-0014.)*
- **The `Collection` payload**, keyed elements, and a multi-value cli binder for a collection boundary
  input — all named in ADR-0014 as the escapes from the accepted costs, none built. *(The `Collection`
  payload's **question** was since answered outside the map — ADR-0018 makes a render a host-owned
  fold, so the big-N case that would have forced it never reaches a map. Answered, not built: no such
  type exists, and the capability seam stays an interface so one could still arrive behind it.)*
- **Per-element incrementality**, which the natural-vector payload makes unavailable: any change
  rebuilds the whole vector, so all N children recompute.

## Milestone 9 — camera calibration and fixed registration (grilled 2026-08-14 to 08-15)

**Designed, not started** — the only milestone from 5 onward that is. M10 was numbered after it and
built before it, discharging its frame-sequence prerequisite.

Domain vocabulary is in [CONTEXT.md](CONTEXT.md). The dependency policy is
[ADR-0015](docs/adr/0015-permissive-by-default-production-dependencies.md), the camera module and evidence
contracts are [ADR-0016](docs/adr/0016-camera-calibration-method-modules.md), and the registration
solver decision is [ADR-0017](docs/adr/0017-ceres-for-registration-refinement.md).

This milestone adds standalone camera libraries first and thin flow adapters second. Camera geometry,
board detection, calibration, validation, and registration remain usable without `lain::flow`; node
tests call the same production operations as direct library consumers. Optional third-party-backed
implementations follow the existing repository split: lain-owned contracts under `libs/`, adapters
that directly include or link OpenCV or Ceres under `plugins/`. No third-party type crosses a
lain-owned interface.

The current flow architecture reinforces the batch design. A calibration or registration node is a
stateless definition; one `Evaluation` supplies a finite request and receives a report. Shared,
immutable `PortValue` payloads make sequence handles and reports cheap to fan out. The vector-backed
`MapNode` is not the production video-ingestion mechanism: materializing thousands of decoded 8K
frames as `std::vector<image::Image>` would violate the bounded-memory requirement. A finite, lazy
`FrameSequence` remains the input seam, with its loader and persistence design deliberately deferred
to a separate grill.

### Prerequisite and review notes

**Do not begin this milestone until Milestone 10 is complete** — it is numbered after M9 but built
**before** it. M10 owns the finite lazy `FrameSequence`, random access, frame identity, the frame
spec's homogeneity guarantee, and the production path from files or video into calibration. Camera
work may define the evidence it needs from a sequence, but must consume the sequence contract
established there rather than inventing a temporary loader or observation-only node path. Capture
manifests and capture datasets remain M9's (M10 explicitly excludes them).

The 2026-08-16 review also left these requirements and decisions visible before implementation:

- **Eigen is an approved scoped exception.** Eigen's MPL-2.0 file-level copyleft is acceptable for the
  optional Ceres-backed registration implementation without changing lain's MIT license. External
  distributions preserve notices, identify the corresponding source, and publish modifications to
  covered Eigen files under MPL-2.0. Builds define `EIGEN_MPL2_ONLY`, and the root `README.md` keeps
  Eigen and every other third-party license visible so replacement candidates can be audited later.
- **Targetless methods consume shared feature tracks.** A shared `lain::camera::feature` contract owns
  backend-neutral observations, tracks, track sets, and extraction reports. Optional producers own
  feature detection and description, matching, track construction, geometric verification, and
  initialization evidence. Targetless calibration and registration consume that same evidence and do
  not grow separate front ends; high-level frame-sequence operations delegate to the shared producer.
- **Registration vocabulary supports both methods.** A capture group is an associated capture
  instant or evidence group, not inherently a set of frames observing one board pose. Global
  refinement optimizes camera transforms plus method-specific latent geometry: board poses for board
  registration, or scene landmarks/tracks for targetless registration. `CONTEXT.md` carries these
  method-neutral definitions.
- **Metric claims distinguish camera calibration from scene scale.** Intrinsics are expressed in
  pixels and lens coefficients do not become metric because board dimensions are known. Known board
  dimensions establish translation and reconstruction scale; targetless intrinsic reliability
  instead depends on motion, scene geometry, priors, and observability. A single unconstrained image
  is not a generally identifiable intrinsics-and-distortion calibration input.
- **Flow adapters include the current persistence and host contracts.** Register stable node factory
  keys, dynamic/boundary port types, codecs for serialized params or defaults, host binders, and
  flowview catalog entries. Disabled camera plugins neither register unavailable node kinds nor fetch
  third-party dependencies, and saved graphs fail clearly when a required optional implementation is
  absent.
- **Fallback needs an explicit graph shape.** Gate targetless work upstream from a board-result
  fitness predicate, then combine attempt reports separately from selecting the accepted camera model.
  The final report retains every attempted path, including a failed board attempt; a downstream
  `SelectNode` alone neither short-circuits work nor preserves that history.
- **Projection math has one production implementation.** Public checked projection/unprojection and
  Ceres residual evaluation share scalar-generic model kernels, or use one explicitly tested Jacobian
  adapter per model. The Ceres plugin must not copy distortion formulas into a second implementation,
  including iterative inverse-model behavior.
- **Unavailable evidence is represented, not fabricated.** A failed calibration report always has a
  validation/stability section, but held-out and deterministic-resampling results may be `Unavailable`
  with a structured reason when failure occurs before they can be computed. A ready verdict still
  requires the configured evidence.
- **Stage 1 needs a concrete board-measurement value.** Board dimensions retain optional hard bounds
  without committing to a generic `Measurement<T>` template. The initial board-local representation
  should leave a clean generalization path when a second measured-quantity caller exists.
- **Dependency footprint is an acceptance criterion.** OpenCV and Ceres camera plugins are opt-in;
  an all-disabled configure performs no dependency fetch. Record exact versions, enabled modules,
  transitive licenses, configure/build time, and representative static and dynamic binary sizes so
  dependency containment is measured rather than inferred from target boundaries.
- **The D455 fixture records capture provenance.** Store RGB stream profile, resolution and format,
  device/firmware identity where available, and librealsense/backend versions. Compare different
  distortion variants by their pixel/ray mappings on shared evidence rather than by coefficient
  differences between incompatible parameterizations.

### Build order

1. **Foundational camera geometry + ChArUco calibration.** Add `core::Length`; the agreed rigid
   transform and axis-convention conversions in `lain::math`; immutable validated camera models;
   the closed distortion-model set; projection, unprojection, containment, applicability, and
   structured failure results. Add board pattern/instance/specification values, deterministic
   ChArUco rendering and fingerprints, backend-neutral observations and detection reports, view
   selection, calibration reports, validation, and reconstruction-fitness profiles. The optional
   OpenCV adapter owns ChArUco detection and estimation. Prove the direct production path with
   formula-level synthetic tests, a compact committed D455 RGB fixture, and an external hashed
   extended dataset. Do not expose targetless or fallback stubs.
2. **Fixed-camera board registration.** Add explicit capture-group input values, observation-graph
   diagnostics, `referenceFromCamera` results, registration scale status, fitness, and reports. Use
   the optional private Ceres adapter for one sparse global-refinement path, with SuiteSparse disabled
   by default. Exercise connected, partial, ambiguous, and disconnected synthetic rigs plus the
   target scale of roughly 100 cameras and 4,000 capture groups without fixed camera/group limits.
3. **Targetless registration with known intrinsics.** Reuse immutable camera models, capture groups,
   registration reports, shared feature-track extraction, and Ceres refinement. Accepted tracks
   establish overlap, initial relative geometry, and scene landmarks before Ceres begins; absent
   metric evidence remains explicitly scale-ambiguous. Compare board and targetless registration only
   on shared held-out evidence.
4. **Targetless calibration + graph fallback.** Add targetless intrinsics/distortion estimation only
   after the shared feature-track evidence and validation path can meet the report contract. Then
   expose the graph method input and ordered board-then-targetless fallback, retaining every attempted
   report and short-circuiting on the first result that satisfies the selected fitness profile.

Each slice lands through the closest real caller: geometry and board interfaces directly, optional
adapters through their public operation, and flow nodes through a retained `Evaluation`. Tests cover
failed reports as ordinary results, deterministic-debug execution, downsampling versus native
refinement, imported-model policies, held-out validation, and dependency-disabled builds. No slice
lands with copied solver/detector logic or a second production path.

### Not in this milestone

- Video/image-sequence loading, capture-manifest persistence, and temporal grouping I/O details.
- Joint intrinsic/extrinsic optimization, multi-board calibration, or mutable intrinsics during
  registration.
- Moving-camera pose tracking, temporal-alignment estimation, and world-frame alignment.
- A generic `Measurement<T>` uncertainty template without a second concrete caller.
- Runtime-defined distortion-model plugins or SuiteSparse-enabled Ceres builds.

## Milestone 10 — video + frame sequences (grilled 2026-08-19, **COMPLETE** 2026-09-04)

**Numbered after M9, built before it** — M9's prerequisite note points here. Domain vocabulary is in
[CONTEXT.md](CONTEXT.md) under *Frame sequences*; the model is
[ADR-0018](docs/adr/0018-frame-sequences-and-host-driven-rendering.md), the codec backend and its
licence exception are [ADR-0019](docs/adr/0019-ffmpeg-lgpl-for-video-codec-support.md) (with
[ADR-0015](docs/adr/0015-permissive-by-default-production-dependencies.md) amended).
**All eight slices (0–7) built; gui-mode live-verified 2026-09-04.**

The ask was "load and save video the way images already load and save". The parallel holds at the
registry / plugin / facade level and **breaks in exactly two places**, which is most of the design:

- **Transport.** `io::read` is a whole-asset slurp and `ImageReader::decode` takes the whole `Buffer`.
  A lazy sequence must hold something open, so this milestone builds the **`Stream`** transport WORK.md
  has had queued for precisely this reason. Skipping it would mean either slurping the file or putting
  `fstream` calls inside a codec plugin, collapsing ADR-0004's transport/codec split.
- **Writing.** An encoder is open-push-finalise and its file is invalid until finalised, so the writer
  is a stateful handle, not `encode(asset) → Buffer`.

Everything else follows from one structural choice: **a frame sequence is a list of frame references
over sources**, which makes clip/concat/select list operations, makes multi-file timelines free, and
makes it structurally impossible for a processing graph to emit a sequence — so a graph is a per-frame
function and the **host owns the frame loop**.

### The shape

```
--video src.mp4 ──► video : FrameSequence ─► ClipSequence ─► FrameAt ─► …graph… ─► result : Image
startFrame ────────────────────────────────────►│              ▲
endFrame ──────────────────────────────────────►│              │
                                    frame : FramePosition ─────┘   (host-bound, once per iteration)
```

`flowview run --video src.mp4 --frame 0-499 --result out.mp4` binds the sequence through
`BoundaryBinders` exactly as `--image` already binds an `Image`, then sweeps. Memory is **one frame**
at any range length; `OpenVideo` is upstream of nothing that changes between iterations, so the
decoder stays warm and sequential — provided the sweep **retains one `Evaluation` across the range**.

### Build order

> **Revised 2026-08-31 — FFmpeg is now fetched, not found.**
> [`luckyneko/ffmpeg-prebuilt`](https://github.com/luckyneko/ffmpeg-prebuilt) publishes
> **tier-verified LGPL** FFmpeg archives (shared, `macos-arm64` / `linux-x86_64` / `linux-arm64` /
> `windows-x86_64`), each carrying a `MANIFEST.txt` that states its tier, full configure string and
> corresponding-source url. A release archive **does** fit the `cmake/addXXX.cmake` FetchContent
> idiom — the reason ADR-0019 chose *located* was that autotools does not — so the dependency risk
> that milestone was ordered around is retired, and **slice 0 is new and lands first**. Slices 1–7
> are unchanged in substance. Amended in place: ADR-0019 (fetched-not-found, the manifest gate, the
> opener registry) and ADR-0004 (lain's first shared-linked dependency).

0. **FFmpeg lands, with nothing depending on it yet.** `cmake/addFFmpeg.cmake` fetches the pinned
   archive for the host platform (`URL` + `URL_HASH` from the release's `SHA256SUMS`, cached under
   `.cache/fetch/` like every other module) and defines imported **SHARED**
   `FFmpeg::avformat / avcodec / avutil / swscale`. `LAIN_IO_VIDEO` defaults **OFF**; an
   all-disabled configure resolves nothing.
   - **The tier gate parses `MANIFEST.txt`.** ADR-0019 specified a runtime `avutil_configuration()`
     check; the manifest states the same facts as text, so the gate becomes a configure-time parse
     that executes nothing and therefore survives cross-compiling, which a `try_run` never could.
     The compiled probe stays as lain's permanent `[video]` **runtime** test — belt and braces, and
     the only cover for a system FFmpeg someone points `LAIN_FFMPEG_ROOT` at.
   - **lain's first shared-linked dependency** (ADR-0004 amendment). LGPL's relinking requirement is
     satisfied by dynamic linking alone, which is why the archives are shared-only. Costs: a
     build-tree rpath so `ctest` can load the libraries, `@executable_path/../lib` / `$ORIGIN/../lib`
     for anything staged, and an explicit DLL copy on Windows, which has no rpath.
   - **The LGPL obligations are discharged here, not deferred.** The module copies the archive's
     `COPYING.LGPLv2.1` / `LICENSE.md` / `CREDITS` / `MANIFEST.txt` into the build tree beside the
     binaries, and `lain::app` grows `--licenses` beside the `--version` it already owns. Notices,
     corresponding source, relinking and prominent notice — all four, at the point the dependency
     enters rather than at some future packaging story. The notice text is **generated from the
     fetched manifest**, not transcribed into a checked-in file: a transcription is a second copy of
     facts that already exist, and its failure mode is silent — it goes on naming the old version
     after a bump, which is worse than useless for a licence claim. The README's table stays the
     human-facing inventory (ADR-0015); a second one would be the *two paths doing one job* shape
     this repo keeps catching.
   - **The platform encoder reality, for slice 6 to consume.** Decoding is uniform (`h264`, `hevc`,
     `vp8`, `vp9`, `av1`, `prores`, `dnxhd`, `ffv1`, `mjpeg`) and so is archival encoding
     (`prores`, `ffv1`, `mjpeg`). **Delivery** encoding is per-platform — videotoolbox on macOS,
     Media Foundation and NVENC on Windows, VAAPI / V4L2-M2M / NVENC on Linux — so slice 6 selects
     by **availability**, never by a hardcoded name.
1. **`lain::media` + the image-sequence source.** `FrameSequence`, `FrameSource`, `FrameRef`,
   `FrameSpec`, the frame table, clip/concat/select, homogeneity validation, the fixed ring cache,
   `io::image::openSequence`. **No new third-party dependency.** Driver-free tests.
2. **`flow` integration.** `registerPortType<FrameSequence>`, the `FramePosition` type
   (`Default{0}`, so a sequence graph renders something the moment it is opened), `FrameAtNode` /
   `ClipSequenceNode`, `describe()`, `ValueCodecs`, `BoundaryBinders`,
   `serialize(Archive&, FrameSequence&)` for the manifest, and the `run` sweep — the range, the
   `####` numbered output, `--on-missing-frame stop|skip`, and one retained `Evaluation` across it.
   **End-to-end proof, still dependency-free:** a folder of stills swept to `out.####.png` in bounded
   memory — every decision in the milestone exercised before FFmpeg or `Stream` exist.
   - **One `openSequence` node over an opener seam**, not a node kind per medium. This settles a
     disagreement already in the docs (this build order said `OpenSequenceNode`; the shape diagram
     and ADR-0018's prose said `OpenVideo`) in favour of the seam, and amends ADR-0019's
     unknown-`kind` consequence. It mirrors `io::image::load`'s extension-keyed dispatch, it means a
     document does not change node vocabulary when its footage goes from stills to mp4, and with
     video disabled it reports *"no opener for .mp4"* instead of dropping the node **and its edges**
     as an unknown kind. It lives one level above the per-medium seams — `media` depends on no `io`,
     so the dispatcher cannot live there.
3. **`Stream` transport** in `lain::io`. Local scheme; handle-pool-ready interface (never hands out an
   OS handle, owns its uri and logical position, every operation may fail, acquisition separate from
   construction). The prebuilt FFmpeg is `--disable-network`, so libavformat **cannot** open
   `http(s)://` at all — which makes this seam more load-bearing than ADR-0018 argued, not less: a
   future `s3://` scheme reaches the decoder through lain's transport or not at all.
   - **Both directions, and `read`/`write` ride them** (settled 2026-09-01, before building). The
     write side was implicitly slice 6's, but slice 6's muxer needs an AVIO write callback for the
     same ADR-0004 reason the reader needs a read one, and reimplementing `io::write` over it gives
     it a production caller immediately — which also means one local backend per scheme rather than
     two implementations of "get bytes from a uri" that can drift.
4. **`ColorSpace += BT709`** against `lain::image`, its own small commit — it touches an enforced
   enum, and it is more than an enumerator: `convert(src, ColorSpace)` is a pairwise dispatch needing
   BT709↔Linear (sRGB↔BT709 composes through Linear), the transfer function in `colormath.h`, and
   the op-class enforcement path. Two decisions, both recorded in ADR-0018: the curve is the
   **inverse Rec.709 OETF**, and **untagged** footage is treated as BT709 with a log line rather than
   guessed as BT.601 by frame size.
5. **`io::video` seam + FFmpeg read plugin.** `libs/io/video` (the registry seam) +
   `plugins/io/video/ffmpeg` + a generated `registerVideoCodecs()` aggregator, mirroring
   `plugins/io/image`. A custom `AVIOContext` over slice 3's `Stream` — not `avformat_open_input(url)`
   — so ADR-0004's transport/codec split holds and no `fstream` enters a codec plugin. Frame table
   from the container index, else a decode-free demux scan of packet pts / position / keyframe flag.
   Decode + `swscale` YUV→RGB8 with range expansion (`PixelFormat` has no planar or subsampled model,
   so RGB8 is the right plugin boundary). BT709 tagging; **explicitly tagged** BT.601 / BT.2020 /
   PQ / HLG reported and refused. Fixture: a tiny embedded byte array like `pngfixtures.h` — a
   64×48 multi-GOP clip is ~9 KB, and generating one at test time would only work on macOS anyway,
   since the other targets have no H.264 encoder. Now `--video src.mp4` works.
6. **Video write.** `VideoWriter` handle seam (`openWriter` / `write` / `finish`, `finish()` explicit
   and status-returning), the FFmpeg writer, the one-shot `save(uri, sequence)` facade, and the
   render loop's encoder. (**Corrected 2026-09-02:** this bullet also listed `--on-missing-frame
   stop|skip`, which actually landed in **slice 2**. What is slice 6's is the *asymmetry* — numbered
   stills represent a hole as a gap in the numbering, a video cannot and must close up, so `skip`
   means something stricter here and a truncated render must still be finalised.) Encoder selection is by
   availability against slice 0's table: a delivery codec where the platform provides one, archival
   ProRes / FFV1 / MJPEG everywhere, and an honest refusal naming what is missing rather than a
   silent fallback. **Never x264/x265** — they are not in the archive and enabling them would
   relicense the combined work.
7. **flowview.** The deferred **GUI view** registry lands here with two customers at once
   (`FrameSequence` → a player, `Image` → the currently-hardcoded thumbnail branch), plus the Preview
   pane's transport, the palette entries, and a `FramePosition` boundary editor in the Interface pane
   — small, but without it a sequence graph cannot be driven in the gui at all. Last, because in this
   repo the gui is where the bugs are (M5's ten, M8's two).

Slices 1–2 remain a complete, useful, dependency-free vertical: if the FFmpeg integration turns
messy, that still ships a bounded-memory sibling to `ListDir → map(LoadImage)`. Slice 0 is
independent of both and can land in either order — it is sequenced first only because it is now the
cheapest way to retire the last unknown in the back half.

**Slice 0 is built (2026-08-31).** `cmake/addFFmpeg.cmake` fetches the hash-pinned archive for the
host platform and defines imported SHARED `FFmpeg::avutil / avcodec / avformat / swscale /
swresample`; `plugins/io/video/ffmpeg` is the plugin the reader lands in, carrying for now only the
licence probe and its `[video]` test. `LAIN_IO_VIDEO_FFMPEG` defaults **OFF**. `ctest` **476/476**
with it on (473 + 3), warning-clean, format-check clean; the default build is unchanged.

- **The gate is sabotage-verified, all three refusals.** A manifest carrying `--enable-gpl`, one
  carrying `--enable-nonfree`, and one declaring a tier other than `lgpl` each fail configure with
  the reason named. A root with **no** manifest — a system FFmpeg — warns loudly instead of passing
  quietly, because establishing its tier would mean executing it, which cross-compiling forbids;
  the `[video]` test is then the only gate and says so.
- **The runtime test caught a trap in itself, which is why it is worth having.** `avutil_license()`
  returns one of five fixed strings, and the obvious `find("GPL version") == npos` check FAILS on a
  perfectly good library: `"LGPL version 2.1 or later"` contains `"GPL version"` one character in.
  A **prefix** test is the only form that separates the five. Anyone would write the substring
  version; it now carries the reason it is wrong.
- **No manual rpath was needed on macOS/Linux, and that is proven rather than assumed.** CMake
  derives a consumer's build-tree RPATH from the directories of the shared libraries it links, so
  the imported targets are enough. The proof is that the `[video]` test *runs at all* — an
  executable that cannot resolve `@rpath/libavutil.60.dylib` does not launch. Windows has no rpath
  equivalent, so `lain_ffmpeg_stage_runtime(target)` copies every DLL in the archive (the `av*` set
  **and** the MinGW runtime closure shipped beside them) next to the executable; it is a no-op
  elsewhere, so a caller writes it unconditionally.
- **Known limitation, recorded rather than left to be found: the notice is build-level, not
  per-binary.** With the plugin enabled, `flowview --licenses` prints the FFmpeg notice although
  flowview does not yet link FFmpeg — that becomes true at slice 5, when the reader is wired in. A
  per-target notice would need every target to declare its own, which `lain::app` cannot know for
  its consumers; revisit only if lain ever ships a binary that deliberately excludes an enabled
  dependency.
- **No aggregator in `plugins/io/video` yet.** The image side generates one because it has three
  codecs to wire into a registry; video has one plugin and, until slice 5, no registry to wire it
  into. It arrives with the reader that needs it, rather than as an empty mechanism.
- **Found 2026-09-02, while committing slice 5b: the licence probe's SOURCE was never committed.**
  `.gitignore` carried an unanchored `build*`, which matches any path COMPONENT — so
  `plugins/io/video/ffmpeg/src/build.cpp` and its header were silently ignored, and slice 0
  committed the plugin's CMakeLists and its test without the two files they name. A fresh clone
  would have failed to configure with the plugin enabled. `git add` skips an ignored file without a
  word, which is what made it invisible for two days; the pattern is now `/build*/`, anchored to the
  repository root and matching directories only, and the files are in.

**Slice 1 is built (2026-08-31).** `lain::media` (`FrameRate`, `FrameSpec`, `FrameRef`,
`FrameSource`, `FrameSequence`, the five list operations) plus `io::image::openSequence` — a folder
or a `####` pattern of stills opened as a real sequence. No new third-party dependency; the media
tests use a synthetic source and the opener tests a fake reader over real temp files, both following
the idiom `test_load.cpp` already set. `ctest` **506/506** (+30), warning-clean, format-check clean.

- **`log::ensure` is the wrong tool for every refusal in this library, and that is worth carrying
  forward.** It is a PRECONDITION helper: it logs *and asserts*, so a violation aborts in a debug
  build. But nothing media refuses is a broken invariant — a graph binds a frame position past the
  end, a folder holds one stray odd-sized still, two real files disagree about their spec. ADR-0018
  fixes the answer as "an invalid Image", and a debug abort is not that. Every refusal here is a
  logged return instead. `lain::image` uses `ensure` correctly for op-class enforcement, which
  genuinely *is* a programming error; the two look alike and are not.
- **The rate is the one axis of a spec that tolerates absence.** Geometry and both colour tags must
  agree exactly, but an unspecified rate adopts the other side's — which is precisely what makes
  ADR-0018's promise that a sequence can span "a video file and a folder of stills" true rather
  than a refusal, since stills genuinely declare no rate. Two *different* declared rates are still
  refused: reconciling them is a retime, and no composition should decide that silently.
- **No public `FrameTable` type.** CONTEXT.md describes the table as a per-source record of offset,
  timestamp and keyframe flag — but two of those three are video-shaped and mean nothing for a
  folder of stills, and inventing the type before its only real implementation exists would fix its
  field set from the wrong end. The *interface* is what the table exists to provide: `frameCount()`
  and `frame(ordinal)`. Slice 5 builds a real one privately behind them.
- **`clip` clamps; `concat` and `select` can fail — and the asymmetry has a reason.** clip, reverse
  and stride only ever drop or reorder entries that were already admitted, so they cannot introduce
  a spec mismatch and are total. The other two can: one adds sources, the other names positions.
  Every verb still routes through one `FrameSequence::of`, so admission — the range check and the
  unification — happens in exactly one place rather than once per verb.
- **`lain::io::canonicalUri` is the one canonicalisation in the tree — and collapsing the second
  one into it was the first thing review caught.** A `FrameRef` names its source by uri and nothing
  else, so two spellings of one path must produce one string or two references to one frame stop
  comparing equal. `graphio::templateKey` was already applying that rule with its own copy of
  `weakly_canonical`, so this slice both introduced `canonicalUri` **and** rewrote `templateKey` as
  a call to it: the concept keeps its name and its four call sites, the rule has one implementation.
  Letting the image and video openers each grow a third version is the exact shape this repo keeps
  catching — a key computed two ways eventually disagrees with itself, and the failure is silent (an
  invalidation that misses simply keeps serving the definition it was told to drop). `canonicalUri`
  has its own tests now rather than only indirect cover through the sequence opener.
- **A decode failure is deliberately not cached**, so one transient error is not remembered for the
  life of the source; re-reading is cheap next to being permanently wrong. And the ring is a ring
  rather than an LRU: for the sequential reader this whole design is built around, every eviction
  policy agrees, so an LRU would cost a data structure to serve a difference nothing can observe.
- **Found by a failing test: `core::Time` quantises to nanoseconds**, so a frame timestamp at
  1/24 s lands within a nanosecond of exact rather than on it. Harmless, and the reason the RATE is
  rational — the exact value lives there, and a timestamp derived from it is a convenience.
- CONTEXT.md's *frame source* entry is corrected: the ring cache and its lock are in the shared
  base, but what a source holds **open** is per medium — an image-sequence source holds nothing but
  paths, since a still is read whole by `io::read`.

**Slice 2 is built (2026-08-31) — THE MILESTONE'S CENTRAL CLAIM IS PROVEN.** `FramePosition`,
`lain::io::sequence`, the three nodes (`OpenSequence` / `FrameAt` / `ClipSequence`), the port types,
the codec, the binders, the manifest writer, and the `run` sweep. A folder of stills renders to
`out.####.png` in bounded memory through the production binary, with the missing-frame policy and
its exit codes exercised by hand. `ctest` **526/526** (+17), warning-clean, format-check clean.

- **The opener seam is `lain::io::sequence`, not `lain::io::media`.** Inside a namespace called
  `media`, the name `media` resolves to *itself* and shadows `lain::media` at every mention — so the
  facade would have had to spell `lain::media::FrameSequence` in full, everywhere, forever. Renaming
  it costs nothing and removes the trap.
- **A thin facade now, the registry at slice 5** (settled with the repo owner). `io::sequence::open`
  is today a direct call through to `io::image::openSequence`; slice 5 replaces the body with the
  extension-keyed registry when video supplies the second registrant. Every call site is already
  right, and no registry-with-one-member exists in the meantime — the same judgement slice 0 made
  about `plugins/io/video` having no aggregator.
- **`FramePosition` is a struct, and it had to be.** Every registry that makes a payload type
  first-class — port types, value codecs, cli binders, param editors — is keyed by
  `std::type_index`, so `using FramePosition = std::size_t` would have been indistinguishable from a
  plain count in all four at once.
- **There is deliberately no `serialize(Archive&, FrameSequence&)`.** A sequence's entries hold
  `shared_ptr<const FrameSource>`, which `data`'s reflection has no arm for, and `Archive` is
  direction-agnostic — so providing one would make `data::fromValue<FrameSequence>` *compile* and
  silently yield an empty sequence. Instead `sequenceManifest()` builds the document one way only,
  which makes the asymmetry structural rather than a rule someone has to remember. Each frame
  carries its own source uri rather than an index into a source list, per CONTEXT.md's *FrameRef*.
- **The natural name for a frame-position pin is `frame`, and `--frame` is the range option.** That
  collision turns out not to matter, and the reason is the design working: the sweep finds the pin
  **by type**, so its name is never typed. `--frame 5` is a one-frame range, which is also the only
  way to set a position from the cli — there is no second spelling to keep in step.
  `warnBoundaryCollisions` now reports a pin that shadows any of `run`'s own options, since such a
  pin is unreachable and was silently so.
- **A `####` field is required only when the range visits more than one frame.** Writing every frame
  to one path would exit reporting success having destroyed the input↔output correspondence; one
  frame to one path is unambiguous, and demanding a pattern there would be ceremony.
- **An invalid image counts as a missing frame.** Only a cleared output is suppression in flow's
  sense, but a position past the end of a sequence yields an *invalid* `Image`, and treating that as
  a write error would report the wrong cause and bypass `--on-missing-frame` entirely — leaving the
  policy unreachable for the case that most obviously needs it.
- **`onProcess` now RETURNS its status, and `Application::exit(code)` is the gui counterpart.** The
  stop policy requires a non-zero exit, and `onProcess` was `void` with its return discarded. The
  first attempt added `setExitCode`, arguing that changing a public virtual cost more than it was
  worth — **review overturned that, correctly**: only 2 of 6 delegates override `onProcess` (the
  rest inherit the default), and a one-shot work routine returning its own status is the shape that
  cannot be forgotten. `Application::quit()` already existed, so `exit(code)` is that same request
  with a reason, and `quit()` is now `exit(0)` — one mechanism, no third channel.
- **Two more "one rule, one function" collapses, both found while building.**
  `io::numberField` / `io::substituteNumber` hoist the `####` rule out of the image opener, because
  a sweep that substituted differently from how the reader matches would write files it could not
  read back. And `io::image::formatKeyOf` is now public so the sweep's `canEncode` preflight asks
  exactly the question `save()` will ask, instead of deriving the key a second way.
- **Fixed here, and slice 2 is what would have exposed it:** `bindDefaultInput` bound `inputs[0]`
  whatever pin it was asked about, and `Evaluation::bind` does no type checking — so the moment a
  graph gained a second boundary input, an unbound frame position would have re-bound the *image*
  pin with a gradient and nothing would have complained. It now takes the pin and refuses a
  non-image one.
- **The node kinds are in the factory but not the catalog.** Serialization names a node by its
  factory key, so the entries are needed regardless; the gui's Add menu is slice 7, the same order
  M8 used when the map node ran headless a slice before it was reachable from a menu.
- **Accepted cost, stated rather than claimed around:** `Evaluation::bind` marks the
  `GroupInputNode`, not the individual pin, so every node fed by *any* boundary pin recomputes each
  frame — not only the frame-position cone. Harmless for ADR-0018's shape, since a `FrameSequence`
  copy is a refcount bump sharing its source and ring cache, but it is not "only the changed cone
  recomputes" and should not be described that way until measured.

**Slice 2 review pass (2026-09-01).** Four design objections, all upheld, and in two cases the
repo's own documents already said so. `ctest` **534/534**, warning-clean, format-check clean; the
end-to-end sweep, both missing-frame policies and their exit codes re-driven through the real binary
unchanged.

- **`--frame` is a TYPED option: `core::Range`.** It was a `std::string` the app parsed three layers
  later. `core::Range {first, last, step}` carries its own literal form — `12` / `0-499` / `0-499x2`
  — the way `core::Version` already carries `1.4.0-rc.2`, and provides CLI11's `lexical_cast` hook so
  `cli.add_option("--frame", range)` simply works. **core names nothing of CLI11 to do it**: the hook
  is a plain signature found by ADL, the same shape `meta::enums` gives an enum option. A malformed
  range is now refused *by the parser*, before a graph is loaded, so the syntax has one home and
  cannot drift — the sweep test's "a range that is not one" case disappeared because the type makes
  it unrepresentable at that layer.
- **`Range` needed the name, so the planned slider helper is renamed `Bounded<T>`** (CONTEXT.md). It
  is not built, so this was a glossary edit; "range" most naturally means a span you iterate, and a
  slider's value is a bounded scalar.
- **Built and backed out: `media::select(sequence, Range)`.** It looked like the overload that would
  make the type earn its place beyond the cli — and it makes `select(seq, {1, 10, 2})` **ambiguous**,
  since three braced integers read equally as three positions or one strided range. A compile error
  rather than a silent misread, but a papercut on an existing call for an operation with *no
  production caller*. Reverted. The trigger for revisiting is a caller: M9's calibration view
  selection is the obvious one, and `selectRange` is the name that avoids the clash.
- **The manifest moved to a new `libs/media/serialize` target** and became declarative. It was in
  flowview building `data::Value` by hand, which contradicted ADR-0018's own words —
  *"`data::toValue` over the sequence through the existing reflection and codec spine, **not a
  bespoke writer**"*. Almost all of it was domain knowledge; the app keeps only when to write one,
  where, and in what codec. Follows the `flow::serialize` precedent exactly, so **`lain::media` still
  links no `data`**, as no foundational library in the set does.
- **What resisted reflection, and why the fix is not a bridge.** `FrameSpec::extent` is a
  `math::Vec2i`, a pure alias for `glm::ivec2` — ADL associates `glm`, so a `LAIN_SERIALIZE` in
  `lain::math` **compiles and is never found**, while one in `namespace glm` is global to the
  program and collides with the next target that wants it. It is flattened to `width`/`height` in
  `FrameSpec`'s own `serialize` instead. `FrameRef::timestamp` is a `core::Time` whose only accessor
  returns by value, and `Archive::member` needs an lvalue to write through on load — hence a small
  `ManifestFrame` carrying seconds. Everything else is `LAIN_SERIALIZE`.
- **A namespace cannot share a name with a function in the same scope.** `FrameSpec`'s `serialize`
  must be in `lain::media` for ADL, so the documents live in `lain::media` too and only the *target*
  is `lain::media::serialize` — precedented by `lain::io::image::codecs`, whose functions live in
  `lain::io::image`. Both traps are now recorded in CONTEXT.md as the boundary rule's corollary,
  after appearing in three `.cpp` comments and no document.
- **The manifest lost its `frameCount` key.** It is `frames.size()`, and this slice had already
  argued about position that *"writing down something derivable lets the two disagree"*. The
  document now round-trips as data, which the hand-built writer could not support, and its wire
  shape is pinned by a test because a manifest is read by things outside this program.

**Slice 3 is built (2026-09-01).** `lain::io::Stream` — the incremental, seekable transport — with
`ReadStream` / `WriteStream` over one local backend, and `io::read` / `io::write` reimplemented as the
whole-asset use of it. `ctest` **549/549** (+15), warning-clean, format-check clean; the slice-2 sweep
was re-driven through the real binary and its four output frames are byte-identical to their four
inputs, in order — every read landed on the right file and every write produced the right bytes
through the rewritten transport.

- **The backend hook is POSITIONAL, and that is the whole design.** `onRead(at, dst, n)` /
  `onWrite(at, src, n)` are handed the absolute position rather than tracking one, because the BASE
  owns the logical position. That makes refusal 2 — *transparently reopen and re-seek* — structural
  rather than a rule each backend has to remember, which is what lets a pooled handle be evicted
  between two reads with nothing above noticing. It also makes a seek free: it touches no backend at
  all, so a released stream stays released through one. Sabotaging it (the backend trusting its own
  physical position) fails the release-and-resume test **and** two seek tests.
- **`read` / `write` ride the stream, so a scheme has ONE backend.** WORK.md's slice text called the
  Stream a peer of `read`; it is a peer in the *interface* and the implementation underneath, which
  deleted `readLocal` / `writeLocal` and gave the slice a production caller three slices before its
  intended one. `test_read.cpp` / `test_write.cpp` and the whole image/data codec suite became its
  regression proof, unmodified — every file lain reads or writes now goes through it.
- **Both directions landed here rather than the write side waiting for slice 6** (settled with the
  repo owner). The muxer's AVIO **write** callback needs a transport for exactly ADR-0004's reason the
  reader's does, and `io::write` supplies the caller now, so the write side is not mechanism sitting
  unused. What its consumer would have pinned is pinned here anyway: a muxer **seeks back to patch its
  header**, so a WriteStream is seekable and is emphatically not an append-only sink.
- **A write re-acquire must NOT truncate — the one silent data-loss failure in the slice.** The first
  open creates/truncates (that is `createStream`'s contract, the same one `io::write` documents); every
  re-acquire after a `release()` opens `in|out`, which both preserves what was written and is what lets
  a positioned write land back over earlier bytes. Sabotage-verified: giving the re-acquire
  `std::ios::trunc` silently loses everything before the release, and the test catches it.
- **`finish()` is explicit and status-returning**, per ADR-0018 — *a failure in a destructor has
  nowhere to go* — and idempotent in the base. Letting a write stream go unfinished still flushes and
  closes it, so no bytes are lost; what is lost is the report, which is the honest division and is
  pinned by its own test rather than left as a comment.
- **Filling a read request is enforced at the seam, not promised by each backend.** `ReadStream::read`
  loops `onRead`, so a transport that can only return what one packet gave it cannot push its
  partiality onto every caller. EOF and failure stay distinct — a short count is the end, `nullopt` is
  a broken transport — because a decoder that reads a lost handle as a clean end truncates the asset
  and reports success.
- **No `flush()` and no scheme registry.** `finish()` covers the only real need, and one member is not
  a registry — the same judgement slice 0 made about a `plugins/io/video` aggregator and slice 2 about
  the opener facade. `openStream` / `createStream` branch on `isLocalScheme` exactly as `read` / `write`
  always have.
- `uri()` is `io::canonicalUri` of what was asked for, because a stream that reopens by a *relative*
  path is one `chdir` from silently touching a different file — and slice 5's `FrameRef` names its
  source by that same canonical string.
- **`io::localPath(uri)` is new, and it fixed a latent bug rather than only tidying two copies.**
  Turning a uri into a path is a one-liner — strip the scheme, keep the rest — which is exactly why
  it kept being written by hand, and the copies had already drifted: `io::image::openSequence` built
  a `fs::path` out of the **whole** uri, so `s3://bucket/frames` became a RELATIVE directory named
  `s3:` and the opener asked the working directory about it, reporting "neither a directory nor a
  pattern" for a resource whose real problem was that it is not local. It now returns
  `std::optional<std::filesystem::path>` — nullopt for a non-local scheme — so a caller that cannot
  serve a remote resource has to *say* so. Both refusals are pinned by tests.
  The scheme is now asked about in exactly one place: `openStream` / `createStream` treat "localPath
  gave me nothing" AS the unsupported-scheme branch, rather than testing `isLocalScheme` separately
  and converting afterwards.
- **Deliberately NOT done here: a `Uri` type.** The pressure is real and recorded — see the note
  below — but the change wanted is a core type reaching `FrameRef`, and it is better shaped against
  slice 5's opener as a third consumer than against two.

**Slice 4 is built (2026-09-01).** `ColorSpace` gains **`BT709`**, so slice 5's decoder has an
honest tag to hand back. Public surface: `image::toLinear(ColorSpace, float)` /
`fromLinear(ColorSpace, float)` in `colormath.h` (curves `inline` in `details/colormath.inl`, so
they still inline into `mapColorChannels` — this runs per colour channel of every converted image).
The curve is the **inverse Rec.709 OETF** per ADR-0018, with the spec's **rounded** constants
(4.5 / 0.018 / 1.099 / 0.099), matching how sRGB is already spelled here. `ctest` **560/560** (+11),
warning-clean, format-check clean.

- **`convert` composes THROUGH LINEAR, and the change is net-negative in code.** Three spaces would
  have meant six ordered pairwise arms; instead `fromLinear(dst, toLinear(src, c))` expresses every
  pairing in one `mapColorChannels` pass, so the pairwise if-chain and its
  `ensure(false, "…not supported")` bail arm were **deleted**, not extended. That arm is now
  unreachable by construction. It is also one pass rather than two: the intermediate stays a float
  inside a single channel visit, so an 8-bit image converting sRGB→BT709 is quantised **once**,
  where chaining two `convert` calls quantises twice. sRGB↔Linear is bit-identical to before (the
  same float ops with an identity on one side), so the pre-existing `[convert]` cases are the
  regression proof — and they do fail under the sabotages below, which is what makes that claim
  worth anything. ADR-0003's "adding a color space is additive" bullet is amended in place with
  what actually made it so.
- **Two functions, not eight.** The individual curves are `detail::srgbToLinear` / `linearToSrgb` /
  `bt709ToLinear` / `linearToBt709`, private in the `.inl`. `toLinear`/`fromLinear` deliberately
  echoes `detail::toUnit`/`fromUnit` sitting in the same file — the same "convert to and from a
  canonical intermediate" job. `encode`/`decode` was rejected: CONTEXT.md has both words spoken for
  by the codec seam (`ImageReader::decode`, `encode(asset) → Buffer`).
- **These are the enum's ONLY exhaustive `switch` in the tree, and that is deliberate.** Before this
  slice every `ColorSpace` site was an `==` comparison, so a new enumerator was invisible to the
  compiler and had to be found by reading. Adding one now fails the build with
  `-Werror,-Wswitch` — verified by temporarily adding an enumerator and watching it break.
- **`Unspecified` passes values through**, because `colormath.h` is public and `lain::log` is
  PRIVATE to the `image` target, so it cannot assert. Documented as a total-function fallback, not
  a meaning; `image::convert` rejects `Unspecified` one level up, where the logger exists.
- **Op-class enforcement needed no code change** — `ensureBlendable` and the to-Gray guard both test
  `== ColorSpace::Linear`, which already excludes BT709 correctly. What it owed them was tests, and
  "value-blending ops require Linear" now loops over every non-Linear space rather than naming sRGB,
  so the next standard joins the enforced set instead of slipping past a check written before it
  existed.
- **Three sabotages, all caught.** Aliasing `bt709ToLinear` to `srgbToLinear` fails the load-bearing
  "BT709 decodes differently from sRGB" case (mid-grey lands 0.2596 vs 0.2140 — the ~20% silent
  blend error the enumerator exists to prevent); making `fromLinear` the identity fails every
  round-trip **including the pre-existing sRGB one**; letting BT709 through `ensureBlendable` fails
  the `[ops]` case under `NDEBUG`.
- **Deliberately unchanged, both pre-existing and now stated rather than left as traps.**
  `convert(src, ColorSpace)` reads only the space tag, never `alphaMode()` — a nonlinear curve on
  Premultiplied colour is wrong and nothing catches it, dodged today only by the convention
  `BlurNode` follows (space first, alpha second). Now a documented limitation on `convert.h`; fixing
  it would change existing results inside a commit whose claim is that sRGB↔Linear did not move, so
  it belongs in its own change. And `pngreader`'s gAMA ≈0.45 branch still returns `sRGB` even though
  0.45 is the Rec.709 exponent — a PNG gamma chunk is a hint, not a Rec.709 tag, and retagging
  stills off a gamma number is exactly the guessing ADR-0003 forbids.
- **Found while verifying, NOT fixed here (pre-existing, unrelated):** the tree **does not build in
  Release**. `libs/memory/src/alloc.cpp:11`'s `isPowerOfTwo` is used only by a debug assert, so
  `NDEBUG` makes it dead and `-Werror,-Wunused-function` fails the build. Consequence worth
  weighing: every `#ifdef NDEBUG` enforcement test in the repo is release-only *and unreachable*, so
  that whole class has not run in some time. This slice's guarded cases were exercised in a
  throwaway Release build with the warning downgraded, and pass; the alloc.cpp fix is a one-liner
  for its own commit.

**Slice 5a is built (2026-09-02)** — the slice is two commits (the seam and the registry, then the
FFmpeg reader), because 5a is testable in the DEFAULT build and reviewing it apart from the FFmpeg
unknowns is the 6a/6b/6c shape that worked in M8. `lain::io::video` (the codec-free seam:
`VideoReader`, the reader registry, the container-extension claim, `VideoSource`, `open()`), the
extension-keyed `io::sequence` registry, the generated `registerVideoCodecs()` aggregator, and the
`io::extensionKey` hoist. `ctest` **574/574** (+14), warning-clean, format-check clean, and the
default (video-off) configuration builds and passes with it.

- **The seam is built unconditionally; only the CODEC is opt-in.** That is what makes ADR-0019's
  amendment true rather than aspirational: with no plugin, `.mp4` still routes to a video opener and
  is told *"this build has no video codec plugin"* — a missing capability — instead of falling
  through to the still opener and being told its file is not a directory. It also means the empty
  aggregator is not a stub: `registerVideoCodecs()` registering nothing, correctly, is the default
  build's whole video story, and `plugins/io/video/test` asserts the count in BOTH configurations.
- **The video reader registry is keyed by BACKEND NAME, not by format — the one place this seam
  deliberately does not mirror `io::image`.** An image codec claims a format (libpng decodes png and
  nothing else), so an extension key there is a capability. A video demuxer claims a whole family and
  identifies containers by CONTENT — FFmpeg opens an mp4 called `.bin` — so an extension key would be
  a second, worse answer to a question the demuxer already answers. The extension selects the
  **medium**; being registered selects the backend.
  - **"Backend" is the exact word, and it was corrected on review** (raised by the repo owner, 5a's
    first draft said *codec name*). A video file has two aspects — a **container** (mp4, mov, mkv:
    what muxes the streams) and a **codec** (h264, prores, vp9: what encoded the frames) — and
    `"ffmpeg"` is neither. It is one implementation covering both, which is exactly what a
    platform-native alternative (AVFoundation, Media Foundation) would also do. Two words survive
    on purpose: what is REGISTERED is a backend, what is BUILT is "the video codec plugin", which
    is what ADR-0019, the README and `LAIN_IO_VIDEO_FFMPEG` all call the dependency.
- **Container and codec are NOT two registries and two interfaces — and the reason is not "FFmpeg
  does both".** The decomposition that looks right (`Demuxer -> Packet`, `Decoder(Packet) -> Image`)
  cuts through the middle of one thing rather than between two:
  - **A demuxer's output is not codec-neutral.** The same H.264 stream leaves an MP4 as
    length-prefixed AVCC with its parameter sets in the container's `extradata`, and a TS as in-band
    Annex-B — which is why FFmpeg needs a *bitstream filter* to convert one to the other. So the
    "clean" seam is wrong until lain also models bitstream filters, and its wrongness would be
    discovered by a file, not by a compiler.
  - **"Decode frame 412" is one algorithm across both halves**: seek to the preceding keyframe
    (demuxer knowledge), flush, decode forward matching by pts (decoder state). Splitting the
    interface does not decouple that; it routes the coupling through a public boundary and a
    `Packet` type lain would then own — bytes, pts, dts, duration, keyframe flag, and that
    codec-specific extradata.
  - **The swap the seam exists for replaces both halves at once**, so two registries would be two
    registries with one member each — what slice 0 declined for the aggregator, slice 2 for the
    opener facade and slice 3 for the scheme registry.
  - **Where the distinction IS real, it is data, not structure.** A reader reports the container and
    codec it found (5b, with a log line consuming them); `videoExtensions()` is a **container**
    claim and says so; and at the writer (slice 6) the codec is an *option* chosen by availability
    while the container follows the extension — one `VideoWriter` per backend all the same.
  - **The trigger to revisit**, so it is not rediscovered: split when lain owns a **demuxer** (a
    capture container, M9's territory) and wants a standard codec to decode its packets, or when a
    second backend covers a genuinely disjoint set. The first type to build then is `Packet`, and
    whoever builds it needs the AVCC/Annex-B trap above in front of them.
- **The container-extension list lives in the SEAM, not in a backend plugin**, and the diagnostic is
  the reason. A list owned by the plugin disappears with the plugin, taking `.mp4`'s meaning with it
  — which is the vocabulary loss the amendment exists to prevent. What a file is *called* is a claim
  about which medium it belongs to, and that claim survives having no codec at all.
- **Dispatch is by extension with ONE default, and the asymmetry is load-bearing.** Video is
  addressed by what a file is called; the image medium is addressed **structurally** — a folder has
  no extension to key on, and `shot.####.png`'s extension names the still format rather than the
  sequence's. So video registers claims and stills are the default, which is also the honest error
  path: `openSequence` already says *"neither a directory nor a ####-numbered pattern"* for anything
  it cannot take.
- **The registry lives in `open.h`/`open.cpp` and the wiring in `openers.h`/`openers.cpp`** — the
  only translation unit in the library that names a medium. Adding a medium touches the second pair;
  a medium outside this tree touches neither. (**Corrected 2026-09-02:** `registerSequenceOpeners`
  was first declared in `open.h` while living in `openers.cpp`, which is a function hiding in a file
  that does not carry its name. One header, one translation unit — and here it also puts the seam's
  only dependency-bearing function out of the header that exists to have none.) `registerSequenceOpeners()` is the app's single wiring point, beside
  `registerImageCodecs()` and `registerVideoCodecs()`, and it is deliberately separate from them:
  those wire CODECS into a medium, this wires MEDIA into the dispatcher.
- **`VideoSource` is in the seam, not the plugin.** A plugin implements `VideoReader` and nothing
  else — the ring cache, the lock, the range check and the spec enforcement are `media::FrameSource`'s
  and are written once for the medium, exactly as `ImageSequenceSource` leaves libpng with only
  `ImageReader` to fill in. Its `timestampOf` override is what makes VFR free, and a `[io::video]`
  test pins it by reporting timestamps **no rate could produce**, since the base's default is
  rate × ordinal and a test that used real rate-derived values could not tell the two apart.
- **`VideoReader::open` takes an already-open `ReadStream`, never a uri.** ADR-0004's split, made
  structural: a codec plugin cannot open a file because it is never given a name. The prebuilt FFmpeg
  is `--disable-network`, so for video this is not a nicety — a future `s3://` reaches the decoder
  through lain's transport or not at all.
- **`io::extensionKey` hoisted into `lain::io`, beside `canonicalUri` / `localPath` / `numberField`,
  and it collapsed TWO copies rather than one.** `io::image` and `io::data` each had a private
  `formatkey.h`, the second carrying a comment explaining that it was duplicated *"so neither seam
  depends on the other"* — a reason that had already expired, since both link `lain::io`, which is
  where the shared home was all along. Three seams now ask this question (a codec key, a container
  claim, a medium dispatch), and a format decided in three places disagrees with itself over
  `clip.MP4` silently: the wrong opener, or none.
- **A `core::Factory` only grows, so the empty-registry case got its own executable.** The default
  build's behaviour — no codec, an honest refusal — cannot share a process with a test that registers
  a fake one, and the alternative was a rule about the order Catch2 runs cases in. One case, one
  binary, no ordering to remember.

**Slice 5b is built (2026-09-02) — `--video src.mp4` WORKS, and M10 slice 5 is COMPLETE.**
`FFmpegVideoReader` in `plugins/io/video/ffmpeg`: a custom `AVIOContext` over slice 3's `Stream`,
the demux-scanned frame table, keyframe seek + pts matching, `swscale` to RGB8 with range
expansion, and the colour policy. `ctest` **589/589** (+15), warning-clean, format-check clean, and
a real mp4 swept to `out.####.png` through the production binary.

- **The container index is KEYFRAME-ONLY, so the frame table is always demux-scanned.** ADR-0018
  and CONTEXT.md both said the index was read "when present" and the scan was the fallback; that is
  not achievable. An index answers *where do I start decoding*, never *what is frame 412* — the
  question the whole model rests on. Both documents are corrected in place, and the index keeps the
  one job it is good for: seek points. **Cost, stated rather than discovered later:** one sequential
  read of the container's packet headers at open. Nothing is decoded in it.
- **The table is sorted into DISPLAY order, and that is the slice's sharpest edge.** With B-frames
  the packets arrive pts 0, 1536, 512, 1024, 3072 … so a table left in arrival order answers
  `image(1)` with the frame stored second — a **wrong image, not an error**, which no exception and
  no eyeball would catch. Sabotage-verified: removing the `stable_sort` fails the B-frame case and
  nothing else, which is exactly why that second fixture exists.
- **A seek matches by PTS, never by counting.** After seeking to a keyframe the decoder hands back
  frames before the target, and with B-frames it reorders them; counting would land on whichever
  frame happened to be third. `m_nextOrdinal` keeps sequential access — the render loop's whole
  pattern — from seeking at all, and a failed decode resets it to *nowhere* rather than trusting a
  decoder whose position is unknown.
- **A weak test was found by sabotage and replaced.** "Random access equals sequential access"
  originally opened a fresh sequence and asked for frame 7 — which decodes *forward* from the start
  and never seeks, so it passed with seeking removed entirely. It now decodes frame 11 first, so
  reaching 7 must seek backwards past the ring, and compares **byte for byte**: a seek that landed
  on the wrong keyframe or forgot to flush produces a plausible image that differs.
- **The colour rule is a pure function refusing at OPEN**, so a mislabelled frame never reaches a
  graph. All three axes are examined, not just the transfer — and that is not theoretical: the PQ
  fixture came out of the encoder with its **matrix** tag surviving into the container and its
  transfer stripped, so a policy reading only the transfer would have accepted BT.2020 material as
  BT709. An explicit **sRGB transfer is believed** (`ColorSpace::sRGB`) rather than flattened to
  BT709: lain has that space exactly, and believing an explicit tag is the same rule the refusals
  follow. Untagged is BT709 **with a log line** — the guess is said out loud.
- **`VideoReader::container()` / `codec()` landed with their consumer**, not before it: `io::video`
  logs *"opened clip.mp4 — mov,mp4,m4a,3gp,3g2,mj2/h264, 64x48 RGB8 BT709 · 24 fps, 12 frames"*.
  That is this session's container/codec discussion arriving as **data**: two facts about one file,
  neither derivable from the other, neither in its name — and an accessor with no caller is how an
  unreachable feature stays unreachable (M5's bug six).
- **Fixtures are embedded bytes, and there are three** because one could not carry the properties:
  h264/mp4 (three GOPs, the realistic case), MPEG-4 part 2 with `-bf 2` (display order ≠ demux
  order, which videotoolbox will not produce), and a one-frame PQ/BT.2020 clip. The prebuilt is
  `--disable-avdevice`, so there is **no lavfi/testsrc** to draw from: the frames are written as raw
  RGB24 by a python snippet recorded in the header and piped through the encoder. Each frame is a
  FLAT colour — these codecs are lossy, so a frame identity that survives encoding has to be a large
  uniform area rather than a marker pixel.
- **Live proof through the real binary:** a 12-frame mp4 rendered to `out.0000.png` … `out.0011.png`
  with the rendered stills tracking the fixture's per-frame colour, and **the file opened exactly
  once for the whole range** — ADR-0018's "one Evaluation across the render keeps the decoder warm",
  observed rather than asserted.
- **Also true now: slice 0's `--licenses` limitation is discharged.** flowview links the video
  aggregator, which links the plugin, which links FFmpeg — so the notice it prints names a
  dependency the binary actually has. (5a made that true; 5b is what makes it *useful*.)

**Slice 6a is built (2026-09-02).** The write seam, in the DEFAULT (video-off) build: `VideoWriter`
(the open-push-finish handle), `VideoCodec` + `VideoWriterOptions`, `writerRegistry()`,
`canEncode(FrameSpec)` / `refusedSpecReason`, `openWriter`, the one-shot `save(uri, sequence)`
transcode facade, and `isVideoUri` beside `videoExtensions()`. `ctest` **589/589** in the default
configuration (+11 cases, all of them reachable with no codec at all), warning-clean, format-check
clean. Nothing here names FFmpeg; slice 6b is the backend.

- **The codec option is a FAMILY, not an encoder name** — `VideoCodec { Auto, H264, HEVC, ProRes,
  FFV1, MJPEG }`. The build order said "encoder selection is by availability"; what it did not say
  is what a CALLER may therefore ask for, and a raw name is the wrong answer:
  `h264_videotoolbox` is a fact about one platform and one build, so a command line or a saved
  document naming it stops working on the next machine — the exact thing "never by a hardcoded name"
  exists to prevent. A caller names the JOB; the backend probes and **reports** what it found
  through `codec()`. **`Auto` refuses rather than falling back** to an archival family: a delivery
  render that quietly became MJPEG is a wrong answer that looks like success, which is the same
  argument the missing-frame policy makes about a silently shortened video.
- **`canEncode(FrameSpec)` lives in the SEAM, not in a backend**, because what it refuses is true of
  the medium rather than of an implementation: no video codec in the LGPL set carries alpha, and
  neither Linear nor Unspecified is a transfer a container can state. It is the `ImageWriter` rule —
  *refuse, never silently degrade* — applied to a spec instead of an image, and quiet (no logging)
  for the same reason `io::image::canEncode` is: a render asks it once, before the first frame.
- **Linear is the arm that is easy to get wrong, and refusing it is not pedantry.**
  `AVCOL_TRC_LINEAR` exists and the file would encode fine — but slice 5b's read policy does *not*
  refuse it, so the result reads back tagged BT709 with every value off by the ~2.2 gamma,
  invisibly. Tolerating it would make lain's own round trip relabel footage, which is the failure
  ADR-0018 refuses on the way in, arriving by the other door.
- **Untagged is tolerated on the way IN and refused on the way OUT**, and the asymmetry is the
  point: reading an untagged file is a guess about someone else's material, where rejecting it would
  be loud and wrong; writing one is **authorship**, and a guess baked into a file will outlive the
  guess.
- **`openWriter` IS the preflight** — the video peer of `canEncode` + `formatKeyOf` on the still
  path, arrived at by a different route because opening an encoder already answers the question.
  A caller with 500 frames asks once, before the first is encoded.
- **It does not loop backends the way `open()` does, and that asymmetry is load-bearing.** A reader
  probes because only content identifies a container; a writer is *told* which container to write,
  so there is nothing to probe — the loop exists only to let one backend refuse a codec family
  another could serve. One thing genuinely differs and is stated rather than discovered:
  `createStream` **truncates**, so a refused attempt has already emptied the file. Harmless, since
  nothing valid existed either way and nothing valid exists until `finish()`.
- **`isVideoUri` is in `open.h`, beside the list it consults**, not in `save.h`. It is a question
  about a NAME, which is why it answers identically in a build with no codec — and why both
  directions plus the render sweep can share one answer instead of scanning the list in three
  places. **The read claim and the write CAPABILITY are deliberately different sets**: an LGPL
  FFmpeg reads `.webm` and cannot encode one, and `openWriter` is where that is said out loud.
- **`save(uri, sequence)` finishes the writer on the failure path too**, so a hole mid-transcode
  leaves a closed, visibly short file rather than a headless one — pinned by a test that counts
  `finish()` calls, not by a comment. `finish()` is idempotent for the reason the trailer makes
  obvious: a second one would corrupt the file it is meant to close.
- **The empty-registry case joins `test_nocodec.cpp` rather than getting a third binary.** That file
  already is "the default build's video story, in its own executable", and an empty *writer*
  registry is the same fact about the same build. Its new case asserts the spec is writable and the
  path is claimed *first*, so the refusal is demonstrably about the missing **capability** and
  nothing else.


**Slice 6b is built (2026-09-02) — a render can produce a real video file.** `FFmpegVideoWriter` in
`plugins/io/video/ffmpeg`: encoder selection by availability, a muxer over an `AVIOContext` built on
slice 3's `WriteStream`, swscale RGB→the encoder's pixel format with range compression, the
write-side colour tags, and the drain/trailer/finish path. Registered beside the reader in the same
`registerCodec()`, so no aggregator or CMake-property change was needed. `ctest` **619/619** with
video on (+13 cases over 6a; the default configuration is unchanged at 589, which is the point —
this slice is all plugin), warning-clean, format-check clean.

- **The availability test is `avcodec_open2` SUCCEEDING, not `avcodec_find_encoder_by_name`
  returning non-null.** An encoder can be compiled into a build and fail to open for want of a
  device — NVENC on a machine with no NVIDIA card — so a name lookup answers a different question
  than the one asked. `avformat_query_codec` runs first and folds container/codec compatibility into
  the same question, which is what makes `.webm` refuse with *"no encoder is available for the webm
  container"* rather than failing later and obscurely inside `write_header`.
- **VAAPI and V4L2-M2M were DROPPED from ADR-0019's delivery table**, which is amended in place.
  Neither accepts a software frame — both need an `AVHWFramesContext` and an uploaded surface — so
  listing them converts a clean absence into a confusing failure to open a codec that visibly
  exists. Real consequence, stated rather than discovered: a Linux box with no NVIDIA card has no
  delivery encoder here and `auto` refuses on it.
- **`AV_CODEC_FLAG_GLOBAL_HEADER` is the one line between a playable MP4 and a file that decodes to
  nothing.** MP4/MOV keep the parameter sets in the container's extradata rather than in the
  bitstream, so an encoder not told to emit them out-of-band produces a stream with no SPS/PPS
  anywhere the demuxer will look. The mp4 round-trip test covers it, and covers the muxer's
  **seek-back** at the same time — patching the mdat size and appending the moov is what slice 3's
  positional, non-truncating `WriteStream` was specified for, three slices before this caller
  existed.
- **`av_guess_format` picks the muxer**, not a table in lain. FFmpeg already owns extension→muxer
  and knows things a second table would eventually disagree with the demuxer about. The name is
  *synthesised* (`"x." + format`) rather than passing the uri, so the plugin still receives a format
  claim and never a path it could open (ADR-0004).
- **The pixel format is chosen for the FAMILY, and for FFV1 that is what makes "lossless" true.** An
  8-bit RGB→YUV matrix is not invertible, so FFV1 over `yuv420p` is a lossless encoding of a lossy
  conversion: the file round-trips and the frames do not. FFV1 offers `bgr0`/`bgra`/`rgb48le`, so
  the byte-exact test is possible without inventing a codec — the `qtrle` fallback that was on the
  table is **not needed**. Everything else prefers `yuv420p`, which every player decodes.
- **Range compression mirrors the reader's expansion.** RGB is always full range and a YUV video
  stream is conventionally limited, so a full-range frame written into a limited-range-tagged stream
  without `sws_setColorspaceDetails` on the DESTINATION comes back crushed — swscale's default is
  not the range the tag claims.
- **An RGB-coded stream is tagged `AVCOL_SPC_RGB`, not BT709.** No matrix was applied, and claiming
  one states a conversion that never happened; `colorSpaceFor` accepts it, so the round trip still
  closes. The two colour directions live in ONE file with a test asserting their composition is the
  identity — split across two files is how they drift, and the drift is silent.
- **The AVIO bridge was HOISTED, not duplicated** (`src/aviobridge.{h,cpp}`). The seek callback is
  identical in both directions because seeking is `io::Stream`'s, not either subclass's:
  `AVSEEK_SIZE` is a question, `AVSEEK_FORCE` is masked, the three whences map the same. The reader
  moved onto it as a pure refactor and its 17 cases pass unchanged. The write callback's
  `const uint8_t*` — which the read callback does not have — is FFmpeg's own asymmetry and is
  commented so nobody tidies it up.
- **Three test premises, one of which was wrong and is recorded.** mp4 **does** carry FFV1 in a
  current FFmpeg (ISO/IEC 23001-17), so it is not a container/codec mismatch; QuickTime genuinely
  refuses it while carrying h264 and ProRes happily, which is the sharper test anyway — a container
  with other options that must still refuse rather than substitute.
- **The licence claim is executable, not prose.** `test_encoderpolicy.cpp` asserts both that no
  candidate list names libx264/libx265 **and** that `avcodec_find_encoder_by_name` cannot find them
  in the linked library — the second is the property ADR-0019 actually rests on, since linking a
  build configured with them relicenses the distribution regardless of which encoder is called.
- **Delivery cases SKIP rather than fail** when the platform provides no encoder, the discipline the
  `[gpu]` tests already use. That keeps ADR-0019's conditionality visible in the suite instead of
  hidden behind an `#ifdef`.

**Slice 6c is built (2026-09-03) — M10 slice 6 is COMPLETE.** The render loop's encoder, `--codec`
and `--rate`, the sequence-to-video transcode arm, and `ConvertNode`. `ctest` **595/595** default,
**625/625** with video on, warning-clean, format-check clean. Driven through the real binary:
a folder of stills renders to `out.mkv` as `matroska/ffv1` and reads back as four PNGs; `--codec
h264` gives `mp4/h264_videotoolbox`; a truncated render exits 1 leaving a **valid four-frame file**;
and the same document on a video-OFF build still lists its whole interface and refuses with a
missing **capability**.

- **`sweep` splits into `sweep` + `sweepFrames` so writers are finished in exactly ONE place.**
  ADR-0018's default policy is "finalise what exists, report the ordinal, exit non-zero", and a
  `finish()` that must be remembered at each of five return sites is one that will eventually be
  forgotten on the path that matters — the failing one. A file that was never finalised is not a
  video at all, which is a worse failure than the truncation it was meant to report. Proven live,
  not just asserted: a render past the end of its footage exits 1 and the file it leaves opens with
  exactly the frames that rendered.
- **The `####` rule became two rules with one home, and they are exact inverses.** A still output
  needs a frame field whenever the range is longer than one; a video output must NOT have one,
  because a container holds the whole range and a numbered pattern asks for one video per frame.
- **`--codec` is a FAMILY, and the first version silently substituted.** `CheckedTransformer`
  rewrites the matched string to the enum's underlying NUMBER, so `runmode` failed to parse `"4"`
  and fell back to `Auto` — `--codec ffv1` rendered **h264**, announcing it only in a log line. Two
  changes, because one would not have been enough: the option now uses `IsMember`, which validates
  and leaves the name alone; and `codecFamily` returns `std::optional` and the render REFUSES on an
  unparseable name instead of defaulting. **Found by driving the real binary** — every test passed
  with the bug in place, because nothing then compared what was asked for against what was written.
  The regression test now does, in the only shape that can tell them apart: QuickTime cannot carry
  FFV1 but carries h264 happily, so `--codec ffv1 --result out.mov` must refuse, where a silent
  fallback would succeed.
- **Where the rate comes from:** `--rate`, else the one specified rate among the bound sequences
  (found **by type**, the rule that already finds the frame position), else refuse. Two bound
  sequences with two different specified rates is refused naming both, mirroring `media::unify`'s
  refusal to reconcile them — choosing one is a retime. `--rate` is typed like `--frame`:
  `media::FrameRate` gains `parse` + CLI11's `lexical_cast` hook, so a malformed rate is refused by
  the parser before a graph loads. **It accepts `24` and `30000/1001` and refuses `29.97`**, which
  is the type's whole reason for existing — and note the asymmetry with `core::Range`, whose
  `toString()` IS its literal form while `FrameRate::toString()` is a deliberately approximate
  display form.
- **OPEN, deliberately: a stride does not retime.** `--frame 0-499x2` writes 250 frames at the
  source rate, so the result plays twice as fast. The alternative — dividing the rate, so the output
  covers the same duration at half the temporal resolution — was argued on the grounds that the
  input↔output correspondence the missing-frame policy protects is a correspondence in TIME as well
  as in count. The repo owner is undecided and asked to keep this and revisit, so it is **recorded
  here as an open decision rather than a settled one**. `--rate` says otherwise either way.
- **`ConvertNode` (`flow-example`) is what lets a graph COMPLY with the writer's refusals** —
  without it the strict rule is just a wall. Two params rather than one, because conflating them is
  how a pipeline acquires a silent lie: **`assume` DECLARES what an untagged image already is** (no
  conversion — there is no source curve to convert from, and `image::convert` refuses `Unspecified`
  for that reason), and **`colorSpace` is the TARGET**, reached by conversion. A tagged image is
  never retagged. Alpha is deliberately absent: it has the same untagged problem a third time and no
  video codec carries it, so the one caller this node exists for would never use it.
- **A bug in ConvertNode, caught by the sweep and worth keeping in mind for every node of this
  shape:** on an invalid input it returned early, LEAVING the previous frame's image in its output
  slot — so a suppressed frame read downstream as "this frame rendered" and the missing-frame policy
  never fired. It now propagates the invalidity. The existing sweep tests never saw it because
  `FrameAt` fed the boundary directly; inserting any node between a source and the output exposes
  it.
- **Why `ConvertNode` was needed at all was a pre-existing gap, found here and FIXED 2026-09-03:**
  `pngwriter.cpp` recorded **no colour chunk** (no `sRGB`, no `gAMA`), so a still saved and reloaded
  through lain's own codec came back `Unspecified` — which is why the natural stills→video path
  needed a Convert in it. The audit that followed found the same hole in the TIFF and JPEG writers
  (never recorded anywhere) and two invented claims besides, and the whole question is now settled by
  **[ADR-0020](docs/adr/0020-codec-colour-tag-policy.md)**. `ConvertNode` remains what a caller uses
  for genuinely untagged input; it is no longer structural.
- **The writer's input boundary is 8-bit Gray or RGB**, refused by name otherwise. The reader's
  boundary is RGB8; widening Gray to a colour stream loses nothing, so refusing it would be stricter
  than the rule requires — "no LOSS" is not "no conversion". 16-bit and float are refused, since no
  delivery codec here takes them.
- **A `FrameSequence` output to a container TRANSCODES** instead of writing a manifest, which is
  what gives `io::video::save` a production caller in the slice that introduces it. A non-container
  path still writes the manifest, which stays the default.
- `--codec` and `--rate` joined the reserved-name set, so a boundary pin called `rate` is reported
  as unreachable rather than silently shadowed.
- **`gui::enumCombo` has a live caller again.** Convert's `PixelFormat`/`ColorSpace` params are the
  first enum params in the tree, and they edit through the combo that has had no caller since the
  preview-size dropdown was retired on 2026-07-31 — a real user, rather than an API kept alive to
  dogfood itself.
- **Not asserted, and the reason is recorded in the test:** the exact rate of a very short Matroska
  clip. A 4-frame file reopens at 24.08 rather than 24, because libavformat estimates the rate and
  the container's timebase is what it has to fit into. A `video_track_timescale` fix was tried and
  **removed when it changed nothing measurable** — unverified code does not go in. Exact declaration
  fidelity is pinned where it belongs, in the plugin's 6-frame round trip.

**Slice 7a is built (2026-09-04)** — the slice is two commits (the registry and the rewiring, then the
sequence made first-class). 7a lands the deferred **GUI view registry** and moves the cache and three
panes onto it, with **no new behaviour**: the only type registered is `image::Image`, so the existing
suite is the regression test. `ctest` **648/648** (+3), warning-clean, format-check clean, both
headless paths unchanged.

- **The registry has TWO HALVES, and that is the whole design.** A value is shown in two places with
  two different lifetimes: a **poster** (the still that stands for it in a list, uploaded into the
  edit-refreshed `PreviewCache`) and a **view** (the Preview pane rendering, which owns state — zoom,
  and at 7b a playback position and a decoded frame). That state changes with **no edit at all**, so
  it cannot live in a cache rebuilt on edits; and a poster must not be re-produced per drawn frame,
  because producing one may decode. One registry, `ValueViews`, keyed by `type_index`, sibling of
  `ParamEditors`: that one is how a type is WRITTEN, this is how it is SHOWN.
- **A poster is a `PortValue`, not an `image::Image`** — the one non-obvious signature, and it is
  what lets the image case **alias its own payload** (`PortValue::alias`, ADR-0014's mechanism). The
  thumbnail of an image IS that image; returning one by value would charge every image port a deep
  pixel copy on every edit, which is exactly the cost M5 slice 1 took off the edge path.
  Sabotage-verified on an **address** comparison.
- **Five hardcoded `typeid(image::Image)` branches retired**, which is the measure of the commit: the
  cache chose what to upload, the Inspector and Interface chose what drew a thumbnail, the Preview
  pane refused everything else outright, and the Interface pane's bind gesture was image-only. Every
  port is now one line of `Evaluation::describe()` — the PortType capability every pane already read
  — plus a thumbnail iff the cache has one. Saving stays image-only, and says why: it writes the
  VALUE, not the thumbnail.
- **`ValueView::summary` was designed and dropped.** The Preview header needed a type-specific tail,
  and `describe()` already answers it for every type through the capability flow owns — a per-view
  string would have been a second answer to a question already answered, and would have had to be
  written again for every new viewable type. The header now reads `tint [a1b2] | result : Image 64x64
  RGB8 sRGB Straight`, which is MORE than the old hand-built one, from less code.
- **`gui::Texture::extent()` is new, and it is not a convenience.** The panes sized a thumbnail from
  `value.get<image::Image>().extent()` — impossible for the sequence pin 7b adds, whose value is not
  an image at all. The aspect belongs to **what was uploaded**, so the texture is the only honest
  source, and `ImageView` stopped reaching back into the value for a fact its own texture holds.
- **The image bind gesture moved into `ParamEditors`, which deleted the last type branch in a pane.**
  The file dialog and `io::image::load` came verbatim out of `interfacepane.cpp`, where they were an
  `if (type == image::Image)` sitting beside a fall-through to this registry — the pane deciding by
  type what ADR-0005 says the type decides for itself. It has to land here rather than at 7b, or
  removing the branch would regress the gesture.
- **`ImageCanvas` is the extracted zoom/pan/fit widget** (cursor-anchored wheel zoom, drag panning,
  the logarithmic percent toolbar), split into `drawImage` + `drawToolbar` so a view owns its own
  layout — 7b's player puts a transport row between them. **`previewFit` / `thumbnailBox` moved out
  of `AppContext`** to sit beside it: they were in the shared model only because there was nowhere
  else, and the move is also what keeps the new tests driver-free, since `appcontext.cpp` reaches the
  app delegate and imnodes while the thumbnail arithmetic reaches nothing.
- **A lifetime hazard the design creates, and where it is discharged:** the Preview pane now owns its
  view, and at 7b that view owns a GPU texture — which must be released while its Context's ImGui
  backend is alive. Pane members are destroyed AFTER the window's Context, so `MainWindow::onShutdown`
  calls `releaseView()` beside `PreviewCache::clear()`. Missing it is a crash on exit, not a leak.
- **New tests (3, `[views]`), one sabotage, caught.** The alias is pinned by an **address**
  comparison (make it copy → fails); plus the aliased poster outliving the slot it came from, an
  unregistered type postering nothing and making no view, and two view instances being distinct.
  `test-flowview` gains `lain::gui` + `Vulkan::Loader` (libs/gui's own recipe) and stays driver-free:
  no `gui::Context` is ever constructed.

**Slice 7b is built (2026-09-04) — M10 slice 7 is COMPLETE, and so is MILESTONE 10.** The frame
sequence becomes a first-class thing to look at and to drive: the player, the palette entries, and the
`FrameSequence` / `FramePosition` boundary editors. `ctest` **652/652** with video on (+4), **626/626**
in the default video-off configuration, warning-clean, format-check clean; both headless paths
unchanged. **gui-mode live-verified by the repo owner 2026-09-04** — no crashes.

- **A sequence's poster is its first frame, decoded** — the second customer 7a's registry was shaped
  for, and where its `PortValue` return type earns itself: unlike an image's, this poster cannot be
  aliased (there is no image on the port to point at), so producing one is work, and it happens once
  per edit rather than once per drawn frame.
- **The player decodes by FRAME IDENTITY, not by position** — `sequence.frame(position)` (which does
  not decode) against the `FrameRef` of what is on screen. Position alone is a real bug: a re-run can
  rebind a *different* sequence onto the pin while the transport sits still, and the view would go on
  showing the old footage. It also records the attempted frame **whether or not the decode
  succeeded**, so a broken file is not re-decoded 60 times a second.
- **Two states that are values, not failures, and read as such.** An **empty sequence** (a folder
  that exists and holds no images) says so and posters nothing — it still has a view; and a frame
  that will not decode says *"frame N could not be shown"* and **clears** the texture. That second
  one is slice 6c's `ConvertNode` bug in a new place: a stale frame presented as the current one is
  worse than a visible hole. ("Shown" rather than "decoded" because the upload can refuse too, and
  the view has not established which.)
- **Playback drops frames rather than sliding behind the clock** — it advances by however many whole
  frames the elapsed wall time covers at the sequence's own rate, which is ADR-0018's "best effort"
  made literal. One decoder behind a mutex cannot promise a frame per refresh, and playing 4K in slow
  motion while claiming to be at rate would be the dishonest alternative. An unspecified rate (a
  folder of stills has none) plays at 24 fps **with a tooltip saying so**, rather than implying the
  footage has a rate. It stops at the last frame; a loop is a choice, not a default.
- **An INSPECTION player, and the boundary is structural, not a rule.** It decodes straight from the
  value on the pin and never re-runs the graph — so it shows the footage a node holds, not the
  graph's output at that frame. Driving a render is binding a `FramePosition`, which is the Interface
  pane's new editor; the transport that does it for you is the follow-on ADR-0018 names.
- **`media::FrameSequence` binds through `ParamEditors`** — **File... / Folder...** through
  `io::sequence::open`, both modes because the opener dispatches by what the uri IS (a video file by
  its extension, a folder or a `####` pattern structurally). It shows `FrameSequence::toString()`
  rather than a path, because a bound sequence is not one: it may span several sources, and the value
  no longer remembers what was typed to get it. This is what 7a's branch removal was for — before it,
  a `FrameSequence` boundary input read **"(no editor)"** and could not be bound at all.
- **`media::FramePosition` edits as a plain drag, deliberately unbounded.** An editor is handed only
  `(label, type, value)`: it cannot know WHICH sequence a position indexes, and a bound taken from
  the wrong one is worse than none. That bound is the per-value hint ADR-0005 already names as a
  refinement, and belongs to the driving transport.
- **The dialog filters were hiding video.** `editPath`'s File... offered Images only — with a comment
  already admitting the gap — so an `openSequence` node's `path` could not be pointed at an `.mp4`
  through the gui. The filters now come from **`io::video::videoExtensions()`**, the seam's own one
  list, so the dialog cannot offer a different set from the opener that will be handed the result,
  plus **All files**, because a footage uri is legitimately anything an opener claims.
- **`openSequence` / `frameAt` / `clipSequence` are on the menu**, in a **Sequence** category of their
  own — which reaches the menu bar's Add, the canvas right-click palette and the Nodes pane at once,
  since all three read `nodeCatalog()`. A category rather than a source plus two filters: what they
  have in common is the payload they pass, which is what a user reaching for them is looking for.
  Canvas colours for the three payload types too, so a footage graph is legible instead of falling to
  the name-hash fallback.
- **New tests (4, `[views]` + `[catalog]`), and a second sabotage caught.** The poster is pinned by
  which frame it decodes (make it frame 1 → fails), plus an empty sequence postering nothing while
  still having a view. `[catalog]` pins the invariant a hand-added menu entry breaks **silently**:
  every catalog key must be creatable by the factory, or the menu item does nothing — M5's bug six
  from the other direction, and the exact failure mode of adding a category by hand.
- **Milestone 10 is complete.** A folder of stills or a video file opens as one sequence, renders to
  numbered stills or to a container, and is now something you can see and drive in the gui. What was
  deliberately left out is unchanged and listed under *Not in this milestone* above — the driving
  timeline, a handle pool, a decoder pool, BT.601 / BT.2020 / PQ / HLG, device capture, and realtime
  playback of processed output. **`core::Uri` is the queued follow-on below.**

#### Queued: `core::Uri` — an identity, not a path algebra (raised 2026-09-01, build after slice 5)

A uri is a bare `std::string` everywhere it matters — `io::read` / `write` / `openStream`,
`media::FrameRef::source`, `FrameSource::uri()`, `graphio::templateKey` — and slice 3 added the
second hand-written uri→path conversion before collapsing both into `io::localPath`. The type is
worth having. **The survey that produced this note also rules out the obvious shape**, and that is
the part worth keeping, because it is what someone would otherwise "fix" later:

- **It must NOT be an RFC 3986 parser, and not a `std::filesystem::path` for schemes.** Two of
  lain's own strings collide with the standard head-on. `shot.####.png` is not a valid URI
  reference — `#` is the fragment delimiter, so a conforming parser reads path `shot.` + fragment
  `###.png`, and `io::numberField` scans for exactly that run for both the sequence opener and the
  render sweep; percent-encoding it to `%23` fixes the parse and wrecks the human-readable identity
  strings in a manifest. And `C:\footage\clip.mp4` parses as **scheme `C`**, on a platform lain
  ships. The current `://` test is immune to both by construction. Keep the opaque scheme/rest
  split; the type's job is to stop a uri being pasted into a `fs::path`, not to model the web.
- **Its home is `lain::core`, not `lain::io`.** The most valuable site is `media::FrameRef::source`,
  and `lain::media` links no `io` by rule. Canonicalisation touches the filesystem, so it stays in
  `io` (`io::canonicalise(Uri) -> Uri`). `lain::core` already names `std::filesystem` in `paths.h`,
  so a local-only `path()` accessor fits its std-only rule.
- **No path algebra on it.** Every `parent_path` / `relative` / `filename` in the tree operates on a
  genuinely local path, and none of those verbs means anything for a scheme with no implementation —
  the registry-with-one-member rule, applied to methods. `path()` plus `fs::path` covers every
  current caller.
- **What it does not buy, stated up front:** canonical-ness stays a discipline rule. The version
  that makes "a key computed two ways" *unrepresentable* is a distinct `CanonicalUri` only
  `io::canonicalise` can mint, which costs a cross-library friend or a token type for a rule that
  has five call sites and one historical miss (`templateKey`, already collapsed). Reach for it if a
  second miss appears.
- **Cheaper than it looks:** `ManifestFrame` already flattens `source` to a `std::string`, so
  typing `FrameRef::source` costs one `toString()` in `manifestOf` — no serialize arm, no wire
  change.
- **Timing:** after slice 5, as its own commit. Slice 5 adds the video opener and the `AVIOContext`
  — the third real consumer of "a name you open" — and a cross-cutting rename touching `read` /
  `write` / `openSequence` / `FrameRef` / `templateKey` should not interleave with the FFmpeg
  unknowns. It wants a short ADR when it lands, carrying the two collisions above.
- Still uncollapsed until then, and the reason it is *not* urgent: `graphio::loadGraph` does
  `std::filesystem::path(uri).parent_path()` on its document argument. flowview's documents come
  from a file dialog, the session file or `--graph`, so they are local by construction — it is the
  same shape as the bug slice 3 fixed, without the exposure.

### Not in this milestone

- **Capture manifests and capture datasets** — M9's, built on this contract.
- **A driving timeline in the gui.** Slice 7 is an *inspection* player on a sequence pin; a transport
  that binds a `FramePosition` boundary input and re-runs the graph is the follow-on. `FramePosition`
  being a distinct type is what keeps it a one-widget change.
- **A handle pool.** The `Stream` interface is specified so it is a pure backend swap; nothing builds
  it until many-source timelines make it hurt.
- **A decoder pool**, and with it parallel decode. One decoder behind a mutex; the contract was
  worded to permit the swap.
- **BT.601 / BT.2020 / PQ / HLG**, U16 decode output, and audio. **BT.1886 display rendering** too:
  slice 4 converts with the Rec.709 OETF and applies no OOTF, which is a processing decision, not a
  viewing one.
- **Device capture.** The prebuilt FFmpeg is `--disable-avdevice`, so there is no camera or
  microphone input. A live source is the streaming pipeline's problem (Tier B #4), and CONTEXT.md
  already says a live capture becomes a frame sequence only once the host establishes its end.
- **Realtime playback of processed output** — that is the streaming pipeline (Tier B #4). Playback
  here is best-effort: advance, rebind, re-run.
- **Per-element incrementality** and **keyed elements** — still ADR-0014's, still waiting on the
  workload.
- **A linked map over N streams** — *no longer deferred but **refused** (2026-09-09): a map whose
  interior holds a linked group is the shape, and it is the better one. See ADR-0014.*

## Codec colour-tag policy (audited + built 2026-09-03)

The question was "what do the readers and writers actually think they read and write, and is it a
fixed assumption or genuinely from the file?" The audit found **four policies for one question, a
WORK.md claim matching none of them, and no ADR owning any of it**. Settled in
**[ADR-0020](docs/adr/0020-codec-colour-tag-policy.md)**: *a reader states only what the file
states; a writer records the tag when the format can state it, and refuses when it cannot.*
`ctest` **642/642** (+23), warning-clean, format-check clean.

- **Only PNG and the video reader were reading anything.** JPEG hardcoded `sRGB`
  (`jpegreader.cpp:59`) — stb_image discards every APP marker, so no JFIF, Exif, ICC or Adobe tag
  was ever consulted — and TIFF hardcoded `Unspecified` (`tiffreader.cpp:163`) without reading
  `ICCPROFILE` or `TRANSFERFUNCTION`. **No still-image writer recorded a colour tag at all**, and no
  image `canEncode` consulted `colorSpace()`, so `Linear` → JPEG → `sRGB` was accepted silently:
  pixels a whole transfer curve away from the tag the file came back with, nothing logged. That was
  the worst path in the image stack.
- **Each codec's colour decision is now ONE file holding both directions** — `pngcolor`,
  `jpegcolor`, `tiffcolor`, on the `colorpolicy.{h,cpp}` precedent whose own header says why:
  *"Splitting them across two files is how the two halves drift."* A reader reading three chunks
  beside a writer writing none is that drift, arrived at over three separate commits.
- **JPEG now scans its own APP markers** (~180 lines, no new dependency): Exif `ColorSpace` (0xA001)
  above the ICC profile above the JFIF header. **Exif outranks the profile deliberately** — that is
  the Exif specification's arrangement, 1 means sRGB and `Uncalibrated` means the answer is in the
  profile, so ordering the profile first would discard an explicit sRGB tag on the very common
  camera JPEG that carries both. The JFIF arm survives as the one conventional answer and is
  **logged** at debug (a stills sequence decodes one file per frame, so info would be 500 lines).
- **The repo's own grayscale JPEG fixture proved the point.** Generated by `sips`, it carries a
  4.5 KB grey-gamma-2.2 ICC profile and no Exif ColorSpace — an ordinary file that lain reported as
  `sRGB` and now reports as `Unspecified`. The test that changed is the evidence, not a casualty.
- **TIFF states its curve with `TransferFunction`**, which makes it the one still format here that
  round-trips all four values — including `BT709`, which **PNG must refuse**: PNG's only handle is
  `gAMA`, BT709's exponent is 0.45, and lain's own PNG reader answers `sRGB` for that window, so
  writing it would produce a file lain relabels. The table is built by sampling `image::toLinear`,
  so the curve written, the curve recognised and the curve `image::convert` applies are one
  function, and **the comparison on read is exact** — a near-miss is `Unspecified` rather than a
  second gAMA-style heuristic in the one place that could be exact.
- **Two invented claims removed, both adjacent to colour and both the same defect.** The PNG writer
  accepted `AlphaMode::Premultiplied` and the reader handed it back `Straight` over already-scaled
  colour — data corruption, and precisely what `writer.h`'s `canEncode` contract says must not
  happen; it is now refused. The TIFF writer recorded `EXTRASAMPLE_UNASSALPHA` whatever it was told,
  so an image lain knew nothing about came back `Straight` — a claim manufactured by the round trip
  itself; `Unspecified` now writes `EXTRASAMPLE_UNSPECIFIED` and reads back as itself.
- **`BlurNode` was overwriting a stated fact.** `in.setColorSpace(sRGB)` ran unconditionally, so a
  BT709 video frame was linearised with the wrong curve and came out relabelled — the one
  unconditional clobber in the tree. It now declares only what is undeclared (`ConvertNode`'s rule)
  and converts back to the space and alpha association it was handed.
- **`Image::toString` names both tags**, because every codec refusal prints it and the refusals are
  *about* the tags: "cannot encode Image 64x64 RGB8" named nothing a caller could act on. The video
  writer's frame-mismatch log was printing "RGB8 does not match RGB8 BT709 · 24 fps" for the same
  reason.
- **The loss matrix is a test, not a table** (`plugins/io/image/test/test_colorroundtrip.cpp`): the
  property every codec must share — a tag survives, or it is refused — asserted across whichever
  codecs the build enabled, so a codec that stops recording cannot pass by having its own
  expectations updated alongside it. One case asserts that *some* codec can carry every `ColorSpace`
  lain has, so a standard nothing can save fails there rather than being discovered later.
- **Every image codec now switches exhaustively on `ColorSpace`.** None did before, which is why
  `BT709`'s arrival in M10 slice 4 reached none of them; a fifth enumerator now fails the build.
- **PNG's `gAMA` and `iCCP` arms had NO fixture and were untested** — reachable only through files
  nothing in the suite produced. Both now have one, and building the iCCP fixture found libpng's
  real constraint: it reads up to 81 bytes of keyword then demands 11 more, so an iCCP chunk under
  ~92 bytes is rejected as "too short" and an all-zeros profile compresses far below that. The
  fixture carries a synthetic, structurally valid v2.1 profile with deliberately wide primaries (so
  libpng's own sRGB-match cannot reclassify it) **and a gAMA of 1.0** — which is what makes the
  assertion sharp: a dropped profile would leave the gAMA arm answering `Linear`, so the test fails
  rather than passing for the wrong reason. It did exactly that on the first attempt.
- **Four sabotages, all caught**: PNG skipping the sRGB chunk, PNG accepting BT709, TIFF's curve
  comparison made tolerant, JPEG dropping its ICC arm. The TIFF one **initially passed and exposed a
  weak test** — libtiff deduplicates identical per-channel transfer arrays and replicates them on
  read, so corrupting the stored table corrupts all three channels and the green/blue compare caught
  what the sabotaged red compare missed. Sabotaging the length every comparison uses fails it.
- **A REAL memory-corruption bug, caught only by building Release.** libtiff's TransferFunction
  getter and setter are **asymmetric**: `TIFFSetField` consumes one array for a single-colour-channel
  image and three otherwise, but `TIFFGetField` **always** consumes three `uint16_t**`, writing NULL
  into the second and third when there is one channel (`tif_dir.c`, the else arm). Passing one
  pointer to the getter — the obvious mirror of the setter — lets libtiff write two NULLs past the
  end of the argument list. The Debug build absorbed it and passed; Release SIGSEGV'd on the first
  grayscale image. Two things worth carrying: a variadic C API's two directions can disagree about
  arity and the compiler cannot help, and **the suite must be run in both configurations** — the
  2026-09-01 note that Release did not build at all is what let this class of bug hide.
- **Accepted, and stated rather than found later:** a TIFF `TransferFunction` is fixed by the
  specification at 2^BitsPerSample entries per colour channel — 1.5 KB on an 8-bit RGB image, 384 KB
  on a 16-bit one. Under 1% of any 16-bit image anyone actually writes; disproportionate only for a
  tiny one.
- **Behaviour changes, none silent:** PNG and TIFF output gains colour tags; a profile-bearing or
  explicitly-uncalibrated JPEG now reads `Unspecified`; and `Linear`/`BT709` to JPEG, `BT709` to
  PNG, and `Premultiplied` to PNG are now refusals rather than mislabelled files.
- **Found in the video path, and FIXED in the follow-up commit** — the same denylist defect one
  level up, plus a finding that did not survive measurement:
  - **The refusal lists were denylists, so anything unlisted was claimed as BT709.** Not a small
    set: the **LOG / LOG_SQRT transfers** (V-Log/S-Log footage, and linearising a log curve with a
    709 OETF is not a subtle error), **`AVCOL_TRC_LINEAR`** — which the write seam already refused
    for exactly this reason, so the two directions disagreed about one value — **DCI-P3 and Display
    P3 primaries**, **CIE XYZ**, and every extension FFmpeg adds later. Now allowlists. A denylist
    defaults to CLAIMING and an allowlist defaults to REFUSING, and only the second is what
    ADR-0018's "reported and refused, not relabelled" means; it is also the shape
    `io::video::canEncode` already had on the write side.
  - **The untagged decode matrix: the finding was right, the proposed fix was WRONG, and measuring
    it is what said so.** Untagged YUV is decoded with BT.601 coefficients while lain tags the
    frame BT709, which looked like ADR-0018's overridden convention surviving in the matrix — so
    the obvious fix was to drive the coefficients from the resolved policy. It is not: lain's own
    untagged fixture decodes back to its source colour **exactly** under BT.601 and **10 counts
    out** under BT.709, at 720p as well as at 64x48, because an encoder that writes no tag is one
    that used BT.601. **The transfer and the matrix are separate questions with different right
    answers** — the two OETFs are the same curve to within rounding, so ADR-0018's transfer guess
    is free, while the coefficient sets are ~17% apart on a saturated green. The behaviour stayed;
    what changed is that it is now a stated, tested, logged decision (`decodeMatrixFor`) rather
    than a swscale default inherited by passing the raw tag through. That was the real defect:
    nobody had chosen it.
  - **The log line missed cases and misdescribed others.** It required BOTH `color_trc` and
    `color_space` unspecified, so a file tagged only by its matrix took the BT709 default silently,
    while one tagged only by its primaries was told it "carries no colour tags". Now one line per
    axis, each naming the decision it is announcing.
  - Sabotage-verified both ways: reverting the allowlist to accept-by-default, and making
    `decodeMatrixFor` state BT.709, each fail the suite.

## Continuous integration (built 2026-09-04, **GREEN on all three platforms 2026-09-05**)

lain had **no CI at all** until now, and every one of its 210 commits was built and verified on a
single macOS arm64 machine. The tree is written portably *by intent* — two files in 208 sources
carry a `_WIN32`/`_MSC_VER` branch, there is not one POSIX header or `__attribute__` in the tree —
but that intent had never been checked by a compiler other than Apple clang.
`.github/workflows/ci.yml` is modelled on archimedes', and its purpose is the plain one: does the
tree build, and do its tests pass, on the three platforms it claims.

**The first run is a discovery run, not a gate.** CI is the only Windows and Linux compiler this
project has, so the workflow is the porting *tool* rather than a check on porting already done.
"CI is done" means three platforms green, not a merged yml. `fail-fast: false` is load-bearing for
exactly that reason: a red leg must not hide what the other three would have said.

### The matrix

| leg | runner | build type | `LAIN_IO_VIDEO_FFMPEG` |
|---|---|---|---|
| Linux Release | `ubuntu-24.04` | Release | ON |
| Linux Release (video off) | `ubuntu-24.04` | Release | **OFF** |
| Linux Debug | `ubuntu-24.04` | Debug | ON |
| macOS Release | `macos-15` (arm64) | Release | ON |
| Windows Release | `windows-2022` | Release | ON |

Plus a `clang-format` job and an `add_subdirectory` smoke, both on Linux. Seven jobs.

- **Release everywhere, Debug on the cheapest runner** — archimedes' shape. Release is what a fresh
  clone gets (`CMakeLists.txt` defaults `CMAKE_BUILD_TYPE` to it) and is the configuration that has
  rotted before (M10 slice 4 found `alloc.cpp`'s unused static under `NDEBUG`, and with it that
  *every* `#ifdef NDEBUG` enforcement test in the repo had been unreachable for some time). Debug is
  not the same test set — the assert-based cases only exist there — so one leg carries it.
- **The default configuration gets its own Linux leg, and that is why there are five.** Video
  defaults OFF, so one leg has to prove that configuration still builds. It was macOS at first, on
  the reasoning that a platform built by hand daily needs CI least — **which inverted once the
  timings arrived** (revised 2026-09-05). macOS is the slowest leg by 3–4×, *and* the daily build
  has video ON, so CI was spending seventeen minutes on a combination nobody builds while never
  exercising the one built every day. Linux does the same work in three, so the cheap runner is the
  one asked twice. Each axis now varies alone: the video-off leg is Release, since Release is what
  a fresh clone gets and so is what "the default configuration" means, and Debug stays video-ON to
  match the daily build.
- **`submodules: recursive`** is the one thing this workflow needs that archimedes' does not:
  lain carries `extern/archimedes`, and without it the first `add_subdirectory` fails.
- **`bash` on every leg, Windows included** (Git Bash ships on the runner), so one command shape
  serves all three and the binary paths need no per-shell quoting. Windows uses the default Visual
  Studio generator, which is multi-config: `--config` / `-C` carry the build type there and
  `CMAKE_BUILD_TYPE` is ignored, so both are passed everywhere.
- **ctest runs serially.** Twenty test files write into the shared `fs::temp_directory_path()`, so
  `-j` invites collisions between them.
- **The `flowview` binary is smoked separately from ctest** (`--version`, `list`, `run`, and
  `--licenses` on the video legs). `apps/flowview/test` compiles `runmode.cpp` *into* the test
  binary, so ctest structurally never exercises the shipped executable — including whether it
  launches and resolves its dynamic dependencies, which on Windows with FFmpeg is the real question.

### What CI deliberately does not cover

**The GPU.** The one `[gpu]` test self-SKIPs unless `LAIN_GUI_SMOKE=1`, and `catch_discover_tests`
sets `SKIP_RETURN_CODE=4`, so ctest reports it skipped rather than failed. `Application::ensureGlfw`
and `ensureInstance` are lazy — only `createWindow` reaches them — so the headless `[app]` tests need
no driver and no display. gui-mode stays what it has always been: eyeballed on a Metal-capable
machine. A hosted runner has no driver worth trusting, and a windowed smoke that passes for the
wrong reason is worse than one that never ran.

### The `add_subdirectory` smoke proves two claims with one job

`.github/subproject-smoke/` is a parent project that adds lain as a subdirectory and links
`lain::flow` alone. Because `LAIN_BUILD_TESTING` / `_APPS` / `_FORMAT` all default to
`${LAIN_NOT_SUBPROJECT}`, being a subproject turns the tests, the app stack and the Vulkan loader
off by itself — so the one job checks both that lain is consumable that way *and* the claim
`CMakeLists.txt` makes in a comment, that a headless consumer of `lain::flow` is not forced to
vendor GLFW or ImGui. Verified locally before the first push: it builds, runs, and pulls no GLFW,
no ImGui and no Vulkan loader — only the Vulkan headers archimedes compiles against.

Note that `add_subdirectory(extern/archimedes)` is unconditional, so even a headless consumer builds
a Vulkan renderer. That is a wart CI now makes visible rather than one it fixes.

### Caching

`actions/cache` over `.cache/fetch` — the downloaded **archives** only; the dependencies themselves
(Catch2, GLFW, ImGui, imnodes, fmt, spdlog, libpng, zlib, libtiff, the Vulkan loader *from source*,
MoltenVK, and all of archimedes) are recompiled per leg. One path covers everything because
archimedes' own `addXXX.cmake` modules write to `${CMAKE_SOURCE_DIR}/.cache/fetch`, which resolves
to lain's root when it is nested here. The video flag is in the key, since the FFmpeg archive is
fetched only when it is ON.

No compiler cache yet, deliberately: a stale one producing baffling errors is the last thing wanted
while diagnosing genuine MSVC breakage. Read the real timings off the discovery run and add `ccache`
via plain `actions/cache` in a follow-up if a leg is painfully slow.

### Found while building this: `flowview` staged no FFmpeg DLLs

`lain_ffmpeg_stage_runtime()` (`cmake/addFFmpeg.cmake`) was called by the two video test
executables and by **neither** `flowview` nor `test-flowview`, both of which link
`lain::io::video::codecs`. Windows has no rpath, so with the plugin enabled neither would have
started. For `test-flowview` that is worse than it sounds: `catch_discover_tests` runs the
executable at build time to enumerate its cases, so a missing DLL fails the **build**, with a
message about test discovery rather than about a DLL. Both now stage, guarded by
`LAIN_IO_VIDEO_FFMPEG` in the pattern `plugins/io/video/test` already used.

This is the one thing fixed ahead of the discovery run rather than left for it. It is not a
prediction about a compiler — it is a written-and-never-called function, the same
compiled-linked-unreachable shape as M5's bug six and `File ▸ Reload Linked Groups`' missing menu
item. Leaving it in would have spent a whole CI round trip re-learning something already known.

### The discovery run (2026-09-04)

Run 33881484863, the first time this tree met a compiler other than Apple clang. **clang-format and
macOS Release passed** — 626/626 in the default video-off configuration, matching the count recorded
for that config, plus a clean binary smoke — which is what says the workflow itself is wired right
and the rest are real findings. The other four legs failed, on **three** distinct causes.

1. **GCC rejects a declared-but-never-defined internal-linkage function.**
   `libs/meta/test/test_traits.cpp` declares `operator<<(std::ostream&, const Streamy&)` with no
   body, because `has_ostream` only inspects it in an unevaluated context. clang accepts that and
   wanted `[[maybe_unused]]`; GCC errors with *"declared static but never defined"*
   (`-Werror=unused-function`), which `[[maybe_unused]]` does **not** cover — that attribute
   excuses a definition that goes uncalled. Fixed by giving it a body, which satisfies both.
   Blocked both Linux legs at ~2 minutes.

2. **libpng's `setjmp`, flagged by both compilers, and one of the two was right.** GCC's
   `-Werror=clobbered` on `pngwriter.cpp`, MSVC's **C4611** on both `pngreader.cpp` and
   `pngwriter.cpp`. The audit they forced found a genuine latent bug rather than a style
   complaint: **`rows` is written after the `setjmp` and read by the error handler**, and a
   non-volatile automatic object's value is *indeterminate* once a longjmp has returned through
   the frame — so the handler's `free()` could be handed a stale pointer. It is now `volatile` in
   both files, as are the writer's `colorType`/`bitDepth`, which are read after the jump.
   Interestingly GCC pointed at those two rather than at `rows`, so the warning that fired was the
   less serious half of what was there.
   C4611 gets a per-target suppression instead, because no change to the code can answer it: MSVC
   warns that a longjmp skips C++ destructors whenever `setjmp` appears in a C++ TU, whatever the
   frame holds. The reasoning sits in `plugins/io/image/png/CMakeLists.txt` beside the flag —
   lain's idiom, as in `libs/app/test`.
   Hit by the subproject smoke first, which builds no tests and so got past `libs/meta`; both
   Linux legs died on finding 1 before reaching it.

3. **The Vulkan loader would not link on Windows — DIAGNOSED, and it was lain's, not
   archimedes'.** 750 unresolved `vkdev_ext0…N` from `dev_ext_trampoline.obj`: the MASM
   trampolines were never assembled. Nothing errored, which is what made it hard — MASM was
   found and enabled, the loader's own assembler check passed, and `unknown_ext_chain_masm.asm`
   was in the target's sources the whole time.

   **Two languages claim the `.asm` extension in this tree.** The loader enables `ASM_MASM`
   for its trampolines; libpng — configured afterwards — declares `project(libpng LANGUAGES C
   ASM)` unconditionally, with no option to turn it off. CMake resolves a source file's language
   by extension across every *enabled* language, so plain `ASM`, whose Windows "assembler" is
   `cl.exe`, took the file. The trampolines were silently dropped from the build.

   The discriminator was in the configure logs: archimedes prints *"The ASM_MASM compiler
   identification is MSVC"* and then, at build time, *"Assembling …
   unknown_ext_chain_masm.asm"*; lain prints **both** ASM and ASM_MASM identifications and never
   assembles anything. archimedes never meets this because it builds no libpng — so the earlier
   guess that this might be archimedes' to fix was wrong, and so was the first probe
   (`LANGUAGES CXX C`, which changed nothing and stays only because it is true of the build).

   Fixed by **stating** the language of the loader's `.asm` (`set_source_files_properties(…
   LANGUAGE ASM_MASM)`, MSVC-only) rather than by reordering the two so `ASM_MASM` is enabled
   last. Reordering would work today, but a positional fix to a resolution rule nobody can see
   is how this comes back silently.

4. **FFmpeg's headers need `__STDC_CONSTANT_MACROS` from C++ — new, and macOS could never have
   found it.** `libavutil/common.h` `#error`s without it. The macro gates the `UINT64_C` family
   in `<stdint.h>`, which C99 says a C++ translation unit only gets when it asks: glibc still
   honours that, libc++ defines them unconditionally. So the video plugin has never compiled
   anywhere but macOS, and both Linux legs stopped there once finding 1 stopped blocking them.
   The definitions go on the **imported FFmpeg targets** in `cmake/addFFmpeg.cmake`, not on
   lain's own: this is a condition of including FFmpeg's headers, so every consumer needs it and
   none should have to remember.

### Run 3 (2026-09-05)

The `.asm` fix worked — `vkdev_ext` unresolved count went from 750 to **zero**, so the Windows
loader links — and the FFmpeg header definitions cleared the Linux compile. Both legs moved on to
new ground. Four more findings, three of them the same shape as everything before: something libc++
supplies that the other two standard libraries do not.

5. **`parameditors.h` uses `std::string` and never includes `<string>`.** libc++ pulls it in through
   `<functional>`/`<map>`; MSVC's STL does not, and the failure cascades — the reported errors were
   `std::move` not found, `std::map` having no `operator[]`, a member function "not found in" its own
   class, and a function pointer that would not convert to the `std::function` it matches. None of
   those are real; the first line of the log is, and it is in the header.
   A sweep for the same defect across every `.h`/`.inl` in the tree turns up ~40 more candidates,
   but almost all are `.inl` files whose parent header supplies the include, or lain headers reached
   through other lain headers — consistent across compilers, so nits rather than portability bugs.
   Only this one demonstrably broke, so only this one is fixed; CI remains the oracle for the rest.

6. **A `//` comment ending in a backslash splices the next line** (`-Werror=comment`).
   `videofixtures.h` documents the ffmpeg commands that generated the checked-in fixtures, and those
   used shell line-continuations. Rewritten with the same `R=`-style shell variables the block
   already used, so the recipe stays copy-pasteable without continuations.

7. **The Linux FFmpeg prebuilt needs `libva` at link time.** Its `MANIFEST.txt` records
   `--enable-vaapi`, so `libavcodec.so` carries an unresolved dependency every consumer's linker has
   to satisfy; without it, anything linking the video plugin fails with a page of undefined `va*`
   references. `libva-dev` joins the Linux apt step. It is the only flag in that build that needs an
   external shared library — `v4l2-m2m` is kernel headers, `nvenc`/`ffnvcodec` are dlopen'd — and it
   is worth knowing generally: **a Linux consumer of lain's video plugin needs libva installed.**

8. **On Windows the Vulkan loader is a DLL, and nothing put it where the executables look.** Every
   test binary linking it died with `STATUS_DLL_NOT_FOUND` — surfaced as a Catch2 *test-discovery*
   error naming neither the DLL nor the loader, because discovery runs the executable at build time.
   `DL_PATHS` on `catch_discover_tests` covers discovery and the ctest run together, which is how
   archimedes' own suite solves it; `flowview` gets the loader staged beside it instead, since an app
   that ships has to carry it rather than be handed a path.

### Run 4 (2026-09-05) — Windows reaches the test suite

Both previous fixes held, and the Windows leg **built and ran ctest for the first time**: 658 tests
discovered, 655 passing. What is left is small and, in both cases, not what it first looked like.

9. **Three test FAILURES on Windows that are not test failures.** Each reported *"No test cases
   matched"* — ctest passes a test's name back to the executable as a filter, and three names
   contain a UTF-8 em dash that does not survive that round trip on Windows (the log shows it
   arriving as `G��`). Catch2 then matches nothing, and "no tests ran" is a failure.
   Renamed to ASCII. Prose keeps its em dashes; a test NAME is an argument that crosses a process
   boundary, so it stays ASCII — a scan confirms these were the only three non-ASCII names in the
   tree. The Windows leg is itself the guard against a fourth.

10. **`test-flowview` alone would not link on Linux**, on `vkDestroyBuffer` and
    `vkGetInstanceProcAddr` from `libarchimedes.a` — while `flowview`, `test-app`, `test-flow` and
    `test-gui`, which link the same two things, were all fine. The link line says why: the loader
    sat at position 34 and `libarchimedes.a` at 109. **CMake emits direct dependencies before
    transitive ones**, and in that target archimedes arrives transitively through `lain::gui` while
    the loader is listed directly — so no ordering of that target's own `target_link_libraries`
    could have fixed it. Apple's linker resolves shared libraries globally and never noticed; GNU ld
    links left to right and Ubuntu defaults to `--as-needed`, so it dropped the loader before
    meeting a single reference to it.
    Fixed by stating the dependency that actually exists — `target_link_libraries(archimedes
    INTERFACE Vulkan::Loader)`, once, after `acm_require_vulkan_runtime()` — so CMake orders the
    loader after archimedes in every target on every platform. archimedes leaves this to consumers
    because it cannot know whether one wants the vendored loader; by that line the choice is made.

### Green (first on 2026-09-05, run 33890635630; current shape run 33940070934)

Five runs from the first red one. **lain builds and passes its tests on Linux x64, macOS arm64 and
Windows x64.** The figures below are the reshuffled five-leg matrix:

| leg | tests | time |
|---|---|---|
| Linux Release (video ON) | 658/658 | 4m26s |
| Linux Release (video off) | 626/626 | 4m14s |
| Linux Debug (video ON) | 652/652 | 4m05s |
| macOS Release (video ON) | 658/658 | 12m46s |
| Windows Release (video ON) | 658/658 | 6m48s |
| clang-format | pass | 26s |
| add_subdirectory smoke | pass | 1m43s |

Moving video onto macOS added 32 tests there (626 → 658) and gave the `macos-arm64` FFmpeg pin and
`flowview --licenses` their first exercise in CI; both passed. The default configuration is now the
626 on Linux. macOS came in at 12m46s against the 17m it took while building *less*, so the earlier
17m was partly runner variance on top of a genuinely slow runner — worth remembering before reading
any single leg's duration as a fixed cost.

**The Debug/Release split earned its place immediately**: Linux Release runs 658 where Debug runs
652, and those six are the `NDEBUG`-guarded enforcement cases — the class this file recorded as
having gone unreachable without anyone noticing. A Release-only matrix would not have run the
asserts; a Debug-only one would not have run those six.

**What the port actually cost: ten findings, and the prediction was wrong about their kind.** Not
one MSVC narrowing warning (C4267/C4244/C4100) appeared, and not one `<windows.h>` `min`/`max`
collision — the two things predicted loudest before any of it ran. What actually turned up, six
times out of ten, was **something libc++ supplies that libstdc++ and MSVC's STL do not**: a
transitively-included `<string>`, `__STDC_CONSTANT_MACROS`, and three diagnostics clang does not
implement at all (`-Wclobbered`, `-Wcomment`, and GCC's rejection of a declared-but-undefined
internal-linkage function). The remaining four were each a different kind of invisible: two
languages claiming the `.asm` extension, a DLL nothing put on the path, a link order only
`--as-needed` enforces, and an em dash that could not survive a trip through argv.

**Two were real bugs rather than portability noise**, and both had been in the tree unnoticed:
`rows` crossing a `setjmp` without `volatile` in both PNG codecs, where a `longjmp` could have
handed `free()` a stale pointer on any platform; and `flowview` never staging the FFmpeg DLLs it
links. Neither is a Windows or Linux problem — they were simply never *asked* about before.

**The macOS leg is the slowest by 3–4× (17m against Linux's 3–5m), and it is not MoltenVK.** An
earlier revision of this section said it was, which was wrong and worth correcting where it stood:
`acmVulkan.cmake` fetches MoltenVK as a **prebuilt tarball**, so nothing builds it. The per-step
timings say where the time actually goes — Configure, which is where every FetchContent download
happens, is 60s; the cache restores in 2s; the **Build step is 15m43s**. macOS compiles 489
translation units to Linux's 506 — *fewer*, since it was the video-off leg — with parallelism
working. It is simply a slower 3-core runner, with nothing structural to fix and nothing about
MoltenVK to cache or to move to archimedes, which already owns it end to end.

Measured while settling that (`build/.ninja_log`, Debug + video ON, 308 CPU-seconds of compilation):
**Catch2 is 15.4% of all compile time** (47.4s, 107 TUs) — the largest single dependency and half of
all third-party time — followed by `apps/flowview` at 12.4%, `apps/flowview/test` at 8.3%,
`extern/archimedes` at 7.8% and `libs/flow/test` at 7.7%. **Third-party plus submodule is 31% of
compile time, not the "over half" a TU count suggests**: libtiff's 44 files cost 3.0s while
flowview's 33 cost 38.3s.

Two conclusions follow, both deliberately not acted on. **Prebuilding PNG/TIFF is not worth it** —
which contradicts the candidate named in `notes.txt`, since libtiff is 1.0% and libpng does not
reach the top 26; it would buy ~2% in exchange for a hash-pinned archive per platform, a
licence-notice obligation and the platform-gap apologies `addFFmpeg.cmake` already carries.
**Catch2 is the only dependency where prebuilding would matter**, and `cmake/addcatch2.cmake`
already declares `FIND_PACKAGE_ARGS CONFIG`, so a system Catch2 is preferred when present —
installing one in CI would skip those 107 TUs with no new machinery, at the cost of testing a
different Catch2 than the pin. A compiler cache remains the untried general lever; no caching
beyond the fetched archives is in place.

### Not covered, and deliberately

The GPU. gui-mode is still eyeball-verified by the repo owner on a Metal-capable machine; the one
`[gpu]` test self-SKIPs, and ctest reports it skipped rather than passed. Nothing here changes that,
and nothing here should be read as covering it.

## Milestone 11 — loop nodes (grilled 2026-09-05, **COMPLETE** 2026-09-08)

**All six slices built and live-verified.** `flow` could run a subgraph **once** (a group) and
**N times independently** (a map). It could not run one **sequentially, feeding each pass into the
next**. ADR-0012 named
*"whether Loop carries state between iterations"* among four questions it refused to guess at;
ADR-0014 settled the other three for the map and re-deferred this one, because *"a map's children are
independent by construction, a loop's are not"*. Decisions in
**[ADR-0021](docs/adr/0021-loop-nodes-carried-state-per-iteration-staging.md)**; vocabulary in
[CONTEXT.md](CONTEXT.md).

**There is no production caller, and the milestone says so out loud.** The owner's framing was *"there
is no direct use case as much as this is a feature that really tests the node approach."* That makes
the standard **stricter** than M8's, not looser — ADR-0012's own warning is that guessing produces a
mechanism fitted to imagined requirements — so everything below is either forced by a rule already in
force or is the smallest thing that makes a count loop and a while loop work. **The vertical is the
deliverable.**

**What it is, in one line each:**

- A **loop** is a group whose interior runs **once per iteration**, each iteration's **carried**
  outputs seeding the next one's inputs. One `LoopNode` beside `MapNode`, inline only.
- **Bounded by construction.** A node-owned `count` input with a `Default{}` plus an **optional**
  inner `continue : bool`. Count loop = count; while loop = condition with count as its bound;
  converging solver = both. There is no unbounded state to represent, so nothing refuses one at
  runtime.
- **The count bound is the staging loop's well-formedness condition, not a nicety.** `runStages`
  terminates today because a map is deferred at most once; a loop is deferred per iteration, which
  destroys that argument. The bound restores it.
- A **carry** is a stored `PortId` **pair** on the inner boundary, created only by a paired gesture,
  so half a carry cannot be authored. Name-pairing was refused: a rename would silently stop a loop
  carrying, with no error.
- Two **reserved inner pins** the engine writes and reads — `index` and `continue` — not mirrored
  outward, skipped **by id**. `continue` defaults to **true**, so unwired is transparent and
  wired-and-suppressed means the iteration failed (`GateNode::enable`'s resolution, reused).
- **A map maps, a loop folds.** Carries mirror out as final values, unpaired outputs as last-iteration
  values, plus `iterations : int` — which is what makes "converged at 7" distinguishable from "hit the
  bound at 100".
- **One iteration per stage**, on the existing frontier machinery. An iterating loop emits its
  interior steps *and* re-raises itself as a frontier; a finished one emits `LoopExit` alone.
- **One retained child evaluation**, reused. A map's elements are results; a loop's iterations are
  steps. Keeps `EvalPath` at index 0, so `PinKey` / the breadcrumb / the element stepper need nothing.
- **Suppression is failure, `continue` is break.** `count == 0` is not failure: zero iterations, each
  carry delivers its **seed** — the fold identity, as a map's `N == 0` yields an empty vector.
- **Store the pairing, derive the ports** — name-addressed on disk like every edge, so a loop follows
  M5's group rule rather than the map's.

**Build order** — structural refactors land as no-behaviour-change commits *before* the feature
exists, flowview last with its own live verification. Same shape as M8.

1. ✅ **The seam becomes an enum — BUILT** (2026-09-05). `Node::evaluatesPerElement()` →
   `interiorEvaluation() -> { Once, PerElement, PerIteration }`, a free `enum class` at `lain::flow`
   scope in `node.h` (matching `Presence` in `port.h`, not nesting like `Port::Direction`, so the type
   and its accessor share a word). Seven call sites. No behaviour change — the existing suite is the
   regression test, and it moved by zero: `ctest` **652/652** Debug and **658/658** Release,
   warning-clean, format-check clean, `flowview run --example` unchanged.
   - **All three enumerators landed now, with ONE exhaustive switch — because `PerIteration`'s arm is
     already correct rather than a placeholder.** `Evaluation::prepare`'s child-count arm became a
     `switch`, where a loop joins a group in the guaranteed-one-child case (ADR-0021: one retained
     child, reused per iteration). That gives `-Wswitch` a real anchor from this commit instead of
     from slice 4 — **sabotage-verified**: deleting an arm fails the build with
     *"enumeration value 'PerElement' not handled in switch [-Werror,-Wswitch]"*. Every other site
     stays an `==` comparison, which is M10 slice 4's `ColorSpace` precedent exactly (one deliberate
     exhaustive switch in the tree; comparisons everywhere else).
   - **The honest statement of this slice's coverage, measured rather than assumed.** Giving
     `PerIteration` the *map's* arm — no child guaranteed — passes all 652 tests, because nothing
     returns `PerIteration` yet. The switch's correctness is bought by slice 4, and this is why the
     enum's value here is the compiler's, not the suite's.
   - **No `None` for a node with no interior**, decided while building. `innerGraph()` already answers
     that question and every reader pairs the two (`== PerElement && inner != nullptr`), so a fourth
     enumerator would be a second source able to disagree with the first — the shape M7 slice 1 and
     ADR-0014 each deleted. Stated in the header so the absence reads as a decision.
   - **flowview's `Crumb` carries the enum, not a derived bool.** The element stepper now tests
     `interior == PerElement`, which is where "the stepper stays off a loop crumb" is actually bought:
     a map's elements are co-equal results you page between, a loop's iterations are steps and only
     the last survives. Carrying the enum is ADR-0021's own anti-two-booleans argument applied one
     level up — a loop crumb that later wants its own marker adds no second field.
   - **Found and removed: `scheduler.cpp` included `group.h` for a class it never names.** The include
     comment read *"MapNode — the scheduler asks whether a node maps, not which class it is"*, so it
     contradicted the rule this slice is about. It compiles and passes without it.
2. ✅ **`LoopNode`, core types only — BUILT** (2026-09-05). The class beside `MapNode`,
   `addCarry<T>`, the carry map, the reserved pin ids, `count` / `iterations`, `editableInner()` (the
   M8 slice 6c lesson — without it every pane is read-only inside a loop), and the mirroring
   overrides. Nothing runs: no factory reaches a `LoopNode`, so the existing suite is the regression
   test and it moved by zero: `ctest` **663/663** Debug with video on (was 652) and **637/637**
   Release in the default video-off configuration (was 626) — both baselines grown by exactly the
   eleven new cases, on both axes. Warning-clean, format-check clean, and `flowview run --example`
   is identical to the pre-change binary once timestamps and freshly-minted uuids are normalised.
   - **The reserved pins are STATIC pins, through a new `addReserved` seam on the boundary nodes** —
     a decision forced by two facts that only appear at the code. `addBoundary<T>` routes through
     `addDynamicPort`, which (a) marks the pin dynamic, so serialization would replay `index` onto a
     constructor that already made it and log *"could not be added — skipped"* on every loop
     document, and (b) has no `Default` overload, while `Node::addInput(name, Default<T>)` is
     protected and reachable only from inside a boundary node. Static is exactly
     `SelectNode::selector`'s precedent: serialization replays only dynamic pins, the `LoopNode` ctor
     rebuilds both on load, an edge to one resolves by name like any other, and `continue`'s default
     round-trips as an ordinary `Param`. **Slice 5 needs no special case, and `populateInputs` already
     seeds an unconnected input from `defaultOf`, so slice 4 needs no new mechanism to read
     `continue`.** A test pins `!isDynamic()` here rather than at slice 5, where the failure would
     look unrelated.
   - **`count` defaults to 1**, so a fresh loop behaves exactly like a group. `0` is the fold
     identity and a perfectly good value, just not a sensible thing for a node to do the moment it
     lands on a canvas. `int` on both sides, because that is the type a graph can actually drive.
   - **"Not a candidate" and "refused" turned out to be different questions, and the tests are what
     said so.** The first cut put the reserved-pin skip inside `exposePort` beside the collision
     refusal, and `GroupSync::refused` then named `index` and `continue` on **every pass for the life
     of the document** — the reporting channel drowned by the one case that is silent by design. Now
     `GroupNode::mirrorsPin(direction, pin)` (default true) answers *is this pin mirrored at all*, and
     `exposePort` answers *was a candidate refused*. A reserved pin is skipped by **id** through the
     first — sabotage-verified: skipping by name mirrors both pins the moment either is renamed.
   - **A loop is the first group kind with ports of its OWN, so it is the first where an inner pin's
     name can collide with one** — which ADR-0021 did not anticipate, and which `addInputLike`
     answers with a duplicate-name **assert**. Refused in `exposePort` and reported by name
     (`sync.refused`), the same add-nothing-rather-than-degrade call `MapNode::exposePort` makes for
     an unliftable type — which that refusal now inherits, having been silent since M8. **The
     collision arrives by two doors**: the add phase, and the *rename* phase retitling a mirrored port
     onto a name the loop already owns — that one leaves two same-named outer ports, which makes
     every name-addressed edge through them ambiguous on disk. Both refuse and report.
   - **`refused` is deliberately not part of `GroupSync::changed()`.** A refused pin is retried on
     every pass and flowview syncs every frame, so folding it in would bump the node's recipe version
     continuously and force every evaluation to re-run forever. It describes a steady state, not a
     transition — sabotage-verified.
   - **`GroupNode::reconcileInterior()` (default no-op) settles what happens to a broken carry.** A
     pairing is the one thing about an interior that is not derivable from it, and a user may delete
     either half through the Interface pane's ±. `syncGroupPorts` calls it before touching any port
     and `LoopNode` drops a pairing whose pin is gone; the survivor then means exactly what an
     unpaired pin means — an invariant, or a last-iteration output — so derivation stays total and
     there is no broken state to represent. A virtual for the reason `exposePort` is one: the
     reconciliation gesture must not learn which kind it is holding.
   - **ADR-0021's removal-phase claim verified, and one of its `exposePort` claims corrected.** The
     removal phase really does need no exception for `count` / `iterations` (it iterates `portMap()`,
     which never names a node-owned port), and `enterGroup` skips them for the same reason. But the
     override does **not** "derive seed / invariant / final / last": that derivation is *identical* to
     a plain group's, because the pairing changes what the engine does BETWEEN iterations and never
     what the ports look like. The override is refusals only.
   - **Found while planning, for slice 4:** `Scheduler::exitGroup` **clears** an output whose
     `innerPin` is null, so it would wipe `iterations` — `LoopExit` must write the node-owned outputs
     after it, or not reuse `exitGroup` at all.
3. ✅ **Staging generalises — BUILT** (2026-09-05). `PreparedMaps` — a `std::vector<Frontier>` of
   frontiers already seen, scanned by a lambda inside `expand` — is now **`Scheduler::Staging`**, a
   record per frontier ADDRESS answering a **count of preparations**. A map still defers exactly
   once, so behaviour is identical and the existing suite is the regression test: `ctest`
   **663/663** Debug with video on and **637/637** Release in the default video-off configuration,
   both unchanged from slice 2. Warning-clean, format-check clean, and `flowview run` over the
   example scene is identical to the pre-change binary once the timestamp and the freshly-minted
   uuids are normalised.
   - **A record per frontier rather than a longer list, which is the whole slice.** The old shape
     encoded its own assumption — *"a map is deferred at most once per invocation"* — in the fact
     that it only ever appended. A loop raises its frontier **per iteration** (ADR-0021), so an
     append-only list would grow an entry per iteration and be re-walked on every `expand` lookup;
     the entry count now stays per frontier address however many times that frontier comes back.
   - **A COUNT, not a flag.** A flag can say a frontier came back; only a count can say *which time
     this is*, which is what slice 4's iteration bookkeeping needs. The map's whole use of it is
     `== 0`, so — stated the way slice 1 stated its own coverage — **nothing reads it as more than
     0-or-1 yet**, and the count itself is bought by slice 4 rather than by this suite.
   - **The map's own bound demonstrably still passes through the new type.** Sabotage: make
     `recordPreparation` record nothing, and the `[flow][map]` cases **hang** — the map is deferred
     again on every stage and its frontier is re-raised forever. That is the same failure M8 slice
     2's staging test exists to prevent, arriving from the other direction.
   - **Sabotaging the ADDRESS changes nothing, and that is a finding rather than a pass.** Keying
     the record on the `NodeId` alone passes all **663** tests, including *"a map inside a map costs
     one more stage and nothing else"*, which exists for the `{definition, evaluation, node}`
     address. The reason is that every frontier raised in a stage is prepared before the next one,
     so two frontiers sharing a node id are always in the same state: with only maps raising them,
     they cannot diverge. The address is structurally required and **observably** bought by the
     first loop whose trip count can differ from a sibling's — a while loop inside a map — which is
     now named in slice 4's test list. (Pre-existing: the old lambda compared all three fields and
     nothing tested that either.)
   - **Checked at the code, needs nothing here:** ADR-0021's *"`expand` gains a step kind that both
     emits and defers"* is already expressible — a node may push its interior steps, then
     `deferred.insert(id)` and set no `ends` entry, and the existing `downstreamOfDeferred` scan
     does the rest. Both `runStages` break conditions already cope with a stage that emits steps
     *and* raises a frontier.
   - `Frontier` gained an inline `operator==` so both `Staging` methods say *the same frontier* once
     instead of open-coding the three-field compare. Equality, not an ordering: nothing needs an
     order over addresses, and equality is a straight port of what the lambda already did.
4. ✅ **The loop runs — BUILT** (2026-09-06). `prepareLoop`, the `LoopExit` step, the count /
   `continue` termination test, the `count == 0` identity, the suppression rules and `iterations`,
   through **both** schedulers. `ctest` **678/678** Debug with video on (+15) and **652/652** Release
   in the default video-off configuration (+15); warning-clean, format-check clean, the `[loop]` tag
   swept **100×** on the parallel path with no failures, and `flowview run` byte-identical to the
   pre-change binary (nothing constructs a loop yet) once the timestamp and minted uuids are
   normalised.
   - **The scheduler learns a loop through ONE new seam, `Node::iterationPorts()`** — `bound`,
     `report`, `index`, `condition` and the carry pairing in one `IterationPorts`, beside
     `innerGraph()` / `innerPin()` / `interiorEvaluation()`. A map needed no equivalent because
     split-or-broadcast follows from a port's own type; none of a loop's five facts follows from
     anything, and pairing by NAME was refused in ADR-0021 precisely because a rename would then
     silently stop a loop carrying. So `scheduler.cpp` still names no node class and still does not
     include `group.h`. **Deviation from the plan:** it is answered `std::optional<IterationPorts>`
     BY VALUE rather than as a pointer to a stored struct — `carries` points into the answering node,
     so a stored one would be a single move of that node away from dangling.
   - **A loop re-raises its own frontier only when the iteration will FINISH in this stage — found by
     the tests, not by the design.** The first cut had an iterating loop always emit its interior's
     steps and re-raise itself; the loop-inside-a-loop and map-inside-a-loop cases then failed on
     their very first run, because the coordinator read the carried outputs between stages while the
     interior had itself deferred, saw them **empty**, and called the iteration a failure — a wrong
     answer, not a crash. `expand` now measures whether the recursive expansion raised any frontier
     of its own and holds the loop back a stage if so. That is what makes ADR-0021's own prediction
     — *"a map inside a loop costs two stages per iteration"* — literally true rather than
     approximately.
   - **A new iteration FORGETS what is staged inside the interior** (`forgetInterior`, recursing
     through the definition). A loop reuses one child evaluation, so a map inside it has the same
     frontier address every pass and would otherwise be expanded against the previous iteration's
     children. Sabotage-verified — and it turned out to be needed for a nested **loop** as well as a
     nested map: dropping it fails both cases, because an inner loop must start its fold over on
     each outer pass. The staging bound becomes *"a map defers at most once per enclosing
     iteration"*, which is still bounded because iterations are.
   - **`LoopExit` is its own routine, which discharges slice 2's flagged trap.** `exitGroup` clears
     an output with no inner pin — exactly the loop's own `iterations` — so it could not be reused.
     Whether the loop could run at all, and whether the fold BROKE, are asked at the exit rather than
     remembered from the preparation that stopped it: `exitMap`'s rule, one question in one place.
     Only a broken **carry** or condition clears everything; an unpaired output being empty is
     ordinary per-port emptiness, exactly as it is for a group.
   - **The iteration count travels in the Step**, as `publish` already does, because a task copies a
     step and can consult no staging state — and it is what separates the two zero-iteration cases at
     the exit: the identity (`count == 0`, every carry delivers its seed) from a loop that could not
     run at all (everything cleared).
   - **Four sabotages, all caught.** Remove the count bound → the count-fold test **hangs**
     (ADR-0021's claim that the bound is the staging loop's well-formedness condition, observed).
     Bind the seeds instead of the carried values → the fold returns 11 instead of 15 and 12 of 26
     cases fail. Drop `forgetInterior` → the map-in-a-loop serves iteration 0's gather forever (4,
     not 10). **Key the staging record on the `NodeId` alone → *"a loop inside a map lets each
     element stop at its own iteration"* fails** — which is the measurement slice 3 could not make
     and recorded as owed here: the `{definition, evaluation, node}` address is now bought.
   - **Accepted costs, both stated in the code.** `bind` marks the whole inner `GroupInputNode`
     rather than one pin, so a subtree fed only by an invariant recomputes every iteration; and a
     loop is not incremental inside — whenever it is selected at all it re-folds from its seeds,
     because iteration k's inputs are iteration k-1's outputs.
   - **Recorded, deliberately NOT fixed here (pre-existing):** `expand` emits a group's `GroupExit`
     unconditionally even when its interior deferred, so a group containing an iterating loop
     republishes a stale value onto its outer outputs on each intermediate stage and its downstream
     recomputes each time. The final value is correct (a test pins it), and **a map inside a group
     does the same thing today** — so the fix (emit entry and inner steps but no exit and no `ends`
     while the interior has deferred, which is exactly the rule the loop arm now follows) belongs in
     its own commit rather than in the one that introduces the loop.
     *(**Fixed 2026-09-06** in its own commit — see "A node does not publish while its interior has
     deferred" below.)*

**A node does not publish while its interior has deferred — BUILT** (2026-09-06, its own commit,
before slice 5). The rule the loop arm arrived at in slice 4, applied to the two arms that did not
follow it. `expand`'s group arm and map arm now measure whether their recursive expansion raised a
frontier, and if it did, contribute the interior's steps but take **no exit and no `ends`** —
deferring themselves so everything downstream waits a stage. Neither raises a frontier of its own:
the interior's is what brings the level back, and `Scheduler::stale` recurses into the child
evaluation, so the node is still selected next stage. `ctest` **680/680** Debug with video on (+2),
warning-clean, format-check clean, `[loop]` swept 100× on the parallel path, `flowview run --example`
unchanged.

- **Both arms, not just the recorded one.** The map's is the same defect with a sharper edge: a
  gather reads elements that have not run, and since ONE hole clears the whole output (ADR-0014) an
  early `MapExit` publishes a **cleared** collection rather than merely a stale value. Fixing the
  group and leaving its twin would have made the rule kind-specific, which is the shape ADR-0009
  keeps deleting.
- **The bug is invisible on a FIRST run, and that is why both tests measure the SECOND.** The first
  draft of each test failed to catch its own sabotage. On a fresh evaluation the node's outer output
  is still empty, so an early publish hands the consumer an empty slot, ADR-0007 suppresses it, and
  nothing computes on nonsense. Only once the node HOLDS a value does an early publish hand over the
  previous run's answer — indistinguishable from a finished one. The map twin needed one more turn:
  with a **new** element the gather reads a child that never ran and publishes empty (suppression
  again), so the row count is held steady and only the values change.
- **The observable is a call count, never a value.** Every value assertion passes with the bug
  present — the final publish is correct — which is exactly why two milestones of tests missed it.
  Sabotage-verified on both: the group's downstream consumer computes **6 times instead of 1**, the
  map's **2 instead of 1**.
- **Deferring a group cost it TWO plan edges, and both are wrong answers rather than wasted work.**
  Neither was anticipated; the parallel section of the group test found them, and both are invisible
  to a serial walk because `plan.steps` order alone happens to be right there.
  1. A group's **entry step still consumes this level's values**, so it needs its incoming edge —
     but the deferral originally took no `ends` entry, and this level's edge wiring skips a node
     that has none. The entry was then free to run before the node feeding it and published the
     PREVIOUS value into the interior. A deferred group now records `Ends{entry, entry}`: the mirror
     image of the map arm's own note, since everything downstream of a deferred node is itself
     deferred and takes no `ends`, so the producing end is never read.
  2. The **entry → inner GroupInput** edge is wired BEFORE the deferral check, not after the exit
     push where it used to sit, because the interior runs in this stage either way. Without it, a
     body node reading the boundary but not downstream of the deferred loop — the ordinary shape —
     races the entry.
  The test needed a scene with a parent-level input relayed through a body node to expose either;
  the first draft had the seed *inside* the group and could not. Sabotage-verified: **11/20** and
  **18/20** failures across the parallel section.
- Two comments corrected with it: `expand`'s header now states the rule once for all three kinds,
  and `runStages`' *"a group is expanded in place and never deferred, so it cannot be a frontier"*
  becomes *never raises a frontier* — which is the property its assert actually depends on, and is
  still true now that a group can be deferred.
5. ✅ **Serialization — BUILT** (2026-09-06). The `loop` section (name-addressed carries + the
   reserved pin names), rectification on the map's precedent, round-trip byte-idempotence. `ctest`
   **690/690** Debug with video on (+10) and **664/664** Release in the default video-off
   configuration (+10); warning-clean, format-check clean, `flowview run --example` unchanged
   (nothing constructs a loop until slice 6).
   - **A loop's reserved pins do NOT survive a load, and both this file and CONTEXT.md claimed they
     did.** The claim was that a reserved pin is *"rebuilt on load by the owner's constructor,
     exactly as a Select's `selector` is"*. That holds for `SelectNode` because the ctor's pin is on
     the node the factory made. A `LoopNode`'s are on **`m_inner`'s boundary nodes**, and the loader
     does `loop->inner() = loadBody(...)` — moving the whole interior away, ctor pins and all. So the
     loader has to declare them onto the graph it is **filling**, before that body's edges resolve,
     which is a new hook: `loadBody` gains a `prepareInterior` callback invoked on the freshly
     constructed Graph before any node is seated. Declaring them FIRST also makes their ids
     deterministic and turns a document naming a dynamic pin `index` into `addDynamicPort`'s
     reported skip rather than `addOutput`'s duplicate-name assert. Sabotage: without the hook, **all
     eight** cases fail.
   - **The reserved pin NAMES are stored, and that is load-bearing rather than tidy.** A reserved pin
     is renameable and an edge into one is name-addressed like every other, so a renamed `continue`
     whose name is not recorded comes back as `continue`, its edge resolves to nothing, and the
     condition falls back to its `Default{true}`. Sabotage-verified: the while loop runs to its bound
     — **110 instead of 13, 100 iterations instead of 3**. (The lost edge *is* reported, so it is not
     perfectly silent; a warning beside a different answer is still a different answer.)
   - **`LoopNode::establishReserved` is ONE routine with two callers** — the constructor on its own
     interior, the loader on the graph it is about to move in — because a fact computed two ways
     eventually disagrees with itself, and here it would do so silently. The canonical names are
     `LoopNode::kIndexPin` / `kContinuePin`, public so the serializer's fallback and a fresh loop's
     spelling cannot drift.
   - **`pairCarry` is the one place a pairing is recorded**, and `addCarry<T>` now goes through it,
     so the authoring gesture and a document restore cannot disagree about what a valid carry is. It
     refuses — recording nothing — a pin not on the matching boundary node, a RESERVED pin, a pin
     already half of another carry, and a **type mismatch**: `Evaluation::bind` type-checks nothing,
     so a hand-written mismatched pair would bind the wrong payload into a slot at runtime.
     Sabotage: skip the restore and **5 of 8** fail, the fold falling back to its seed (11, not 15).
   - **The section is read WHOLE and applied in two parts**, because its halves become true at two
     different moments: the reserved names must reach the interior *before* that body's edges
     resolve, and the carries need the pins to *already exist* to be paired.
   - **No stored interface, unlike a map.** A loop's face is derived exactly as a plain group's, so
     storing the ports would put a derivable fact beside the underivable one. **No version bump**
     either: a v2 document whose loop has no `loop` section loads as a loop with no carries, which is
     legal, just degenerate — and no issue is raised, because nothing went wrong.
   - **Found by the tests, fixed at its source: renaming a defaulted input's port stranded its
     param.** `Port::setName` says a rename *"touches nothing structural"* — false once a port has a
     `Default`, because the param behind it keeps the old name and **a param is addressed by name on
     disk**, so the default silently reverts to the constructor's on the next load. New
     **`Node::renamePort(PortId, name)`** moves both and refuses an invalid or already-taken name;
     `edit::syncGroupPorts`'s retitle phase and flowview's Interface pane now go through it. Latent
     until now — `continue` is the first port in the tree that is both defaulted and renameable — and
     found only because the round-trip test logged *"unknown param \"continue\" on node
     \"GroupOutput\" — skipped"*.
6. ✅ **flowview + the verticals — BUILT** (2026-09-06). `Add ▸ Groups ▸ loop`, an **Add Carry**
   gesture in the Interface pane, the two example nodes that make the condition path real, and both
   verticals driven through the **real binary**. `ctest` **697/697** Debug with video on (+7) and
   **671/671** Release in the default video-off configuration (+7) — both baselines grown by exactly
   the seven new cases; warning-clean, format-check clean, `[loop]` swept 100× on the parallel path.
   **gui-mode was eyeballed by the repo owner on 2026-09-07 and FOUND ISSUES**, not yet triaged at
   the time of writing. *(**Superseded** — they were triaged in the 2026-09-08 pass: the crash was
   M12's stale-value `bad_any_cast`, and the remaining oddity was a documentation gap, not a bug —
   a loop's condition is a POST-test, so a loop is a do-while. **Slice 6 and M11 are COMPLETE as of
   2026-09-08.** See the 2026-09-08 update in CLAUDE.md.)* That is the expected distribution: this slice's
   whole surface is the one all ten of M5's bugs lived on, while the engine slices produced none.
   - **The verticals, through `flowview run`.** The count loop folds a gradient through five blurs
     and reports `iterations: 5`; the while loop blurs until the picture **stops changing** and
     reports `iterations: 23` against a bound of 100 — which is the ADR's own claim that
     `iterations` is what separates "converged at k" from "hit the bound", observed rather than
     argued. Both documents are self-contained (a `GradientNode` is the seed), so the binary opens
     and runs them with nothing bound, and they are written to a stable scratch path so the run is a
     copy-paste.
   - **"Stopped changing" turned out to be LITERAL, which removed an invented number.** The first
     cut carried a tolerance of 0.002; probing it showed a cliff (2 iterations at any threshold down
     to 0.0001, then 8, then **23 for every threshold below ~0.00003**) — because a clamped Gaussian
     blur of an 8-bit image reaches an exact fixed point, so the difference eventually becomes zero.
     The threshold is therefore **0**, `> 0` is an exact test, and the magic number is gone. A
     positive tolerance is the same mechanism stopping earlier, which the bounded section
     demonstrates from the other end.
   - **The count vertical is checked against an INDEPENDENT chain of five blurs, byte for byte.**
     Every weaker assertion — it is an image, it is the right size, it is blurrier than the seed —
     passes just as well when the carry is not carrying at all. Sabotage-verified: making
     `pairCarry` record nothing fails the count vertical on that comparison **and** the while
     vertical on `iterations` (100 instead of 23, i.e. it ran to its bound), while the round-trip
     case correctly still passes, since it only asks whether a load folds the same as the build.
   - **Found and fixed here: the Interface pane's `×` would remove a RESERVED pin.** It calls
     `edit::removePort`, and `Graph::removePort` erases a static port as happily as a dynamic one —
     so from the very pane this slice adds the carry gesture to, a user could delete `index` or
     `continue`. `LoopNode` then held a `PortId` naming nothing, the condition fell back to its
     `Default{true}`, and the while loop ran to its bound with no error anywhere — while a
     save-and-reload quietly healed it (`establishReserved` remakes them), which is exactly what
     would have made it hard to find. The guard is **`Port::isDynamic()`**, which is general rather
     than loop-shaped: every pin a user adds here is dynamic, and a static one exists only because
     the node that OWNS this graph declared it. So the panel needs no idea what a loop is, and at
     the root — where every pin is dynamic — it changes nothing. The NAME stays editable, because a
     reserved pin is renameable by design and its name is stored precisely so a rename survives a
     round trip.
   - **`LoopNode::addCarry` gains a registry-keyed twin, and both run ONE routine.** A host's "+ add
     carry" menu is `portTypeKeys()`, so it has a string where `addCarry<T>` wants a type;
     `addCarryUsing` holds the body and the two spellings differ only by their adder. Sabotage: make
     an unknown key fall back to a default type — a genuinely plausible wrong implementation, since
     a mistyped carry is not a broken carry a user can see but one that binds the wrong payload at
     runtime — and the new `[loop]` case fails. Worth recording: the undo inside `addCarryUsing` is
     still **unreachable**, because both spellings of the adder refuse the same things, so anything
     that fails on the second side already failed on the first. It stays, for the reason the
     original comment gave: it makes atomicity a property of the code rather than of that argument.
   - **The Carries section states the pairing rather than letting the pins imply it.** A carried
     `Image` and an invariant `Image` are the same pin of the same type, and a document may
     legitimately pair two DIFFERENTLY named pins — which per-pin "(carried)" markers alone could
     not show. Each row's `×` removes **both** pins, the atomic inverse of the paired add; the
     pairing needs no explicit drop, because `syncGroupPorts` calls `reconcileInterior()` before it
     touches a port and the host syncs every group on the path every frame. The remove-confirm
     generalised from one target to a list to carry that.
   - **`groupnav::loopAt` / `editableLoopAt`** resolve the loop whose interior a path names — one
     level UP, because the pairing is the loop node's while the pins are its interior's. Two
     functions rather than one plus an "am I allowed?" check, for the reason `resolvePath` and
     `resolveEditable` are two. `loopAt` also checks that the parent path resolved **completely**:
     `resolvePath` truncates what did not, and looking the node up in an ancestor would find a
     different node — M5's bug six, which is what made `enclosingLinkedGroup` return the node rather
     than an id.
   - **A loop crumb says `[last iteration]`** where a map crumb offers its element stepper. It
     discharges ADR-0021's own consequence — *only the last iteration is inspectable* — at the one
     place a user would look for the missing stepper, so its absence reads as a decision.
   - **All four group kinds gained a canvas colour, not just the loop.** `group`, `map`,
     `linkedGroup` and `loop` share one, because they are one category in the catalog and
     `canvasstyle.cpp`'s own rule is that categories are emergent from shared colour. Until now none
     of them had one at all: a group, a map and a linked group were each the default title grey,
     indistinguishable from an ordinary node on a canvas where the one thing you most need to see is
     what you can descend into.
   - **`CompareNode` carries the four INEQUALITIES and no equality.** Exact float equality would
     need an epsilon policy nothing asks for, and inventing one is the guessing ADR-0012 warns
     against — "close enough" is `Less` against a tolerance, which is the shape a convergence test
     has anyway. Its `op` is an enum param, so it round-trips through `data`'s enum-as-name (the
     document reads `"Greater"`) and edits through the existing `editEnum`, both one line beside
     `ConvertNode`'s three.
   - **`ImageDifferenceNode` is a MEASUREMENT, not a blend**, so ADR-0003's op-class enforcement
     does not apply and nothing is converted to Linear — it compares the bytes it was given, which
     is what makes "the last pass produced the same picture" mean what a caller expects. Differing
     size or format is refused rather than reconciled, `CombineNode`'s call; inside a loop that
     refusal is an iteration that FAILED, which is exactly right.

**Verification.** All of it done except the last line. `ctest` green in **both Debug and Release**
(M10 slice 4 found every `#ifdef NDEBUG` enforcement test had gone unreachable) — **697/697** Debug
with video on, **671/671** Release in the default video-off configuration; warning-clean;
`format-check` clean; `[loop]` swept 100× on the parallel path. Both verticals driven headless
through the **real binary** (`flowview run --graph …`: `iterations: 5` for the count fold,
`iterations: 23` against a bound of 100 for the converging one), plus save ⇒ load ⇒ save
byte-idempotence and a `count = 0` document delivering its seeds. Sabotages, each caught: removing
the count bound **hangs** `runStages`; binding the seed every iteration stops the carry carrying;
dropping the reserved-pin skip leaks `index` / `continue` onto the outer face; recording no pairing
makes the count vertical produce one blur instead of five and the while vertical run to its bound;
and an unknown port-type key silently substituting a type is refused. **gui-mode still needs the
repo owner's eyeball on the Metal machine** — slice 6, and the milestone, is not done until that
happens. *(**Superseded** — it happened 2026-09-07 → 09-08; **M11 is COMPLETE as of 2026-09-08.**)*

### Not in this milestone

- **Surfacing `GroupSync::refused`.** Slice 2 built it — an inner pin whose name collides with
  `count` / `iterations` cannot be mirrored, and the refusal is reported BY NAME because that is the
  part a user can act on — and `groupnav::syncPathGroups` drops it on the floor. It is the same
  compiled-linked-unreachable shape this file keeps catching, and it is not caused by this slice, so
  it gets its own commit: `syncPathGroups` returns the names, the Issues pane renders a row each.
  **BUILT 2026-09-09**, and it needed TWO readers, not one: `syncGroupPorts` has exactly two
  production callers and *both* dropped the field. `syncPathGroups` now returns a `PathSync
  {changed, refused}` whose `PinRefusal` carries the group, its name and the path to its INTERIOR —
  the level the pin lives on, so the Issues row leads where it can be renamed. The **loader** is the
  second, found while fixing the first, and it is the more important one: it syncs EVERY group in a
  document, where the host reaches only the groups the user is inside, so a refusal now survives
  navigating away and reaches headless `run` / `list` (through `ctx.warn`'s log line — `runmode`
  reads `LoadResult::issues` for nothing else either). The row states the CONSEQUENCE, not the cause
  — *"inner pin 'count' could not be mirrored onto its face — nothing outside can connect to it"* —
  because there are two causes (a loop's name collision, a map's unliftable type) and `refused`
  deliberately carries names alone. `ctest` **731/731** (+3); sabotage: drop either reader and its
  own case fails; proved through the real binary on a hand-collided `count-loop.json`.
- **The Issues pane flagging an unwired DEFAULTED input** as "required input is not connected". A
  defaulted input stays `Required` by design (that is what keeps a default from swallowing
  suppression), so every fresh loop reports `count` and every loop interior reports `continue`.
  Pre-existing: `BlurNode`'s `radius` / `sigma` have done it since 2026-08-15. One line — skip when
  `node.defaultOf(port) != nullptr` — and its own commit.
  **BUILT 2026-09-09**, and the one line was the smaller half. It was wider than recorded — TWELVE
  defaulted inputs exist (Blur radius/sigma, Gate enable, Select selector, Loop count, a loop
  interior's continue, ClipSequence position/count, FrameAt position, OpenSequence path, LoadImage
  path, ListDir directory/extension), and the default example scene holds a Blur, so flowview OPENED
  with two false warnings. It lasted because nothing could test it: `collectIssues` was a `static`
  function inside `issuespane.cpp`, which the driver-free test binary does not compile, so the
  panel's whole validation — this rule, the map-hole row, the Cast row — had never been under test.
  So it moved to **`apps/flowview/src/validation.{h,cpp}`** (it touches no ImGui: a pure
  `Graph` + `Evaluation` → `vector<Issue>`), compiled into `test-flowview` beside `groupnav.cpp`.
  `ctest` **734/734** (+3): the rule driven through the production `BlurNode`, which carries both
  kinds of required input at once; a loop reporting neither `count` nor `continue`, asked of both
  levels; and the readiness rule, pinned so the move is known to be faithful. Sabotage: restore the
  old condition and the first two fail.
- **`refusalText(NotInline)` saying "This group is linked - make it local first"**, which is wrong
  for a map and now for a loop as well: `edit::ungroup` refuses both, and neither is linked. The
  menu greys the item out (`canUngroupSelection` asks for an `InlineGroupNode`), so the wording is
  only reachable from a keyboard shortcut — pre-existing since M8.
  **BUILT 2026-09-10, and that last sentence was wrong**: the shortcut runs the same
  `ungroupSelection`, which pre-filters with `soleSelectedOfType<InlineGroupNode>` and answers
  *"Select a single inline group to ungroup"* before `edit::ungroup` is ever called. So the string
  was unreachable from all four of its call sites (`groupSelected` cannot return it and
  `replaceGroup` returns only `NotAGroup`), and `NotAGroup`'s was dead beside it. Fixing the wording
  alone would have edited text nobody could see. So: `ungroup` now asks the two SEAMS instead of one
  class — `editableInner()` for *is this interior ours* (**`LinkedInterior`**, renamed from
  `NotInline` to say the fact rather than the failed test) and `interiorEvaluation()` for *does it
  run once* (**`InteriorRepeats`**, new) — so it names no node kind, as the scheduler does not; and
  `ungroupSelection` asks only *is exactly one node selected*, letting each of the four cases carry
  its own reason. `ctest` **735/735** (+1): the case that was missing, since this file tested the
  linked and plain-node kinds and neither of the two added since. Sabotage: put the single class
  test back and it fails.
- **A linked loop** — one shared template iterated, exactly as ADR-0014 defers the linked map.
  *(**Refused** 2026-09-09 along with it: compose a loop whose interior holds a linked group.)*
- **Retaining per-iteration state**, and the breadcrumb iteration stepper it would enable. Needs a
  retention policy, which ADR-0012 refuses to have.
- **Plan caching across stages** — the optimisation that makes this lowering cheap, already deferred
  elsewhere in this file, and the named escape if `O(N × document)` planning ever bites.
- **A scan output** (gathering across iterations). Refused so Loop and Map stay one job each; the
  machinery would be `exitMap`'s gather.
- **`count` visible to the interior**, for progress. Additive, nothing asks.

## Milestone 12 — payload types: a node's type as data (grilled 2026-09-07, **COMPLETE** 2026-09-08)

**All four slices built and live-verified.** Testing M11's loop in the GUI was blocked: `LoopNode`'s
`index` is an `int`, `CompareNode`'s inputs were `float`, and `Graph::connect` type-checks exactly — and there is **no conversion anywhere in
`flow`**, implicit or explicit. That is an instance of a wider gap: a node's payload type was baked
into its **factory key**, so `ConstantNode<T>` cost one palette entry per type (five, against a
registered port-type set of ten and growing), `Gate`/`Merge`/`Select` existed for `image::Image`
alone, and a **Cast** — the node that fixes the original problem — is impossible in that model at any
size, needing one key per *pair*.

Decisions in **[ADR-0022](docs/adr/0022-payload-types-as-data.md)**, vocabulary in CONTEXT.md's
*Payload type* section. **The Cast is the forcing function; Constant and the control nodes are the
beneficiaries** — collapsing five keys into one is a refactor with no new capability, and this repo's
record is that a mechanism built without a caller stays unreachable.

Four slices, one commit each:

1. **the mechanism + `ConstantNode`** — built 2026-09-07
2. **the conversion registry's consumer: `CastNode`** — built 2026-09-07
3. **the ordering capability's consumer: `CompareNode`** — built 2026-09-07
4. **`GateNode` / `MergeNode` / `SelectNode`** — built 2026-09-07; the case that proves a retype
   reaches DYNAMIC pins

All four are built, and **gui-mode was eyeballed 2026-09-08** — it found one crash (the stale-value
`bad_any_cast`, fixed; see below) and nothing else. **M12 is COMPLETE as of 2026-09-08.**

### Slice 1 — built 2026-09-07: the mechanism, and Constant stops being a template

`ctest` **717/717** Debug with video on (+20 new cases, from 697), warning-clean, format-check clean.
**gui-mode NOT eyeballed** — the Inspector's payload-type dropdown is new UI and needs a Metal
session, the standing gap for every adapter change. *(**Superseded** — eyeballed 2026-09-08; see the
stale-value section below.)*

- **`Node` gains payload types as DATA, not a virtual.** `payloadTypes()` is a plain accessor and
  `declarationsOf(name)` says what a retype would move; the one virtual is `acceptsPayloadType`. A
  nullable virtual like `innerGraph()` / `iterationPorts()` was the plan, and was wrong: the tagging
  has to live on `Node` anyway (it tags `Node`'s own ports and params), so a virtual answering a fact
  `Node` already holds would be a second source able to disagree with it.
- **Retyping is IN PLACE, and that decision was made at the code.** A declaration's identity is its
  `PortId` and its type is a field, so a retype assigns the field. Remove-and-re-add draws a fresh
  `PortId` and would dangle every edge, canvas id and layout entry naming the old one —
  sabotage-verified: implementing it that way **aborts** two cases.
- **`addInputLike` / `addOutputLike` already existed** (M5 built them so a group could mirror any
  type with no `T`), so `PortType` needed **no** "declare a port of me" field — a plan claim
  corrected during the build. What was missing was the param twin, now `addParamLike`-shaped
  (`addParamOf`), seeded from the new `PortType::defaultValue` capability.
- **The primitive REFUSES, so the hazard is structural.** `Graph::setPayloadType` refuses while any
  incident edge would be left with ends that disagree; `edit::setPayloadType` cuts exactly those and
  reports them. Without that, retyping under a live edge installs a wrongly-typed payload —
  **nothing re-checks an edge after `connect`**, since `populateInputs` copies into the slot blind —
  and it surfaces as a `bad_any_cast` thrown inside a worker task, far from the click that caused it.
- **Recorded, because the comments first claimed otherwise:** *"edges that still typecheck survive"*
  describes a case that **cannot occur**. `connect` is the only edge-maker and it type-checks, so an
  existing edge's other end always carries the port's current type — a genuine retype therefore
  breaks *every* edge on *every* port it moves. Edges on the node's **untagged** ports (a Gate's bool
  `enable`) are never examined and survive, and that is what the test pins. The predicate stays
  spelled as the invariant rather than as its present consequence, since it is the primitive's
  contract.
- **Per-type behaviour follows ADR-0014's own split**, which settled two questions at once:
  **ordering** is a `PortType` field (derivable from `T`), **conversion** is a registry (a relation
  between two types, and nothing can enumerate the second where a `PortType` for the first is built).
  The payoff is that a Compare's accepted set is derived — `acceptsPayloadType` asks
  `isOrderable()` — rather than listed and left to rot.
- **`meta::is_less_comparable` had to ask a container about its ELEMENT**, found by the build and not
  by reading: before C++20 `std::vector`'s `operator<` is declared for every element type and fails
  only when **instantiated**, so plain detection answered true for `std::vector<image::Image>` and
  the failure was a hard error inside `<algorithm>`, not a false from the trait.
- **The conversion registry landed in slice 1, not slice 2 as planned** — `edit::setPayloadType` is
  its first caller (carrying a param's value across a retype), and deferring it would have shipped a
  slice whose retype loses the number the user typed, then corrected it a commit later. A retype uses
  **the same conversion a Cast will**, so the registry means one thing everywhere.
- **No schema version bump.** One optional `types` section per node; **absent means the factory's
  preset stands**, exactly as an absent `params` means the declared defaults do. The five keys
  `constant` replaced stay registered through `Factory`'s **string-creator** form, which records no
  reverse type → key entry — load-bearing, because `Factory::keyOf` maps one class to one key, so
  the typed form would make a Float constant **save as `constInt`** with a `types` section
  contradicting its own kind. Sabotage-verified.
- **Six sabotages, all caught**, each by a named case: the primitive skipping its edge check; the
  gesture skipping the disconnect; retype by remove-and-re-add; `float → int` rounding instead of
  truncating; the legacy keys registered the typed way; the loader ignoring `types`.
- **A bug in a new test, found by running it:** the conversion-menu case registered the app's
  *conversions* without its *port types*, so every `portTypeKey` was empty and the sorted menu was
  meaningless. It now goes through `registerSceneSerialization`, which is what the binary does.
- **Also recorded: `ctest -j8` fails ~7 io/io::video cases** on shared scratch paths in the temp
  directory. Pre-existing, unrelated to this work, and invisible serially — worth its own commit.

### Slice 2 — built 2026-09-07: the conversion registry's consumer, `CastNode`

`libs/flow/include/lain/flow/nodes/cast.h` — two payload types (`from`, `to`), `compute` looks the
pair up and applies it. The node the whole mechanism exists for.

- **An unconvertible pair is REPRESENTABLE, and that was forced by the loader.** Refusing one through
  `acceptsPayloadType` is the shape this repo normally prefers and is wrong here: the loader applies
  payload types **one at a time**, so a document saying `Path -> String` would have its `from`
  refused while `to` was still the factory's `Float` preset, and the node would come back as
  something other than what was saved. So the pair is checked where it can be answered whole
  (`canConvert()`), and the **Issues pane** reports it.
- **That row is the one place the adapter asks a CLASS** rather than a structural fact (the map-hole
  row beside it asks `interiorEvaluation()`). There is none to ask — nothing in general says two
  payload types must be bridged — and a `Node` virtual for one caller would be a seam with nothing
  behind it. flowview already names every node kind, so the `dynamic_cast` is not the layering break
  the same line would be in the scheduler.
- **A type converts to ITSELF**, answered by `convertValue` rather than by a registration
  (`registerConversion` refuses a self-pair). Without it, the state a user passes *through* while
  changing both ends would read as broken.
- **Cast sits in the Control category**, whose meaning is now stated: the graph's PLUMBING — what
  routes, tests and converts values, as opposed to what processes pictures.
- **Deviation from the plan, deliberate: no generated per-pair palette entries.** `Cast > Int ->
  Float` read off the registry needs `NodeCategory` to carry a preset and three render sites to apply
  it — a refactor for ergonomics, where the Inspector's two dropdowns already reach every pair in two
  clicks. Recorded below as a follow-on rather than skipped silently.

### Slice 3 — built 2026-09-07: `CompareNode` over any orderable payload type

The blocker's real fix. `CompareNode` is non-template with one payload type, and its accepted types
are **derived** — `acceptsPayloadType` asks `PortType::isOrderable()`, so Int, Float, String and Path
all compare and an Image never appears in the menu. A list would need revisiting every time the app
registered a type.

- **`compute` names no type**: it asks the payload type's own three-way `compare` and maps the
  answer onto the four inequalities.
- **The threshold lost its constructor argument.** `b`'s default is the payload type's own default
  value, because a compile-time seed cannot follow a type chosen at runtime. A caller that wants one
  sets the param — which is what the Inspector does.
- **The factory preset stays Float**, so every document written before this loads unchanged.

### Slice 4 — built 2026-09-07: `GateNode` / `MergeNode` / `SelectNode`

All three were already type-agnostic in `compute` (`set<T>(get<T>())` is a whole-slot copy), so they
needed only the mechanism — and they are the case that keeps it honest.

- **A DYNAMIC pin has to be TAGGED as it arrives.** New `Node::tagAs`, called from
  `onDynamicPortAdded`: a Merge's branches are added at runtime through the port-type registry, whose
  creator has a compile-time `T` and knows nothing about payload types. Without it a retype moves the
  constructor-declared `out` and leaves every branch behind — the node forwards nothing while looking
  correctly configured. **Sabotage-verified**, and the entire reason these three were in scope.
- **A retype leaves the non-payload pins alone** — a Gate's bool `enable`, a Select's int `selector`.
  Neither is declared from the payload type, so neither moves.
- **Merge and Select now TYPE-CHECK the branch they forward.** `acceptsPortType` is the HOST's filter
  and `addDynamicPort` answers to nobody, so a mistyped branch must produce nothing rather than put a
  wrongly-typed value in a slot that declares another type.
- **The factory presets stay `image::Image`**, which is what these were fixed to before M12 — so
  every existing document, which names no `types` for them, loads exactly as it did. What is new is
  that a graph can now gate a float, merge bools or select between paths.

### The milestone's vertical, and its state

**`a loop's own index drives its condition and its body`** (`apps/flowview/test/test_loopscene.cpp`)
is the blocker, resolved, through the production factory and the production retype gesture:

    index -> Compare<Int> -> continue      the condition, needing NO cast (slice 3)
    index -> Cast(Int -> Float) -> sigma   the body, needing one (slice 2)

It reports **4 iterations against a bound of 100** — it stopped on the condition (`index < 3`, so the
iteration at index 3 is the last that ran), which is the fact ADR-0021 built `iterations` to make
readable. And the Cast is proved to actually feed the blur by cutting its edge and checking the fold
produces a **different picture**: every weaker assertion passes either way.

`ctest` **724/724** Debug with video on (+27 from 697) and **698/698** Release in the default
video-off configuration (+27 from 671); warning-clean, format-check clean, and `flowview --version` /
`list` / `run` unchanged through the real binary. **gui-mode NOT eyeballed** — the Inspector's
payload-type dropdown and the Issues row are new UI and need a Metal session. That was the one thing
standing between M12 and complete. *(**Superseded** — the eyeball happened 2026-09-08 and is the very
next section: it found one crash, fixed there. **M12 is COMPLETE as of 2026-09-08.**)*

### Found by the repo owner in gui-mode, fixed 2026-09-08: a stale value crashed the panes

Adding a Constant (Int) and changing its type to Image threw **`std::bad_any_cast`**. The mechanism
is one the milestone creates and nothing else in the tree had: a retype makes a port's declared type
disagree with the value the LAST RUN left in the evaluation. Before payload types a port's type was
fixed at declaration, so a slot held that type or nothing — `empty()` was the only mismatch possible.

- **The retype cannot clear the value, and should not try.** Invalidation is PULLED (ADR-0012): a
  definition holds no list of its evaluations, so `edit::setPayloadType` has nothing to reach. It
  bumps the node's version and the next run corrects the slot — but **a host draws in between**, and
  every pane renders a port through `Evaluation::describe`, which resolves the port's declared
  `PortType` and calls its `describe` bridge on whatever the slot holds.
- **The fix is at the bridges, which must be TOTAL in the value they are handed.**
  `describePortValue` / `describeCollection` / `collectionSize` / `collectionAt` now test
  `holds<T>()` rather than `empty()`. `gather` already did.
- **`describe` answers `"(stale)"`, deliberately not `"(empty)"`** — empty means SUPPRESSED
  (ADR-0007), and saying it here would be a lie about a port that has a value, just not one of its
  type yet.
- **The same bug was in the CLI, unreported**: `dump.cpp` tested the port's **declared** type and
  then read the value, so `flowview run` over a retyped document would have thrown the same way.
  Both sites now ask what the VALUE holds. Found by auditing for the shape rather than by hitting it.
- **The two gui value paths were already safe** (`valueviews.cpp`, `previewcache.cpp`,
  `interfacepane.cpp`, `inspectorpane.cpp` all guard with `holds<T>()`), which is why the crash came
  out of the text line and not the thumbnail.
- `ctest` **727/727** Debug with video on (+3) and **701/701** Release video-off (+3).
  Sabotage-verified: dropping the guard fails the engine case *and* the cli case.

### Not in this milestone

**Generated per-pair palette entries** (`Cast > Int -> Float` read off the conversion registry) — it
wants `NodeCategory` to carry a payload-type preset and the three render sites to apply it; the
Inspector's dropdowns already reach every pair. Rounding / saturating / checked numeric conversions (one numeric node, when something asks);
`bool ↔ int` (what `2 -> true` means is a decision nothing is asking for); a payload type that is
itself a **collection** (nothing refuses it, nothing tests it); and the canvas offering to **insert a
Cast on a mismatched drag** — the registry makes it possible, but it is an adapter gesture with its
own design (what if two conversions exist? none?) and wants grilling of its own.


## Outstanding work — one index

Every deferred item, known defect and standing refusal in this file, in one place. It exists because
that information is otherwise spread across nine *Not in this milestone* sections, four ADR
*Deliberately unsettled* lists and three backlog tiers — which is how the 2026-09-09 audit found
**thirteen** claims a later milestone had already overtaken, three of them milestone status lines
saying work was unbuilt that was finished weeks earlier.

**This is a POINTER index, not a second copy.** Each row names an item and says where the reasoning
lives; none of them restates a decision, because a restatement is a second source able to disagree
with the first — the shape M7 slice 1 and ADR-0014 each deleted. Owners are named by **section**
rather than by heading anchor, since the headings here carry dates that change.

**Keeping it honest:** when an item is built or refused, amend its owning section and **delete its
row here**. A row is cheap to delete and expensive to leave.

### Unfinished milestones

- **M9 — camera calibration and fixed registration.** Designed 2026-08-14/15 with three ADRs, no
  code. **Unblocked:** M10 was numbered after it and built before it, discharging its frame-sequence
  prerequisite. *(Milestone 9; [ADR-0015](docs/adr/0015-permissive-by-default-production-dependencies.md),
  [ADR-0016](docs/adr/0016-camera-calibration-method-modules.md),
  [ADR-0017](docs/adr/0017-ceres-for-registration-refinement.md).)*

M1–M8 and M10–M12 are built. M9 is the only milestone from 5 onward that is not.

### Deferred — engine / `flow` core

- **The `Collection` payload** — the zero-copy element aggregate behind the same
  `size`/`at`/`gather` interface. The *question* was answered outside the map by ADR-0018; the type
  is not built. *(ADR-0014 §Deliberately unsettled; M8 §Not in this milestone.)*
- **Keyed elements** — the parallel key input, so "stream 3" survives a reorder. Downstream of the
  `Collection` payload. *(ADR-0014 §Deliberately unsettled.)*
- **Per-element incrementality** — unavailable while the payload is a natural `std::vector`; blocked
  on `Collection`. *(ADR-0014 §Consequences; M8 §Not in this milestone.)*
- **Retaining per-iteration state**, and the breadcrumb iteration stepper it would enable. Needs a
  retention policy ADR-0012 refuses to have. *(ADR-0021 §Deliberately unsettled; M11.)*
- **Plan caching** across runs and across stages — the optimisation that makes the group/map/loop
  lowering cheap. *(ADR-0009; ADR-0021; M5 §Deferred; M11.)*
- **A scan output** for Loop (gathering across iterations) — refused so Loop and Map stay one job
  each; not impossible, the machinery is `exitMap`'s gather. *(ADR-0021; M11.)*
- **`count` visible to a loop's interior**, for progress. Additive. *(ADR-0021; M11.)*
- **In-run liveness release** — M6 bounds retention *after* a run, not peak *during* one. Cheap to
  add because `PortValue` payloads are already shared and immutable. *(M6 §Not in this milestone.)*
- **A multi-value cli binder** for a collection boundary input (`--files a.png b.png c.png`).
  *(ADR-0014; M8.)*
- **A registered node-serializer seam**, if third-party structural node kinds ever appear.
  *(M5 §Deferred.)*
- **The Merge / fan-in (multi-connectable) port** — one input pin accepting N connections, which is
  Merge's natural form; the N-pin Merge that exists is explicitly a strawman for it.
  *(M4, deferred as planned.)*
- **Port reorder.** Ids ride along, so edges are untouched by a permutation — but nothing performs
  one. *(M4, deferred as planned; see also the storage note in M4's mutation design.)*

### Deferred — payload types

- **A payload type that is itself a collection** — nothing refuses it, nothing tests it.
- **`acceptsPayloadType` refusing per-VALUE** rather than per-type. No caller.
- **Rounding / saturating / checked numeric conversions.** One numeric node, when something asks.
- **`bool ↔ int`** — what `2 → true` means is a decision nothing is asking for.
- **Generated per-pair palette entries** (`Cast ▸ Int → Float`) — wants `NodeCategory` to carry a
  preset; the Inspector's dropdowns already reach every pair.
- **The canvas offering to insert a Cast on a mismatched drag** — possible, but an adapter gesture
  with its own design (two conversions? none?), so it wants grilling of its own.

*(All six: [ADR-0022](docs/adr/0022-payload-types-as-data.md) §Deliberately unsettled;
M12 §Not in this milestone.)*

### Deferred — groups and templates

- **Prefab overrides / per-instance divergence.** Parameterising via boundary pins is preferred, and
  sharing strengthened the argument. *(ADR-0010; M5 §Deferred; M7 §Not in this milestone.)*
- **File watching on templates** — an external edit needs the explicit *Reload Linked Groups*
  gesture, which shipped in M7 slice 3. *(M7; M5 §Deferred.)*
- **In-place template editing** — `Edit Template…` stays the explicit act. *(M7.)*
- **"Map over selection"** — a group-kind parameter on `edit::groupSelected`, the ergonomic gap left
  by refusing the linked map. *(Backlog Tier A #10.)*

### Deferred — video and media

- **A driving timeline in the gui.** Slice 7 is an *inspection* player; a transport that binds a
  `FramePosition` and re-runs the graph is the follow-on.
- **A handle pool** and a **decoder pool** (with it, parallel decode). Both interfaces were specified
  so each is a pure backend swap.
- **BT.601 / BT.2020 / PQ / HLG**, U16 decode output, audio, and **BT.1886** display rendering.
- **Device capture** — the prebuilt FFmpeg is `--disable-avdevice`.
- **Realtime playback of processed output** — that is the streaming pipeline, Tier B #4.
- **`core::Uri`** — queued with its own ADR; must *not* be an RFC 3986 parser, and its home is
  `core`. *(§Queued: `core::Uri`, in Milestone 10.)*

*(Others: M10 §Not in this milestone.)*

### Deferred — serialization, params, graphics

- **Untagged variant, yaml / xml / binary codecs, per-node-type schema versioning, and
  C++26-reflection auto-`serialize`.** *(Backlog Tier A #1, §Deferred (designed-for).)*
- **A read-only ("Debug") param kind**, the **per-value editor hint** (M10 slice 7 met it twice —
  a `path` cannot tell an image from a folder from a clip, a `FramePosition` cannot know its
  sequence), and **keyboard-search add**. *(ADR-0005.)*
- **The non-encoding swapchain** — designed, dormant on MoltenVK; an archimedes change for when a
  target surface actually forces sRGB. *(ADR-0002.)*

### Backlog tiers — speculative and foundational

- **Tier B:** #4 streaming pipeline · #5 node hot-reload + a node-type plugin registry ·
  #6 imgui-node-editor + docking/multi-viewport · #7 type-owned factory keys.
- **Tier C:** #8 `core::DateTime` · #9 video `Timecode` / `Timestamp`. **Both triggers have fired
  and were answered negatively** — json shipped without timestamps, and M10 addresses a frame by
  ordinal with `media::FrameRate` + `core::Time`. Still unbuilt, now with no predicted caller.

### After M9 exists — its own exclusions

Joint intrinsic/extrinsic optimization, multi-board calibration, mutable intrinsics during
registration; moving-camera pose tracking, temporal-alignment estimation, world-frame alignment; a
generic `Measurement<T>` without a second caller; runtime-defined distortion-model plugins and
SuiteSparse-enabled Ceres; capture manifests and capture datasets (which M10 hands to M9).
*(M9 §Not in this milestone; M10 §Not in this milestone.)*

### Known defects, each owed its own commit

Not deferred features — acknowledged bugs, listed here so they stop being rediscovered.

**The list is empty.** All four are fixed — the three from M11 §Not in this milestone
(`GroupSync::refused` had no reader, 2026-09-09; the Issues pane flagged an unwired DEFAULTED
input, 2026-09-09; `ungroup` told a map it was linked, 2026-09-10) and the parallel-ctest collision
below (2026-09-10). Two of the four turned out to be **larger than their one-line description**, and
in the same way each time: the described symptom was the visible end of a duplicated or unreachable
decision. Worth remembering when writing the next entry here.

### `ctest -j` and scratch paths — FIXED 2026-09-10

`ctest -j8` failed ~7 `io` / `io::video` cases, nondeterministically, and passed serially every time.
**`catch_discover_tests` registers each TEST_CASE as its own ctest test and runs it by invoking the
executable again with a filter — so every case is a separate PROCESS.** Fourteen test files had each
grown the same private fixture uniquifying its path with a `static int counter = 0`, under a comment
promising *"each test owns an isolated fixture on disk"*. That counter restarts at 0 in every
process, so two concurrent cases of one executable took the same name and wrote over each other: the
failure output showed one file holding two tests' interleaved frames.

- **A counter cannot fix it**, because the processes cannot see each other's. What can is identity
  minted without coordination — `core::Uuid`, already in the tree for exactly that. So the new
  header-only **`lain::testing`** (`libs/testing`, built only under `LAIN_BUILD_TESTING`) puts the
  uniqueness in a per-process **directory** and a plain counter inside it, which is sufficient
  because Catch2 runs one case at a time within a process.
- **The uuid buys the CLEANUP as much as the name**, which the sabotage is what showed: fixing the
  directory name to a constant fails **10–19** tests, more than the 7 it replaces, because each
  finishing process then `remove_all`s the directory out from under every other one still running.
- **Fourteen copies deleted, plus three fixed-name paths** that were the same latent shape and
  happened not to be hit. Measured bonus: a full `-j16` run now leaves **zero** scratch directories
  behind, where the old scheme had left 13 stray files in the temp dir.
- **`lain::testing` has no test of its own, deliberately.** The property that matters is
  cross-process, and no in-process fixture can assert it — a case checking "these two paths differ"
  would pass just as happily under the bug. The regression test is **the suite at `-j`**, which is
  now green at `-j8` (×5), `-j16` (×3) and serially.
- **CI now runs `ctest --parallel 4`**, and that is part of the fix rather than a follow-on: its
  step carried a comment reading *"Serial on purpose: twenty test files write into the shared
  `fs::temp_directory_path()`"*, a workaround whose reason this commit removes. Leaving it serial
  would also leave the regression untested — a collision is invisible to a serial run, so a serial
  CI could not catch this coming back.
- **Every private helper is gone, including flowview's four** (which root their per-case names in
  `scratchDir()` now) and the deliberately-absent paths in the io tests, which were still spelling
  `temp_directory_path()` by hand and so still inviting the next author to copy that line for a
  fixture that WRITES. The one deliberate exception is `test_loopscene.cpp`, whose stable directory
  is documented as a copy-paste target for a manual `flowview run --graph …`; one case writes there,
  so a uuid would buy it nothing.
- Not covered: nothing makes a NEW test file use the helper. A private fixture reintroducing a
  counter would still work serially — but now fails in CI, which is the point of the line above.

**Noticed while fixing the second, not built:** the Issues pane has no row for *a node that ran and
produced nothing*. It matters because a `LoadImage` added from the palette defaults its `path` to an
empty one — so it loads nothing, suppresses everything downstream, and (correctly, now) reports no
missing connection. The old row said the wrong thing about it; saying nothing is better, and saying
the right thing is a check the panel does not have. It wants care: a suppressed node is ORDINARY
under ADR-0007, so the rule cannot simply be "empty output is a problem".

### Refused, not deferred — do not re-raise

- **A linked map and a linked loop** — compose instead: a map (or loop) whose interior holds a
  linked group. *(ADR-0014, amended 2026-09-09; ADR-0021.)*
- **A value-derived key** for map elements — unimplementable for the type the first vertical maps
  over. *(ADR-0014.)*
- **An ambient time cursor**, and **a decoded-unit `Frame` aggregate**. *(ADR-0018.)*
- **Relabelling explicitly tagged BT.601 / BT.2020 / HDR footage** — reported and refused, never
  silently converted. *(ADR-0018; ADR-0020.)*
- **thorax** — static linking with service-shaped seams instead. *(ADR-0004.)*
- **GPU coverage in CI.** gui-mode stays eyeball-verified on a Metal machine and the one `[gpu]` test
  self-SKIPs. Deliberate and permanent. *(§Continuous integration.)*

### Standing verification gap

`flowview` gui-mode is verified by the repo owner's eyeball on a Metal machine, not by CI. Nothing in
the suite or the workflow should be read as covering it.

## Backlog (deferred — don't build speculatively)

### Tier A — when a real graph demands it

1. ✅ **Graph serialization — BUILT and live-verified** (2026-07-12; grilled + designed
   2026-07-11). The whole spine — `C++ type ⇄ data::Value ⇄ JSON on disk ⇄ data::Value ⇄
   Graph` — is in and idempotent. See the **2026-07-12 update in [CLAUDE.md](CLAUDE.md)** for the
   full landing notes, the `data` + `flow::serialize` sections of [CONTEXT.md](CONTEXT.md) for the
   model, and [ADR-0006](docs/adr/0006-format-neutral-value-dom-serialization.md) for the
   format-neutral-DOM decision. What landed: `libs/data` (Value DOM + reflection + macros +
   variant), `libs/io/data` + `plugins/io/data/json` (the codec seam), `libs/flow/serialize` (the
   graph walk — `ValueCodecs`, name-addressed edges, canonical id remap, dynamic-pin replay, the
   opaque `editor` section, best-effort `LoadResult`), the flow-core additions it needed
   (`hasPortNamed` + port-name uniqueness + `validPortName`, `portTypeKey` reverse lookup,
   `Factory::keyOf`, `Graph::nodeIds`), and flowview (`graphio`, gui Save…/Load… with canvas layout,
   and the headless **`run` / `list` subcommands**). `ctest` 275/275; headless round-trip is
   byte-idempotent; gui Save/Load eyeballed on the live Metal driver; `run`/`list` exercised live.

   **The cli `run` mode is done:** `flowview list [--graph <path>]` prints a graph's boundary
   inputs/outputs (`--<name> : <type>`, marking any input with no cli binder), and `flowview run
   [--graph <path>] [--save <path>] [--<boundary> <value> …]` loads (or builds the example), binds
   boundary inputs by name, runs, dumps, writes bound outputs. `--graph` is an option (not a positional)
   so `allow_extras` can carry the `--<boundary> value` bindings unambiguously. Port names are now
   identifiers (`validPortName`), so they work as flags.

   **Scalar boundary binding BUILT** (2026-07-13). Boundaries are no longer image-only: `int` / `float`
   / `bool` / `string` are registered **port types** (so they're creatable in the gui Interface panel's
   ± menu and round-trip through save/load) *and* have cli binders. The **`BoundaryBinders`** seam
   (`clibinders.{h,cpp}` — a `type_index → (string → PortValue)` registry) is the "cli medium" for a
   type, parallel to `ParamEditors` (gui) and `ValueCodecs` (disk); flow core still names no payload
   type. `run --<input> <value>` parses by the pin's type (a failed parse and an unbindable type are
   distinct errors); an image **output** writes a file, a scalar output writes its `describe()` text
   (new `BoundaryOutput::describe()`), a suppressed output writes nothing. Verified headless: an
   int/string/bool passthrough graph binds and writes its outputs; the image scene is unchanged.

   **Notable deviations from the plan:** static-port uniqueness uses a plain `<cassert>` assert (not
   `log::ensure`) so `flow` core stays log-free; the id remap isn't exposed as a raw table — instead
   `fromValue` re-keys the `editor` blob onto the loaded ids in `LoadResult.editor`; the
   `loadGraph`/`saveGraph` facade lives in flowview (`graphio`, app-level), not `flow::serialize`.

   **Deferred (designed-for):** untagged variant, yaml/xml/binary codecs, per-node-type schema
   versioning, C++26-reflection auto-`serialize`, and ~~the **video** boundary loader~~ — **claimed by
   Milestone 10**, which binds `--video <uri>` to a `FrameSequence` through the same `BoundaryBinders`
   seam `--image` already uses, and sweeps a `FramePosition` input. `core::DateTime` (Tier C #8) is
   **not** pulled in — `version` is a plain int, no timestamps.
2. **Conditional / gated nodes** — a node suppresses downstream eval; the one piece a pure push DAG
   can't express. **Mechanism BUILT** (2026-07-12, [ADR-0007](docs/adr/0007-conditional-eval-via-input-readiness.md)):
   modelled as **input readiness** — a node computes iff every **Required** input has a value; else
   `Scheduler::runNode` clears its outputs and doesn't compute, and that emptiness propagates. Per-input
   **`Presence { Required, Optional }`** (`addInput<T>(name, Presence::Optional)`); an Optional input
   (a `Select`/`Merge` branch) doesn't block, so `compute()` checks `ready()` and picks a live one. A
   `Gate` suppresses by `clear()`ing its output — **"skip" is just absence-of-value**, no separate port
   state. Composes with incremental for free (a gate flip is dirty → the closure resurrects/suppresses
   the subtree). The framing that shapes it: gate **upstream** to save work, don't select downstream.
   3 tests. **Concrete nodes BUILT** (slice 2): payload-generic, header-only templates in flow core
   (`flow/nodes/`) — **`ConstantNode<T>`** (a value source; its value is a **serialisable Param**, so it
   round-trips through save/load and the inspector renders a type-editor for it; feeds a Gate's bool
   `enable` / a Select's int `selector`), **`GateNode<T>`** (the upstream suppressor — passes `value`
   when `enable`, else clears), **`MergeNode<T>`** (rejoin: forwards the first live of two Optional
   branches), **`SelectNode<T>`** (mux by an int selector). 4 tests wire an if/else (two gates → merge)
   + a select. **Variadic Merge/Select via dynamic ports BUILT** (2026-07-13): `MergeNode<T>` and
   `SelectNode<T>` are now `DynamicPortsNode`s — N homogeneous branch inputs (`acceptsPortType` narrows
   the "+" menu to T's registered key), empty at construction per the dynamic-side contract. Both flip
   each new branch to **Optional** in `onDynamicPortAdded` (the registry creator adds Required pins, and
   `Port::setRequired` makes the flip stick however the pin arrived — menu / `addDynamicPort` /
   serialize-replay), so a gated-off branch never blocks readiness. Merge forwards the **first live**
   branch; Select routes by an int **`selector` input** — a *connectable* pin (wire a `ConstantNode<int>`
   for a fixed choice, or any int producer for data-driven routing), Optional so an unconnected selector
   defaults to branch 0. The selector is a **static** input declared in the ctor, sitting among the
   dynamic branches on the input side; `compute()` routes over the dynamic pins only (skips the static
   selector). **Static-vs-dynamic pin distinction** (`Port::isDynamic`, set by `addDynamicPort`):
   serialization replays **only** the dynamic pins, so the ctor-declared selector is rebuilt on load and
   never double-added — even when the selector's type (`int`) is itself a registered port type. A
   flow-serialize test proves exactly that (a Select with an int selector + int branches under a
   registered `"Int"` type reloads with the selector present, not duplicated). **flowview**: `gate` /
   `merge` / `select` (over `image::Image`) plus `constInt` / `constBool` (scalar sources to drive
   `enable` / `selector`; `bool` added to `sceneCodecs`) are palette entries, and the canvas grows a
   dynamic node's branches via a per-node **"+ <type>" ±** (deferred-applied after `EndNodeEditor`; the
   boundary Interface panel keeps its own ±). GUI-tested live: **select** driven by a constInt routes;
   **gate**/**merge** exercised (merge needs a gate + constBool to feed it). The canvas ± and the
   gate/merge wiring got their fuller eyeball — **live-verified 2026-07-26**; no gui-mode gap remains
   here. **Host surfacing (slice 3)
   BUILT** (2026-07-13): a suppressed boundary output (its producer gated off — an empty `PortValue`) is
   now a first-class "no output this run", not an error. The `run` subcommand reports it (`log::info`)
   and **writes nothing** — distinct from the "output isn't an image" type error (verified headless:
   the example scene run with its `source` unbound suppresses `result`, logs "no output this run", and
   no file is written; binding a real image writes it as before). The gui Interface panel shows a dim
   `(no output this run)` for an empty output pin (`refreshPreviews` already drops the stale thumbnail).
   **Canvas surfacing — already covered (checked 2026-07-23), not outstanding work.** This was listed as
   "remaining: dim the skipped nodes + edges", but the UI pass's **slice A** delivered exactly that from
   the other direction, so no further code is wanted: a suppressed node is precisely one that isn't
   `Node::ready()` (a Required input empty), which is what the canvas dims; and because
   `Scheduler::runNode` **clears an unready node's outputs**, the emptiness propagates — so its outgoing
   links mute too (a link is muted when its source port carries no value), and so does the next node
   down. One deliberate non-case: a `Gate` that ran with `enable=false` is *not* dimmed, because it did
   activate — it chose to suppress. What stopped is the flow, and that is exactly what its muted output
   link shows. **#2 is complete.**
3. ✅ **Incremental re-eval — BUILT** (2026-07-12). `Scheduler::run` is now dirty-driven: a
   shared `runOrder(graph)` returns the **dirty closure** (every dirty node + everything downstream
   of one) in topo order, and both `SerialScheduler` and `ParallelScheduler` recompute only that —
   a clean node keeps its cached (persistent) `PortValue`. A fresh graph runs fully (all nodes start
   dirty); **`Graph::markAllDirty()`** forces a full refresh. `Graph`'s structural mutations
   (`connect`/`disconnect`/`removeNode`/`removePort`) mark the affected downstream node dirty, and
   flowview marks a node dirty on a param edit (`GroupInputNode::setValue` already did on bind), so
   incremental is correct by construction. Tested with compute-counting nodes; full suite 279/279.

10. **"Map over selection" — a group-kind parameter on `edit::groupSelected`.** The ergonomic gap
    left by refusing the linked map (ADR-0014, 2026-09-09). Composing a map around a linked group is
    the right shape, but its one real cost is plumbing the map's boundary to the link's face by hand,
    and re-doing it when the template's interface changes. `groupSelected` already computes the
    cut-set and builds the boundary pins; it hardcodes the kind at exactly **one** site
    (`libs/flow/src/edit.cpp:512`), steps 3–5 are kind-agnostic, and the lifting falls out for free
    because `syncGroupPorts` calls the **virtual** `exposePort` that `MapNode` overrides.
    `replaceGroup` already takes a `unique_ptr<GroupNode>`.

    **The one thing it has to decide, and the reason it is a gesture rather than a derivation:**
    step 5 re-attaches the parent's edges to the new outer ports, and a **lifted** port will not
    type-check against a source producing `T` — so it cannot reconnect everything the way grouping
    does, and at least one input must be lifted or the map has no arity to size its children from.
    That choice is the split/broadcast bit, which is the instance's to make and cannot be derived —
    which is itself part of why the linked map is refused.

    A second gesture ("re-mirror my boundary against this node's face") covers the template-changed
    case. It should stay offerable rather than automatic, for the same reason: re-deriving silently
    is how the broadcast choice would be lost.

    *(Numbered 10 because Tier A/B/C share one sequence and prose refers to "Tier B #4" — inserting
    a 4 here would renumber those.)*

### Tier B — speculative / large

4. **Streaming pipeline** — process a stream of inputs through the graph with
   stage overlap. Taskflow's `tf::Pipeline` is the natural fit; build only if
   streaming becomes real.
5. **Node hot-reload** and a node-type plugin registry.
6. **imgui-node-editor + docking/multi-viewport** upgrade from imnodes.
7. **Type-owned factory keys.** `lain::core::Factory<Base>` keys on an explicit
   string today (stable across compilers / class renames — unlike
   `meta::typeName<T>()`, which is display-only). A future refinement: let a type own
   its stable key via a `static constexpr` member (e.g. `T::factoryKey()`), enforced
   by a trait, so `registerType<T>()` reads it and the key can't drift from the type.
   Caveat to resolve first: a type constructible under two different `Base` factories
   can't have one canonical key — so the key may need to be per-(Base,T), not per-T.

### Tier C — foundational (when a consumer needs it)

8. **`lain::core::DateTime`** — a wall-clock / calendar time type (Python
   `datetime`-style) for system-time management: capture now, arithmetic, and
   parse/format ISO-8601 (`%FT%T%z` for local, `%FT%T` for UTC). Split from
   `lain::core::Time` (which stays monotonic-only) because calendar/formatting is
   the `std::chrono` *time_point* side, not durations. Likely first needed when
   graphs serialize to json (Tier A item 1 — timestamps). *(**That trigger fired and was answered
   negatively:** Tier A #1 shipped and records "`core::DateTime` (Tier C #8) is **not** pulled in —
   `version` is a plain int, no timestamps". Still unbuilt, now with no predicted caller.)*
9. **Video `Timecode` / `Timestamp`** — a frame-rate-aware time type for camera /
   video data (SMPTE-style timecode, drop-frame, frame ↔ time conversions). Lands
   when a camera / video node needs it. *(**That trigger fired and was answered negatively:**
   Milestone 10 built the video and camera-facing nodes and needed none of it — a frame is addressed
   by **ordinal**, with `media::FrameRate` for the rate and `core::Time` for the presentation
   timestamp (`frameref.h`, `framesource.h`). SMPTE timecode and drop-frame land when something asks
   for them by name.)*

## Open questions

- ~~Final names (`flow` / `flowview`, namespace).~~ **Resolved** — settled and committed; see the
  Decisions section (engine `lain::flow` / `libs/flow`, viewer `flowview` / `apps/flowview`,
  internals `lain::flow::detail`).
- Port type set: **resolved** — `PortValue` is an open type-erased slot (no `PortKind`
  tag, no GPU arms), keeping `flow` payload-agnostic and dependency-free. Since 2026-07-29
  it stores the payload **shared + immutable** (`shared_ptr<const void>` + `type_index`), so
  the per-edge copy is a refcount bump. Could lean to a closed `std::variant` later for speed
  once the node set is known, but only if profiling a real graph shows the erasure cost matters.
- `lain::task` is scoped minimal: an executor + the small DAG-build surface
  `flow`'s scheduler needs (a flow that emplaces node-tasks and adds edges), and
  nothing more. Subflows / `tf::Pipeline` stay unexposed until a caller needs
  them.
- Resolved for now: the `archimedes` submodule tracks `develop` (no tagged
  release exists; its real code isn't on `master`), and Taskflow FetchContent
  pins `v3.7.0`. Revisit if either gains a release worth pinning.
- **thorax: resolved — deferred, static linking with service-shaped seams**
  ([ADR-0004](docs/adr/0004-static-linking-service-shaped-seams-over-thorax.md)). Not vendored;
  `core::Factory<Base>` is the seam a plugin backs later. Reopen only if `lain` starts
  distributing binaries that need runtime-optional heavy deps.
