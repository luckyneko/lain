# CLAUDE.md

Guidance for Claude Code working in this repository. [AGENTS.md](AGENTS.md) holds
the general working discipline and applies in full; this file is more specific
and overrides it where they differ.

## Handoff status (2026-06-30)

`lain` is a collection of small C++17 prototyping libraries. The current focus is
a **node-graph engine** (`flow`) and the **reusable app stack** that lets it (and
future apps) run on a live Vulkan driver. The authoritative build plan is
**[WORK.md](WORK.md)** — it owns *what* to build and in *what order*. This file
owns *how* to build it so the result looks native to `lain`.

### Update 2026-07-05 — archimedes bumped to `develop` tip; gui parked

`extern/archimedes` was moved from the old `feature/old_attempt` commit to `develop`
tip (`bfa118d`): the **handle-style refactor** + **Vulkan 1.3** baseline. What this
changed for lain (repair-only — behaviour unchanged where gui isn't involved):

- **`acm::Instance` / `acm::Device` are now move-only owning roots** (copy deleted),
  where they used to be copyable handles. `Application::device()` / `instance()` now
  return **references**; the example nodes hold the shared device by reference (passed
  through the node factory via `std::ref`).
- **Device/surface selection is acm's now.** The old `getAvailableGPUs()` /
  `GPUSurfaceSupport` enumeration is gone; `lain::app` uses
  `Instance::graphicsOptions()` (headless) / `surfaceOptions(surface)` (windowed) +
  `createDevice(option)` + `createSwapChain(surface, option, SwapChainConfig{...})`.
- **Getter renames** throughout (`vkInstance`→`vulkanInstance`, `createSurface`→
  `createVulkanSurface`, `Texture::getExtent`→`extent`, `SwapChain::getExtents`→
  `extent`, …). `acm::Version` dropped to 3 fields (`patch` is `uint16`).
- **`lain::gui` + flowview gui-mode: restored via the acm interop seam.** archimedes
  sealed the raw Vulkan handles ImGui's backend needs, so they were briefly parked
  behind a `LAIN_ENABLE_GUI` gate; that gate is now removed. archimedes exposes the
  handles through a single opt-in `acm::interop` header (`acmVulkanInterop.h`, mainline
  acm stays Vulkan-free), `lain::gui`'s `context.cpp` drives ImGui via `acm::interop` +
  **dynamic rendering** (no `VkRenderPass`; the swapchain color format feeds
  `PipelineRenderingCreateInfo`), and gui-mode is verified opening / rendering / exiting
  cleanly on the live driver. flowview builds its gui-mode window unconditionally again.
  See **[docs/adr/0001](docs/adr/0001-gui-parked-pending-acm-interop-seam.md)** (resolved)
  for the history.

Verified this session: warning-clean strict build of every lib+app; `ctest` 85/85
(the `[gpu]`/window tests SKIP — the sandbox has no Metal access); `flowview
--headless` dumps the `gradient → tint` graph. The live-driver GPU round-trip and (once
the seam lands) gui-mode remain to be eyeballed on a Metal-capable session.
*(**Superseded** — that eyeball happened; see the 2026-07-23 update below.)*

### Update 2026-07-11 — archimedes bumped to `develop` tip (`61a890f`); gui adopts `interop::withQueue`

`extern/archimedes` moved to `develop` tip (`61a890f`): device feature negotiation,
pipeline/surface config presets, and an interop polish pass. Only one breaking change
touched lain; the rest are additive and noted here as knobs available when we want them.

- **`interop::createSurface` → `adoptSurface`** (breaking — the only compile break).
  Same ownership contract: the `VkSurfaceKHR` moves to acm, lain keeps the native GLFW
  window alive. `libs/app/src/window.cpp` adopts the GLFW surface through the new name.
- **`interop::withQueue(device, work)` (new — adopted).** archimedes now asks that any
  *external* backend which submits to / waits on the device queue run under its queue
  mutex via `withQueue`, rather than reaching for `deviceMutex()`. `lain::gui`'s
  `Context::newFrame` now wraps `ImGui_ImplVulkan_NewFrame` (ImGui lazily uploads
  font/texture atlases there, which submits + waits) in `withQueue`, future-proofing the
  viewer for the day a `ParallelScheduler` worker funnels a GPU submit while the main
  thread is mid-`newFrame`. `withQueue` errors only on an invalid device / null callback
  (impossible at that site), so the returned `acm::Error` is discarded with a comment.
- **`SurfacePreferences` presets + empty-means-unranked (behaviour note, no lain change).**
  A default-constructed `SurfacePreferences{}` now means "no ranking preference"; the old
  populated sRGB/FIFO defaults live behind `SurfacePreferences::Default()` (plus
  `::HardwareSrgb()` / `::LowLatency()`). lain is unaffected because `application.cpp`
  calls `surfaceOptions(surface)` with no preferences, and that overload applies
  `Default()` internally — same UNORM+FIFO ranking as before. If we ever hand-build a
  `SurfacePreferences{}` expecting the old defaults, call a preset explicitly. (Available
  knob: `HardwareSrgb` is the clean switch if flowview goes linear-lit.)
- **`DeviceConfig` feature negotiation (new — additive, not used).** `createDevice(option,
  DeviceConfig{})` gains required-vs-optional `DeviceFeatures` (`fillModeNonSolid`,
  `wideLines`, `samplerAnisotropy`, `sampleRateShading`); `DeviceInfo::features` reports
  availability, `Device::enabledFeatures()` reports what got turned on. `lain::app` uses
  the default (enable-everything-supported), so behaviour is unchanged — a future knob if
  the app needs to hard-require or query a feature.
- **`PipelineConfig` presets (new — not used).** `Default/Mesh3D/Sprite2D/Wireframe`. lain
  builds no acm pipelines directly (gui rides ImGui's backend), so no impact; handy for a
  future `lain::graphics` layer.

Verified this session: warning-clean strict build of every lib+app; `ctest` 228/228 (the
window/`[gpu]` tests SKIP — no Metal in the sandbox); `flowview --headless` round-trips
the `GroupInput → Tint → Blur → GroupOutput` graph. gui-mode's `withQueue`-wrapped
`newFrame` runs only on a live driver and was **not** exercised here — it needs an eyeball
on a Metal-capable session, same standing gap as the rest of gui-mode.
*(**Superseded** — that eyeball happened; see the 2026-07-23 update below.)*

### Update 2026-07-12 — graph serialization built (data + io::data + flow::serialize + flowview)

**Tier A #1 (graph serialization) is built and live-verified** — the whole spine, grilled
2026-07-11 (see the `data` + `flow::serialize` sections of [CONTEXT.md](CONTEXT.md) and
[ADR-0006](docs/adr/0006-format-neutral-value-dom-serialization.md)): `C++ type ⇄ data::Value ⇄
JSON on disk ⇄ data::Value ⇄ Graph`.

- **`libs/data`** (`lain::data`) — the format-neutral **`Value`** DOM + the `toValue`/`fromValue`
  reflection over a single `serialize(Archive&, T&)` visitor. Distinct number arms, ordered Object,
  a Bytes arm; enum-as-name (`meta::enums`), `std::filesystem::path`, `std::map<string,V>`, tagged
  `std::variant`, and the `LAIN_SERIALIZE` / `_INTRUSIVE` / `_VARIANT_ARM` macros. "The type is the
  schema" — no validation layer. Split by owner: `value.h` / `archive.h` (customization surface) /
  `data.h` (facade) + `details/reflect.h` engine. The structural std-shape traits (`is_optional` /
  `is_vector` / `is_variant`) live in `lain::meta::traits`.
- **`libs/io/data`** (`lain::io::data`) + **`plugins/io/data/json`** — the parser-free codec seam
  (Reader/Writer over `Value`, `core::Factory` registries, `load`/`save`), mirroring `io::image`;
  the json codec owns `nlohmann::ordered_json` privately (order-preserving → diff-clean/idempotent),
  Base64s a Bytes node. **Known JSON property** (in ADR-0006): a positive `Int` normalises to `UInt`
  on read (JSON has no signedness) — harmless, typed reads cross-accept, text is stable.
- **`libs/flow/serialize`** (`lain::flow::serialize`) — a **separate target** over `flow`'s public
  API + `data` (the payload-agnostic boundary rule keeps it out of core). `toValue(graph, factory,
  codecs, editor)` / `fromValue → LoadResult`; the app-populated **`ValueCodecs`** for params,
  **name-addressed edges**, **canonical node-id remap** (fresh ids → doubles as paste),
  **dynamic-pin replay** via the port-type registry, the opaque adapter-owned **`editor`** section
  (re-keyed onto the loaded ids in `LoadResult.editor`), a document `version` (too-new = fatal), and
  best-effort **`LoadResult { graph, issues }`** (each issue logged *and* returned).
- **flow-core additions** for the walk: `Node::hasPortNamed` + port-name uniqueness (static asserts
  via plain `<cassert>` — flow stays log-free, a deviation from the planned `log::ensure`; dynamic
  adds reject), `portTypeKey(type_index)` reverse lookup, `core::Factory::keyOf` (node→kind on save),
  `Graph::nodeIds()` (stable enumeration), and the empty-dynamic-side contract documented on
  `DynamicPortsNode`.
- **flowview** — `graphio.{h,cpp}` (`sceneCodecs`, a `serialize(Archive&, image::ColorRGBf&)` bridge
  in `lain::image`, `saveGraph`/`loadGraph`); gui **Save…/Load…** on the canvas with node positions
  round-tripped through the `editor` section; and the headless **`run` / `list` subcommands**
  (`runmode.{h,cpp}`) — `list [--graph f]` prints a graph's boundary interface (`--<name> : <type>`),
  `run [--graph f] [--save f] [--<boundary> <value> …]` loads/builds, binds boundary inputs by name
  (image → load the path; other types not cli-bindable yet), runs, dumps, writes bound outputs.
  `--graph` is an option so `allow_extras` carries the `--<boundary>` bindings unambiguously; port
  names are identifiers (`validPortName`) so they double as flags.

Verified: warning-clean strict build; `ctest` **275/275**; the headless save ⇒ load ⇒ save round-trip
is **byte-idempotent** on the real scene; the `run`/`list` subcommands were **exercised live**
(bind an input from a file → it flows source→tint→blur→result in the dump); and gui-mode Save/Load was
**eyeballed on the live Metal driver** (positions restored, result renders) — the one part that needed
a Metal session. **Tier A #1 is complete.** Only designed-for follow-ons remain (untagged variant,
yaml/binary codecs, the `memory::Buffer` bridge, the scalar/video boundary loader).

### Update 2026-07-23 — gui backlog live-verified; ImGui pinned; session state + editable node names

The accumulated "built but never driven on Metal" gui work (the whole flowview UI pass, the
window/panel layout pass, and the pane-split refactor) was **eyeballed on the live driver by the repo
owner** — WORK.md's per-slice markers now read *live-verified 2026-07-23* instead of *eyeball pending*.
On top of that, four small items landed (see WORK.md for the detail):

- **`IMGUI_REF` is pinned to a commit** (`162ce49`, the 1.92.9 WIP everything was developed against).
  `cmake/addImGui.cmake` had claimed a commit pin while actually tracking the moving `docking` branch
  tip. Note it is a CMake **cache** variable: an existing build dir keeps its old value until
  reconfigured with `-DIMGUI_REF=…`.
- **Session state** (`apps/flowview/src/session.{h,cpp}`) — `~/.flowview/session.json` (beside the dock
  layout's `imgui.ini`), holding the last graph (reopened at launch, `--example` suppresses), an
  **Open Recent** list, and the **file-dialog folder**, which now persists across runs via new
  `lain::gui::lastDirectory()` / `setLastDirectory()` (the dialog seam owns the folder; *where settings
  live* stays the app's business). It round-trips through `data`/`io::data` — flowview's own settings
  dogfood the graph-serialization spine. Convenience state: a missing/malformed file never errors.
- **Editable node names** — `flow::Node::setName` (display only, like `Port::setName`; `NodeId` remains
  identity), the serializer now *reads* the stored `name` back, and the Inspector grows a **Name**
  field. The `[id]` prefix drops from the canvas title (the Inspector header keeps it); new palette
  nodes get uniquified names ("tint", "tint 2", …) so the canvas stays readable without it.
- **Document-guard + rename correctness.** **Open... / Open Recent are now guarded** like New (both
  replace the graph destructively; only New asked), through one `PendingSwap` request path with a
  single `performSwap`; a **cancelled Save panel now cancels the swap** instead of discarding the graph;
  both rename paths **mark the document dirty**; and a **boundary-pin rename is checked for uniqueness**
  (`hasPortNamed`, as `node.h` already documented) — edges are serialized *by port name*, so a duplicate
  made an edge ambiguous on load.
- **Doc correction:** Tier A #2's "remaining: dim the skipped nodes/edges" was **stale** — slice A's
  `ready()`-driven dim plus the scheduler clearing an unready node's outputs already produces exactly
  that, including muted dead links. #2 is complete.

### Update 2026-07-25 — undo/redo

(The prior text-surface cleanup — flowview display strings → `lain::string::format`, the `char[]`
`InputText` sites → ImGui's `imgui_stdlib` `std::string` overload, no `snprintf` left — landed with the
2026-07-23 flowview commit; see WORK.md's "Text-surface cleanup" section.)

**Undo / redo** (`apps/flowview/src/undo.{h,cpp}`, `UndoStack`) — snapshot-based, reusing the serialize
spine: a snapshot is `graphio::snapshotGraph` (= the Save document, in RAM), a restore is `restoreGraph`
fed through the existing deferred-load swap. Edits route through `AppContext::markChanged()`;
`MainWindow` snapshots at end of frame only when `!gui::IsAnyItemActive()`, so a param/colour **drag
coalesces to one step**. `push()` skips a snapshot equal to the current one, so a **bound-value-only
edit records nothing** (bind values aren't serialized). A `pendingBaseline` distinguishes a New/Open
swap (**resets** history) from an Undo/Redo swap (keeps it). **Edit menu + Ctrl+Z / Ctrl+Shift+Z.**
Defaults: node *moves* not undoable, Open resets the stack. First app-level unit tests
(`apps/flowview/test`, 8 cases over the real `undo.cpp`); `ctest` **297/297**. The interactive
restore/coalesce/redo-truncate behaviour was **live-verified 2026-07-26**, along with the follow-up fix
that **keeps the canvas selection across an undo/redo** (selected nodes captured as ordinals into
`nodeIds()` — stable across the load's fresh-id remap — and re-selected after the swap; New/Open still
clear, since they carry a `pendingBaseline`).

### Update 2026-07-29 — `PortValue` payloads are shared + immutable

`Scheduler::populateInputs` copies a `PortValue` **per edge, per run**, and an `image::Image` copy is a
**deep pixel copy** — so every edge of every graph was paying a full payload copy on every run. The slot
now stores `std::shared_ptr<const void>` + `std::type_index` (one indirection, and the shared_ptr keeps
`T`'s deleter while the type_index restores the identity `shared_ptr<void>` loses). `set<T>` moves the
value into a shared const allocation and **rebinds** the slot, so an earlier copy keeps the old payload
and a recompute never disturbs a value another slot is still reading; `get<T>()` still returns
`const T&` and still throws `std::bad_any_cast` on a mismatch (the exception type deliberately retained
from the `std::any` era). Safe because nothing mutates in place — a node that wants to modify a value
copies it out (`TintNode`'s `image::Image src = input(m_in).get<image::Image>()`), which is now the only
deep copy paid, by the node that asked for it. **No call site changed.** `ctest` **299/299** (+2 guards:
one proving copies share a payload *address*, one proving a fan-out graph run twice performs zero payload
copies); headless `run` still flows source→tint→blur→result with each stage distinct, and save→load→save
is still byte-idempotent.

**Engine core is built, tested, committed. The remaining M1 work is one decoupling
refactor of `flow` plus the app stack + viewer:**

- ✅ Build skeleton (umbrella CMake, `lain::task` wrapping Taskflow), `PortValue`,
  `Port`/`Node`/`Graph` (type-checked + cycle-rejecting), and the `Scheduler`
  (push run via Taskflow, pull evaluate — reshaped since; see the execution bullet below).
  All in `libs/flow`.
- ✅ GPU-port path proven: `lain::flow-example`'s `GradientNode` emits an
  `acm::Texture` through a port, verified by a headless `[gpu]` test on a real
  driver (archimedes' `acm_require_vulkan_runtime()` builds the loader from source).
- ✅ **`flow` decouple** — `PortValue` is now a thin `std::any` slot; `flow` core
  names no GPU types and links only `lain::task` + `lain::meta` (archimedes moved to
  `flow-example`). A `Port` also captures `lain::meta::typeName<T>()` at declaration
  (`Port::typeName()` — a `string_view` into static storage), so the inspector can label
  pins by type. Verified: warning-clean build + all flow tests pass, incl. the
  live-driver texture round-trip.
- ✅ **`libs/math`** (`lain::math`) — typed GLM wrapper (generic `Vec<N,T>`/`Mat`/
  `Quat`, per-dim `Vec2/3/4<T>`, named concretes; GLM free fns re-exposed). GLM
  1.0.3 via `cmake/addGLM.cmake` (SYSTEM). Builds warning-clean; 3 tests pass.
- ✅ **`libs/core`** (`lain::core`) — foundational std-only types. `Time` (monotonic,
  int64-ns storage, seconds-facing, chrono-interop, `as<>`/`from<>`/`since()`) +
  `TimeState` lives in `lain::app`. `Version` (semver-style `major.minor.patch` +
  optional pre-release/build tags, `toString()`/`parse()`, comparison on the numeric
  triple — tags ignored) for app/library identity (formattable by `lain::string` via
  its `toString()` — `core` itself stays format-unaware). Wall-clock `DateTime` + video
  `Timecode` deferred (WORK.md Tier C). **`Factory<Base>`** (`factory.h`, header-only)
  is a generic string-keyed registry — `registerType`/`create`/`keys`, plus a typed
  `registerType<T>(key, args...)` that synthesises the creator with construction context
  captured in the closure; keys stay explicit strings (not `typeName<T>()`, which is
  display-only). 16 tests pass.
- ✅ **`libs/string`** (`lain::string`) — string utilities behind a lain:: face,
  header-only. `format(fmtStr, args...)` wraps `fmt::format` (C++17 has no
  `std::format`) with compile-time-checked format strings. A generic `fmt::formatter`
  for any type exposing `toString()` (detected via `lain::meta::has_to_string`, which
  requires a string-convertible return) renders through it, inheriting
  `fmt::formatter<std::string>` so width/fill/align specs apply — so a type is formattable
  just by having `toString()`, no per-type registration, and the fmt dependency stays out
  of `core`. (Caveat: a type that is both a range/tuple and has `toString()` would be
  ambiguous with fmt's range formatter; none of lain's are.) Depends on `fmt::fmt` +
  `lain::meta`. 3 tests pass (format, the Version-via-`toString` path, specs).
- ✅ **`libs/meta`** (`lain::meta`) — compile-time introspection behind a lain:: face,
  header-only. Enum reflection over magic_enum 0.9.8 (`cmake/addmagicenum.cmake`, SYSTEM)
  in the `lain::meta::enums` sub-namespace (short names kept clear of the type-level
  introspection to come): `name` / `fromString` (case-sensitive or -insensitive) /
  `count` / `values` / `names` / `entries`, plus `nameValueMap` (a name→value
  `std::map`); magic_enum is named nowhere past `lain::meta::enums`, and every function
  is enum-only (`static_assert`). The map is the idiomatic building block for a CLI
  option (`cli.add_option(...)->transform(cli::CheckedTransformer(enums::nameValueMap<E>(),
  cli::ignore_case))` — no special wrapper) and backs `lain::gui::enumCombo`. magic_enum
  sees only enumerators in [-128, 128] by default — fine for lain's small enums, but a
  large-valued flag enum needs the range customized. **typeName** (`typenames.h`) sits
  directly in `lain::meta` (type-level, not enum): `typeName<T>()` ("lain::core::Version")
  + `typeNameShort<T>()` ("Version", trimmed to the last `::`). It's an **owned parse** of
  the compiler's signature intrinsic (`__FUNCSIG__` / `__PRETTY_FUNCTION__`) — no external
  dep — using a void-probe to measure the type's window in the signature, plus a leading
  `class`/`struct`/`enum` keyword strip (MSVC spells class types "class X"). A clean
  `string_view` into static storage on every compiler, no RTTI/demangling. (We dropped
  nameof: we used one of its functions, the technique is ~15 lines, and it lagged the
  newest MSVC — see the void-probe in `typenames.h`.) Names are human/debug-facing (port
  labels, logs), not stable serialization keys — `flow`'s `Port` captures `typeName<T>()`
  at declaration so the inspector and the cli dump read `Port::typeName()` directly (no
  runtime type_index → name lookup). **Type traits** (`traits.h`, pure std — no magic_enum/nameof)
  in std::type_traits style (`_v` variants): `has_to_string` (string-returning
  `toString()`; backs `lain::string`'s formatter) and `has_ostream` (stream insertion
  operator). `has_ostream` is a detection primitive only — built-ins are stream-able, so
  gating a *global* fmt formatter on it would be ambiguous with fmt's own formatters; use
  it surgically. 9 tests pass.
- ✅ **`libs/log`** (`lain::log`) — thin wrapper over spdlog. Own `Level` enum +
  `setLevel`/`level`/`log(Level, string_view)` seam + typed front-ends
  (`trace`/`debug`/`info`/`warn`/`error`/`critical`) that format with fmt and funnel
  through that seam; spdlog is named only in `log.cpp` (linked PRIVATE), so nothing of
  the backend leaks past `lain::log`. fmt is PUBLIC (its `format_string` appears in the
  header — C++17 has no `std::format`); spdlog is built against that shared fmt
  (`SPDLOG_FMT_EXTERNAL`) so the program links one fmt (`cmake/addfmt.cmake` 10.2.1 +
  `cmake/addspdlog.cmake` 1.14.1; archimedes does **not** provide spdlog). Default
  logger is a mutex-guarded **stderr** colour sink (`[time] [level] message`, no
  logger-name field), so diagnostics never pollute a program's stdout. 3 tests pass.
- ✅ **`libs/app`** (`lain::app`) — GLFW 3.4 + CLI11 app framework (delegate-based,
  not a testbed): `Application` owns the instance + lazy shared device + the windows
  it creates + the single-threaded run loop; `ApplicationDelegate`
  (`onInit(cli)`/`onStart`/`onUpdate`/`onProcess`/`onStop`/`onShutdown`, where
  `onProcess` runs once in headless and on demand via `Application::process()` in gui)
  + `WindowDelegate` (`onInit`/`onRender`/`onResize`/`onShutdown`); `InputState`
  (decoupled `Key`/`MouseButton` enums) + `TimeState` snapshots; CLI11 re-exposed as
  `lain::app::cli`. Constructed with an `AppInfo{name, version}` (`appinfo.h`): the
  name drives the CLI program name + the Vulkan instance app name, `--version` is wired
  via CLI11's `set_version_flag`, and a normal run logs `"<name> <version>"` (through
  `lain::log`) once past the parse; the `core::Version` is clamped to `acm::Version`'s
  uint8 fields for the instance. `-v`/`--verbose` is a reserved framework flag that
  raises the log level (`-v` debug, `-vv` trace) before anything logs (the raw count is
  exposed via `Application::verbosity()`); `--version` is reserved too. A delegate that
  re-registers a reserved flag is caught (CLI11 throws on construction) and reported via
  `lain::log::error` + a clean `run()` exit 1, not an uncaught terminate. `lain::app`'s
  own diagnostics go through `lain::log` (no `fprintf`). (An enum-valued CLI option is
  the plain CLI11 `add_option(...)->transform(...)` fed by `lain::meta::enums::nameValueMap`
  — no app-side wrapper.) Headless = an app that opens no windows. Gated by `LAIN_BUILD_APPS`;
  both modes verified on the live driver (headless + cli in ctest, opt-in `[gpu]` window
  smoke `LAIN_GUI_SMOKE=1`).
- ✅ **`libs/gui`** (`lain::gui`) — Dear ImGui 1.92.8 wrapper. `gui.h` re-exposes
  `ImGui::` as `lain::gui::`; `imconfig_lain.h` bridges `ImVec2/4` ↔ `lain::math
  Vec2f/4f` (`IM_VEC*_CLASS_EXTRA` via `IMGUI_USER_CONFIG`). `Context` is the
  per-window seam: `imgui_impl_glfw` on `Window::nativeHandle()` + `imgui_impl_vulkan`
  on the shared device/swapchain (auto descriptor pool), with `newFrame()` /
  `render(cmd)` / `image()` (acm::Texture → `lain::gui::Image`). Built warning-clean;
  re-export + bridge tested, and **runtime/visual-verified via flowview** (`Context`
  drives the inspector window end-to-end on the live driver). `gui.h` also pulls in ImGui's
  official **`imgui_stdlib`** (`misc/cpp`, compiled into the `imgui` target — ships in the
  tarball, no extra dep); its overloads land in `namespace ImGui`, so the using-directive
  surfaces `lain::gui::InputText(label, std::string*)` — a std::string-native text field with
  no caller-managed `char[]` edit buffer (which also capped/truncated long input). **imnodes** (node canvas)
  is now in too: `nodes.h` aliases it as `lain::gui::nodes`, and `Context` owns the
  per-window `ImNodesContext` alongside the ImGui one. Pinned to a master commit
  (`addImnodes.cmake`, built against our `imgui` target) since no imnodes release tracks
  ImGui 1.92 — that commit branches on `IMGUI_VERSION_NUM >= 19200`. (`Application::instance()`
  was added to `lain::app` for ImGui's `VkInstance`.) `enums.h` adds `enumCombo` — an
  ImGui combo over an enum's values labelled from `lain::meta::enums` (headless-smoke
  tested, and dogfooded live in flowview's inspector — see below).
- ✅ **`apps/flowview`** — the inspector. Both modes built + verified on the live
  driver. Shared scene: `buildExampleScene` adds `flow-example`'s `GradientNode`; the
  graph is pulled (`Graph::evaluate`). **cli-mode** (`--headless`/`-c`): `dumpGraph`
  writes each node/port in topo order to stdout — CPU values as text, an `acm::Texture`
  port read back through the shared device to extent + corner pixels. **gui-mode**
  (default): flowview's `MainWindow` (a `WindowDelegate`; originally `InspectorWindow`, since
  split into one pane per file under `apps/flowview/src/panes/` over a shared `AppContext` — see
  [ADR-0008](docs/adr/0008-flowview-pane-architecture.md)) owns a `lain::gui::
  Context` + an `acm::Sampler`, reads the evaluated graph each frame into an ImGui
  "Inspector" panel (port text; each `acm::Texture` port previewed via a
  `Context::image` cache — see the interactive-editing bullet below). `--frames N` quits after N frames
  (0 = until closed) for a windowed smoke. Verified: the 64×64 gradient dumps
  `TL=rgba(0,0,128,255) BR=rgba(255,255,128,255)` (cli), and the same gradient renders
  as a live thumbnail in the gui inspector (screenshot-confirmed). gui-mode also draws a
  **"Graph" node canvas** (`lain::gui::nodes`/imnodes; read-only as first built, now
  editable — see the interactive-editing bullet below): one imnodes node per
  `flow` node with its ports as pins and edges as links, laid out by topo column on the
  first frame; node ids are `NodeId`, pins are `pinId(node,dir,port)`, links the edge
  index. Screenshot-confirmed rendering the gradient node + its `texture` pin. It also
  **dogfoods `lain::meta`**: each port is labelled with `Port::typeName()` (captured from
  `lain::meta::typeName<T>()` at port declaration) on the inspector text and the canvas
  pins, and a `lain::gui::enumCombo` drives a `PreviewSize` enum that resizes the texture
  thumbnail (so `enumCombo` gets its live exercise, not just the headless smoke).
  ImGui sizes/positions here are passed as `lain::math::Vec2f` (the `imconfig_lain.h`
  bridge converts to `ImVec2`), not raw `ImVec2`. Links `lain::app` + `lain::gui` +
  `lain::meta` + `lain::flow-example` + `archimedes` + `Vulkan::Loader` (loader resolves
  acm's `vk*`). This also gives `lain::gui` its runtime/visual verification — `Context`
  is exercised end-to-end here.
- ✅ **`flow` identity + execution reshape** (2026-07-02) — two structural changes to
  the engine core. (1) **`NodeId` is an opaque handle** (`types.h`): a strong type over
  a monotonic `uint64` counter (fixed width for stable serialisation; 0 is the reserved
  sentinel, real ids from 1), and `Graph` stores nodes in a `std::map<NodeId,
  unique_ptr<Node>>` keyed by id — so an id outlives the removal of other nodes (it is
  *not* a list index). `std::hash<NodeId>` provided for unordered use. (2) **`Graph` is
  now pure data** — `run`/`evaluate` moved *off* it into a public `Scheduler` layer
  (`scheduler.h`): abstract `Scheduler` (virtual `run(Graph&)` push + shared non-virtual
  `evaluate(Graph&, NodeId)` pull), `SerialScheduler` (single-threaded topo pass, no
  execution deps — used by flowview + the headless/cli paths + most tests), and
  `ParallelScheduler` (lowers onto an **injected, caller-owned** `lain::task::Executor`;
  the old hidden process-wide static pool is gone). `flow` still links `lain::task`
  PRIVATE (only `ParallelScheduler` names it; the header forward-declares `Executor`).
  Verified: warning-clean; 63 tests pass; flowview both modes still render the gradient.
- ✅ **Interactive graph editing** (2026-07-02) — the viewer became an editor.
  **Engine:** `Graph` gained `removeNode` (drops the node + its incident edges) and an
  `add(std::unique_ptr<Node>)` adopt overload (the template `add<T>` forwards to it) — the
  node lifecycle editing needs now that ids are stable handles. **`flow-example`** adds
  **`TintNode`** (`acm::Texture` in→out: CPU readback → tint → re-upload) so an edge
  actually carries data; the smoke scene is now `gradient → tint`, and a `[gpu]` test
  round-trips it through the edge. **flowview** owns a `lain::core::Factory<flow::Node>`
  (`registerExampleNodes`, keys `"gradient"`/`"tint"`) and its canvas is editable:
  drag to connect (replacing an occupied input), drag a link off a pin to detach/move
  (imnodes `EnableLinkDetachWithDragClick` + `IsLinkDestroyed`), Delete to remove selected
  nodes/links, right-click for the add-node palette; a `SerialScheduler` re-runs the graph
  after each edit. The inspector previews **every** texture port via a
  `VkImageView`→`ImTextureID` cache (register-once — `Context::image` has no removal).
  Interaction gotchas that bit us and are now handled: focus/hover checks use
  `ImGuiFocusedFlags_RootAndChildWindows` (imnodes runs in a child window), and the canvas
  is drawn + edited *before* the inspector panel so a deletion never leaves a freed texture
  on screen. Verified: warning-clean; 71 tests; add / delete / detach / reconnect confirmed
  live. A deleted node's preview descriptor is reclaimed via `lain::gui::Context::releaseImage`
  (the inspector prunes its cache to the views still held by live ports after each edit).

Keep this section current as work lands. Once `flow` is fuller, this file is its
standing architecture reference (the role `CLAUDE.md` plays in the sibling repos).

## What `lain` and `flow` are

`lain` is a **cumulative set** — an umbrella of small libraries under `libs/`,
each either owned `lain` code or a thin wrapper giving an external library a
`lain::` face (`libs/task` → `lain::task` over Taskflow; `libs/math` →
`lain::math` over GLM; `libs/core` → `lain::core` std-only types; `libs/log` →
`lain::log` over spdlog; `libs/string` → `lain::string` over fmt; `libs/meta` →
`lain::meta` over magic_enum; `libs/app` → GLFW 3.4 + CLI11; `libs/gui` → Dear ImGui).

`flow` is a fast, threadable node-graph engine: typed-port nodes connect into a
DAG, the graph evaluates across worker threads, and every intermediate result
stays inspectable. It is **payload-agnostic** — a port carries any copyable value,
CPU or a GPU handle — so `flow` pulls in no GPU/UI deps; it links only two
featherweight libs:

- **Taskflow** (via **`lain::task`**, `libs/task`) — the execution substrate. It
  *is* a task-graph executor with a work-stealing pool, so the push scheduler is
  largely its job; our owned scheduler code just lowers our DAG onto it. We wrap
  it thin so `flow` sees `lain::task`, never `tf::`.
- **`lain::meta`** (`libs/meta`) — a `Port` captures `lain::meta::typeName<T>()` at
  declaration so the inspector can label pins by type. Compile-time, header-only — no
  GPU/UI coupling, in keeping with `flow`'s minimalism.

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
   `flow` names no GPU types and links only `lain::task` + `lain::meta` (both
   featherweight; the latter for `Port::typeName()`). "Is this a texture, preview it"
   is the viewer's job: it compares `PortValue::type()` against `typeid(acm::Texture)`
   (it already links archimedes). Supersedes the earlier dual-tagged-PortValue plan.
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

1. **`Graph`** — a pure data model: owns nodes (`add<T>(...)`, keyed by opaque
   `NodeId`), `connect`/`disconnect` with type-checking + cycle rejection, holds the
   edge list + topo order. It does **not** execute — a `Scheduler` (layer 3) consumes
   it.
2. **`Node` / `Port` / `PortValue`** — `Node` is an abstract base with a
   polymorphic `compute()`; declares ports in its ctor. `Port` owns a
   **persistent** `PortValue` (overwritten on recompute, never consumed
   downstream — this is what makes stages inspectable). `PortValue` is a thin
   type-erased slot carrying a `std::type_index` for connection checks — it holds
   any payload (CPU value or GPU handle), with no GPU types in `flow`, and holds it
   **shared + immutable** so a per-edge copy is a refcount bump. A node's
   `constant` / on-request character is expressed through `dirty()`: a constant
   clears it after first compute; a `CameraCapture` stays dirty so each pull
   refires.
3. **`Scheduler`** (public, `scheduler.h`) — consumes a `Graph` and evaluates it;
   this is where execution lives, not on `Graph`. An abstract base exposes the
   varying full push `run(Graph&)` plus the shared, serial pull `evaluate(Graph&,
   NodeId)` (recompute only `dirty()` upstream — the constant / on-request path).
   Two backends: `SerialScheduler` (one topo-order pass, no execution deps) and
   `ParallelScheduler` (lowers to a `tf::Taskflow` — one task per node calling
   `compute()`, edges become `precede` — and runs it to completion on an
   **injected, caller-owned** `lain::task` executor; no default/hidden pool).
   Taskflow owns the push scheduling, so `ParallelScheduler` stays thin.

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
