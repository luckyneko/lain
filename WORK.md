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

## Milestone 1 — engine + app stack + viewer

**Status:** the engine core (steps 1–4 below, plus the engine test suite + example
GPU node) is built, tested, and committed. The remaining M1 work — decouple `flow`
from archimedes, then `lain::math` / `lain::app` / `lain::gui` / `flowview` — is in
"Remaining work" after the engine steps. Steps 1–4 record what was built.

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

## Remaining work — decouple + app stack + viewer

Build order. Each lib builds standalone + as a subdirectory, strict-clean.

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

## Milestone 2 — interactive graph editing (done)

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

## Milestone 3 — file loading

**Status:** the **read vertical is done and cli-verified** — `lain::memory` (Buffer) → `lain::io`
(`read → Buffer`) → `lain::io::image` (reader registry + `load` facade) → the **png / tiff / jpeg**
codec plugins (build-discovered aggregator) → `flow-example::LoadImageNode`. `flowview --headless
--image <file>` loads a real PNG/JPEG/TIFF through the node and dumps its extent + format + corner
pixels. The **write library is also done** — `io::write` + the `ImageWriter` seam (`save` facade +
`canEncode`) + png / tiff / jpeg encoders (see the write pass below) — and so is its **interim save
consumer**: flowview saves any image output through a native file dialog (Mac-verified). The M3
write pass is **complete**; the `ImageWriteNode` is **superseded** — see the M4 reframe below. The
gui-mode thumbnail follow-on is the only M3 loose end.

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
  expanded *losslessly in pixel value* — as the reader's documented contract, not a silent surprise
  — and `ColorSpace` is read from the file, `Unspecified` when untagged (never guessed). **lain
  format gaps still filled by expansion** (candidates to add as native formats later): indexed /
  palette color; sub-byte channel depths (1/2/4-bit); colourkey transparency (tRNS, vs. a full
  alpha channel); and colour spaces beyond `Unspecified`/`Linear`/`sRGB` (arbitrary gamma, ICC
  profiles, non-sRGB primaries — currently collapsed to `Unspecified`). A codec may instead
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

**Deferred behind these seams:** `remote`/`s3` IO schemes; a **`Stream` transport** — an
incremental/seekable read (chunked, possibly mmap-/socket-backed) peer to `read` for video and
large/network assets, since `read → Buffer` is a whole-asset slurp that video can't use;
`lain::io::video` / `audio` + `Timecode` (Tier C item 9); the memory pool/arena +
`BufferView`/`SharedBuffer`; magic-byte format sniffing (extension-keyed for now); thorax
adoption. **TIFF reader breadth ("one day", not now):** float sample format → `F32` Images
(intermediary/HDR files — lain already has the `*32F` formats), `CIELab`/`YCbCr` photometrics
(need a colour conversion), and tiled / planar / multi-page (currently rejected / first-page).

## Milestone 4 — pipeline I/O: graph boundary + host binding (rough plan)

**Not yet designed — a rough sketch, captured so M3 closes cleanly. Grill + ADR before building.**

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
   encoding/boundary handles move `PortIndex` → `PortId`; `add*`/`addBoundary` return `PortId`. **No
   behaviour change** — a standalone commit kept fully green on the pure rename.
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
planned: Merge / fan-in port, reorder, cli named-binding.

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
- **Deferred:** Merge / fan-in port; port **reorder**; cli **named-binding** (`--input cam=foo.png`,
  a post-serialization concern).

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
- **Preview zoom/pan**, and pane **pop-out** — both deferred to a future `lain::app` **multi-window**
  rich viewer (a 3D voxel view, a zoom/pan image view), explicitly *not* ImGui multi-viewport.
Needs its own grill before building, as the layout pass got.

**`lain::gui::nodes` wrapper pass** — front the raw `Im*` surface the `namespace nodes = ImNodes` alias
leaks (`ImNodesCol_*`, `ImNodesPinShape_*`, `PushColorStyle(ImU32)`, attribute flags) with lain-typed
calls, so a client passes lain colours/enums and never touches `ImU32` — which retires `gui::packColor`
from flowview's call sites. **Not to be confused with the colour discipline, which is done:**
`gui::packColor(image::ColorRGBA8) → ImU32` exists, the palette speaks `image::ColorRGBA8` end-to-end,
and the pack happens only at the imnodes boundary. What remains is the *typed surface* — `nodes.h` is
still a bare `namespace nodes = ImNodes;`, and `panes/graphpane.cpp` still names `ImNodesCol_*` (lines
~219–232), `ImNodesPinShape*` (~261, ~278), and `ImNodesAttributeFlags_*` (~254) directly.

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
   versioning, C++26-reflection auto-`serialize`, and the **video** boundary loader (scalars + image
   bind from the cli now — see "Scalar boundary binding" above; a frame/timecode-shaped boundary is the
   remaining case). `core::DateTime` (Tier C #8) is **not** pulled in — `version` is a plain int, no
   timestamps.
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
   graphs serialize to json (Tier A item 1 — timestamps).
9. **Video `Timecode` / `Timestamp`** — a frame-rate-aware time type for camera /
   video data (SMPTE-style timecode, drop-frame, frame ↔ time conversions). Lands
   when a camera / video node needs it.

## Open questions

- Final names (`flow` / `flowview`, namespace).
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
