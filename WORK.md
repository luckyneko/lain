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

**Boundary representation (leaning A, not locked):** boundary ports are **designated Input/Output
nodes** inside the graph (Blender group style) — a `GroupInput` node whose *output* pins are the
graph's inputs, a `GroupOutput` node whose *input* pins are its outputs; the host enumerates them to
bind. Reuses the existing node/port machinery and unifies with group nodes for free (vs. a second
port system living on the `Graph` object).

**Key engine gap this exposes — dynamic ports.** Ports are declared in a `Node`'s ctor and fixed for
life today. N synced inputs means an Input node must **add/remove ports at runtime**, which needs:
(1) runtime port mutation (`addPort`/`removePort` post-construction), (2) edge cleanup on removal
(as `removeNode` already does for incident edges), (3) gui affordances ("+"/"–" on the node). This
is the first real M4 build item.

**Also implicates** (each its own effort): the deferred **`Stream` transport** (video / live
frames, incremental vs. `read`'s whole-asset slurp); **multi-stream sync** (temporal alignment of N
streams); and a **live / real-time execution** model (Taskflow `tf::Pipeline`, Tier B item 4). The
**side-effecting-sink** concept a true write-in-graph node would need is deferred with it — the host
binding sidesteps it for now.

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
  pins `v3.7.0`. Revisit if either gains a release worth pinning.
- **thorax: resolved — deferred, static linking with service-shaped seams**
  ([ADR-0004](docs/adr/0004-static-linking-service-shaped-seams-over-thorax.md)). Not vendored;
  `core::Factory<Base>` is the seam a plugin backs later. Reopen only if `lain` starts
  distributing binaries that need runtime-optional heavy deps.
