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

## Milestone 5 — group nodes / subgraphs (grilled 2026-07-29)

A **group node** contains its own graph and exposes selected inner ports as its own, through the
*same* boundary mechanism the top-level graph uses — "the top-level Graph is the outermost group",
made real. Two kinds: an **inline group** (recipe stored in the parent document, editable in place)
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
6. ⚠️ **flowview — navigation + the palette entries** (built 2026-07-29; **gui-mode NOT eyeballed —
   no Metal in this sandbox**). New **`groupnav.{h,cpp}`**: a `GraphPath` (the group nodes descended
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

   **Residual, not fixed:** `ctx.saveFormat` is also keyed by `PinKey` and persists across navigation,
   so a same-numbered pin at another level can inherit a format choice. Cosmetic — the dropdown falls
   back when the stored format isn't in that port's list — but if a third cross-level `PinKey` consumer
   appears, the key should gain a level rather than relying on "only one level is cached at a time".

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

   **Only the last two changes are unconfirmed:** the watermark's new tone + size
   (`CanvasStyle::readOnlyMark`, ~1.8x), and making node LAYOUT read-only
   (`SetNodeDraggable` + its transient message).

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

- **`Group Selected`** — move a selection into a new inner graph, computing the **cut-set**: each
  distinct *outer output port* feeding the selection → **one** boundary input pin (dedupe by source
  `PortAddress`, so a fan-out doesn't spray duplicate pins); each distinct *inner output port* feeding
  outside → **one** boundary output pin; pin names derived from the mirrored port and uniquified via
  `hasPortNamed`; group placed at the selection centroid; moved nodes' editor metadata migrates into
  the inner body; **refuse if the selection contains a boundary node** (grouping the graph's own
  interface would leave the document with no interface).
- **`Ungroup`** (splice inner nodes into the parent with fresh ids, resolving each boundary pin back to
  direct edges), **`Save as Template`** (inline → linked), **`Make Local`** (linked → inline).
- **Prefab overrides** — per-instance divergence from a template. Needs a template-stable inner-node
  address + conflict rules; ADR-0010 explains why parameterising via boundary pins is preferred.
- **Plan caching** across runs; **file-watch** on templates (manual *Reload linked groups* first);
  a **registered node-serializer seam** if third-party structural node kinds ever appear.

## Milestone 6 — definition & evaluation (grilled 2026-07-31 → 08-01)

**Designed, not started.** `flow` currently keeps a graph's *recipe* and its *run state* in the same
objects: `Port` owns a `PortValue`, `Node` owns `m_dirty`. Three pressures converged on that —
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

5. **Host-side keys and paths.** `PinKey` → `{EvalPath, NodeId, PortId}`; preview cache, Inspector,
   Interface, Preview pane, `saveFormat` follow. The clear-on-navigation scoping becomes a memory
   choice rather than a correctness one.

**Hold the leftover group GUI work until at least step 5** — it lives in the panes this changes, and
doing it twice is how the last four bugs happened.

### Not in this milestone

- **SplitGroup and Loop themselves.** M6 makes them expressible; building them is separate, and their
  open questions (map index stability, loop carry, suppression across a map, how ADR-0009's plan lowers
  a map) need a concrete feature in front of them.
- **Shared definitions for linked groups.** M6 makes and scheduler-tests it as *safe* — ADR-0010's
  sharing constraint falls — but LinkedGroupNode keeps its copied inner Graph in this milestone.
  Shared template ownership/cache, reload propagation and file-watch policy form one later vertical;
  do not imply template edits propagate live until that lands.
- **In-run liveness release.** M6 bounds retention *after* a run, not the peak *during* one. Releasing
  a value once every consumer has read it is separable, and cheap to add later precisely because
  `PortValue` payloads are already shared and immutable.

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
