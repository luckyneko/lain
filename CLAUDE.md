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

### Update 2026-08-10 — M7 designed: shared template definitions (nothing built yet)

M6 removed the reason every linked group owned a private copy of its template, and proved N
evaluations over one `const Graph` safe — but left `LinkedGroupNode` copying. **Milestone 7** makes
the sharing real. Grilled 2026-08-10; decisions in
**[ADR-0013](docs/adr/0013-shared-template-definitions.md)**, build order in WORK.md, vocabulary in
CONTEXT.md. **Nothing is built.**

- **One definition per template**, in a host-owned **`TemplateCache`** (canonical path →
  `{shared_ptr<const Graph>, EditorTree}` — the layout travels with it, or a linked group's nodes land
  in default columns again). The TYPE is `flow::serialize`'s, so the recursive load and the self-link
  cycle guard stay where they work; the INSTANCE is the host's, which is the only side that sees the
  events that invalidate it.
- **`Node::innerGraph()` becomes `const`,** and the group types split to say why: abstract
  `GroupNode` (mirroring), `InlineGroupNode` (owns a `Graph`, mutable `inner()`), `LinkedGroupNode`
  (`shared_ptr<const Graph>`). Read-only-in-place stops being a `bool` every pane must remember —
  which is what M5's bugs three and ten were — and becomes something the type refuses. It also brings
  the code onto ADR-0010's own *inline/linked* vocabulary.
- **A template edit REPLACES the definition, never mutates it.** That is what keeps ADR-0012's "a
  `const Graph&` is concurrently readable" true once a definition is genuinely shared: there is no
  window where one is both read and written, rather than a guard around one.
- **Reload is snapshot → invalidate → restore**, through the loader that already exists. Patching
  instances in place would be a second implementation of what the loader does — the shape that
  produced M5's bugs six and eight.
- **Saving a document invalidates its own path — required, not a nicety.** `Edit Template… → Save →
  Return` works today *because* the return re-reads the file; a stale cache would break it.
- **`IdPolicy::Mint` is deleted.** It existed only because loading one template twice made duplicate
  ids certain. Sharing removes the duplication instead of compensating for it — and the decision is
  only safe because M6 step 5 put the level in `PinKey`, since two instances now have identical inner
  node ids by construction.
- Three slices: (1) hierarchy + constness, a pure refactor; (2) the cache; (3) the reload gesture.

**Slice 1 is built (2026-08-10).** The hierarchy split landed as designed, with one addition: since
read-only-in-place is now the *type's* job, `LinkedGroupNode` exposes no mutable interior at all —
a loader establishes one through **`adoptInterior(Graph)`**, the seam slice 2's
`shared_ptr<const Graph>` replaces without touching a caller. `Node::innerGraph()` is const-only.
flowview's `resolvePath` split into a const read resolution and **`resolveEditable`** (nullptr at a
linked group), and **`editableAt` is deleted**: a pane derives "may I edit?" from *having something
to edit through*, so the check can no longer be forgotten — which is what M5's bugs three and ten
were. Panes take `const Graph&` + a nullable `Graph*`; `MenuBarPane` takes only the const graph
(Save writes the root, and `Add ▸ Linked Group…` resolves its own level). `ctest` **398/398**,
warning-clean, format-check clean; headless `run` unchanged and a real linked-group document still
resolves + runs. **gui-mode live-verified 2026-08-10.**

**Slice 2 is built (2026-08-10).** `TemplateCache` (canonical path → `{shared_ptr<const Graph>,
EditorTree}`) lives in `flow::serialize`; the host owns the instance (`AppContext::templates`, beside
`CanvasIds`, cleared in `performSwap` — so a document swap re-reads templates from disk while an edit
or an undo keeps them, which is what holds a template's inner ids steady). `LinkedGroupNode` holds
`shared_ptr<const Graph>` + `definition()`; an unresolved link owns its placeholder alone behind the
same pointer. A definition containing an unresolved link is **not** cached (a structural walk), so a
missing-then-created template heals with no gesture. **`IdPolicy` is deleted whole** — a load always
preserves identity, and cacheless loading just yields separate equal copies, which is safe because ids
need only be unique *within* a graph. `ctest` **404/404**, warning-clean, format-check clean.
- **Two deliberate deviations, both written up.** (1) The injected resolver runs *before* the cache
  lookup — the canonical key is the resolver's answer, and flow must not interpret a `source` path —
  so a template file is still read per instance and what the cache shares is the BUILD; ADR-0013's
  ordering bullet is amended in place with the reasoning. (2) New public
  **`serialize::resolveLinkedGroup`**: resolving one link (source already set) is now ONE routine the
  loader and the host share. `Add ▸ Linked Group…` had its own copy — which is how it once dropped the
  template's layout, and would now have given the added instance a private copy of a shared
  definition. That discharges the M5 note asking for the two halves to be collapsed.
- Live headless proof: a document with **two linked groups on one template** resolves, mirrors both
  faces, and pushes two *different* images through the one shared definition to two distinct results;
  save ⇒ load ⇒ save still byte-identical. **gui-mode live-verified 2026-08-11.**
**Slice 3 is built (2026-08-11) — M7 is COMPLETE.** **File ▸ Reload
Linked Groups** clears the cache and rebuilds the document through **snapshot → invalidate → restore**,
the loader that already exists rather than a second in-place patch path. It keeps the user where they
are (active path + selection, like an undo), pushes **no** undo entry, and marks the document dirty
only when the rebuilt document actually differs — a `data::Value` compare of the before/after
snapshots, so a template edit that leaves its interface alone claims no unsaved work. The item is
greyed out when the document links nothing (`groupnav::hasLinkedGroups`, recursive — a link most often
sits inside an inline group).

- **Saving any document drops that path's cache entry** (Save *and* Save As). Required, not tidiness:
  `Edit Template… → Save → Return` works because the return re-reads the file.
- **One canonical key, one function** — `graphio::templateKey` (weakly_canonical, so a not-yet-existing
  template still has a stable key) serves the resolver *and* save-invalidation. A key computed two ways
  eventually disagrees with itself, and the failure is silent: an invalidation that misses simply keeps
  serving the definition it was told to drop. A test pins the two together.
- **Accepted consequence, in the code:** after a reload that changed the document, the undo cursor is
  still the pre-reload state, so the next edit's undo steps past the reload too. The graph that returns
  is the same (ports re-derive from disk) with a stale interface cache, which rectification reports.
  Recording the reloaded document would tidy that at the cost of making reload look undoable, which it
  cannot be.
- `ctest` **407/407**. New: one file has one key however spelled (and it is the key the resolver
  reports); a stale entry demonstrably keeps serving the old definition, while dropping that key or
  clearing the cache picks the edit up, group face and all; `hasLinkedGroups` finds a nested link.
  **gui-mode live-verified 2026-08-11 — M7 is COMPLETE.**
- Still out: file watching, prefab overrides, in-place template editing.

### Update 2026-09-05 — M11 designed: loop nodes (nothing built yet)

`flow` can run a subgraph **once** (a group) and **N times independently** (a map). It cannot run one
**sequentially, feeding each pass into the next**. ADR-0012 named *"whether Loop carries state between
iterations"* among four questions it refused to guess at; ADR-0014 settled the other three for the map
and re-deferred this one. **Milestone 11** answers it. Grilled 2026-09-05; decisions in
**[ADR-0021](docs/adr/0021-loop-nodes-carried-state-per-iteration-staging.md)**, build order in
WORK.md, vocabulary in CONTEXT.md. Both earlier ADRs are resolved in place. **Nothing is built.**

- **There is no production caller, and the milestone says so out loud** — the owner's framing was
  *"there is no direct use case as much as this is a feature that really tests the node approach."*
  That makes the standard **stricter** than M8's, not looser, since ADR-0012's own warning is that
  guessing produces a mechanism fitted to imagined requirements. The vertical is the deliverable.
- **One `LoopNode`, bounded plus an optional condition** — a node-owned `count` input with a
  `Default{}` and an optional inner `continue : bool`. Count loop = count; while loop = condition with
  count as its bound; converging solver = both, which is what real solver code already is. There is no
  unbounded state to represent, so nothing has to refuse one at runtime.
- **The count bound is the staging loop's well-formedness condition, not a usability nicety.**
  `runStages` terminates today because *"a map is deferred at most once per invocation"*; a loop is
  deferred **per iteration**, which destroys that argument outright. The bound is what restores it —
  the reason this decision is load-bearing only became visible while working out the lowering.
- **A carry is a stored `PortId` pair**, created only by a paired gesture so half a carry cannot be
  authored — id-keyed for the reason `portMap` already is. Name-pairing was refused on its failure
  mode: a rename would silently stop a loop carrying, with no error and a plausible answer.
- **Two reserved inner pins** the engine writes and reads (`index`, `continue`), not mirrored, skipped
  **by id**. `continue` defaults to **true**, so unwired is transparent while wired-and-suppressed
  means the iteration failed — `GateNode::enable`'s 2026-08-15 resolution, reused rather than
  reinvented for the same trap.
- **A map maps, a loop folds.** Gathering across iterations was refused so the two node kinds stay one
  job each. `iterations : int` is what makes "converged at 7" distinguishable from "hit the bound at
  100" — a fact the graph reads instead of a policy the engine invents.
- **One iteration per stage, one retained child evaluation.** A map's elements are co-equal *results*,
  a loop's iterations are *steps*, so retaining them would turn ADR-0014's self-declared sharpest cost
  into something a typo makes unbounded. It also keeps `EvalPath` at index 0, so `PinKey`, the
  breadcrumb and the element stepper need **nothing** — the surface that produced all ten of M5's bugs
  is untouched.
- **Reading a carry out before rebinding the same child is sound only because of M5 slice 1** —
  `PortValue` payloads are shared and immutable, so *"an earlier copy keeps the old payload"*. Without
  it, reuse would need a second child or a deep copy per carry per iteration.
- **Store the pairing, derive the ports** — so a loop follows M5's group rule rather than the map's.
  A map stores its *ports* because its mode lives in a type; a loop's mode **is** the pairing, so
  storing that stores the fact rather than its consequence, and no derivable second source can
  disagree with it.
- Six slices, the two structural refactors landing as **no-behaviour-change** commits first: (1)
  `evaluatesPerElement()` becomes an `interiorEvaluation()` enum, (2) `LoopNode` core types, (3)
  staging generalises so a frontier may be raised repeatedly, (4) the loop runs, (5) serialization,
  (6) flowview + both verticals. Count vertical needs no new node (`gradient → carry → blur ×5`); the
  while vertical adds `ImageDifferenceNode` + `CompareNode` and blurs until it stops changing.

### Update 2026-09-05 — M11 slice 1 built: the structural seam becomes an enum

The first of M11's two no-behaviour-change refactors. `Node::evaluatesPerElement()` is now
**`interiorEvaluation() -> { Once, PerElement, PerIteration }`** — a free `enum class` at
`lain::flow` scope in `node.h` (matching `Presence`, so the type and its accessor share a word).
Seven call sites, nothing returns `PerIteration` yet, and the existing suite is the regression test:
`ctest` **652/652** Debug and **658/658** Release, warning-clean, format-check clean, `flowview run
--example` unchanged. Full landing notes in WORK.md M11.

- **All three enumerators landed now, with ONE exhaustive switch**, because `PerIteration`'s arm is
  already *correct* rather than a placeholder: `Evaluation::prepare`'s child-count arm became a
  `switch` where a loop joins a group in the guaranteed-one-child case (ADR-0021 — one retained
  child, reused per iteration). So `-Wswitch` has a real anchor from this commit rather than from
  slice 4, **sabotage-verified** by deleting an arm and watching the build fail. Everything else
  stays an `==` comparison — M10 slice 4's `ColorSpace` precedent exactly.
- **The coverage claim is measured, not assumed.** Giving `PerIteration` the *map's* arm passes all
  652 tests, because nothing returns it yet. The switch's correctness is bought by slice 4; here the
  enum's value is the compiler's, not the suite's.
- **No `None` for a node with no interior.** `innerGraph()` already answers that and every reader
  pairs the two, so a fourth enumerator would be a second source able to disagree with the first —
  the shape M7 slice 1 and ADR-0014 each deleted. Stated in the header, so the absence reads as a
  decision.
- **flowview's `Crumb` carries the enum, not a derived bool** — which is where *"the element stepper
  stays off a loop crumb"* is actually bought (a map's elements are co-equal results you page
  between; a loop's iterations are steps, and only the last survives).
- **Found and removed: `scheduler.cpp` included `group.h` for a class it never names**, under a
  comment saying the scheduler asks the fact rather than the class — the include contradicted the
  rule this slice is about.

### Update 2026-09-05 — M11 slice 2 built: `LoopNode`, core types only

The second of M11's two no-behaviour-change refactors, and the last before the loop runs. `LoopNode`
lands beside `MapNode` — `addCarry<T>`, the id-keyed carry map, the two reserved inner pins, the
node-owned `count` / `iterations`, `editableInner()`, and the mirroring overrides. **Nothing runs and
nothing constructs one** outside its own tests (no factory reaches it until slice 6), so the existing
suite is the regression test and it moved by zero: `ctest` **663/663** Debug with video on (was
652) and **637/637** Release in the default video-off configuration (was 626) — both baselines grown
by exactly the eleven new cases. Warning-clean, format-check clean, and `flowview run --example` is
identical to the pre-change binary once timestamps and minted uuids are normalised. Full landing
notes in WORK.md M11.

- **The reserved pins are STATIC pins, through a new `addReserved` seam on the boundary nodes.**
  ADR-0021 asked for `index : int` and `continue : bool` with `Default{true}`, and neither was
  reachable: `addBoundary` routes through `addDynamicPort`, which **marks the pin dynamic** — so
  serialization would replay `index` onto a constructor that already made it, logging *"could not be
  added"* on every loop document — and has no `Default` overload, while `Node::addInput(name,
  Default<T>)` is protected and reachable only from inside a boundary node. Static is
  `SelectNode::selector`'s precedent exactly: replay skips it, the ctor rebuilds it on load, an edge
  to it resolves **by name** like any other, and `continue`'s default round-trips as an ordinary
  `Param`. **Slice 5 needs no special case, and `populateInputs` already seeds an unconnected input
  from `defaultOf`, so slice 4 needs no new mechanism to read `continue`.**
- **"Not a candidate" and "refused" are different questions, and the tests are what said so.** The
  first cut put the reserved-pin skip inside `exposePort` beside the name-collision refusal, and the
  new `GroupSync::refused` then named `index` and `continue` **on every pass for the life of the
  document** — the reporting channel drowned by the one case that is silent by design. Now
  `GroupNode::mirrorsPin(direction, pin)` (default true) answers *is this pin mirrored at all* and
  `exposePort` answers *was a candidate refused*. The skip is **by id** — sabotage-verified: by name,
  renaming `index` mirrors both reserved pins straight onto the outer face.
- **A loop is the first group kind with ports of its OWN, so it is the first where an inner pin's
  name can collide with one** — not anticipated by ADR-0021, and `addInputLike` answers a duplicate
  with an **assert**. Refused and reported by name, the same call `MapNode::exposePort` makes for an
  unliftable type — which that refusal now inherits, having been silent since M8. It arrives by
  **two** doors: the add phase, and the *rename* phase retitling a mirrored port onto a name the loop
  already owns, which would leave two same-named outer ports and make every name-addressed edge
  through them ambiguous on disk.
- **`refused` is names, not a count, and deliberately not a `changed()`.** A refusal is the only
  outcome here a user can act on and acting needs to know which pin; but a refused pin is retried
  every pass and flowview syncs every frame, so folding it into `changed()` would bump the recipe
  version continuously and re-run every evaluation forever. Sabotage-verified.
- **`GroupNode::reconcileInterior()` settles what ADR-0021 did not: a broken carry.** A pairing is
  the one thing about an interior that is not derivable from it, and the Interface pane's ± can
  delete either half. `syncGroupPorts` calls the hook before touching a port; `LoopNode` drops a
  pairing whose pin is gone, and the survivor then means exactly what an unpaired pin means — an
  invariant, or a last-iteration output. A virtual for the reason `exposePort` is one.
- **Two ADR claims checked at the code — one confirmed, one corrected.** The removal phase really
  does need no exception for `count` / `iterations` (it iterates `portMap()`, which never names a
  node-owned port), and `enterGroup` skips them for the same reason. But `exposePort` does **not**
  "derive seed / invariant / final / last": that derivation is *identical* to a plain group's,
  because the pairing changes what the engine does BETWEEN iterations, never what the ports look
  like. The override is refusals only. Both amended in place in ADR-0021 and WORK.md.
- **Found for slice 4, not fixed here:** `Scheduler::exitGroup` **clears** an output whose `innerPin`
  is null, so it would wipe `iterations` — `LoopExit` must write the node-owned outputs after it, or
  not reuse `exitGroup` at all.

### Update 2026-09-05 — M11 slice 3 built: staging state becomes a record per frontier

The last structural change before the loop runs. `Scheduler::PreparedMaps` — a `std::vector<Frontier>`
of frontiers already seen, scanned by a lambda inside `expand` — is now **`Scheduler::Staging`**, a
record per frontier ADDRESS answering a **count of preparations**. A map still defers exactly once, so
behaviour is identical and the existing suite is the regression test: `ctest` **663/663** Debug with
video on and **637/637** Release in the default video-off configuration, both unchanged from slice 2;
warning-clean, format-check clean, and `flowview run` over the example scene identical to the
pre-change binary once the timestamp and the minted uuids are normalised. Full landing notes in
WORK.md M11.

- **A record per frontier rather than a longer list is the whole slice.** The old shape encoded its
  own assumption — *"a map is deferred at most once per invocation"* — in the fact that it only ever
  appended. A loop raises its frontier **per iteration** (ADR-0021), so an append-only list would grow
  an entry per iteration and be re-walked on every `expand` lookup; the entry count now stays per
  frontier address however many times that frontier comes back.
- **A COUNT, not a flag** — a flag can say a frontier came back, only a count can say *which time this
  is*. The map's whole use of it is `== 0`, so, stated the way slice 1 stated its own coverage,
  **nothing reads it as more than 0-or-1 yet**: the count is bought by slice 4, not by this suite.
- **The map's own bound demonstrably still passes through the new type.** Sabotage: make
  `recordPreparation` record nothing and the `[flow][map]` cases **hang** — the map is deferred again
  on every stage and its frontier re-raised forever, which is exactly what M8 slice 2's staging test
  exists to prevent.
- **Sabotaging the ADDRESS changes nothing, and that is a finding rather than a pass.** Keying the
  record on the `NodeId` alone passes all 663 tests — including *"a map inside a map costs one more
  stage and nothing else"*, which exists for the `{definition, evaluation, node}` address — because
  every frontier raised in a stage is prepared before the next one, so two frontiers sharing a node id
  are always in the same state and, with only maps raising them, cannot diverge. The address is
  structurally required and observably bought by the first loop whose trip count can differ from a
  sibling's: a **while loop inside a map**, now named in slice 4's test list. Pre-existing — the old
  lambda compared all three fields and nothing tested that either.
- **Checked at the code, needs nothing:** ADR-0021's *"`expand` gains a step kind that both emits and
  defers"* is already expressible — a node may push its interior steps, then `deferred.insert(id)` and
  set no `ends` entry, and the existing `downstreamOfDeferred` scan plus both `runStages` break
  conditions already cope. Amended into the ADR in place.

### Update 2026-09-06 — M11 slice 5 built: loop serialization

A loop can now be saved. It writes a nested body like an inline group plus one **`loop` section** —
the carry **pairing** as name pairs, and the two reserved pin names — and, unlike a map, **not** its
own ports: a loop's face derives exactly as a plain group's, so storing them would put a derivable
fact beside the underivable one. `ctest` **690/690** Debug with video on (+10) and **664/664**
Release in the default video-off configuration (+10); warning-clean, format-check clean, `flowview
run --example` unchanged (nothing constructs a loop until slice 6). Full landing notes in WORK.md M11.

- **A loop's reserved pins do NOT survive a load, and this file and CONTEXT.md both claimed they
  did.** The claim was that a reserved pin is *"rebuilt on load by the owner's constructor, exactly
  as a Select's `selector` is"* — true for a Select, whose static pin is on the node the factory
  made, and false for a loop, whose pins are on its **interior**, which the loader replaces
  wholesale. `loadBody` gains a **`prepareInterior`** hook, invoked on the freshly constructed Graph
  before any node is seated, so the pins exist before that body's edges resolve. Declaring them first
  also makes their ids deterministic and turns a document naming a dynamic pin `index` into a
  reported skip rather than a duplicate-name assert. Sabotage: without the hook, **all eight** cases
  fail.
- **The reserved pin NAMES are stored, and that is load-bearing.** A reserved pin is renameable and
  an edge into one is name-addressed like every other, so a renamed `continue` whose name is not
  recorded comes back as `continue`, its edge resolves to nothing, and the condition falls back to
  `Default{true}`. Sabotage-verified: the converging while loop runs to its bound — **110 instead of
  13, 100 iterations instead of 3**.
- **`establishReserved` and `pairCarry` are each ONE routine with two callers** — the constructor and
  the loader; the authoring gesture and a document restore. `addCarry<T>` delegates to `pairCarry`,
  which refuses a reserved pin, a pin already half of another pairing, and a **type mismatch**
  (`Evaluation::bind` type-checks nothing, so a mismatched pair binds the wrong payload at runtime).
  Sabotage: skip the restore and **5 of 8** fail, the fold falling back to its seed.
- **Found by the tests, fixed at its source: renaming a defaulted input's port stranded its param.**
  `Port::setName` says a rename *"touches nothing structural"* — false once a port has a `Default`,
  since the param behind it keeps the old name and **a param is addressed by name on disk**, so the
  default silently reverts on the next load. New **`Node::renamePort`** moves both and refuses an
  invalid or already-taken name; `edit::syncGroupPorts` and flowview's Interface pane now use it.
  Latent until now — `continue` is the first port in the tree that is both defaulted and renameable.

### Update 2026-09-06 — M11 slice 6 built: a loop is reachable, authorable, and both verticals run

The milestone's deliverable. Slices 1–5 built the loop end to end and **nothing constructed one** —
no factory key reached `LoopNode`, so it was unreachable from the gui, the palette and any document a
user could write, which is the compiled-linked-unreachable shape this file has caught four times.
`ctest` **697/697** Debug with video on (+7) and **671/671** Release in the default video-off
configuration (+7) — both baselines grown by exactly the seven new cases; warning-clean, format-check
clean, `[loop]` swept 100× on the parallel path. **gui-mode was eyeballed by the repo owner on
2026-09-07 and found issues** — untriaged at the time of writing. *(**Superseded** — triaged in the
2026-09-08 pass: one crash (M12's stale-value `bad_any_cast`) and one documentation gap (a loop is a
do-while). **Slice 6 and M11 are COMPLETE as of 2026-09-08** — see the 2026-09-08 update below.)*
Full landing notes in WORK.md M11.

- **Both verticals run through the REAL binary.** `flowview run --graph count-loop.json` folds a
  gradient through five blurs and reports `iterations: 5`; `while-loop.json` blurs until the picture
  **stops changing** and reports `iterations: 23` against a bound of 100 — ADR-0021's claim that
  `iterations` separates *"converged at k"* from *"hit the bound"*, observed rather than argued. Both
  documents are self-contained (a `GradientNode` is the seed), so the binary opens and runs them with
  nothing bound.
- **"Stopped changing" is LITERAL, which deleted an invented number.** The first cut carried a
  tolerance of 0.002; probing showed a cliff — 2 iterations at every threshold down to 0.0001, then
  8, then **23 for anything below ~0.00003** — because a clamped Gaussian blur of an 8-bit image
  reaches an exact fixed point. So the threshold is **0**, `> 0` is an exact test, and the magic
  number is gone. A positive tolerance is the same mechanism stopping earlier, which the bounded
  section demonstrates from the other end.
- **The count vertical is checked against an INDEPENDENT chain of five blurs, byte for byte** —
  because every weaker assertion (it is an image, the right size, blurrier than the seed) passes just
  as well when the carry is not carrying. Sabotage-verified: recording no pairing fails the count
  vertical on that comparison and the while vertical on `iterations` (100, i.e. it ran to its bound),
  while the round-trip case correctly still passes, since it only asks whether a load folds the same
  as the build.
- **Found and fixed here: the Interface pane's `×` would remove a RESERVED pin.** `Graph::removePort`
  erases a static port as happily as a dynamic one, so from the very pane this slice adds the carry
  gesture to, a user could delete `index` or `continue`; `LoopNode` then held a `PortId` naming
  nothing, the condition fell back to its `Default{true}`, and a while loop ran to its bound with no
  error — while a save-and-reload quietly healed it (`establishReserved` remakes them), which is what
  would have made it hard to find. The guard is **`Port::isDynamic()`**: general rather than
  loop-shaped, since every pin a user adds here is dynamic and a static one exists only because the
  node that OWNS this graph declared it. The panel needs no idea what a loop is, the root is
  unaffected, and the NAME stays editable — a reserved pin is renameable by design.
- **`LoopNode::addCarry` gained a registry-keyed twin, and both run ONE routine.** A host's menu is
  `portTypeKeys()`, so it has a string where `addCarry<T>` wants a type. Sabotage: let an unknown key
  fall back to a default type — a plausible wrong implementation, since a mistyped carry is not a
  visible break but one that binds the wrong payload at runtime — and the new case fails. Recorded:
  the undo inside is still **unreachable**, because both adders refuse the same things, so anything
  failing on the second side already failed on the first. It stays for the reason its comment gives.
- **The Carries section STATES the pairing** rather than letting the pins imply it — a carried
  `Image` and an invariant `Image` are the same pin of the same type, and a document may pair two
  differently-named pins. Each row's `×` removes both pins (the atomic inverse of the paired add);
  the pairing needs no explicit drop, since `syncGroupPorts` calls `reconcileInterior()` first.
- **`groupnav::loopAt` / `editableLoopAt`** resolve the loop whose interior a path names — one level
  UP, since the pairing is the node's and the pins are its interior's. Two functions rather than one
  plus a remembered check, as `resolvePath` / `resolveEditable` are. `loopAt` also checks the parent
  path resolved COMPLETELY: `resolvePath` truncates what did not, and a lookup in an ancestor finds a
  different node — M5's bug six.
- **All four group kinds gained a canvas colour, not just the loop** (`group`, `map`, `linkedGroup`,
  `loop` share one): they are one category in the catalog, and `canvasstyle.cpp`'s rule is that
  categories are emergent from shared colour. None of them had one before, so what you can descend
  into was the default title grey.
- **`CompareNode` carries four INEQUALITIES and no equality** (exact float equality would need an
  epsilon policy nothing asks for); its `op` round-trips through `data`'s enum-as-name. And
  **`ImageDifferenceNode` is a measurement, not a blend**, so no op-class enforcement applies —
  differing size or format is refused rather than reconciled, which inside a loop is an iteration
  that failed.
- **Three adjacent gaps found and deliberately left out**, each with its own commit owed and listed
  in WORK.md: `GroupSync::refused` still has no reader; the Issues pane flags an unwired DEFAULTED
  input as a missing required one (`BlurNode` has done this since 2026-08-15); and
  `refusalText(NotInline)` says "linked" for a map or a loop. *(**All three fixed** 2026-09-09 /
  09-10 — see the entries of those dates. Each was larger than it looked here: the first needed two
  readers, the second was untestable where it sat, and the third's wrong string turned out to be
  unreachable.)*

### Update 2026-09-06 — a node does not publish while its interior has deferred

Slice 4's recorded pre-existing bug, fixed in its own commit before slice 5. `expand`'s group and map
arms emitted their exit even when the recursive expansion of the interior had deferred something — so
a group containing a mid-fold loop republished the **previous stage's** value on every intermediate
stage, and a map containing a deferred map gathered elements that had not run and published a
**cleared** collection (ADR-0014: one hole clears the whole output). Both now measure whether the
interior raised a frontier and, if so, contribute its steps but take **no exit and no `ends`** —
which is the rule the loop arm already followed. `ctest` **680/680** Debug (+2), warning-clean,
format-check clean, `[loop]` swept 100×, `flowview run --example` unchanged.

- **Neither arm raises a frontier of its own**, so `runStages`' *"a group never raises a frontier"*
  assert stays true; the interior's own frontier brings the level back, and `Scheduler::stale`
  recurses into the child evaluation so the node is still selected next stage. The stale comment
  saying a group *"is never deferred"* is corrected to *never raises a frontier*.
- **Both tests had to be rewritten to catch their own sabotage**, and the reason is the finding: the
  bug is **invisible on a first run**. With nothing computed yet, an early publish hands the consumer
  an *empty* slot, ADR-0007 suppresses it, and nothing runs on a wrong value. Only on a re-run does
  an early publish hand over a plausible, finished-looking previous answer. The map twin needed one
  turn more — a **new** element gathers empty (suppression again), so the element count is held
  steady and only the values change.
- **The observable is a call count, never a value**: every value assertion passes with the bug
  present, which is why it survived two milestones. Sabotage-verified — the group's consumer computes
  **6 times instead of 1**, the map's **2 instead of 1**.
- **Deferring a group cost it two plan edges, and those ARE wrong answers.** Found by the test's
  parallel section, not by the design, and invisible to a serial walk because `plan.steps` order
  happens to be right there. (1) A deferred group took no `ends` entry, and this level's edge wiring
  skips a node with none — so the entry step lost its incoming dependency and published the previous
  value into the interior. It now records `Ends{entry, entry}`, the mirror of the map arm's own note:
  everything downstream of a deferred node is deferred too, so the producing end is never read.
  (2) The **entry → inner GroupInput** edge moved BEFORE the deferral check, since the interior runs
  in this stage either way. Exposing either needed a scene with a parent-level input relayed through
  a body node; sabotage-verified at **11/20** and **18/20** parallel failures.
- `ctest` **680/680** Debug (video on) and **654/654** Release (default video-off) — both baselines
  grown by exactly the two new cases.

### Update 2026-09-06 — M11 slice 4 built: loops run

The milestone's centre: `prepareLoop`, the `LoopExit` step, the count / `continue` termination test,
the `count == 0` identity, the suppression rules and `iterations` — through **both** schedulers.
`ctest` **678/678** Debug with video on (+15) and **652/652** Release in the default video-off
configuration (+15); warning-clean, format-check clean, the `[loop]` tag swept **100×** on the
parallel path with no failures, and `flowview run` byte-identical to the pre-change binary (nothing
constructs a loop until slice 6). Full landing notes in WORK.md M11.

- **One new structural seam, `Node::iterationPorts()`** — `bound`, `report`, `index`, `condition`
  and the carry pairing in one `IterationPorts`, beside `innerGraph()` / `innerPin()` /
  `interiorEvaluation()`. A map needed no equivalent because split-or-broadcast follows from a
  port's own type; none of a loop's five facts follows from anything, and pairing by NAME was
  refused in ADR-0021 because a rename would then silently stop a loop carrying. `scheduler.cpp`
  still names no node class. Answered `std::optional` **by value**, not as a pointer to a stored
  struct: `carries` points into the answering node, so a stored one would be one move away from
  dangling.
- **A loop re-raises its frontier only when the iteration will FINISH in that stage — found by the
  tests, not the design.** The first cut had an iterating loop always emit its interior's steps and
  re-raise; loop-in-a-loop and map-in-a-loop then failed on their first run, because the coordinator
  read the carried outputs between stages while the interior had itself deferred, saw them **empty**,
  and called the iteration a failure — a wrong answer, not a crash. `expand` now measures whether
  its recursive expansion raised a frontier and holds the loop back a stage, which is what makes
  ADR-0021's own *"a map inside a loop costs two stages per iteration"* literally true.
- **Seeding a new iteration FORGETS what is staged inside the interior.** One child evaluation is
  reused, so a map in the body keeps the same frontier address every pass and would be expanded
  against the previous iteration's children. Sabotage-verified, and needed for a nested **loop**
  too — dropping it fails both cases. The staging bound becomes *a map defers at most once per
  enclosing iteration*, still bounded because iterations are.
- **`LoopExit` is its own routine, discharging slice 2's flagged trap** (`exitGroup` clears an
  output with no inner pin — exactly `iterations`). Whether the loop could run, and whether the fold
  BROKE, are asked at the exit rather than remembered — `exitMap`'s rule. Only a broken **carry** or
  condition clears everything; an unpaired output being empty is ordinary per-port emptiness.
- **Four sabotages, all caught.** Remove the count bound → the fold **hangs**. Bind the seeds
  instead of the carries → 11 instead of 15, 12 of 26 cases fail. Drop the interior forget → the map
  in a loop serves iteration 0 forever. **Key staging on the `NodeId` alone → *"a loop inside a map
  lets each element stop at its own iteration"* fails**, which is the measurement slice 3 recorded
  as owed: the `{definition, evaluation, node}` address is now bought.
- **Recorded, not fixed (pre-existing):** a group emits its `GroupExit` even when its interior
  deferred, so a group containing an iterating loop republishes a stale value each intermediate
  stage. The final value is correct (pinned by a test) and a map inside a group does the same today,
  so the fix belongs in its own commit.

### Update 2026-09-28 — M14 designed: the gui never waits on graph work (nothing built yet)

gui-mode runs `SerialScheduler::run` synchronously in the frame loop on every edit — every frame of a
param drag included — so a slow graph freezes the window and `ParallelScheduler` has no gui caller.
Grilled 2026-09-28; decisions in **[ADR-0025](docs/adr/0025-runs-read-a-clone-gui-never-waits.md)**
(amending ADR-0012 and ADR-0024 in place), build order in WORK.md's **Milestone 14**, vocabulary in
CONTEXT.md's new *Running while you edit* section plus *clone*, *lineage* and *coordinator*.
**Nothing is built.**

- **The recorded blocker was the smaller half.** ADR-0024 named torn reads of an evaluation slot. A run
  also reads `const Graph&` on workers for its whole length while four panes and `syncPathGroups`
  mutate the document in place — edge-vector reallocation, plan steps holding `const Graph*` into a
  deleted interior. That is undefined behaviour, so "run it on another thread" was never an option
  until something decides what an edit does meanwhile.
- **A run reads a CLONE of the document.** `Graph::clone()` preserves NodeIds, per-node versions and
  **lineage**, so one evaluation stays incremental across clones while edits land on the document and
  never race a run. Glossary: *load* preserves identity, *paste* mints it, *clone* preserves identity
  **and history**. Rejected: draining in-flight steps before each mutation (the stall is the longest
  single step — seconds for a footage open — and it needs a choke point four panes do not have), a
  reader/writer lock, edits as queued commands.
- **Lineage replaces address identity in `Evaluation::prepare`** — at the root AND for children,
  whose guard (`child->m_definition != inner`) would reject every cloned interior. It also closes the
  recycled-address hole ADR-0012 admitted.
- **A dedicated coordinator thread calls the unchanged, blocking `run()`**; the frame loop polls.
  **WORK.md's recorded `start()`/`poll()` sketch is superseded**: driven from the frame loop, staging
  is frame-quantised (a 100-iteration loop costs ≥ 100 frames), and Serial and `--threads 0` would
  block the UI again. Under Parallel the coordinator is the pool's participating "+1".
- **Panes read a *published evaluation*** — a refcount-cheap copy that keeps its run's clone alive —
  so the ten pane files do not change. Published at stage boundaries + run end, then per node as
  steps finish. Not a "snapshot": that word is the undo document.
- **Cancel at step boundaries + an opt-in `NodeEvaluation::cancelled()`.** A step that finishes after
  a cancel is KEPT — its version still matches for every node the edit did not touch.
- **Found while grilling: staleness assumes a run finishes its closure.** A run cancelled after U but
  before its consumer D leaves both clean, and D keeps a value computed from U's old output. The
  planned closure must be persisted as recompute requests at plan time. Also: a throwing `compute()`
  would crash gui-mode today (nothing catches), and `onStop` must join the coordinator before the pool
  stops.
- **UI:** run triggers **Live / On commit / Manual** per document (no timer — the delay worth waiting
  depends on a cost nobody knows in advance; On commit reuses undo's gesture boundary), scheduler
  Serial / Parallel per session, a Run menu with **Run Selection** and **Stop**, and five
  **freshness** states — Stale / Queued / Computing / Current / Failed — shown by a title-bar glyph,
  an outline pulse, thumbnail badges and a status bar, on channels no existing canvas state uses.
  *Stale* includes "anything upstream is Stale", and flow exposes that closure rule publicly rather
  than letting the viewer re-implement it.
- **The promise is exact: the frame loop never waits on GRAPH work.** Texture upload and poster decode
  stay on the UI thread, made proportional to what changed (payload identity) rather than moved.

### Update 2026-09-28 — M14 slice 1 built: clone + lineage

The structural half of ADR-0025, with no change in behaviour. **`Node::clone()`** (pure) on every
kind, **`Graph::clone()`** (the same NodeIds, versions, order and lineage; owned interiors deep-copied,
linked definitions shared), **`flow::Lineage`**, and `Evaluation::prepare` checking **lineage** where
it checked addresses — at the root and for every child. Nothing in production calls `clone()` until
slice 4's runner; the gui still runs the document itself. `ctest -j8` **807/807** Debug with video on,
**813/813** Release with video on, **780/780** Release video-off (+10 each, measured against the
pre-change tree); warning-clean, format-check clean, `flowview run --example` identical to the
pre-change binary once timestamps and minted ids are normalised. Full landing notes in WORK.md's
*Milestone 14 › Slice 1 landed*.

- **Three clones are hand-written, not mechanical.** A `Graph` is move-only, so `InlineGroupNode`,
  `MapNode` and `LoopNode` have deleted implicit copies and a copy constructor that clones the
  interior. `LoopNode`'s also copies state that is mostly not serialized as itself (its PortIds), so a
  forgotten member is invisible to the document census. The loop-scene and map-scene *"a cloned …
  runs the same"* cases are what catch one, and sabotage confirms it: dropping `m_carries`, `m_count`
  or `m_continue` each fails.
- **Pure `clone()` cost 68 test fixtures one line each** (the owner's call over a refusing default:
  the compiler lists every kind that lacks one). Three fixtures had a `mutable std::atomic` probe a
  copy cannot carry; it moved behind a shared counter with a `computes()` accessor.
- **The census runs over the PRODUCTION factory** (`apps/flowview/test/test_clone.cpp`): every key
  clones to the same dynamic type and the same document. A subclass of a concrete kind that inherits
  its parent's `clone()` is caught by `Graph::clone`'s assert in Debug and by the census in Release,
  both verified with a deliberately slicing kind.
- **Lineage is an opaque in-process counter**, not a uuid (the owner's call: it is never stored and
  never crosses a process). The loader needed no change, since every load constructs a Graph.
- **What address identity got wrong was a WRONG ANSWER, not only a missed refusal.** An interior
  rebuilt in the same storage, with the same ids and versions but a different constant, kept its
  child evaluation and served the old value: **1 instead of 2, measured against the pre-change
  checks**. At the root, a graph rebuilt and moved into the same variable was accepted. Both are now
  tests.
- **`Evaluation::definition()` is "the graph last prepared against"**, which will be a clone once runs
  read clones. `evaluation.h` now states that it must outlive any read; slice 2's published
  evaluation is what will hold it.
- **Found, pre-existing:** `edit::replaceGroup` (Save as Template / Make Local) re-seats a
  node at the same NodeId, and its restarted version can land exactly on its predecessor's
  `computedAt`. **Measured: 3 == 3 after making a one-input linked group local**, so the group looks
  clean over a fresh child and **its output goes empty**. The same happens under the old checks.
  *(**Fixed the same day** — see the next entry.)*

### Update 2026-09-28 — versions become process-unique (the replaced-group defect, fixed)

The defect slice 1 found, fixed at its cause. A node's version was a per-node counter from 1, so a
DIFFERENT node object re-seated at the same NodeId could climb to exactly the version its predecessor
had been computed at, and an evaluation keys its records by NodeId alone. **A version is now drawn
from one process-wide sequence** (`Node::mintVersion`, new `libs/flow/src/node.cpp`) at construction
and at every bump, so no two node objects share one unless one is a clone of the other. The repo
owner chose this over patching `replaceGroup` and over a per-lineage clock. `ctest -j8` **810/810**
Debug with video on, **816/816** Release with video on, **783/783** Release video-off (+3 each);
warning-clean, format-check clean. Full notes in WORK.md's *Milestone 14 › Versions become
process-unique*.

- **Larger than its description:** any node, not just a group. A source removed and replaced at its
  own id served its predecessor's constant, 1 instead of 2, because both objects sat at version 1.
  That shape, the measured `replaceGroup` shape and the property itself are the three new tests;
  sabotage (the counter restored) fails exactly those plus the two slice-1 cases built on the
  coincidence.
- **Lineage is now the PAIRING rule, not the thing preventing a false match**, because a version can
  no longer produce one. It refuses an evaluation of one document run against another (loudly,
  instead of a silent full recompute), accepts a clone, and gives a replaced interior a fresh child.
  ADR-0012 and ADR-0025 are amended in place; CONTEXT.md's *Version* entry now says a version is
  process-unique and compared only for equality.
- **Found while sabotaging, fixed in its own commit: an order-dependent test.** A M12 payload-type case
  assumed the process-wide conversion registry held no int -> string conversion. Catch2 v3 shuffles
  case order, so as a whole binary it failed about half the time; ctest never saw it, since it runs
  each case in its own process. The case now registers what it relies on. **A whole-binary sabotage
  run is not ctest**, and its shuffled order can turn up an unrelated failure.

### Update 2026-09-28 — M14 slice 2 built: the published evaluation + payload identity

**`flow::PublishedEvaluation`** — the copy of an `Evaluation` a gui host's panes will read while a run
holds the working one — and **`PortValue::samePayload`**, with its first production caller: flowview's
`PreviewCache` now skips a port whose payload its thumbnail was made from. `ctest -j8` **816/816** Debug
with video on, **822/822** Release with video on, **789/789** Release video-off (+6 each);
warning-clean, format-check clean; `flowview run --example` identical to the pre-change binary once the
timestamp and minted ids are normalised. **gui-mode NOT eyeballed** — a `--frames` smoke renders and
exits cleanly, but the skip needs an edit to exercise. Full landing notes in WORK.md's *Milestone 14 ›
Slice 2 landed*.

- **A copy reads through the definition its source was prepared against**, so the copy and that graph
  are one value: `{shared_ptr<const Graph>, Evaluation}`. `Evaluation`'s copy constructor is
  **private** with `PublishedEvaluation` its one caller, so no copy can outlive the graph it reads —
  the *"a lifetime held only by a scope is a lifetime nothing states"* shape, closed by the type. Chosen
  with the repo owner over a public `Evaluation::copy()` and over a keep-alive field in `Evaluation`.
- **Only const access goes out, so a published copy can never be run.** ADR-0025's deferred
  *evaluation fork* stays deferred by construction. Publishing checks the definition **by address** —
  right here, where it was wrong for `prepare`, because the question is lifetime rather than history.
- **`samePayload` is identity, never an exposed address**: a freed payload's address can be reused, so
  a kept address would call a new value unchanged. It compares the stored pointer (not the control
  block — every alias of a collection shares its owner's) **and** the type (a first member shares its
  owner's address). A caller asking "changed since?" keeps the `PortValue` it saw.
- **The preview cache keeps the port's value, not its poster** — a sequence's poster is decoded afresh
  and never compares the same — so a skip saves the decode as well as the stalling upload. Before this,
  every frame of a param drag re-uploaded every image on the level. Pulled forward from slice 6 so the
  accessor did not land four slices ahead of its caller; it is not under ctest (`previewcache.cpp`
  needs a `gui::Context`).
- **The clone tests' scene is now `libs/flow/test/testscene.h`**, shared, with the map's list as a
  param so a test can edit arity. Seven sabotages, all caught, recorded in WORK.md.

### Update 2026-09-28 — a compute() that throws stays stale (fixed ahead of M14 slice 3)

Found while planning slice 3 (cancellation), fixed in its own commit. `runNode` clears a node's
recompute request before `compute()` and skips `markComputed` on a throw. So a node in the run only
because something UPSTREAM changed (its own version already recorded) went **clean** when it threw,
and kept the value it had built from the old input: **1 instead of 2, measured**. It now asks for the
node again on the way out of a throw. `ctest -j8` **817/817** Debug with video on, **823/823**
Release with video on, **790/790** Release video-off (+1 each); warning-clean, format-check clean.
Full notes in WORK.md's *Milestone 14 › A compute() that throws stays stale*.

- **It matters for slice 4**, whose coordinator marks a throwing node **Failed**, defined as "stays
  Stale". Without this fix, a Failed node would have gone clean.
- **It fixes the thrower only.** Nodes a throw leaves UNREACHED (a serial walk stops, multi skips the
  thrower's successors) stay wrongly clean until slice 3 persists the planned closure as recompute
  requests. *(Discharged by slice 3, below.)*

### Update 2026-09-28 — M14 slice 3 built: cancellation

A run can be stopped and leave the evaluation where the NEXT run computes the right answer.
**`flow::RunControl`** (new `runcontrol.h`) holds a cancel flag and the progress read (node computes
planned and finished, summed across stages), any thread. `Scheduler::run(definition, evaluation,
control)` sits beside the unchanged two-argument `run`. `NodeEvaluation::cancelled()` lets a long
compute give up. Nothing in production passes a control until slice 4, and an uncancelled run behaves
as before. `ctest -j8` **827/827** Debug with video on, **833/833** Release with video on, **800/800**
Release video-off (+10 each); warning-clean, format-check clean; `[cancel]` swept 100× in Debug and
in Release through the Catch2 binary; `flowview run --example` identical to the pre-change binary.
Full notes in WORK.md's *Milestone 14 › Slice 3 landed*.

- **Only COMPUTE is cancelled; crossings always run** (decided with the repo owner, and ADR-0025 is
  amended in place). A skipped group exit had nothing to say the group was owed. A skipped entry left
  the group, on the next run, not republishing and computing its old input for good. A crossing only
  copies values, so running it keeps the owner consistent with its interior and needs no new state.
- **The planned closure is persisted as recompute requests** before each stage runs, because
  staleness assumed a run finishes its closure. There are exactly three kinds of entry: a node step,
  a node deferred because of something upstream of it, and a raised frontier. There are deliberately
  none for a group or map expanded without republishing, which would make it republish next stage, a
  change to an uncancelled run. The same requests fix what a THROW leaves unreached on a serial walk.
- **A compute that finishes after the cancel is KEPT; one that asked `cancelled()` and heard yes has
  GIVEN UP** and stays stale, whatever it wrote.
- **`stale()` asks every child of a map**, not child 0: a cancel stops a map part-way while its
  gather still runs. Slice 6's public freshness query inherits that.
- **Stopping between stages is what stops a loop between iterations.** Without it, the skipped
  iterations walk to the bound and the fold (a crossing) publishes `iterations == 100` for a fold
  that stopped at 4.
- **Deviations:** progress is on the scheduler's own atomics, not `RecipeHandle::finishedCount()`,
  which exists only inside `ParallelScheduler::executePlan`. It counts node computes, including a
  graph's own boundary pair, not crossings. `evaluate` takes no control until slice 8's Run
  Selection. Seven sabotages, all caught.

### Update 2026-09-29 — M14 slice 4 built: the gui never waits on graph work

gui-mode stops running the graph in the frame loop. **`flowview::Runner`** (`apps/flowview/src/
runner.{h,cpp}`) calls the unchanged, blocking `Scheduler::run` on a coordinator thread of its own,
over a **clone** of the document taken on the UI thread, and the panes read a **published copy**
(`flow::PublishedEvaluation`). The frame loop polls at its start (`FlowviewApp::pollRun`) and pumps at
its end (`pumpRun`), and never waits. Parallel by default, Serial selectable, Stop; Live is the only
trigger. `ctest -j8` **837/837** Debug with video on, **843/843** Release with video on, **810/810**
Release video-off (+10 each); warning-clean, format-check clean; `[runner]` and `[cancel]` swept 100×
through their Catch2 binaries in Debug and Release; `flowview run --example` identical to the
pre-change binary; `--frames` quits cleanly with a run in flight. **gui-mode eyeballed by the repo
owner 2026-09-29** (a quick pass, which looked good). Full notes in WORK.md's *Milestone 14 › Slice 4
landed*.

- **Two pieces came forward, decided with the repo owner while planning.** Pending bindings, whole,
  from slice 5: the Interface pane's direct `evaluation.bind()` is a race once a run owns the
  evaluation, and the immediate write into the published copy (`PublishedEvaluation::bind`) keeps a
  scalar drag from snapping back. And a between-stages observer on `RunControl`
  (`setStageObserver`), because the staging loop lives inside the blocking `run()` and a host could
  otherwise publish only at the end.
- **One job slot, and a supersede never waits.** `pumpRun` cancels a run in flight and keeps the
  request; the frame after it drains, it clones and starts the next. So one clone per run that starts,
  always of the newest document, and never a second job queued behind a first (that would need a second
  working evaluation — ADR-0025's deferred fork).
- **Found while wiring: a publication can be older than a binding already handed to the next job** —
  a run ending between one frame's poll and its pump publishes after the next job took the queue. So
  jobs have serials, a publication says which job made it, and `PendingBindings` lays every binding a
  publication cannot contain over it. Pinned by a thread-free test.
- **Bindings are applied after the runner's own `prepare`, outside the run lease** — the host cannot
  take the scheduler's lease and does not need to, since the coordinator is the working evaluation's
  only user once a job holds it. Prepare-then-bind is what lets a freshly added pin receive a value.
  ADR-0025 amended in place.
- **A throw is a RUN-level failure**: caught on the coordinator (it used to be nothing — gui-mode had no
  `try` around a run), logged, and shown as a persistent Issues row until a run completes. Which node
  threw is unknown — the exception does not say — and slice 6's per-node Failed now records that it
  depends on settling that first.
- **A document swap ABANDONS the run in flight** — drops whatever it would still publish — and the
  panes read an empty published evaluation until the new document's first run lands, rather than the
  old document's values from a dead lineage.
- **`onStop` stops the runner first** (cancel + join), before the published copy, the evaluation, the
  graph and — after the last `onShutdown` — the pool go. The coordinator starts lazily with the first
  job, so headless never has one.
- Seven sabotages, all caught — including the one that proves the point of the catch: without it, the
  throw case aborts the whole test binary with *uncaught exception ... boom*.

### Update 2026-09-29 — M14 slice 5 built: run triggers and persistence

**On commit** and **Manual** join Live. The trigger is saved with the document, and the scheduler is
saved with the session. The Run menu gains **Run** (Cmd/Ctrl+Enter), a Stop shortcut (Cmd/Ctrl+Period)
and **Trigger ▸** Live / On Commit / Manual. `ctest -j8` **849/849** Debug with video on,
**855/855** Release with video on, **822/822** Release video-off (+12 each); warning-clean,
format-check clean. `flowview run --example` is unchanged, and the headless save writes no new key.
**gui-mode NOT eyeballed** (the triggers act only in the frame loop). Full notes in WORK.md's
*Milestone 14 › Slice 5 landed*.

- **Three decisions, made with the repo owner while planning.**
  - **Manual is literal.** A document swap (New, Open, undo, Reload) doesn't run either, so the panes
    stay empty until Run.
  - **The trigger is saved but not undoable.** Undoing edits made before a switch to Manual must not
    flip a slow graph back to Live.
  - **The blob is per level.** `flow::serialize`'s `EditorTree` gains `graph`, the adapter's blob for
    a graph as a whole, written under `graphEditor` only when set. That means no version bump, and a
    Live document keeps its bytes.
- **The rule is a driver-free unit, `RunRequests`** (`apps/flowview/src/runpolicy.{h,cpp}`). It holds
  two asks (a change, an explicit Run) and answers `due(trigger, gestureEnded)` fresh each frame. Under
  Manual only Run is ever due, so an edit never cancels a run in flight. `RunStrategy` moved there
  from `runner.h`.
- **On commit keys on undo's boundary (`!IsAnyItemActive()`), not undo's `pendingSnapshot` flag.** A
  document swap and the startup request carry no snapshot and must still run. It is asked once a
  frame, for both readers.
- **The options have one live owner, `AppContext::options`.** New and Open bring them in through
  `pendingOptions`, applied before the pump, so a reopened Manual document never runs even for one
  frame. `saveGraph` owns the root blob, and `takeDocumentOptions` strips it from a load, so no undo
  snapshot carries a frozen copy. **I first claimed the strip prevented a spurious undo step after
  Open; tracing it said otherwise.** The comments now give the real reason.
- **Found and corrected in CONTEXT.md, not in code:** `lain::data` reads a struct MEMBER best-effort
  (an unknown enum name keeps its default and the struct still loads), which is deliberate
  (`reflect.h`). But *The type is the schema* claimed `fromValue` "fails on a shape/type mismatch",
  which is true of a struct's shape only.

### Update 2026-09-29 — a texture's descriptor is archimedes' (the mid-session descriptor free, fixed)

The defect the shutdown drain could not reach. `gui::Texture` took its descriptor from ImGui's pool and
gave it back with `ImGui_ImplVulkan_RemoveTexture`, an immediate `vkFreeDescriptorSets`. That ran on
every preview reallocation and prune, and every frame change of the sequence player, while up to
`acm::Renderer::MaxFramesInFlight` frames that had bound the set could still run. **The descriptor is
now an `acm::DescriptorSet`**, so acm's existing deferred destroy covers it exactly as it always
covered the image. `ctest -j8` **910/910** Debug with video on and **916/916** Release with video on
(+2 each: the two windowed gui cases, which skip under ctest); archimedes' own suite **57/57** (+1);
warning-clean, format-check clean in both repos. Full notes in WORK.md's *Milestone 14 › A texture's
descriptor outlives the frames that drew it*.

- **Decided with the repo owner.** A Texture holding a pointer to its Context's release queue was
  refused outright: a Texture holds no Context. Moving the descriptor into the library that already
  owns GPU lifetimes needed no reference at all. `gui::Texture`'s destructor is now `= default`, and
  it no longer has to die before its Context, only before the device.
- **The seam is layout identity.** `Context` allocates each descriptor against a layout identical to
  ImGui's texture set (one fragment `SAMPLED_IMAGE` at binding 0), and hands ImGui the raw handle as
  its `ImTextureID`. Vulkan binds a set where another layout is expected only when the two are
  identically defined, so that layout must follow ImGui's; the comment on it says so.
- **archimedes gained exactly what that needed** (`2df9d31` on its `develop`, pushed first):
  `DescriptorType::SampledImage`, `DescriptorSet::setSampledImage`, `acm::interop::descriptorSet`,
  and a test. Its deferral stamping is **unchanged**. A "last submitted + 1" stamp was proposed and
  dropped, because the case it aimed at is a caller rule rather than a timing one: a texture released
  between being drawn and being rendered is used after release when the frame is recorded.
- **flowview broke that rule once**: the document-swap block cleared the preview cache after the
  panes drew that frame. The clear now happens at the top of the next frame.
- **ImGui's descriptor pool stops capping previews** (it was 64 sets, about 63 thumbnails). It serves
  ImGui's own atlas only now, sized at ImGui's minimum.
- **Tests pin the arrangement, not the defect**, which nothing on this machine can observe: there is
  no validation layer, and MoltenVK does not fault on it.
  - archimedes: a sampled image read back through `texelFetch`.
  - lain::gui (`[gpu]`, `LAIN_GUI_SMOKE=1`): a texture drawn through ImGui offscreen and read back;
    72 textures from one Context, which fails if descriptors come from ImGui's pool again.
  - Each was sabotage-verified. **The draw test does NOT catch layout drift**: MoltenVK drew correctly
    through a stage-mask and a descriptor-type mismatch, both measured.
- **A sabotage restored in the SAME SECOND as the build that compiled it stays compiled**, even via
  `cp` plus `touch`: make wants the source strictly newer than the object. archimedes' suite then went
  red on the restored-and-correct test, which looked like an order-dependent flake until the
  assertion count (8, the sabotaged run's, not 11) gave it away. Check the object's mtime, or touch
  again a second later, before believing a post-sabotage result.

### Update 2026-09-29 — the GPU is drained before teardown (the early-quit device loss, fixed)

The defect M14 slice 8b found while smoking: `flowview --frames 1` or `2` logged a MoltenVK *"Lost
VkDevice ... GPU Address Fault"* on every try. **The cause was in `lain::app`, not flowview**:
`Application::shutdown()` handed teardown to every `WindowDelegate::onShutdown` while the frame loop's
last frames could still be executing, and flowview's destroys its `gui::Context`, whose
`ImGui_ImplVulkan_Shutdown()` frees ImGui's buffers, font image and pipeline at once. Those are raw
Vulkan objects ImGui owns, so acm's deferred destroy never sees them. The device was waited idle, but
only inside `device.reset()`, after every delegate had released. `shutdown()` now calls
`device.waitIdle()` **before** the first `onShutdown`. `ctest -j8` **908/908** Debug with video on,
**914/914** Release with video on (+1 each, a case that skips under ctest); warning-clean,
format-check clean. Full notes in WORK.md's *Milestone 14 › Quitting early lost the device*.

- **The three leads were ruled out by measurement.** A blank document (no previews, a trivial run)
  faulted exactly as `--example` did, leaving only what every document has: ImGui. A throwaway drain
  in `~Context` took it to 0 of 10 before the fix was placed where it belongs.
- **In `shutdown()`, not at the end of `run()`**: `acm::Device::waitIdle` is externally synchronized,
  and only after `onStop` has a delegate joined its own threads (flowview's run coordinator among
  them). `WindowDelegate::onShutdown` and `gui::Context` now state the guarantee and what relies on it.
- **A race, not a first-frame bug**: every try at `--frames 1/2`, never at `30`, but nothing bounds
  how long a last frame runs.
- **The test observes the contract, not the MoltenVK message**: a `[gpu]` window delegate whose last
  frame copies a 2048² texture into a readback buffer, and whose `onShutdown` checks every byte landed.
  Sabotage (drain removed): 10 of 10 runs fail, and flowview faults 3 of 3. Opt-in behind
  `LAIN_GUI_SMOKE=1`, like the smoke beside it.
- **Found on the way: `test-app`'s `[gpu]` cases could never reach a driver on macOS**, because no
  MoltenVK ICD was staged beside it; they died on the instance and then on GLFW's null-window assert.
  `acm_stage_vulkan_runtime(test-app)` now stages it, as flowview's CMake does.
- **Recorded, not fixed here: the same shape mid-session.** `gui::Texture` freed its ImGui descriptor
  set immediately (`ImGui_ImplVulkan_RemoveTexture`) whenever the preview cache reallocated or pruned
  one, while a frame that drew it could still run. *(**Fixed the same day** — see *a texture's
  descriptor is archimedes'*, above.)*
- **A verification that was hollow the first time:** a loop passing `"--example --frames 1"` as `$args`
  under zsh handed CLI11 ONE argument, so every run exited 109 (an extras error) without opening a
  window, and "0 errors" meant nothing. Re-run with the arguments split. **Check the exit code of a
  smoke, not only its log.**

### Update 2026-09-29 — M14 slice 8b built: Run Selection (**M14 COMPLETE**)

The flowview half, on 8a's pull. **Run ▸ Run Selection** (Cmd/Ctrl+Shift+Enter) brings the selection
up to date and nothing else. Its upstream cone goes Queued → Computing → Current, while what it
leaves reads Stale and is never Queued. That completes the build order, and with 8b's gui-mode
checklist walked by the repo owner, **M14 is complete** — and M9 is again the only milestone from 5
onward that is not built.
`ctest -j8` **907/907** Debug with video on, **913/913** Release with video on, **880/880** Release
video-off (+6 each); warning-clean, format-check clean; `[runner],[runpolicy]` (27 cases) swept 100×
in Debug and Release; `flowview run --example` unchanged; the `--frames` smokes exit 0. **gui-mode
live-verified by the repo owner 2026-09-29** — the whole checklist in WORK.md's *Slice 8b landed*.

- **Every trigger, and it falls out of the policy rather than needing a gate** (decided with the repo
  owner):
  - `RunRequests` gains a selection ask and answers `scope()` beside `due()`. A **`RunScope`** is the
    whole closure or a selection's targets.
  - A due change or a Run is **whole**, and a whole run covers any selection.
  - A partial start answers only its own ask. Under Live or On commit a pending change therefore
    supersedes the partial run with a whole one, and under Manual it waits.
- **Inside a group, the selection means the root-level group it is in, run whole**
  (`selectionTargets`). The menu item's tooltip says so. Per-level cones are deferred, with a trigger,
  in WORK.md and ADR-0025.
- **The selection is filtered to the level on screen.** Group's private filter became `canvasstate`'s
  `selectedNodesIn`, shared by both gestures. `CanvasIds` spans the whole document, so an unfiltered
  stale int could decode to a node at another level and, mapped to the root, run the wrong thing.
- **`RunReport::partial` travels with the outcome**, set on the coordinator from the job. The app's
  own start bookkeeping would mislabel a run that finished between one frame's poll and the next
  pump. The status bar reads "Last run (selection)".
- **Six sabotages, all caught.** Two first ran against a stale binary, because the sabotage itself
  failed to build under `-Werror`, and were re-run once they compiled: **a sabotage that does not
  build proves nothing**, and the old binary's answer looks exactly like a pass.
- **Found while smoking, older than this slice:** quitting in the first frame or two logs a MoltenVK
  *"Lost VkDevice ... GPU Address Fault"*. The process exits 0, and the pre-change binary does the
  same. A copied binary cannot be used to check this: it has to run from `build/apps/flowview/`,
  where the MoltenVK ICD is staged, or it aborts with *"no driver/ICD found"*. *(**Fixed the same
  day**, in `lain::app` — see *the GPU is drained before teardown*, above.)*

### Update 2026-09-29 — OpenCV for M9: a pinned, minimal, verified prebuilt (decided; `opencv-prebuilt` built)

M9 needs OpenCV (ChArUco detection, calibration), and building it from source inside lain is the
cost to avoid. **No existing binary fits:**
- the official releases cover Windows, iOS and Android only;
- opencv-mobile drops calib3d, objdetect and flann, and is built without RTTI or exceptions;
- conda-forge is the full contrib build inside conda;
- ConanCenter and vcpkg fall back to source builds;
- system packages are whatever version ships, with large dependency trees.

So **[`luckyneko/opencv-prebuilt`](https://github.com/luckyneko/opencv-prebuilt)** now exists
(`83943af`), a sibling of `ffmpeg-prebuilt`. The decision is
**[ADR-0026](docs/adr/0026-opencv-from-a-pinned-minimal-prebuilt.md)**, with ADR-0004 amended,
ADR-0016 pointed at it, and M9 given a **slice 0** in WORK.md. **Nothing in lain's code changed**,
and `addOpenCV.cmake` waited on the first published release, since its pins come from that
release's `SHA256SUMS`. **That release is out:**
[`opencv-4.14.0-calib`](https://github.com/luckyneko/opencv-prebuilt/releases/tag/opencv-4.14.0-calib),
built at `a8985f9` after two verify-only CI runs were green on all four targets. WORK.md's M9 slice 0
holds the hashes, and **slice 0 can start**.

- **Decided with the repo owner:**
  - OpenCV **4.14**, not 5.0 (5.0 splits calib3d and needs eight modules, and is a `.0`);
  - **shared** libraries;
  - the Windows archive carries **Release and Debug**.

  The fixed **`calib` profile** is core, imgproc, flann, features2d, calib3d, objdetect. ChArUco has
  been in objdetect since 4.7, so contrib is not needed. A published profile's module set never
  changes.
- **Shared is for symbol isolation, not licensing** (ADR-0004's second exception). OpenCV compiles
  zlib 1.3.2 into `opencv_core`, and lain statically links zlib 1.3.1 into flowview (png/tiff). A
  static OpenCV would put two zlibs in one link. Apache-2.0 would have allowed static.
- **Windows is MSVC, not MinGW like FFmpeg.** OpenCV's API is C++, and a MinGW C++ library cannot
  be consumed by MSVC. The Debug set exists because an MSVC Debug consumer changes `std::vector`'s
  layout.
- **Three OpenCV defaults each produce a broken artifact with a green build**, all found by reading
  its CMake:
  - the absolute install prefix baked into every RUNPATH;
  - the **system** zlib on Linux (`BUILD_ZLIB` defaults ON only on Windows/Apple);
  - the **static** CRT under MSVC.

  `build.sh` overrides all three. Also disabled explicitly: IPP (a configure-time blob download),
  Eigen (MPL, scoped to Ceres), LAPACK/OpenCL/TBB/OpenMP, and the ARM HALs (KleidiCV is another
  download), plus all I/O and GUI.
- **Vendored code is more than zlib, and found only by installing:** SoftFloat, DLPack, KAZE/AKAZE
  and MSER's chi-table, all BSD-3-Clause or Apache-2.0. The profile records them as reviewed, and
  `check.sh` fails if the installed licence set differs.
- **Upstream tags are unsigned**, so the pin is the commit (`0654a42…`). There is no GPG gate as
  FFmpeg has.
- **Verified locally on linux-x86_64 only:** `check.sh` PASSED; the archive is 6.1 MB xz, 26 MB
  extracted; the build takes about 5 min on 4 cores.
  - The check covers: the exact module set; no downloads; the reviewed licence set; no exported
    zlib symbol; self-containment; `$ORIGIN` RUNPATHs; a **moved** tree loading unaided through
    `find_package(OpenCV CONFIG)`; the library's own build info; and a synthetic ChArUco
    calibration recovering fx = 999.98 against 1000.
  - **CI (added after publishing):** run 1 was green on all four targets at the first attempt;
    Windows' relocated consumer built and ran in both Release and Debug. Run 2 re-verified a
    packaging change, and run 3 published. The Windows zip had been made by PowerShell 5.1's
    `Compress-Archive`, which writes backslash entry names. It is now Python's `zipfile`, and the
    published zip has 276 entries and no backslash in any of them.
- **Sabotages, all caught:**
  - One build with three defects (an extra module, OpenCV's default rpath, visible zlib symbols)
    gives 17 distinct failures.
  - The Linux system-zlib trap is caught by **two** independent checks, the licence set and the
    build info. Self-containment alone passes it, because a system `libz` is legal.
  - A 1% error in the smoke's expected camera fails it.
- **Found in my own check, and fixed before it shipped:** `ldd … | grep -q` under `pipefail`
  **races**. When `grep -q` exits early, `ldd` takes SIGPIPE and the `if` reads a positive as a
  negative. The sabotaged tree reported 14, 16, then 17 failures, silently skipping broken
  libraries. Now it reports 17 every time.
- **Found in the smoke:** a single linear warp aliases the markers, which biased fx 0.8% against a
  1% tolerance. Supersampling ×4 then area-averaging gives 0.002%. Rendering smaller instead
  breaks marker decoding below about 700 px.
- **Owed by M9 slice 1, recorded in ADR-0026:** OpenCV's own thread pool against ADR-0024's one
  pool. Either `cv::setNumThreads`, or `cv::parallel::setParallelForBackend` onto `multi`.

### Update 2026-09-29 — M9 slice 0 built: OpenCV lands, with nothing depending on it yet

The first code of Milestone 9, and deliberately nothing but the dependency. It is the shape M10
slice 0 gave FFmpeg. Full notes in WORK.md's *Milestone 9 › Slice 0 built*.
- **`cmake/addOpenCV.cmake`:**
  - fetches the published `opencv-4.14.0-calib` archive by SHA-256;
  - refuses at configure time any `MANIFEST.txt` whose version, profile, modules, linkage,
    **target** or third-party line differs from the pins;
  - then takes the targets from the archive's own `OpenCVConfig.cmake` (`EXACT`, `GLOBAL`,
    `NO_DEFAULT_PATH`);
  - stages the licence texts and a generated `--licenses` notice.
- **`plugins/camera`** with **`LAIN_CAMERA_OPENCV`**, default OFF *(**superseded 2026-10-02** — it now
  defaults ON for a top-level build and is skipped where no prebuilt exists; see that update)*. Its one member is
  **`lain::camera::opencv`**, today only `build.h` (`version()`, `buildInformation()`).
- **An `[opencv]` runtime test** asserts the prebuilt's contract from the linked library. Its pins
  are compile definitions fed from the module, so the gate and the test cannot describe two builds.
- **CI** gains a `camera` field moving with `video`.
- **`ctest` could not be run on the full tree here, and that is an environment limit, not a
  skipped step.** The sandbox's egress policy answers 403 to GitHub source-archive downloads, which
  is how every other lain dependency is fetched, so lain's root does not configure there.
  - Verified instead through a standalone harness that includes the **real** module, Catch2 module
    and `plugins/camera` unchanged: 3/3 in Release and Debug, warning-free, and nothing fetched
    with the option OFF.
  - clang-format 20 (the pinned binary) is clean on the new sources.
  - **CI then proved the full tree** (`c90d6a8`, every job green):
    - `[opencv]` passed on Linux Debug (910/910), Linux Release, macOS (916/916) and Windows
      (916/916). On Windows, the tests launching at all proves the DLL staging.
    - `flowview --licenses` prints the notice everywhere.
    - The default leg, with `LAIN_CAMERA_OPENCV=OFF`, fetched and configured nothing of OpenCV,
      and stayed at 880/880.
- **Found while building:** the planned `MAP_IMPORTED_CONFIG_{RELWITHDEBINFO,MINSIZEREL}` is
  already done by OpenCV's own config under MSVC (`OPENCV_MAP_IMPORTED_CONFIG`), so the module
  relies on it instead of stating it twice. WORK.md is corrected in place.
- **Sabotages, all caught:**
  - each of the six manifest fields doctored in turn fails configure, naming the field;
  - a missing manifest configures with the warning;
  - an extra module pinned into the test fails exactly the module case.
- **Footprint** (linux-x86_64): six libraries, 19.6 MB. The probe library is 2 KB and the test
  executable 1.2 MB, since OpenCV stays in its DSOs.

### Update 2026-10-02 — optional plugins: wanted, available, built; camera kinds are vocabulary

A default build could not load `render-board.json` (`unknown node kind "boardSpecification"`): the
OpenCV plugin defaulted OFF, and a build with no camera backend registered no camera kinds (ADR-0016),
so the loader dropped those nodes and their edges and `run`/`list` refused the document. Two parts,
decided with the repo owner (the CMake shape; camera kinds as vocabulary); the rule they establish is
in *Conventions to inherit*.

- **The option says what is wanted, `add<Dep>.cmake` says what is available, consumers ask what was
  built.** `addFFmpeg` / `addOpenCV` no longer fail on a platform with no archive: they set
  `LAIN_<DEP>_FOUND FALSE` with a reason, fetch nothing, and the plugin dir returns before defining a
  target. A broken source still fails. Because availability now gates *compilation* rather than the
  *default*, nothing has to know it before `option()`, so no second file per dependency: an earlier
  cut split OpenCV's table into its own module for exactly that, and it is gone.
- **FFmpeg and OpenCV default `${LAIN_NOT_SUBPROJECT}`** — ON top-level (FFmpeg was opt-in), OFF when
  bundled, so a consumer's configure never downloads a binary unasked or fails on that download.
- **Linux FFmpeg needs libva, so it is part of availability.** Both Linux archives are
  `--enable-vaapi`; `libavutil`/`libavcodec` name `libva.so.2` and `libva-drm.so.2` (read from the
  archives' ELF `DT_NEEDED`), and the linker resolves those by soname, so the runtime packages suffice.
  Also new: Windows ARM64 is unavailable for FFmpeg, as it already was for OpenCV.
- **`FeatureSummary`** at the end of a top-level configure, so an explicit ON a platform could not
  honour is visible ("not built, no prebuilt OpenCV for macOS x86_64 (arm64 only)").
- **Consumers moved from the option to `if(TARGET …)`**: flowview's and the tests' DLL staging, and
  `LAIN_EXPECT_CAMERA_BACKEND` (was `_NODES`). The image/data aggregator tests now assert exactly the
  codecs built, both ways, instead of assuming every option stayed ON.
- **Camera kinds are vocabulary** (ADR-0016 amended, reversed): `registerCameraNodes` registers all
  five; `availableCameraNodeKeys` still decides what the menu offers. The nodes already reported a
  missing backend at run time, so no node changed. A camera document now loads whole without a backend,
  and `run` reports `Failed: NoDetector (this build has no board detector …)` instead of refusing.
  `writeDocuments` and the camera vertical lost their hand-registered copies of the camera kinds.
- **Verified:** every table arm of both modules in script mode (found or a reason, never a fetch on
  an unavailable arm; libva arms with and without the sonames); an x86_64-mac cross configure with
  both plugins forced ON configures, says why, builds neither and fetches nothing; the subproject smoke
  resolves both OFF, fetches nothing, prints no summary; a fresh no-flag configure fetches and verifies
  both. `ctest -j8` **1042/1042** all-on and **985/985** with video and camera OFF (the 14 skips
  there are the video-writer, calibrating-fixture and windowed cases); with camera OFF the real binary
  lists `render-board.json` and runs it to "no board renderer", exit 0, where it used to refuse.
  Format-check clean. Two sabotages, both caught: conditional registration again fails four flowview
  cases and the registration case; dropping the plugin's `_FOUND` check fails the x86_64 configure.
  Not run here: the Linux libva arm on a real Linux machine (CI installs `libva-dev`, so its legs take
  the found arm).
- **Still open:** `LAIN_<DEP>_ROOT` cannot rescue a platform with no archive (the table answers
  first); `CMAKE_OSX_ARCHITECTURES=x86_64` on an arm64 Mac still picks the arm64 archive, since
  `CMAKE_SYSTEM_PROCESSOR` stays the host's; menubar.cpp's `nodeCount() == 0` load guard is dead.
  *(The guard is **fixed the same day** — see the next entry.)*

### Update 2026-10-02 — the gui refuses a lossy load too (`LoadResult::intact`)

menubar.cpp's dead guard, fixed. The gui's Open (Open..., Open Recent, Return, the reopen at launch)
guarded a failed load with `nodeCount() == 0`, which no `Graph` can be (it is born with its boundary
pair), so it opened whatever was left of a document that lost nodes, under that document's name. A
plain Save would then have written the smaller graph over the original. M9 sub-slice 0 had already
fixed the identical check in headless `run` / `list`. `ctest -j8` **986/986** Release with video and
camera off (+1); warning-clean, format-check clean. **gui-mode NOT eyeballed.** Full notes in
WORK.md's M9 slice 1, sub-slice 0, *the gui follows*.

- **Decided with the repo owner:**
  - **Refuse and keep the current document.** The alternative was to open it with Save blocked,
    which is new state every save path would have to remember.
  - **The path leaves Open Recent only if the file is gone.** One that needs a newer build stays.
  - **Make Local's identical dead guard is fixed in the same commit** (`groupedit.cpp`). Copying a
    template that lost a node INTO the document is exactly the loss a refusal prevents.
  - **The predicate lives on the result**: `LoadResult::intact()` (no Error), beside `clean()` (no
    issue at all). It reads the library's own severity scale, where Error is defined as a structural
    loss. `runmode`'s `buildOrLoad`, `openGraphPath` and Make Local all ask it, so there is one answer.
- **A recursive template is now a Warning, not an Error**, which is what made refusing safe. The link
  is refused and the document is not: it loads as the same placeholder a missing template leaves (a
  Warning already), and saving it writes back exactly what was read. As an Error, a self-link would
  have made the gui refuse that template and every document linking it. The gui can make one (Edit
  Template…, Add ▸ Linked Group… on the same file, Save) and is the only place to delete it.
  Headless `run` / `list` now run such a document too, as they run one with a missing template.
- **A refused swap must change nothing, and `performSwap` changed two things first.** It maintained the
  return stack and cleared the template cache BEFORE loading. A refused Edit Template… would have left
  a crumb returning to the file you are in, and a refused Return would have dropped the way back. The
  stack is now maintained only after a successful open. The load fills a cache of its own, which
  replaces `ctx.templates` only when accepted. Moving the bookkeeping also made a warning visible that
  never was: Edit Template… from an untitled document noted "never saved", and the open's
  `loadIssues.clear()` then wiped the note.
- **Behaviour change: Edit Template… on a missing template is refused.** It used to open an empty graph
  that Save would turn into the file, but only because the guard was dead.
- **Two sabotages, both caught.** An `intact()` that ignores Errors fails four cases. Putting the
  recursive template back to Error fails the serialize case and the new `[graphio]` one (a self-linking
  template on disk opens through the app's real resolver and saves back byte-identical).
- **Driven through the real gui binary** with a scratch `HOME` (`--frames 20`, exit 0 each time):
  - a missing `lastGraph` is dropped from the session and from Open Recent, where the old guard
    reopened it as an empty graph;
  - a lossy document that still exists is refused and kept;
  - an intact one opens with no error.

  Not yet eyeballed: the transient message, the Issues rows, and the current document staying on
  screen.

### Update 2026-10-02 — a node can fail without throwing (`NodeEvaluation::fail`)

From a user report: in the gui, `render-board.json`'s RenderBoard showed nothing and never updated.
Two causes. **The build had no renderer**: an existing `build/` keeps `LAIN_CAMERA_OPENCV=OFF` (and
FFmpeg OFF) in its CMake cache after the defaults changed, because `option()` never overrides a cached
value — reconfigure with `-DLAIN_CAMERA_OPENCV=ON`, the `IMGUI_REF` trap again. And **the gui hid the
reason**: `board::render` logged it, so in gui-mode it went to stderr and the node just sat empty.
`ctest -j8` **995/995** Release with video and camera off (+9), **1019/1019** with camera on;
warning-clean, format-check clean. **gui-mode NOT eyeballed.** Full notes in WORK.md's *A node could
only report a failure by throwing*.

- **`NodeEvaluation::fail(reason)`**, decided with the repo owner over a throwing node and a
  flowview-only capability row. After the compute, the scheduler clears every output, records the
  reason in the failure record (`Evaluation::failure`, so the canvas marks the node Failed and Issues
  lists it), and the run carries on. ADR-0025 and CONTEXT.md's *Failed* are amended.
- **Such a node is COMPUTED, not owed**, unlike a throw: each stage plans from the whole stale
  closure, so a failure left stale would recompute in every stage of one run. It is kept until an
  edit. `RunControl::failed()` still counts throws only; `finished()` counts it.
- **`board::render` returns `RenderResult {rendering, diagnostics}`** instead of logging, like
  `Pattern::create`. `renderBoard`, `boardSpecification`, and the example `combine` and
  `imageDifference` now fail with their reasons; `lain::camera`, `lain::camera::flow` and
  `flow-example` no longer link `lain::log`.
- **Headless:** the dump prints `failed: <reason>` under the node, and `run` logs each failure to
  stderr in the Issues pane's wording (`collectFailures`). The example test now runs
  `render-board.json` without a camera backend and requires the failure row naming the renderer.
- **Owed:** `LoadImage` / `OpenSequence` still suppress while io logs why; an empty path on a fresh
  palette node needs a decision before they adopt `fail`.

### Update 2026-10-02 — a group node reads as one, and its glyph opens it

From a user report on `bagel.json`: a group could not be told from an ordinary node, and nothing said
that double-clicking opens it. Its only cue was a title colour sitting between the source blue and the
control purple, which a dimmed node loses anyway. `ctest -j8` **1053/1053** (+1), warning-clean,
format-check clean; the gui smoke opens bagel and exits 0. **gui-mode live-verified by the repo owner
2026-10-02.** Full notes in WORK.md's *Group nodes read as groups*.

- **A kind glyph before the title** (squares / chain / list / circular arrow, drawn in the title's
  text colour), from the new driver-free **`groupnav::groupKind`**. The glyph is a button: a click opens
  the group, and its tooltip names the kind and says *double-click the node* too.
- **Groups are olive** now, a hue no other category is near.
- **A double-click that straddles a navigation opens nothing**, so a habitual second click after opening
  by the glyph does not drop a further level (in bagel, into the Map).

### Update 2026-10-05 — `Length` reads and writes units the way `Time` does

`core::Length` was documented as working "as `core::Time` is for seconds", but it grew a named
function per unit (`fromMillimetres`, `millimetres()`, ...), where `Time` has one mechanism over unit
tags. It now has `Time`'s shape: `Length::from<Length::Millimetres>(23.7)` and
`length.as<Length::Millimetres>()`, over `std::ratio` tags of the metre. `ctest -j8` **1047/1047**
Debug and **1054/1054** Release, both with video and camera on (+1 each); warning-clean, format-check clean. `flowview run` on
`render-board.json` writes a PNG byte-identical to the pre-change binary's. Full notes in WORK.md's
*`Length` reads and writes units the way `Time` does*.

- **Two named readings stay, each mirroring `Time`** (decided with the repo owner):
  - `metres()` is the default external reading, as `seconds()` is;
  - `nanometres()` is the exact int64 representation, as `chrono()` is.

  The other per-unit factories and readers are gone, `fromNanometres` included.
- **The old readers were wrong 29% of the time.** They multiplied by `1e-6` / `1e-9`, which no
  double holds exactly, so 28,950 of the 100,001 whole micrometres up to 100 mm read back as a
  neighbour (0.1 mm as 0.099999999999999992). The board serializer writes that reading to disk.
  `as<>` divides, as `std::chrono` does; division is correctly rounded and misses none.
  - Nothing committed was affected: no document holds a `Length`.
  - A new case sweeps the range. Putting the multiply back fails exactly that case.
- **`operator*` scales the count directly**, without the round trip through metres.
- **A recorded sabotage result was false.** Sub-slice 1 said truncation fails the exact-sum case
  because "0.3 mm is 299999.99999999994 nm". `0.3 * 1e6` is exactly 300000. Re-measured, truncation
  is caught by the rounding case and the new sweep instead. Corrected in place in WORK.md.

### Update 2026-10-05 — `Time::from` rounds, as `Length::from` does

Found while comparing the two. `Time::from` was a `duration_cast`, which truncates, so
`from<Milliseconds>(1.001)` came out 1000999 ns: 1,491 of the 100,000 whole microseconds below
100 ms lost a nanosecond. A NaN reaching the cast was undefined behaviour. `ctest -j8` **1048/1048**
Debug (+1) and **1056/1056** Release (+2), both with video and camera on; warning-clean, format-check
clean. Full notes in WORK.md's *`Time::from` rounds, as `Length::from` does*.

- **One routine, two consumers**: `core::detail::roundToInt64` (`details/rounding.h`), which is
  `Length`'s former `toNanometres` moved, so `src/length.cpp` is gone. It rounds to nearest; NaN and
  out-of-range values assert in debug and are made defined in release.
- `Time::operator*` scales the count directly, as `Length`'s does.
- **Frame timestamps move by up to 1 ns, toward the right value**: `video.json`'s manifest changes in
  8 of 24 timestamps, from `0.041666666` to `0.041666667`. Every example's dump is otherwise
  identical to the pre-change binary's.
- The new rounding test was red against the unchanged code first. Making the helper truncate fails
  three cases across both types.

### Update 2026-10-05 — `AxisConvention`'s long bodies move to a `.inl`

`math/axisconvention.h` was 165 lines for an interface of about a dozen declarations. The `switch`
helpers, `rightHanded()`'s parity loop and the `vectorOf` / `basisChange` bodies now live in
`details/axisconvention.inl`, and the header is 102 lines. It is case 3 of the "where a body lives"
rule, since `lain::math` is header-only and most of these bodies are `constexpr`. No behaviour
change: `ctest -j8` **1048/1048** Debug and **1056/1056** Release, unchanged. Full notes in WORK.md's
*`AxisConvention`'s long bodies move to a `.inl`*.

- One-liners, `create()` and the two named conventions stay in the class. The helpers stay private
  members, defined in the `.inl`; only `name()` needs an explicit `inline`.
- A scratch `static_assert` confirmed `create()` and `rightHanded()` still evaluate at compile time.
- Adjacent, in the next commit: `kCameraFrame` in `camera/cameramodel.h` becomes `inline constexpr`.

### Update 2026-10-05 — each distortion model carries its own algorithm

A distortion struct held only its coefficients. Its projection lived in `if constexpr` chains in
`details/projection.inl`, its fold scan and name in `cameramodel.cpp`, and its coefficient order in
three more places. Now each struct in `camera/distortion.h` declares `kDisplayName`,
`kCoefficientNames`, `coefficients()`, `project<T>`, `unproject<T>` and `domain()`. The free
`project` / `unproject` are the intrinsics plus one visit, and name no model. ADR-0016 is amended in
place, since it already said each variant *defines* its projection. `ctest -j8` **1050/1050** Debug
and **1058/1058** Release with video and camera on (+2 each); warning-clean, format-check clean.
Full notes in WORK.md's *Each distortion model carries its own algorithm*.

- **Decided with the repo owner:** members rather than free overloads, and the descriptive facts as
  well as the algorithm.
- **Three catch-all `else`s are gone.** Each would have compiled for a seventh model and described it
  wrongly; `displayName`'s returned "Kannala-Brandt 4" for anything it didn't name. A model missing
  a member now fails the build at the visit, which a sabotage confirmed.
- **`project` takes the 3D direction** (Kannala-Brandt needs `atan2(rho, z)`) and every member is
  total: each refuses `z <= 0` itself.
- **The raw formulas (`distort`, `undistort`, `distortedAngle`) are public**, a deviation from the
  plan, because the shared forward helpers call `distort`. The structs stay aggregates.
- **The OpenCV estimator's `coefficientsOf` is deliberately kept**: it is OpenCV's order, which
  happens to equal lain's.

### Update 2026-10-12 — M9 slice 4 planned: targetless calibration and the graph fallback (nothing built yet)

Slice 4 of M9 is planned as **thirteen sub-slices (0 to 12), one commit each**, in WORK.md's M9 build
order. ADR-0016 and ADR-0017 are amended in place. CONTEXT.md gains *focal sweep*, *held parameter*,
*held-out frame* and *accepted attempt*, and amends *Targetless calibration*, *Feature-track
extraction*, *Calibration method* and *Calibration attempt*.

- **Decided with the repo owner:**
  - **The fallback is gates and small nodes.** Each method stays one node, and its footage passes
    through a Gate. `calibrationMethod` and `nextAttempt` switch the gates. `calibrationAttempts`
    combines the attempts made and names the accepted one. A method that is not needed never runs
    (ADR-0007 suppression), and editing one method's setting re-runs only its node. It ships as an
    example document. One fallback node and an attempt chain were refused.
  - **With no imported model, a focal sweep starts the reconstruction**: log-spaced candidate focal
    lengths tried through `relativePose` on the best-verified frame pairs, each scored at one pixel
    threshold so a long candidate is not favoured.
  - **The principal point is held by default.** A held parameter's variation is reported held, never
    zero.
  - **No standalone extraction node yet.** Calibration's extraction differs from the fixed rig's, so
    its trigger is restated rather than fired.
- **Taken by precedent, or found by the design review:**
  - **Every sampled frame of the moving camera is its own view**, and frame pairs are verified by a
    **fundamental matrix** that lain measures (Sampson distance), since there is no model yet.
  - **The refiner seam moves from `registration` to `lain::camera::refinement`.** Calibration may not
    depend on its sibling, as with slice 2's board pose seam. The seam gains cameras sharing one model
    and free model parameters. Registration never frees one.
  - **The projection kernels become scalar-generic in intrinsics and coefficients**, so Ceres
    differentiates the one implementation. A model whose Jet derivatives fail is not declared
    estimatable.
  - **Validation is on held-out frames**, board calibration's held-out views with the scene in place of
    the board. The calibration criteria keep their meaning, and `reconstruction-targetless/1` differs
    only in its numbers, measured in sub-slice 10.
  - **Acceptance is Ready under the attempt's own profile**, decided by one `calibration::accepts`.
  - **Targetless registration's incremental reconstruction is lifted** into a shared private module,
    held to a golden targetless registration report.
  - **The calibration report becomes method-neutral first**, held to a golden board calibration
    report, as registration's did in slice 3.
- **Found in the code:**
  - `calibration::Request` carries a board detection request.
  - The calibration report is board-shaped.
  - `distortion.h`'s coefficients stay double.
  - `extractTracks` needs a model per camera, and its track identities would collide across one
    camera's frames.
  - `RenderedSource` has one pose.
  - flowview has no enum boundary binder.

### Update 2026-10-29 — M15's waiting decisions settled

Decisions on what M15 found, taken with the repo owner before slice 9 is planned. Full notes beside
each finding in WORK.md M15 (*Sub-slice 7a built*, *Sub-slice 5e built*).

- **A registration verdict takes the camera models as given — documented, not changed.** With its
  uncalibrated nominal models, `three-poles` registers Ready, with the same held-out residual as with
  the true ones (0.0549 mrad), while 14 times further from the truth. Ready means "consistent with the
  models it was given"; a comparison against an independent truth (`compareRigs`) is what sees a wrong
  model. Capping the verdict unless every model was calibrated is refused until a real rig mixes the
  two, since a camera model records no origin. ADR-0016 is amended; `registration/fitness.h`,
  `report.h` and CONTEXT.md's *Registration fitness* say so.
- **The size estimate's `floor` is renamed `flat`**: a flat mid-grey picture with the capture's noise,
  a reference and not a bound, since a dark scene writes less (49.9 MB against 52.0 MB). The estimate
  now reads "at most {ceiling} on disk (uncompressed); about {flat} if every frame were a flat
  mid-grey picture". A true floor would be a black frame, which says nothing.
- **The CPU device gets every logical core**, not half (`lain::app`'s `runtimeConfig()`; ADR-0028
  amended). The pool sits idle while the CPU device renders a capture, so half only shrank the
  bottleneck: 7 threads took 8.0 s and 14 took 6.3 s, and a 4-core CI runner gave the device 2.
  Measured on `three-poles.json` here, twice each: 21.0 s rendering at half, 15.2 s at every core.
  **Owed:** CI's render step time against slice 7's 68 s to 221 s.
- **Noted for later, not M15's: window renders that run asynchronously** in `Application::run`, the
  repo owner's idea. WORK.md's Outstanding index, *Deferred — the app harness*, lists what it has to
  decide first (GLFW's main-thread events, ImGui's single-threaded contract, the shared queue lock,
  the teardown order).

### Update 2026-10-29 — flowview's Quit and close button ask before throwing work away

The last known defect, fixed with M15 8e's close veto, the way rigplan uses it. File ▸ Quit, Ctrl+Q and
the window's close button exited at once, while New and Open asked. `DocumentSwap` gains `Quit`, and
`MenuBarPane::requestQuit(ctx, app)` is what all three call (the close button through
`MainWindow::onCloseRequested`). With unsaved work the guard holds it and the modal asks; Save or
Discard quits, Cancel stays, and a cancelled Save panel cancels the quit. `performSwap` stays New and
Open's, and the modal ends a held Quit through the Application it is now handed. Warning-clean in both
configurations; flowview's, the editor's and the app's suites **212/212**, and the whole suite with
video, camera and Ceres off **1484/1484**; the `--frames` smokes exit 0. The menu bar is reachable only
with the whole app, so it is not under ctest. **gui-mode live-verified by the repo owner 2026-10-29**,
which discharges slice 0's owed check of flowview's unsaved-changes modal. Full notes in WORK.md's
*Known defects*.

### Update 2026-10-29 — M15 slice 8 live-verified: a landing bake stalled the window, and a drag baked every size

The repo owner walked slice 8's gui-mode checklist on Metal, and **all twelve steps pass**, so **slice 8
is live-verified**. Step 12 (quit while assets bake) found two defects first, both fixed. Step 11
discharges slice 0's live check of the unsaved-changes modal, in rigplan. `ctest -j8` **1625/1625**
Release with video, camera and Ceres on (+3), and **1484/1484** with all three off (+3); warning-clean,
format-check clean. After the fix, Quit waits for the running bakes and exits cleanly, as designed.
Full notes in WORK.md's *After 8f*.

- **Step 10 needed explaining, not fixing.** The row is about the camera's optical centre: 33.7 mm of
  three-poles' pole axes, not the body touching the pole. The step's text now says so.
- **three-poles' bakes are too quick to quit before** (0.20 s for its floor), so a 20 m room was used
  (1.83 s at the 8192 cap), and the window stalled as those bakes landed.
- **The stall was the conversion to the device's 8-bit sRGB, on the frame loop.** A temporary probe
  timed one 8192² upload on Metal at 0.70 to 0.75 s from a linear bake against 0.015 to 0.029 s
  already stored.
  - **`render::storedTexture`** makes that conversion public, and an upload of a stored texture neither
    converts nor copies it.
  - **`rigplan::scene`'s jobs make every picture, bake and board drawing stored**, on the pool, so the
    device holds the same texels and a landing texture costs the frame its copy.
  - **Measured in the real binary** on a 20 m room: the longest of 300 frames fell from 1.85 s to
    0.033 s.
- **A drag posted a bake every frame, each run to its end.** Decided with the repo owner: **no asset job
  is posted during a gesture**; it is owed and posted when the gesture ends, the boundary undo commits
  at. The rule is a driver-free unit, `AssetJobs` (`apps/rigplan/src/assetjobs.{h,cpp}`), with two cases.
- **Five sabotages, all caught.** One first failed to build under `-Werror`, so the old binary answered
  until it was redone.
- **CI is green on all eight jobs**: [run 40798806425](https://github.com/luckyneko/lain/actions/runs/40798806425)
  on `b41eb90` (slice 8 as built, the first CI build of ImGuizmo and rigplan's gui on Linux and
  Windows), and [run 40827876486](https://github.com/luckyneko/lain/actions/runs/40827876486) on
  `7f4b6a4` (these fixes and flowview's Quit fix), 1625/1625 on Linux, macOS and Windows Release.

### Update 2026-10-28 — M15 slice 8, sub-slice 8f built: the Viewport (**slice 8 BUILT; gui-mode checklist owed**, *live-verified 2026-10-29, see above*)

The plan drawn on the device in rigplan's Viewport pane, with its overlays, the floor grid and
ImGuizmo. `ctest -j8` **1622/1622** Release with video, camera and Ceres on (+3), and **1481/1481** with all three
off (+3); warning-clean, format-check clean. **gui-mode NOT driven by hand**: the input is the repo
owner's 12-step checklist (WORK.md's *Slice 8 planned*, at its end), which is now owed. Full notes in
WORK.md's *Sub-slice 8f built*.

- **`ViewportPane`** holds the renderer, the plan's `GpuScene` and a `render::Viewport`, and releases
  them before the device goes.
  - Each frame it keeps what the asset jobs made, never waiting.
  - It builds the scene again when the plan, the frame shown or the assets changed, posting missing
    jobs on the process pool.
  - It records in the pre-pass and is shown through 8a's frame-resolved texture.
- **Found by looking at the picture: a room hid the rig.** Framing all of a plan puts the eye outside
  the room, and its two-sided walls showed a closed box. A room is now a **dollhouse**: each surface the
  eye is outside of is left out (`cutaway`), and the device's copy changes only when that set does.
  CONTEXT.md's *Viewport* and ADR-0028 are amended.
- **Looked at through a temporary readback probe** (removed; `screencapture` is refused here): the
  dollhouse with the grid, poles, frustums, cones and the board's path, and a selected, framed camera
  in its highlight, with the ChArUco board drawn.
- **Measured on Metal:** three-poles' six bakes land within 20 frames (0.4 s), at about 8 ms a frame.
  Quitting while they bake waits for them and exits 0.
- **The View menu's Frame All (Home), Frame Selected (F), Preview Light and Frustum Depth** land here.
  A plan opened or made is framed whole.
- **Owed next:** the gui-mode checklist, then flowview's Quit fix (*Known defects*), then slice 9.
  *(The checklist passed 2026-10-29, and the Quit fix landed the same day; see the entries above.)*

### Update 2026-10-28 — M15 slice 8, sub-slice 8e built: the editor's shell, and `lain::app`'s close veto

`rigplan [plan]` opens the editor: the menu bar, the Outliner, the Inspector, Issues and a status bar,
docked round the Viewport's place, over the open plan. `ctest -j8` **1619/1619** Release with video, camera
and Ceres on (+6), and **1478/1478** with all three off (+6); warning-clean, format-check clean. The windowed
`[app]` cases ran on Metal. **gui-mode NOT driven by hand**: `--frames` smokes exit 0, and the panes
run headless. Full notes in WORK.md's *Sub-slice 8e built*.

- **`lain::app` gains `WindowDelegate::onCloseRequested`**, true by default. On false the loop clears
  GLFW's close flag and carries on. It also gains `Window::setTitle`. The veto's case is windowed and
  opt-in, like the other windowed cases, since a window needs a display: a deviation from the plan's
  "headless". flowview's Quit fix, which this unblocks, is still owed (*Known defects*). *(Fixed
  2026-10-29.)*
- **New, Open, Open Recent, Quit and the close button all go through `DocumentGuard`.** A cancelled or
  failed Save cancels the request.
- **`require_subcommand(1)` is gone**, so `rigplan --licenses` works again (8a's finding), and
  `rigplan render ...` is still the subcommand.
- **The panes live in `rigplan-tested`** and run in a headless ImGui frame: every pane, 16 selections,
  each frame held to ImGui's count of recovered errors, which Release keeps though its asserts are off.
  Sabotage: a `TreePop` left out fails every frame.
- **The session file is a path the context holds**, empty in a test, so no test writes `~/.rigplan`.
- **Deviation:** the View menu has Reset Layout alone. Frame All, Frame Selected, Preview Light and
  Frustum Depth come with the Viewport in 8f.
- **Next is 8f**, the Viewport pane, then the repo owner's gui-mode checklist.

### Update 2026-10-28 — M15 slice 8, sub-slice 8d built: rigplan's editor, driver-free

Seven units in `apps/rigplan/src`, compiled once into the OBJECT library `rigplan-tested`, which
`rigplan` and `test-rigplan-app` both link: document, selection, edits, orbit, picking, overlays and the
gizmo bridge. `ctest -j8` **1613/1613** Release with video, camera and Ceres on (+30), and **1472/1472** with all
three off (+30); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 8d built*.

- **The document applies ADR-0027 at the file and nowhere else.** `readPlan` is `render`'s old
  `loadPlan`, so both read a plan through one routine. An undo snapshot is `rigPlanToValue`, which takes
  no folder. `three-poles.json` opened and saved is byte for byte the committed file.
- **A selection is by identity**, so it survives an undo that reorders a list, and `exists` says when
  the undo removed its object.
- **Each edit changes the plan whole or refuses**, by `check()`'s rules (a camera's name a valid folder
  name and unique ignoring case). Added beyond the plan:
  - a duplicated mount copies the cameras and lights on it;
  - a key is duplicated one frame on;
  - deleting the reference camera clears the reference.
- **Deviations:**
  - `apply` takes the gizmo's mode, so a move never rewrites an aim the Inspector typed;
  - overlays and picking take the render scene, to outline and hit surfaces and props;
  - a fixed point gets a marker, so an empty one can be seen and clicked;
  - `selection.h` has a `.cpp`.
- **Taken as a default, to object to:** the scroll wheel's zoom is a dolly, a tenth of the way per
  step, and the field of view stays 50°.
- **ImGuizmo's matrices are held to ImGuizmo's own code**: its `ComputeMouseRay`, through where lain
  projects a point, passes through the point, on a pane of half the view's pixels.
- **Found by the first run:** a click on a plane's centre fell between its two triangles by rounding.
  Triangle edges now have 1e-9 of slack.
- **Found by a sabotage:** the undo case passed with relativized snapshots, since both its undos landed
  on the unrelativized baseline. It now checks the first undo. Eight sabotages, all caught.
- **Next is 8e**, the app shell, then 8f, the Viewport pane, then the repo owner's gui-mode checklist.

### Update 2026-10-28 — M15 slice 8, sub-slice 8c built: the nominal layout, `aimOf`, and pooled asset jobs

What an editor needs of `lain::rigplan` and its scene. `ctest -j8` **1583/1583** Release with video,
camera and Ceres on (+13), and **1442/1442** with all three off (+12); warning-clean, format-check clean.
Full notes in WORK.md's *Sub-slice 8c built*.

- **`nominalLayout(plan)`**: each mount's, camera's and light's planned pose, and each camera's nominal
  model, each optional, never refusing the whole. It equals `realise()` at scales 0, since the two share
  `mountPose` and `cameraPose`. It has no reasons of its own (a deviation): every absence is a `check()`
  problem.
- **`aimOf(rotation)`**, the aim convention's inverse; straight up or down, the roll is 0.
- **`PlanDiagnostic::owner`**, through one `report()` helper in `check()`. **`SceneResult::owners`**, and
  a `renderScene` over a nominal layout that leaves out what has no pose. A surface waiting on its
  asset is matte mid grey.
- **Asset jobs**: `sceneGeometry`, `missingAssets`, `addAsset`, `pruneAssets`. `loadSceneAssets` is now
  rounds of those jobs, so the capture writer and the editor share one path.
  - **Added:** a failure is remembered under its job's key. A sabotage showed that without it
    `loadSceneAssets` hangs.
  - **Added:** props and boards record what they were made from, so a changed file or pattern is drawn
    again and shown as it was meanwhile.
- **OpenCV's board renderer draws the same board on another thread**, byte for byte (`test-camera-capture`).
  Nothing had drawn one off the main thread before.
- Seven sabotages, all caught; one first failed to build and was redone.

### Update 2026-10-28 — M15 slice 8, sub-slice 8b built: a grid on a plane, drawn in shading

`render::Grid` and `Viewport::record(..., lines, grid)` (a `std::optional`, no default). `ctest -j8`
**1570/1570** Release with video, camera and Ceres on, and **1430/1430** with all three off (+5 each);
warning-clean, format-check clean. Every case runs on Metal and on the CPU device. Full notes in WORK.md's
*Sub-slice 8b built*; ADR-0028's grid bullet is amended.

- **Two paths, one line.** A surface lying on the plane (within 0.1 mm, and parallel) mixes the lines
  into its colour in `scene.frag`. A backdrop quad on the plane draws them elsewhere. Both mix in the
  display encoding.
- **Deviation from the plan: the backdrop is drawn after the scene.** Drawn first with no depth, a
  surface behind a grid point covered it, which is wrong from below. Now it is depth-tested, pushed back
  1e-4 so a surface on the plane always wins, and blended with alpha equal to the coverage.
- **Measured:** the two paths agree to within 0.0053 of coverage on Metal and 0.032 on the CPU device.
  Overlay lines lose up to three quarters. **I first reported 0**: Catch2 wrapped `0.0319149`, and my
  grep caught `0.` alone.
- **`frameConstants` takes a `const Grid*`**; `render()` and `capture()` pass none, and a test holds
  them to it.
- **A sabotage showed the wall case tested nothing**: its points were where the grid had faded out. It
  now has a positive control. Five sabotages, all caught.

### Update 2026-10-28 — M15 slice 8, sub-slice 8a built: a pane shows a texture it did not upload; ImGuizmo lands

`gui::FrameTexture`, made by `Context::frameTexture(provider)` and resolved by the now-required
`Context::render(cmd, frameIndex)`; ImGuizmo pinned at `105e4fd` as `lain::gui::gizmo`. `ctest -j8`
**1565/1565** Release with video, camera and Ceres on, and **1425/1425** with all three off (+6 each);
warning-clean, format-check clean. The four windowed cases ran on Metal (`LAIN_GUI_SMOKE=1`). Full notes
in WORK.md's *Sub-slice 8a built*.

- **A pane draws a stand-in id**, and `render` swaps it for that frame's descriptor just before ImGui
  records, after the pre-pass. The rewrite rule is `detail::resolveStandIns`, tested headless. The
  descriptor cache holds its textures, and an empty answer draws a 1 x 1 transparent texture.
- **`gizmo::BeginFrame` is the pane's to call**, not the Context's: it opens an invisible full-viewport
  window.
- **On MoltenVK a stale descriptor draws the right picture**: with the cache holding nothing, the new
  texture's object lands where the destroyed one's was. So the case reads the resolved descriptor ids
  back from ImGui's draw data, and requires that an address really came back.
- **The headless ImGui cases wrote `imgui.ini` into the working folder**, carrying window sizes from one
  ctest process to the next. All five now set `IniFilename = nullptr`.
- **Older than 8a: `rigplan --licenses` refuses** ("A subcommand is required") since 5e; 8e fixes it.
- Five sabotages, all caught. Owed: CI's first build of ImGuizmo on Linux and Windows. *(Green on all
  eight jobs, run 40798806425 on `b41eb90`.)*

### Update 2026-10-28 — M15 slice 8 planned: the gui, part 1 (nothing built yet)

Slice 8 is planned as **six commits, 8a to 8f**, in WORK.md's *Slice 8 planned*. 8a to 8d are headless
or driver-free; 8e and 8f are gui code, live-verified by the repo owner against a 12-step checklist.
Build-order item 8 and the Outstanding index are amended in place, and so are ADR-0028's viewport
bullet and CONTEXT.md's *Viewport*.

- **Decided with the repo owner:**
  - **Navigation is Alt-drag, Maya style**: orbit as a turntable with Z up, pan, dolly, scroll to zoom,
    and F and Home to frame. Each has a trackpad chord, and plain left-click stays free for picking
    and the gizmo. GLFW delivers no pinch, so a trackpad zooms by its scroll.
  - **A mover's keys are a table in the Inspector, and its path is drawn.** The selected key's frame
    is the frame shown. The scrubber and playback are slice 9's.
  - **A plan's own specifications are edited in the Inspector.** A New plan starts with the test
    plan's four. The catalogue stays slice 9's.
  - **The gui never waits on asset work** (M14's rule carried over). Bakes, board drawings and mesh
    loads run on the pool. Measured: three-poles takes 0.8 s to prepare on Metal, so a room resized
    on the frame loop would stall every frame of the drag.
  - **A pane shows a viewport's texture through `lain::gui`, resolved when the frame is recorded**
    (`Context::render(cmd, frameIndex)`). archimedes does not change.
  - **The Viewport draws the nominal plan only**, so the gizmo edits what is drawn.
  - **The view is not kept**: a plan opens framed, and the document stays at version 1.
- **Found while planning:**
  - **`realise()` refuses a whole plan on any `check()` problem**, so a camera dragged into its pole
    would blank the Viewport. 8c adds `nominalLayout`, which never refuses the whole.
  - **`renderScene` draws a surface whose asset is missing white**; 8c makes it mid grey.
  - **`Viewport::record` reallocates on resize**, which is why the pane's texture is resolved at record.
  - **`PlanDiagnostic` names no owner**; 8c adds one, so an Issues row can select it.
  - **flowview loses unsaved work on Quit**: the menu item, Ctrl+Q and the window's close button all
    exit unasked, and `lain::app` gives a delegate no veto. 8e adds `WindowDelegate::onCloseRequested`,
    and flowview's fix is its own owed commit (Outstanding index, *Known defects*). *(Fixed
    2026-10-29.)*
  - **A floor grid cannot be overlay lines** (3b/3d): 8b draws it in shading, with a depthless
    backdrop, so no capture can ever draw one. *(Built with the backdrop after the scene, depth-tested
    and blended instead; see the 8b entry above.)*

### Update 2026-10-27 — feature/M15's CI run after slice 7: `run` writes text the same on every platform

[Run 40493396763](https://github.com/luckyneko/lain/actions/runs/40493396763) on `2977ed0`, the first
since slice 5. Seven jobs were green, and Linux ran the new render, register and score step through the
shipped binaries. Windows failed two tests, M9's board rig vertical and 7b's capture vertical, in one
check: the comparison line `run` wrote must be the library's, byte for byte. Slice 6c added that check,
and 7b copied it. **The fix is green on all eight jobs**
([run 40497652103](https://github.com/luckyneko/lain/actions/runs/40497652103)), 1559/1559 on Linux,
macOS and Windows. Full notes in WORK.md's *feature/M15's CI run after slice 7*.

- **The cause was `run`.** It wrote a scalar or text output through a text-mode `std::ofstream`, so on
  Windows the line ended in CRLF. Every other output it writes goes through io, whose local stream is
  binary.
- **Fixed by writing it through `io::write` too.** That branch dated from M10 slice 2, and nothing had
  compared its bytes on Windows before. macOS writes LF either way, so only the Windows leg can check
  the fix.
- **The new step ran through the shipped binaries on all three platforms**, on the CPU device. Every
  platform wrote the same frame bytes and registered Ready. Linux and Windows scored 0.676 mrad and
  0.394 mm from the truth, and macOS 0.674 and 0.391, well inside the bounds. Rendering took 68 s to
  221 s on the runners.

### Update 2026-10-27 — M15 slice 7, sub-slice 7b built: the vertical and CI (**slice 7 COMPLETE**)

The committed example rig plan rendered, registered and scored end to end, as a user would.
`ctest -j8` **1559/1559** with video, camera and Ceres on (+1), **1419/1419** with all three off
(unchanged); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 7b built*.
*(CI's first run of it, [run 40493396763](https://github.com/luckyneko/lain/actions/runs/40493396763),
passed the new step on Linux and failed two Windows tests on a CRLF `run` wrote; see the next entry.)*

- **`test-flowview-capture`** renders `three-poles.json` with `writeCapture` on every device the
  machine has, then runs `register.json` twice (`truth/models`, `nominal/`) and `compare-rigs.json`
  for each, all through `runGraph`.
  - With the true models it requires Ready and M9's rig bounds (measured 0.68 mrad and 0.40 mm).
  - With the nominal models it requires the worst rotation and centre each more than 5 times worse
    (measured 14 and 12 times).
  - It also requires that the truth was derived again, since the registration takes another reference.
  - Without a refiner it requires `Failed: NoRefiner` and no rig.
  - It runs in Release only, in 62 s for both devices here.
- **Deviation: the two flowview test executables share an OBJECT library, `flowview-tested`**, of the
  production sources, compiled once. That replaces the planned shared list, so a copy cannot drift.
- **CI renders on the CPU device on every Release leg**, then registers and scores. A leg with no
  refiner must report `Failed: NoRefiner`; a leg with everything off must refuse the render, naming
  the board renderer. The step's script passed here on all three configurations.
- **`synthetic-capture`** renders the example into `build/synthetic-capture`, with a README of the
  flowview commands, which were run as written.
- **Four sabotages, all caught.**
- **The new linker warns about duplicate static libraries** on many executables this slice did not
  touch. Recorded, not chased.

### Update 2026-10-27 — M15 slice 7, sub-slice 7a built: the example rig plan

`apps/rigplan/examples/three-poles.json`, written by a new `rigplan-examples` builder (regenerated by
the `rig-plans` target, since flowview owns `examples`), with a README and a driver-free
`test-rigplan-app`. Committing it **froze the rig plan document at version 1**. `ctest -j8`
**1558/1558** with video, camera and Ceres on and **1419/1419** with all three off (+3 each);
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 7a built*.

- **The plan:** a procedural room; three poles on a 1 m arc round a working point, two monochrome
  1280 x 720, f = 2000 px cameras each (0.9 m up, 1.6 m down) and a spot; M9's board on a 24-frame
  path. Every camera sees all 24 corners in every frame. Stored at intrinsic scale 1, noise 1, seed 15.
- **The freeze review changed nothing**: units in datasheet keys, plain metres for geometry, stable
  variant keys, enums by name.
- **The probe, through the shipped binaries, on both devices** (Metal 4.0 s, the CPU device 21.5 s for
  144 frames):
  - with `truth/models`, Ready, 0.05% in scale, 0.68 mrad and 0.40 mm from the truth;
  - with `nominal/`, 9.86 mrad and 4.82 mm, 14 and 12 times worse.

  The two devices agree to about 1%.
- **Found, for the repo owner:**
  - **`registration/1` calls the nominal registration Ready**, with the same held-out residual as the
    true one (0.0549 mrad): board poses absorb an intrinsics error, as M9 slice 1's sub-slice 8b found
    for calibration;
  - **the size estimate's floor is not a floor for a dark scene**: 49.9 MB written against 52.0 MB.
- **Two sabotages, both caught.**
- **macOS 27.2 moved the toolchain** (SDK 27.0, Apple clang 21). `build/` and `build-off/` now name
  the unversioned `MacOSX.sdk`. lain is warning-clean under the new compiler; fmt 10.2.1's own sources
  hit a libc++ deprecation that a later libc++ will make an error.

### Update 2026-10-26 — M15 slice 7 planned: the headless vertical (nothing built yet)

Slice 7 is planned as **two commits, 7a and 7b**, in WORK.md's *Slice 7 planned*. Build-order item 7
and the Outstanding index are amended in place.

- **Decided with the repo owner:**
  - **The board is M9's** (7 x 5 squares of 24 mm, ArUco 5x5_100), so `register.json` runs unchanged
    and no M9 document changes. The working volume is small (cameras about 0.9 m from the board). A
    room-scale board, with a board input to `register.json`, waits for the first rig plan whose board
    is not M9's.
  - **One capture at intrinsic scale 1, bound two ways**: `truth/models` should register near-exactly,
    and `nominal/` should be measurably worse. The build order's "at scale 0" claim is restated,
    since slice 1 already proves scale 0 is exactly nominal.
  - **The vertical is a new `test-flowview-capture`**: `writeCapture` on every device kind, then the
    committed documents through `runGraph`, skipping in Debug. `test-flowview` stays driver-free, and
    the shipped binaries are CI's step.
- **7a**: `apps/rigplan/examples/` and a `rigplan-examples` builder. The scene is a room, three poles
  of two cameras (the test plan's 1280 x 720, f = 2000 px camera, which 5c already detected the board
  through), and M9's board on a 24-frame path. The plan stores intrinsic 1, noise 1 and a fixed seed.
  The scene's numbers come from a probe on both devices first. Committing the example **freezes the
  rig plan document at version 1**: after that, a shape change is a version bump and a migrator.
- **7b**: the vertical's two registrations and two comparisons. A CI step runs the shipped binaries on
  the CPU device, and on the leg with everything off checks the refusal that names the board renderer.
  A `synthetic-capture` build target is added.
- **Recorded, not fixed:** slice 6's two findings. A one-frame camera is named by its file, which is
  owed to M9's binder beside 5e's sorted-position pairing. There is no gui editor for a rig input,
  which belongs to slices 8 and 9.

### Update 2026-10-26 — M15 slice 6, sub-slice 6c built: `compareRigs` and `compareModels` (**slice 6 COMPLETE**)

The two comparisons as `camera::flow` nodes, with `compare-rigs.json` and `compare-models.json`.
`ctest -j8` **1555/1555** with video, camera and Ceres on and **1416/1416** with all three off (+2
each); warning-clean, format-check clean. **gui-mode NOT eyeballed**: added to the camera examples'
eyeball in the Outstanding index. Full notes in WORK.md's *Sub-slice 6c built*.

- **A refused comparison fails the node with its reason**, so `run` logs it and writes nothing; a
  comparison's port and `run`'s output are its one line.
- **Offered beside what makes their inputs** (`canUseCalibration`, `canUseRegistration`), not always:
  three tests take an empty camera menu to mean no backend. Both register in every build.
- **`compareRigs` is the first consumer of a registered rig**, discharging M9's deferred item.
- **The camera footage now carries its truths** (`board/truth.json`, each rig's `truth.json`), an
  addition, so both documents can be run on drawn footage; its README gives the commands.
- **Through the real binary**: the board rig 0.36 mrad and 0.08 mm from its truth; the targetless rig,
  relative to cam1 against a truth relative to cam0, derived again and scaled by 4.18 onto metres; the
  board camera's model 0.38 px mean, 0.81 px worst. Both refusals fail the node, naming why.
- **Two sabotages, both caught.**

### Update 2026-10-26 — M15 slice 6, sub-slice 6b built: the tests compare through the library

The rig and model comparisons the tests made by hand go through `camera::compareRigs` and
`camera::compareModels`. `ctest -j8` **1553/1553** with video, camera and Ceres on and **1414/1414**
with all three off, unchanged; warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice
6b built*.

- **The rig verticals change by zero numbers**, measured rather than argued: every asserted scale,
  worst rotation and worst centre at all seven sites, printed as hexfloats on the unchanged tree and
  the changed one, is identical. A sabotage shows the check can see a change: through acos, all seven
  worst rotations differ. `rigcompare.h` keeps only the truth (`truthRig`, `rebased`, `truthReport`).
- **The model numbers move on purpose**, since the grid now reaches the edges: the board's calibration
  1.19 to 1.44 px at worst, everything else by at most 0.19 px, no bound moved. Comments this machine
  can re-measure are updated; the Linux-recorded targetless ones say which grid they are on.
- **The fixture's manufacturer comparison reports a model comparison**, still never gating.
- **Two cases now check what they assumed**: the both-methods and targetless end-to-end cases took the
  truth relative to cam01 without asking the registration which reference it chose.

### Update 2026-10-26 — M15 slice 6 planned; 6a built: `lain/camera/comparison.h`

Slice 6 (rig and model comparison promoted to `lain::camera`, and their nodes) is three commits, 6a to
6c; the plan is in WORK.md's *Slice 6 planned*. **6a is built**: `camera::compareModels` and
`camera::compareRigs`. `ctest -j8` **1553/1553** Release with video, camera and Ceres on (+11), and
**1414/1414** with all three off (+11); warning-clean, format-check clean. Full notes in WORK.md's
*Sub-slice 6a built*; CONTEXT.md's *Model comparison* and *Rig comparison* and ADR-0029's
consequence are amended.

- **Decided with the repo owner:**
  - **Model comparison's grid reaches the edges**: every 16th pixel centre and the last row and
    column. The three test helpers it replaces sampled only the top-left corner, so the model numbers
    move and are re-measured in 6b; the zero-numbers claim is the rig verticals';
  - rigs with different cameras compare the shared ones and name the rest;
  - example documents land in slice 6;
  - the fixture's manufacturer comparison gains a model comparison.
- **`compareModels`** sends the expected model's rays through the found one, and says so, since the
  measure is not symmetric. A ray the found model cannot project makes the comparison infinite.
- **`compareRigs`** derives an expected rig of another reference again, never rebasing the found one,
  with the reference the identity exactly, as `rigplan::rigRelativeTo` does (camera cannot call it).
- **`rotationBetween` moved to `camera::detail`**, the arithmetic rigcompare.h already used.
- **Three sabotages, all caught.** One first failed to build, so the previous binary answered until it
  was redone.

### Update 2026-10-26 — feature/M15's CI run after slice 5: the sensor's output table is decided without libm

[Run 40259562592](https://github.com/luckyneko/lain/actions/runs/40259562592) on `5637dcf`: macOS green,
every Linux leg failed two tests, Windows failed to build `apps/rigplan`. All three findings are fixed in
one commit. `ctest -j8` **1542/1542** Release with video, camera and Ceres on (+1), **1403/1403** with all
three off (+1); warning-clean, format-check clean. **The fixes are green on all eight jobs**
([run 40265665844](https://github.com/luckyneko/lain/actions/runs/40265665844)), so the sensor's tables
and recordings are proved identical on Linux, macOS and Windows. Full notes in
WORK.md's *feature/M15's CI run after slice 5*; ADR-0029 is amended.

- **The 16-bit sRGB output table differed between macOS and Linux**, which is what 5a's pins were for.
  It was lain::image's float curve rounded, and Apple's and glibc's `powf` round some inputs
  differently. 904 of its 65,536 entries sit within two float ulps of a rounding boundary.
- **Decided with the repo owner: exact by construction.** The table is decided without libm:
  - linear segments and breakpoints are compared exactly in integers, an exact tie rounding up;
  - a power segment compares x^5 with w^12 (sRGB's 5/12) or x^9 with w^20 (BT.709's 9/20), by repeated
    IEEE multiplication, monotone in the code, so a search from the float estimate lands on one answer.
- **A new case holds every table lain can build to the curve correctly rounded** (786,420 entries).
  813 entries differ from the float curve's rounding, and 596 are exact ties, all BT.709's. Only the
  16-bit sRGB pin moved.
- **The truth rig test's angle** used `2 acos(|a · b|)`, which reads one ulp as 3e-8 rad. It now uses
  `atan2` of the relative rotation; the library was right.
- **`View` and `CaptureView`'s `near` / `far` are now `nearPlane` / `farPlane`**, since `<windows.h>`
  (through CLI11) defines both as empty macros. Checked by compiling the app with `-Dnear= -Dfar=`.
- **Five sabotages; four caught**, and the fifth (an sRGB tie rounded down) correctly finds no tie to
  break.

### Update 2026-10-26 — M15 slice 5, sub-slice 5e built: the capture writer and `rigplan render` (**slice 5 COMPLETE**)

A rig plan's synthetic capture now reaches disk: `lain::rigplan::capture`, `capture.json` in
`rigplan::serialize`, and `apps/rigplan`, lain's second app. `ctest -j8` **1541/1541** Release with
video, camera and Ceres on (+11), **1402/1402** with all three off (+11); warning-clean, format-check
clean. Full notes in WORK.md's *Sub-slice 5e built*; ADR-0028 and CONTEXT.md's *Synthetic capture*
are amended.

- **`writeCapture(plan, device, options)`, in four steps:**
  1. Refuse before writing anything: a non-empty folder; check(); a refused draw; an asset, scene or
     ray table that will not be; a codec that cannot hold its camera's recording; names a rig folder
     would hide or pair wrongly with their models; no json writer.
  2. Write the truth and the nominal models.
  3. Render camera by camera through one `GpuScene` (`update`), each frame recorded, encoded and
     written on the pool, at most `maxInFlight` at once.
  4. Write `capture.json` last. A folder without it is an incomplete capture.
- **`estimateSize`** gives a ceiling (uncompressed) and a floor (a flat picture with the capture's
  noise), and says when noise defeats compression.
- **`rigplan render <plan> --out <folder> [--seed --deviation --noise --in-flight --estimate]`.** Its
  library target is now `lain_rigplan`, so the executable can be `rigplan`.
- **Three deviations, each measured:**
  - **`Recorder::record` is serial**, since rows dispatched inside one of many frame tasks let a
    waiting worker steal a whole other frame. At 1080p, 26 frames in flight took 7.2 s with rows on
    the pool and 1.8 s without.
  - **`maxInFlight` defaults to one a pool worker.** At 1080p, 4 in flight took 4.1 s and 13 took
    1.9 s, since encoding is the slow part. `--in-flight` is the knob.
  - **The estimate separates a file's overhead from its cost a pixel.** A 16-bit TIFF carries a 128 kB
    transfer function in every file.
- **Measured at 1080p:**
  - On Metal a capture takes about 7 ms a camera-frame.
  - On the CPU device it takes about 260 ms, with the pool idle behind it. 7 device threads (3d's half
    of 14) took 8.0 s and 10 took 6.2 s. **Recorded for the repo owner in ADR-0028; the split is
    unchanged.**
  - 100 cameras peak at 81 MB.
- **Six sabotages, all caught.** One case exists only because of one: noise keyed by list position
  passed everything until a case reversed the plan's cameras.
- **Found, owed to M9:** its rig binder pairs footage with models by sorted position, so `a` and `a-b`
  would pair wrongly; the writer refuses such names.
- **Owed:** the first CI run since slice 5, which proves 5a's pinned digests on Linux and Windows.

### Update 2026-10-25 — M15 slice 5, sub-slice 5d built: `Renderer::update`

A `GpuScene` follows a changing scene, so a capture can stream camera by camera, as decided, without
re-uploading a room's baked textures for every frame of every camera. `ctest -j8` **1530/1530** Release
with video, camera and Ceres on (+6), **1391/1391** with all three off (+6); warning-clean, format-check
clean.
Full notes in WORK.md's *Sub-slice 5d built*; ADR-0028 is amended.

- **`update(GpuScene&, Scene) → UpdateResult{problems, meshesUploaded, texturesUploaded}`.** It shares
  `upload`'s one build routine, which shares every mesh and texture held under the same pointer
  (assets are never compared by contents) and rewrites the rest under a new serial, so a viewport
  rebuilds its sets.
- **The `SceneState` keeps its source pointers**, so an address cannot be reused while a device copy
  names it. A refused update leaves the scene as it was.
- **`rigplan::scene` builds its surfaces' and poles' meshes once**, in `SceneAssets`. Two frames of a
  plan with a moving board share every mesh and texture, and the device update between them uploads
  nothing, drawing bit-identically to a fresh upload.
- **Four sabotages, all caught.** Keeping the old serial leaves a viewport drawing the old material.

### Update 2026-10-25 — M15 slice 5, sub-slice 5c built: ground truth

What a synthetic capture was rendered with, computed through lain's own projection from the realisation.
`ctest -j8` **1524/1524** Release with video, camera and Ceres on (+10), **1385/1385** with all three
off (+9); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 5c built*.

- **`rigplan/truth.h`:**
  - `boardTruth` gives each board's corners **in view** per camera: inside the image through the
    **realised** model, from the printed side. No occlusion test, as decided.
  - `cameraPoses`, and `rigRelativeTo(poses, reference)`, the one routine slice 6 will use to derive
    the truth for whichever reference a registration chose. `truthRig` is that relative to
    `RigPlan::reference`, a new optional camera uuid defaulting to the first camera.
- **Documents:** `truth/poses.json` and `truth/board.json` in `rigplan::serialize`, versioned and read
  strictly. A transform is spelled as the rig document spells it.
- **On the device, a realisation reaches a capture for the first time** (slice 4's finding 6). Two
  realised cameras (pinhole and Brown-Conrady, 2% focal and 10 px of decentring at full scale)
  capture a moving board through their own models and grey sensors. All 24 corners in view are
  detected at means of 0.057 to 0.090 px, on Metal and on the CPU device.
- **The test plan's board faced away from the pole's cameras**, and is turned to face cam0.
- **Four sabotages, all caught.** The truth projected through the nominal model puts the detected
  corners 11 px from it on the device.

### Update 2026-10-25 — M15 slice 5, sub-slice 5b built: movers and paths

A rig plan's boards and moving props. `ctest -j8` **1514/1514** Release with video, camera and Ceres
on (+13), **1376/1376** with all three off (+13); warning-clean, format-check clean. Full notes in WORK.md's
*Sub-slice 5b built*.

- **`rigplan/mover.h`:** `PathKey` / `Path` / `poseAt`, and `BoardMover` with `planFromBoard`.
  - `poseAt` holds a key at its frame, holds the first before it and the last after it, and between
    two keys is linear in position and slerps in rotation the shorter way.
  - A key is aimed as a camera is, so a board's key places the centre of its squares, seen face on
    and upright by the camera sharing its aim.
- **A prop mover is a prop with a path.** `planFromProp(prop, frame)` undoes the camera's rest turn,
  so a pan is a yaw.
- **`check()`** reports `InvalidMover`. The document gains `boards` and an optional prop `path`, and
  a board is spelled as a board specification document spells it.
- **`rigplan::scene` draws a board's sheet**, using 4c's mapping, **over a plain back**: the renderer
  lights both faces, so without one a camera behind the board would see the pattern mirrored, and a
  mirrored marker can read as another id. The device case shows the back. With no board renderer,
  the load names the board, and the rest still draws. The sheet meshes live in `SceneAssets`, ready
  for 5d's update.
- **A new `test-rigplan-scene-board`** holds a stand-in renderer, since a registry only grows.
- **Nine sabotages, all caught.** glm's `mix` in place of `slerp` (no short-path check) is caught
  only by the short-way case, which is why that case exists.

### Update 2026-10-25 — M15 slice 5 planned; 5a built: the sensor model

Slice 5 (the sensor model, movers and the capture writer) is five commits, 5a to 5e, in one
session; the plan is in WORK.md's *Slice 5 planned*. **5a is built**: `rigplan/sensor.h`, a
`SensorModel` in each `SensorSpecification`, and a `Recorder` that turns a capture's linear light
into the codes a sensor records. `ctest -j8` **1501/1501** Release with video, camera and Ceres on
(+14), **1363/1363** with all three off (+14); warning-clean, format-check clean. Full notes in WORK.md's
*Sub-slice 5a built*.

- **Decided with the repo owner:**
  - the sensor model is physical (electrons, ADC bits, an output curve and depth);
  - noise is Irwin-Hall, exact by construction;
  - a capture streams camera by camera over a new `Renderer::update` (5d);
  - the board truth is "in view", with no occlusion test.
- **"The same light records the same codes on every machine," built four ways:**
  - an Irwin-Hall draw summed in integers;
  - SplitMix64 outputs addressed by sample from a SHA-256 seed;
  - `sensor.cpp` compiled with `-ffp-contract=off`, since clang fuses a·b+c on arm64;
  - the output curve as a finite table, so its `pow` is a fixed set whose digest a test pins.

  ADR-0029's claim that a capture's bytes are identical "whatever GPU rendered the light" is
  corrected: the light differs by device.
- **Pinned outside lain:** the noise seed and draws, computed in Python. Also pinned on this
  machine: four output tables and two whole recordings. **The first CI run of those pins on Linux
  and Windows is owed**, and is what proves the claim.
- **Deviation:** the ADC depth and the output depth are separate (a 12-bit ADC written as 8-bit sRGB
  by default). An 8-bit linear ADC encoded as sRGB posterises the shadows.
- **Named for the glossary:** `SensorModel` and `Recorder`, since CONTEXT.md's *Sensor model* lists
  "camera response" under *Avoid*.
- **Found by the first run, in the test:** a digest taken through a reference into a temporary,
  slice 1b's bug again.
- **Five sabotages, all caught.** One is a measurement: without `-ffp-contract=off`, clang emits 9
  fused multiply-adds in `sensor.o` and the 16-bit sRGB output table changes on arm64. So the flag is
  load-bearing, and the pinned table is what sees it.

### Update 2026-10-24 — M15 slice 4, sub-slice 4d built: the bake density is enough (**slice 4 COMPLETE**)

The measurement 3c left owed: does SIFT see the density `rigplan::scene` bakes a procedural surface at
(20 texels to the finest noise cell)? **It does not**, so the density stands; `scene.h` says so beside
the constant. A fourth case in `test-camera-capture`. `ctest -j8` **1487/1487** Release with video,
camera and Ceres on (+1), **1349/1349** with all three off; warning-clean, format-check clean.

- **The setup.** A procedural plane, seen by the pinhole and by Brown-Conrady 5 near (a pixel a fortieth
  of a cell, the bake magnified) and far. It is captured at the density and at twice it on both
  devices, and drawn exactly per ray on the CPU.
- **Doubling the density moves a capture by 0.31 to 0.47 grey levels RMS**, and 85 to 100% of features
  are found in both.
- **Against the exact texture**, the share of features also found is 0.95 to 0.98 at the density and
  0.94 to 1.0 at twice it. What parts a capture from its texture is the 8-bit sRGB texture store and
  averaging light, which the density leaves alone.
- **Sabotage, caught:** a quarter of the density fails every comparison in all eight runs.
- **Slice 4 is complete:** the capture pass is built (4a, 4b), its claim holds through all six models
  (4c), and the bake density is enough (4d). Next is slice 5: the sensor model, the movers, and the
  capture writer.
- **CI is green on all eight jobs** ([run 40094754758](https://github.com/luckyneko/lain/actions/runs/40094754758)):
  Linux and Windows Release passed 1487/1487, every capture case drawn on the CPU device alone, since
  neither runner has a GPU. So slice 4's claim holds on all three platforms.

### Update 2026-10-24 — M15 slice 4, sub-slice 4c built: the ADR-0028 claim holds

A board rendered through each of the six distortion models is detected where lain projects the truth,
as accurately as the CPU renderer manages. The capture pass is run on Metal and on the CPU device, and
the baseline is `texturedscene.h`'s ray per sub-sample. The test is `test-camera-capture`
(`plugins/camera/test`), built where `lain::render` and the OpenCV plugin both are, Release only.
`ctest -j8` **1486/1486** Release with video, camera and Ceres on (+3), **1349/1349** with all three
off; warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 4c built*.

- **Measured with M9's board**, its squares about 62 px across, ahead and toward a corner:
  - **the capture's mean corner error is 0.068 to 0.086 px; the CPU baseline's is 0.067 to 0.099**;
  - Metal and the CPU device agree to 0.002;
  - M9's own pinhole scene through the capture pass gives 0.067 px, against M9's 0.068.

  Each renderer's worst corner falls at a different pose, so those outliers are the detector's. The
  test asserts a mean under 0.1 and a worst corner under 0.4; M9's 0.25 holds for neither renderer
  where the frame cuts the board.
- **Found by the first run: the scene set the error, not the renderer.** At 45 px squares on a raster of
  60 px a square, both renderers sat at 0.11 to 0.23 px. Matching M9's geometry brought both to M9's
  accuracy.
- **Encoded before detecting**: RGBA32F Linear → Gray32F → sRGB → Gray8, since `board::detect` narrows a
  float picture without encoding it.
- **The m × s sweep** (hidden, `[.sweep]`):
  - one ray a pixel aliases (0.11 to 0.15 px);
  - four reach the baseline (0.071 to 0.078);
  - at four, the oversampling barely matters for a board, which is texture.

  The defaults stay at s = 4 and m = 2, since a silhouette is placed to half a texel (4b).
- **Two sabotages, both caught**: a table built through a pinhole with its self-check fooled the same
  way (corners 1.2 to 33 px off), and every ray taken from its pixel's corner (0.7 px).
- ADR-0028 records the result. Left of slice 4: **4d**, the procedural bake density. *(Built the same day; see above.)*

### Update 2026-10-24 — M15 slice 4, sub-slice 4b built: the capture pass on the device

`Renderer::upload(RayTable)` → `GpuRayTable` and `Renderer::capture(scene, table, CaptureView)`, over
`shaders/capture.comp`. A capture returns render()'s contract: RGBA32F linear light at the camera's
size, with alpha as the share of a pixel's rays that met a surface. `ctest -j8` **1483/1483** Release
with video, camera and Ceres on (+7), **1349/1349** with all three off; warning-clean, format-check
clean. Every case ran on Metal and on the CPU device. Full notes in WORK.md's *Sub-slice 4b built*.

- **Each tile is a pinhole `View`**: turned to its face, its principal point moved to the tile, its
  near plane scaled by `nearScale`. It is drawn through the existing scene pipeline into one RGBA32F
  storage texture.
- **The remap adds each ray's manual bilinear blend** (`imageLoad`) into banded sums. The host divides
  by s², dividing rather than multiplying by a reciprocal, so a pixel every ray of which sees one value
  keeps it exactly.
- **Found: drawing and remapping in one submission raced, on both devices.** archimedes' `CopySrc`
  finish then a transition to `General` is recorded in a valid order, yet some rays read texels not
  yet drawn: pixels missing one or two of sixteen rays, in a different place per device. **Each tile
  is two waited submissions now.** The finding joins the archimedes todo in the Outstanding index.
  The plan is corrected in place, and so is ADR-0028.
- **Measured on both devices** (160×120, the strong camera set):
  - a pinhole against `render()`: 1.1e-4;
  - a lit plane through every model: 3.4e-5 to 1.3e-4;
  - a fisheye in a turned box room, with no seam where faces meet: 3.6e-5;
  - 102 tiles against 5: 4.8e-5;
  - a square is exact away from its edges;
  - nothing farther than the near plane is lost, at any angle off a face's axis.
- **Silhouettes are placed to half a texel**: coverage at a square's edges is up to 0.34 off the exact
  area. A board on a sheet is texture and does not feel it; ADR-0028 says so, and MSAA in the face
  pass is deferred.
- **Eleven sabotages; seven caught.** The single-submission race is among them, failing 6 cases. A
  first half-texel sabotage, `floor(q + 0.5)`, rightly passed: it only changes which two texels the
  blend extrapolates from.
- **Four not caught on either device**: the accumulator barrier, the open edges, widened membership and
  widened pixel rectangles.
  - The barrier is Vulkan's rule between submissions, and the wait between them covers it in practice.
  - The other three guard against float disagreements larger than any measured here.
  - All four stay, as their comments say.
- **Found by the room case, in the test:** a wall crease can pass between two pixels' rays, so creases
  are found from neighbours too.
- **One unrelated timing flake, recorded:** the OpenCV pair-matching scale case failed once under
  `-j8` on its timing ratios. It took 41.9 s there against 23.7 s alone, and passed on the rerun and
  on its own.

### Update 2026-10-24 — M15 slice 4 planned; 4a built: the capture pass's ray table

Slice 4 (the capture pass, ADR-0028) is four commits, planned after a design review checked the first
draft against the code: **4a** the ray table on the CPU, built here; **4b** the remap on the device;
**4c** the board-corner claim through all six models; **4d** the procedural bake density 3c left
owed. The plan and its defaults are in WORK.md's *Slice 4 planned*; ADR-0028 and CONTEXT.md are
amended. `ctest -j8` **1476/1476** Release with video, camera and Ceres on (+11), **1342/1342** with all
three off; warning-clean, format-check clean. Debug not run (no Debug tree here).

- **Decided with the repo owner:**
  - **The ray table is a pixel-corner lattice**: `unproject` at the (w + 1) × (h + 1) corners, and a
    sub-sample's ray is its pixel's four corner rays blended. A direction per sub-sample is 16×
    larger at s = 4, 6.4 GB at 8K.
  - **No archimedes change in slice 4.** A capture keeps to Vulkan's guaranteed limits: tiles at most
    4096 a side, bands of at most 2^27 bytes. It reads a rendered tile through `CopySrc`, then
    `General`, then `imageLoad`, since archimedes' `Sampled` finish orders only the fragment stage.
    Both archimedes changes (limit queries, a compute-visible finish) are a todo in WORK.md's
    Outstanding index, with what lain drops once they land.
- **Found while planning:**
  - **No lain model reaches 180°** (every one needs Z > 0), so "a cubemap past 180°" never fires. The
    intermediate image is the front face alone or the cube's five faces, whichever takes fewer texels.
  - **The ~0.07 px target was a pinhole's.** No test detects a board through the other five models, so
    4c measures each one's CPU baseline.
  - **`texturedscene.h` averages 8-bit sRGB codes and the GPU averages linear light**, and
    `board::detect` narrows RGBA32F without encoding it. So 4c judges both against the truth, never
    against each other.
  - **The face pass is aliased at silhouettes** (no MSAA): ±0.5 / m camera px, which a board on a sheet
    does not feel. 4b measures it.
- **`lain/render/capture.h`:**
  - `CaptureSettings`;
  - `FaceAxis`, `cameraFromFace` (signed permutations) and `majorAxis`;
  - `CaptureFace` and `CaptureTile`;
  - `RayTable::build`, run on the process pool, so `lain::render` links `lain::task`.
- **Its rules:**
  - A face's focal length is m over the least singular value of any cell's pixel-to-face Jacobian, one
    focal length for every cube face.
  - Membership is widened, and tile interiors are half-open face-global texel ranges with open outer
    edges.
  - The table measures its interpolation error by projecting each pixel centre's blended ray back
    through the model, and refuses one past 0.01 px.
- **Measured at 640×480 on strong sets of all six models** (`libs/render/test/testcameras.h`, for 4b
  and 4c too):
  - the interpolation error is 2.5e-4 to 2.4e-3 px, and no sub-sample is worse than its pixel's
    centre;
  - the least stretch of a pixel step is exactly m;
  - the Brown-Conrady family keeps the front face (up to 2139×1589 texels), and the 168° fisheye takes
    all five cube faces;
  - a Brown-Conrady camera 0.6% inside its fold interpolates 0.017 px off and is refused.
- **Eight sabotages; five caught.** The three not caught (no widening, closed edge tiles, unwidened
  pixel rectangles) guard the shader's float arithmetic, which a CPU case cannot exercise. They stay,
  and 4b is where they can be seen.
- **Corrected in passing:** WORK.md's Outstanding index still had M15 at slices 0 and 1, and the CPU
  device's adoption as owed; and CONTEXT.md's *Rig design* still said nothing was built.

### Update 2026-10-24 — feature/M15's first CI runs: the CPU device renders on every platform

[Runs 40057291780](https://github.com/luckyneko/lain/actions/runs/40057291780),
[40062334857](https://github.com/luckyneko/lain/actions/runs/40062334857) and
[40064440056](https://github.com/luckyneko/lain/actions/runs/40064440056), dispatched by hand, since CI
runs by itself only on master, main and develop. **The third, on `c293ba8`, is green on all eight
jobs** (1465/1465 on Linux Release and Windows). Full notes in WORK.md's *feature/M15's first CI
runs*, under M15.

- **3d's claim holds on all three platforms.** Neither the Windows nor the Linux runner has a GPU,
  and every render and scene case passed on both, on the CPU device alone (Linux Debug 1457/1457).
- **GCC 13 failed the Linux builds on two false positives** that clang does not raise:
  `-Wdangling-reference` on a test helper (its name is now a `string_view`), and, in Release only,
  `-Wmaybe-uninitialized` in `DocumentGuard::request`. `emplace` raised the second as `=` did, so the
  held request is now a `std::unique_ptr`. Past it, GCC found nothing else in the optimised build.
- **Three Windows failures were tests that took `/projects/hall` for an absolute path.** It has no
  drive there. New `lain::testing::rootedPath` roots a path at this platform's root.
- **Found, not fixed: on Windows a rig plan's uris change spelling across a save and a load**
  (`Uri::fromPath` keeps `\`, a document stores `/`). Graph documents hold `std::filesystem::path`
  and do not see it. This is predicted from the code, not observed. Trigger: the first comparison of
  a rig plan's uris across a load.

### Update 2026-10-24 — M15 slice 3d built: lain's pin moves to the CPU device (**slice 3 COMPLETE**)

archimedes PRs #1 and #2 merged, and `extern/archimedes` moves to `develop` (`828c3e2`). `ctest -j8`
**1465/1465** Release with video, camera and Ceres on (+2); warning-clean, format-check clean. **Not yet
on CI:** the slice's claim, a plain scene rendering in CI on every leg, is owed by the first push of
`feature/M15`. Full notes in WORK.md's *Sub-slice 3d built*, under M15; ADR-0028 is amended.
*(Proved on 2026-10-24 on all three platforms; see the entry above.)*

- **Every `[gpu]` render case runs on hardware and on the CPU device**, a Catch2 generator as in
  archimedes. Where the CPU driver is staged, a missing CPU device fails rather than skips.
- **The CPU device found three things:**
  - its texture filtering is about 1e-4 short, so an untextured material no longer multiplies by a
    sampled white;
  - it draws depth wrong for a triangle with a vertex almost in the camera's plane, as a camera flush
    on a wall would make. `offPlane` moves such a vertex a few millimetres along the camera's axis
    first;
  - a 1 px line on a surface seen obliquely loses samples to it on both devices, whatever the pull,
    so **3b's claim that a line on a surface is drawn on it was an overclaim** (corrected in place).
    The grid needs its own pass in slice 8.
- **`--device gpu|cpu`** is a reserved `lain::app` flag, and **the CPU device gets half the logical
  cores** (the repo owner's call), the pool keeping its own workers.
- **SwiftShader's notice is in `--licenses`**, naming its upstream commit and its one patch.
- **Six sabotages, all caught.**

### Update 2026-10-23 — poles are drawn, and a camera inside its own pole is refused

From the same review. CONTEXT.md's *Mount* already said a pole is real geometry in every capture
render, so 3c's "not drawn" fell short of the design. `ctest -j8` **1463/1463** Release with video,
camera and Ceres on (+4, with the MTL change); warning-clean, format-check clean. Full notes in WORK.md's
*After 3c: what the review settled*, under M15.

- **Drawn:** a 32-sided cylinder from the floor to the top at the realised mount, a matte mid grey.
  On the device, a pole 2 m ahead hides the wall behind it, lit as the bulb says.
- **Refused: `PlanProblem::InsidePole`.** A camera whose optical centre, or a light, could lie inside
  its own pole, as far as its tolerances move it at full scale. A camera on top of its pole is fine.
- **The test plan's pole cameras moved 12.5 cm off the axis**, and the cases pinning cam0's position
  follow.
- **Five sabotages, all caught.** The build order's slice 5 now names the movers, as slice 1's plan
  had.

### Update 2026-10-23 — a textured MTL material shows its texture as it is

From the repo owner's review of the open questions after M15 slice 3c. Full notes in WORK.md's *After
3c: what the review settled*, under M15.

- **MTL multiplies Kd by map_Kd, and the OBJ reader now keeps a texture visible.** A missing `Kd` or
  an explicit `Kd 0 0 0` with a texture reads white; a stated `Kd` (0.6 included) is kept.
- **3c's finding was wrong and is corrected in place.** It said a missing `Kd` reads black. That came
  from tinyobjloader's initialiser alone; its parser gives such a material a 0.6 grey, so the texture
  drew at 60%. Telling a missing `Kd` from a stated 0.6 needs the MTL's text, which the reader now
  scans as it hands the file over.
- **Also settled:** each surface keeps its own procedural pattern, and pushing `feature/M15` waits
  for 3d.

### Update 2026-10-23 — M15 slice 3c built: a rig plan's render scene

The third of slice 3's four commits; only 3d, the archimedes pin, is left, and it waits on archimedes'
PRs. `ctest -j8` **1459/1459** Release with video, camera and Ceres on (+9); warning-clean, format-check
clean. Full notes in WORK.md's *Sub-slice 3c built*, under M15.

- **`lain::rigplan::scene`**: `loadSceneAssets(plan)` reads meshes and pictures and bakes procedural
  surfaces once, then `renderScene(plan, realisation, assets, frame)` builds each frame's scene.
  One walk over the surfaces serves both. What will not load is named, by owner and file.
- **Surfaces read upright from the side they face**: the room's walls from inside, the floor with
  north at its top, a plane as a camera aimed the same way sees it. Pictures repeat every tile.
- **A procedural surface is baked to its own extent**: 20 texels to the finest cell, capped at 8192 a
  side. **Added: its seed is mixed with the surface's identity**, so walls of one material do not
  repeat each other (M9 found repeated patterns make wrong tracks). The repo owner may object.
- **Props play their clip frame**, shared without a copy, with their MTL kept (Kd × map_Kd, Ks, Ns),
  the mapping 2d deferred. Lights are their specifications at their realised poses.
- **Bodies are not drawn yet.** A drawn pole needs a rule keeping a camera clear of it: the test
  plan's cameras sit on their pole's axis. That rule is the repo owner's call. *(**Answered the same
  day:** poles are drawn and a camera inside its pole is refused; see the entry above.)*
- **On the device**, a camera at rest in a room sees the north wall at the bulb's inverse square
  (0.25 at 4 m), holding the plan frame, the camera frame and the renderer to one another.
- **Eight sabotages, all caught**; two first failed to build and were redone.

### Update 2026-10-23 — M15 slice 3b built: the viewport and its overlay lines

The second of slice 3's four commits. `ctest -j8` **1450/1450** Release with video, camera and Ceres on
(+7); warning-clean, format-check clean. The cases ran on Metal. Full notes in WORK.md's *Sub-slice 3b
built*, under M15.

- **`render::Viewport` records into a window's frame**, in the pre-pass `acm::Renderer` hands a frame
  index, and never waits for the device. It keeps everything it writes per frame in flight: its
  constants, target, line vertices and descriptor sets. So two viewports of one scene in one frame
  share nothing, and the sets follow a new upload by its serial, not its address.
- **Display-encoded in the shaders** (lain's sRGB curve into 8-bit UNORM, as ImGui composes),
  multisampled at up to 4x.
- **Overlay lines (`render::Segment`)** are drawn in the scene's rendering scope, depth-tested and
  1 px. Their vertices come from a storage buffer by index, since a vertex buffer's write waits for
  the device.
- **Found by a probe: a line on an oblique surface loses half its samples**, since a 1 px line's
  depth is constant across its width and a floor's is not. A pull toward the camera of 1e-4 of the
  depth buffer fixes it: 0 of 26 floor points drawn whole with no pull, 15 at 2e-6, all from 2e-5.
  The pull stays small enough that a line 5 cm behind a wall at 2 m is still hidden, up to 1e-3.
  Two cases bracket it. *(**Corrected in 3d:** "drawn whole" was a loose criterion; on an oblique
  surface the pull changes nothing on either device. It does draw a line on a surface facing the
  camera. See the 3d entry above.)*
- **Eight sabotages, all caught.** Showing the texture in a pane is `lain::gui`'s, with slice 8.

### Update 2026-10-23 — M15 slice 3 planned; 3a built: `lain::render`'s offscreen raster pass

Slice 3 is four commits:
- **3a**, `lain::render` and the offscreen raster pass, built here;
- **3b**, the viewport pass and the line pass;
- **3c**, a rig plan's scene (`lain::rigplan::scene`), with the mapping of a mesh's MTL materials that
  2d deferred;
- **3d**, lain's archimedes pin moving to the CPU device.

**3d waits on archimedes PRs #1 and #2, still open.** They change nothing the renderer records with,
so 3a to 3c are built against the current pin. The plan and its defaults are in WORK.md's *Slice 3
planned*. `ctest -j8` **1443/1443** Release with video, camera and Ceres on (+11); warning-clean,
format-check clean. The GPU cases ran on Metal through MoltenVK.

- **A render scene, a pinhole view, a renderer.**
  - `Scene` holds meshes and textures as `shared_ptr<const>`, plus materials, instances, point and
    spot lights, ambient light and a background.
  - `View` is a rigid pose plus `camera::Intrinsics`, so no distortion can be stated and ignored.
  - `Renderer::upload` gives a `GpuScene`, and `render` reads back RGBA32F linear light, with alpha
    as coverage.
- **The SPIR-V is embedded in the library**, compiled at build time by archimedes' glslang setup
  (`cmake/lainShaders.cmake`), so nothing is staged beside an executable. Built from source here,
  glslang costs 7.5 s wall.
- **Shading:** two-sided Lambert times a texture, plus Blinn-Phong, under inverse-square point and
  spot lights. No shadows yet.
- **Textures** are uploaded as 8-bit sRGB with mips, after lain's own `convert`, so the device
  decodes before it filters. The renderer refuses one that states no colour space.
- **The cases judge against lain's geometry and the shading model, never stored pixels**: coverage
  against `camera::project`'s outline of a tilted square, exact away from the edges; lighting within
  1e-4 at every pixel; depth order; texture orientation, repeat and decode.
- **Six sabotages, all caught.** The half pixel dropped from `clipFromCamera` fails four cases.

### Update 2026-10-23 — every built dependency's notice in `--licenses`

A repo-wide licensing question M15 slice 2c raised, decided with the repo owner and built on a branch of
its own off `feature/M15` (so tinyobjloader is covered). `ctest -j8` **1420/1420** Release with video,
camera and Ceres on (+2); warning-clean, format-check clean. Full notes in WORK.md's *Every built
dependency's notice in `--licenses`*; the decision is ADR-0015's, amended in place.

- **`flowview --licenses` prints 25 notices**: lain, archimedes and multi first, then every dependency
  the build contains, each a header line plus its own licence file verbatim, read at configure time.
  The Apache-2.0-only ones (the Vulkan loader, MoltenVK) stage their text in `third-party/<id>/`, as
  OpenCV's does. A system package is named, with where it is.
- **Adding a dependency now means one `lain_register_notice` call** in its `add<Dep>.cmake`, and one
  row in `libs/app/test/test_notices.cpp`. The test fails both ways: a built dependency with no notice,
  or a notice the table does not expect.
- **Findings:** Ceres also carries Apache-2.0 (two Abseil-derived headers), and the README row is
  corrected. fmt, zlib, GLFW, libpng, stb and portable-file-dialogs do not strictly need a notice in a
  binary; they print one anyway, by the owner's uniform rule.
- **The generated source is now pieces**, because MSVC refuses a literal over 16380 bytes and the text
  is about 48 KB. Not yet built on Windows: CI's first run is owed.

### Update 2026-10-23 — M15 slice 2d built: the room, planes, boxes and props (**slice 2 COMPLETE**)

The last of slice 2's four commits, after a no-behaviour-change hoist. `ctest -j8` **1430/1430**
Release with video, camera and Ceres on (+12); warning-clean, format-check clean. Full notes in
WORK.md's *Slice 2 planned*, under M15.

- **Hoisted first: ADR-0027's rule for one path** (`io::relativizePath` / `io::resolvePath`), out of
  `flow::serialize`, so a graph document and a rig plan document share every subtlety of it. A
  sabotage writing `../` fails the graph cases too, which proves they go through it.
- **The rig plan gains its surroundings**:
  - `Material`: a colour, a picture with a tile, or a procedural texture, plus MTL's specular;
  - a `Room` with six switchable surfaces;
  - `Plane`, `Box` and `Prop`. A prop states its up axis, its metres per unit and whether its mesh is
    a clip, and `planFromProp` converts its mesh to the plan frame once.

  `rigplan::serialize` writes them, and walks them with its own `relativizePaths` / `resolvePaths`.
- **Deviations:**
  - the mesh-to-rig-plan material mapping moves to slice 3, its first caller;
  - a prop's clip is stated, not detected from its name.
- **Four sabotages, all caught.** CONTEXT.md's *Room*, *Prop* and *Material* are amended.

### Update 2026-10-23 — M15 slice 2c built: the OBJ / MTL plugin

The third of slice 2's four commits. `ctest -j8` **1418/1418** Release with video, camera and Ceres on
(+7); warning-clean, format-check clean. Full notes in WORK.md's *Slice 2 planned*, under M15.

- **`plugins/io/mesh`**, with a generated `registerMeshCodecs()` aggregator, and
  **`plugins/io/mesh/obj`** over **tinyobjloader `v2.0.0rc13`** (MIT). The pin is a tag, since the
  project's releases stop at 2019.
- **The MTL is read through the `MeshFolder`**, never opened by the plugin.
- **What an OBJ leaves open**:
  - normals are all or none;
  - a corner without a texture coordinate gets (0, 0);
  - a face without a material gets a plain default, which also keeps a mesh whose MTL went missing
    visible.
- **Corners are shared only where the file shares them**, so a cube with face normals is 24
  vertices. Four sabotages, all caught.
- **Noted for the repo owner:** `--licenses` covers only dependencies that register a notice, and the
  statically linked MIT and zlib ones are in the README alone. ADR-0015 does not say whether that is
  enough. *(Settled the same day; see *every built dependency's notice in `--licenses`*.)*

### Update 2026-10-22 — M15 slice 2b built: `lain::mesh` and the `io::mesh` seam

The second of slice 2's four commits, after a no-behaviour-change hoist. `ctest -j8` **1411/1411**
Release with video, camera and Ceres on (+9); warning-clean, format-check clean. Full notes in WORK.md's
*Slice 2 planned*, under M15.

- **Hoisted first: `io::frameKey` and `io::numberedFiles(pattern)`**, out of `io::image`'s private
  matcher, since a mesh clip is its second reader. flowview names `io::frameKey` now.
- **`lain::mesh`** (the data model): `Mesh`, `Material` as the file states it, `validate`, `MeshClip`,
  and `clipFrame`, which picks the nearest clip frame, looping, in exact integers.
- **`lain::io::mesh`** (the seam): `MeshReader` and its registry, and `decode` / `load` / `loadClip`.
  A reader is handed bytes and a `MeshFolder` to read what the mesh names beside itself, never a path
  (ADR-0004's split). A clip with a frame that will not load is refused, naming the frame.
- **Found by a sabotage: the per-part check had no case only it could catch**; two now exist.
  Four sabotages, all caught.

### Update 2026-10-22 — M15 slice 2 planned; 2a built: the procedural texture in `lain::image`

Slice 2 is four commits:
- **2a** the procedural texture, built here;
- **2b** a `lain::mesh` data model and the `io::mesh` seam;
- **2c** the OBJ/MTL plugin, over tinyobjloader;
- **2d** the room, planes, boxes and props in the rig plan, with its first document paths, per
  ADR-0027.

The plan and its defaults are in WORK.md's *Slice 2 planned*. `ctest -j8` **1402/1402** Release with
video, camera and Ceres on (+3); warning-clean, format-check clean.

- **`image::proceduralGrey` / `bakeProceduralTexture`**, the M9 test renderer's texture moved verbatim:
  `texturedscene.h` now forwards to it.
- **Proved byte for byte on M9's footage**: all 152 files `camera-footage` draws were hashed before
  the change (and drawn twice, to show the baseline is deterministic), and are identical after it.
- **Found by its sabotage: random pinned points miss the texture's discs**, so the first pins passed
  a change that altered 49 footage files. Two pins now sit in a disc's soft edge.

### Update 2026-10-22 — M15 slice 1c built: the documents (**slice 1 COMPLETE**)

The last of slice 1's three commits, after a no-behaviour-change hoist. `ctest -j8` **1399/1399**
Release with video, camera and Ceres on (+8); warning-clean, format-check clean. Full notes in
WORK.md's *Slice 1 planned*, under M15.

- **`lain::rigplan::serialize`**: the rig plan document and the specification catalogue document,
  versioned and in the camera documents' conventions (a unit in a length's key, `Degrees` in an
  angle's, `{x, y, z}` positions, uuid strings). A distortion is spelled exactly as the camera-model
  document spells it, since the lens's reuses camera::serialize's mapping.
- **A read is strict about shape and not about content.** A misspelled key, an unknown name or a
  non-uuid identity is a named problem; a rig plan `check()` refuses still reads, so it can be opened
  and fixed. CONTEXT.md gains *Rig plan document*.
- **Found: `core::Length` asserts on a value it cannot hold**, so the reader range-checks every
  length first. Without the guard, in Release, a 1e300 mm focal length read as a plan.
- **`data::versionedDocument` / `versionProblems`** moved from camera::serialize's private files into
  `lain::data`, as their second user arrived. Their messages are unchanged and are now pinned by tests.
- **The round trip is checked by what it builds**, not only by writing back the same document, which
  could hide a field both directions drop. Four sabotages, all caught.
- **The rig plan document stays at version 1** until slice 7 commits the first example.

### Update 2026-10-22 — M15 slice 1b built: the rig plan and realisation

The second of slice 1's three commits. `ctest -j8` **1391/1391** Release with video, camera and Ceres
on (+22); warning-clean, format-check clean. Full notes in WORK.md's *Slice 1 planned*, under M15.

- **What it adds:**
  - **`lain::rigplan`'s model**: mounts (a pole or a fixed point), placed cameras and lights, the
    capture timeline, the seed and scales, and `check(plan)`.
  - **`realise(plan)`**, one seeded build.
  - **`math::AxisConvention::XRightYForwardZUp()`**, behind `rigplan::kPlanFrame`.
- **A draw is SHA-256 of a canonical text** (seed, owner UUID, parameter key). It is pinned exactly
  against values computed by Python's `hashlib`, so it is the same on every platform, and keyed by
  the owner's identity, so adding, removing, reordering or renaming a camera re-rolls no other.
- **The aim convention is pinned** against a camera placed by hand: roll, tilt, then pan, in the
  mount's frame. CONTEXT.md gains *Aim*.
- **At scales of 0 a realisation is the nominal plan exactly**, to the last bit.
- **The plan's sweep claim was corrected**: intrinsics move exactly in proportion to a scale, a pose
  only to first order (about 1e-4 m).
- **Added beyond the plan:**
  - camera names unique ignoring case;
  - Windows' reserved names refused;
  - identities unique across every kind of owner.
- **Two bugs in the new tests, found by their first run:** a reference into a temporary, and a fold
  bound computed from the wrong radius.
- **Five sabotages, all caught.**

### Update 2026-10-22 — M15 slice 1 planned; 1a built: specifications and the nominal model

Slice 1 is three commits: **1a** specifications, **1b** the rig plan and realisation, **1c** the
documents. The plan and the decisions taken as defaults are in WORK.md's *Slice 1 planned*:
- units: `Length` for datasheet lengths, a fraction for a tolerance, degrees for an angle;
- a draw from SHA-256 of a canonical text;
- a version-1 document that may change shape until slice 7 commits an example.

**1a is built**: `libs/rigplan` (`lain::rigplan`, no data, no GPU) holds `SensorSpecification`,
`LensSpecification`, `CameraSpecification`, `nominalModel(sensor, lens)` and `check(specs)`.
`ctest -j8` **1369/1369** Release with video, camera and Ceres on (+7); warning-clean, format-check
clean.

- **The nominal model is derived exactly**: a ratio of whole nanometres, so 4 mm over 2 um is exactly
  2000, and the principal point is ((w - 1) / 2, (h - 1) / 2). It goes through `CameraModel::create`,
  so a lens that folds inside a sensor's image makes no model, and `check` names the camera.
- **`check` reports one problem per fault.**
- **Two sabotages, both caught.**

### Update 2026-10-22 — M15 slice 0 built: `lain::editor`

What every document editor needs and no document type owns, hoisted out of flowview so `rigplan`
cannot disagree with it. `libs/editor` is driver-free and gui-free. `ctest -j8` **1362/1362**
Release with video, camera and Ceres on (+20: 18 in `test-editor`, 2 in flowview; the 8 undo cases
moved). Warning-clean, format-check clean. **gui-mode NOT eyeballed** beyond a `--frames` smoke.
Full notes in WORK.md's *Slice 0 built*, under M15.

- **`UndoStack`** moved verbatim with its tests. **`RecentDocuments`** with `noteDocument` /
  `forgetDocument` is flowview's recents rule. **`loadSession<T>(file)` / `saveSession`** is the
  best-effort session file over any struct; `sessionFile(app)` derives the path separately, so the
  tests never write into the real home directory.
- **`DocumentGuard<Request>`** is the unsaved-changes rule as a state machine: a cancelled or failed
  Save cancels the swap, and a new request replaces an unanswered one. These rules were never under
  test before, because they lived in `menubar.cpp`, which needs a gui. The modal stays flowview's,
  and so does dirty tracking: what marks a document changed is the app's.
- **flowview's session file keeps its shape**: a hand-written `serialize` writes the recents flat,
  under the old keys. A copy of the repo owner's real session file, in a scratch `HOME`, loaded in
  gui-mode and was written back byte-identical.
- **Two defects in this slice's own new code, found while wiring and fixed before commit**:
  - a stale prompt flag that would have opened the modal asking about nothing;
  - a json-only test helper that would have failed a json-off build under `-Werror`.
- **Four sabotages, all caught.** Owed: a live check of the unsaved-changes modal (New and Open with
  unsaved work; Save, Discard and Cancel; a cancelled Save panel). *(Done 2026-10-29: in rigplan by
  M15 slice 8's checklist, and in flowview with its Quit fix.)*

### Update 2026-10-22 — M15 designed: rig design, rig plans and synthetic captures (nothing built yet)

A new app for designing camera rigs as they would be installed (a room, props, poles carrying
cameras and lights, a board waved through), which renders **synthetic captures**: footage in the
shape M9 already binds, plus the ground truth it was rendered with. A layout is judged by running
M9 on its capture and scoring the result against the truth, so the app never registers anything
itself. Grilled 2026-10-21 to 10-22; decisions in
**[ADR-0028](docs/adr/0028-one-raster-renderer-capture-by-ray-table-remap.md)** and
**[ADR-0029](docs/adr/0029-rig-plans-are-nominal-captures-are-seeded-realisations.md)**, build order
in WORK.md's **Milestone 15**, vocabulary in CONTEXT.md's new *Rig design* section plus *Model
comparison* and *Rig comparison*. **Built on `feature/M15`, branched from `feature/M9`.** Nothing is
built.

- **One raster renderer, `lain::render` over archimedes, for both views.** The viewport is a
  pinhole preview. A capture render rasterises into an intermediate image and remaps it through a
  ray table that lain's own `unproject` builds, so shading is written once and the camera model is
  never ported to GLSL. A CPU ray-caster was the first recommendation and was refused for
  duplicating the shading. The remap biases no geometry, and slice 4 measures it: a rendered
  board's corners against lain's projection, through all six distortion models. CI renders on
  **archimedes' CPU device** (SwiftShader), on every leg *(amended 2026-10-22; this said lavapipe on
  a Linux leg)*.
- **A rig plan is nominal; a capture renders one seeded realisation of it.**
  - Tolerances are symmetric bounds in their part's own unit, drawn uniformly.
  - Each draw is seeded by the rig plan's seed, the owner's UUID and the parameter's key, so an edit
    or a rename re-rolls nothing else.
  - Two deviation scales, intrinsic and installation, run 0 to 1 and are locked by default.
  - A draw that makes no valid model is refused, never redrawn.
- **Specifications are a sensor plus a lens**, so each tolerance stays in the unit of the part that
  causes it. The nominal model is derived from the pair. A lens's distortion tolerance is one
  percentage on its strength, not per coefficient, because independent coefficient draws fold
  inside the image. Lights follow the same pattern as cameras, minimal in the first cut.
- **The sensor model runs in C++ after readback**: a fixed gain, read and shot noise under a third
  scale, then encoding. So the bytes are identical on every machine. **Auto-exposure and every
  automatic adjustment are refused, not deferred**: a detector's evidence must not change with what
  is in view.
- **Every capture writes both the nominal and the realised models**, so "register an uncalibrated
  rig" and "register a perfectly calibrated one" are a binding choice, not a regeneration. The truth
  is plan-frame poses, from which a rig document is derived for whatever reference camera the
  registration chose.
- **Scoring is camera geometry.** Rig comparison and model comparison are promoted from test code
  (`rigcompare.h`) into `lain::camera`, with `compareRigs` / `compareModels` nodes. This discharges
  M9's deferred *"a node that consumes a registered rig"*.
- **The plan frame is `XRightYForwardZUp`, in metres, with the floor at Z = 0.** It is one named
  convention, never stored per document, and an asset's up axis is converted once at import.
- **Movers only**: a board mover or a prop mover, on paths keyed at frames. A prop may loop a mesh
  clip, first from an OBJ sequence; skinned glTF comes later, and FBX is refused. Cameras are fixed,
  genlocked, and each frame is an instant.
- **Specifications are copied into a rig plan, never linked**, so rig plan plus seed always
  reproduces a capture.
- **ImGuizmo** handles manipulation. Overlays are a depth-tested line pass in `lain::render`.
- **flowview's undo, session and document guard are hoisted into `lain::editor`** as slice 0,
  since this app is their second consumer.
- **Ten slices**, the headless vertical (slice 7: render a rig plan, then `flowview run
  register.json` + `compareRigs` against the truth, in CI) before either gui slice.
- **Deferred:**
  - shadows and area lights (needed);
  - moving cameras;
  - a capture manifest (after the basic version, with the generator as its first writer);
  - a camera-authoring view and multi-sensor devices;
  - video capture output;
  - a predicted observation graph.

  The repo owner had a **CPU fallback for archimedes** planned separately, as archimedes work. It
  became archimedes' CPU device (PRs #1 and #2), verified against lain on 2026-10-22:
  - **What works.** lain builds unchanged (1208/1208), Metal still ranks first, and a GPU-less
    probe through `lain::app` got SwiftShader and an exact float readback.
  - **Two archimedes fixes it found are in** (`2eb294b`), and re-verified from lain: the
    SwiftShader pins were invisible to a consumer's scope (a blocker), and the licence texts were
    not exposed (`ACM_SWIFTSHADER_LICENSES` now names them).
  - **Owed by lain, in M15 slice 3.** The licence notice, a thread split with the CPU device, a
    `--device gpu|cpu` choice, and three renderer limits to design around.

  All of it is in WORK.md's *Verified against archimedes' CPU device*.

### Update 2026-10-21 — M9: a no-refiner CI leg, and the footprint measured again (P4)

The review's P4 items 8 and 9. Full notes in WORK.md's *A no-refiner CI leg, and the footprint measured
again*, under M9.

- **CI gains a `ceres` field and a Linux Release (no refiner) leg**: video and OpenCV on, Ceres off. The
  `camera` field used to drive both options, so no leg built OpenCV without Ceres. CI's first run of
  the leg is owed.
- **Run locally first, the leg found a bug in P3's composition vertical.** It drew 2 frames a camera
  whenever registration could not run, so without Ceres every calibration failed and nothing was
  written, where `Failed: NoRefiner` was expected. It now draws full footage wherever detection runs.
- **Three configurations green on macOS arm64, Release:** everything on 1342/1342, OpenCV without Ceres
  1283/1283, everything off 1208/1208. The last fetches no OpenCV, FFmpeg, Ceres or Eigen.
- **The footprint, macOS arm64, Release:** `flowview` is 6.46 MB with everything off, 6.64 MB with
  video and OpenCV, and 9.19 MB with Ceres too.
  - Ceres adds 2.55 MB, statically; `libceres.a` is 6.0 MB.
  - FFmpeg's 20.0 MB and OpenCV's 10.7 MB are shared libraries beside it.
  - The last record, "flowview grows by 88 bytes", predated anything referencing Ceres.

### Update 2026-10-21 — M9: camera footage, examples and the composition (P3, but for its gui session)

The review's P3 items 5 and 6. `ctest -j8` **1342/1342** Release with video, camera and Ceres on (+1);
warning-clean, format-check clean. The builds with all three off were not run. Full notes in WORK.md's
*Camera footage, examples and the composition*, under M9.

- **`flowview-examples --camera-footage <folder>`**, and a `camera-footage` target into
  `build/camera-footage`, draws footage for every camera example, with a README.txt of the commands. It
  takes about 5 s and 8 MB, and is never committed.
- **The writers moved out of the vertical** into `examples/src/camerafootage.{h,cpp}`, which the builder
  and the verticals both compile, with exceptions in place of `REQUIRE`. libs/camera/test's camera
  parameters moved to a Catch2-free `testcameraparameters.h`.
- **Four committed examples**, every one `needsCamera` and `needsInputs`:
  - `calibrate`;
  - `register` and `register-targetless`;
  - `calibrate-and-register`: a map calibrates each camera with the board broadcast, and the gathered
    models register the rig.

  `detectBoard` was left out, decided with the repo owner.
- **The camera verticals run the committed documents.** The in-test document builders are gone, except
  the two that are a test's own.
- **A composition vertical**: footage alone bound, Ready, the rig 0.05%, 0.87 mrad and 0.24 mm from the
  truth.
- **The board rig's cameras have their own pinholes.** Registering a camera with the next one's model
  now fails four checks in both rig verticals. With one shared pinhole it was invisible.
- **Owed:** the gui eyeball of the four examples on footage from `camera-footage`, in the Outstanding
  index's item 3.

### Update 2026-10-21 — M9: identities that do not depend on where footage sits (**P2 COMPLETE**)

The review's P2 item 4. A camera was named by its footage's absolute path, track and group identities
digested it, and every k-th unit in identity order is held out, so the same rig in another folder held
out other evidence (0.795 against 0.966 mrad). `ctest -j8` **1341/1341** Release with video, camera
and Ceres on (+3); warning-clean, format-check clean. The builds with all three off were not run. Full
notes in WORK.md's *Identities that do not depend on where footage sits*, under M9.

- **`capture/location.h`**, one rule:
  - a camera's location is its one source (a video) or the deepest folder holding its stills;
  - the footage's root is the deepest folder holding every location strictly within it;
  - `identitiesByLocation` names each camera by its location under the root (`cam0`, `cam0.mp4`);
  - `relativeTo` spells a source under a root.
- **Where it is applied:**
  - the nodes name cameras with it, so the cli's and the gui's bindings both get it;
  - `groupsByPosition` passes the root to a new `CaptureGroup::create(members, root)`, whose old overload
    and pinned digest are unchanged;
  - a moving camera's track identities spell their frames under the root, with the pinned digest
    recomputed outside lain.
- **What each method needed, found by reading:**
  - targetless registration only the names;
  - board registration the names and the groups' frames;
  - targetless calibration's numbers never depended on the folder, since it orders tracks by capture
    order, so only its reported identities change;
  - board calibration was already location-free.
- **Proved end to end**: the flowview verticals run every method again from a copy three folders down
  and require the same bytes (both registrations' reports and rig documents, both calibrations'
  attempts and models).
- **Three sabotages.** Groups spelling frames whole fails the board registration vertical; dropping the
  root's "strictly within" fails the location cases; track identities spelling frames whole is caught by
  the pinned digest only, as it should be.
- **Reports read `relative to cam0`**, which retires the verticals' `rig\cam0` Windows searches.

### Update 2026-10-21 — M9: held-out evidence judged from medians, and per camera

The review's P2 item 3: the three held-out changes queued on 2026-10-09, plus registration's
resampling spread. `ctest -j8` **1338/1338** Release with video, camera and Ceres on (+3);
warning-clean, format-check clean. The builds with all three off were not run. Full notes in WORK.md's
*Held-out evidence judged from medians*, under M9.

- **Fitness judges a median-based held-out residual**, `HeldOutEvidence::robustAngle`. Per kind of
  member (a track member against its predicted point, against an epipolar plane, a board view), it is
  the median mean squared residual per point times that kind's mean-to-median ratio under normal errors.
  It reads as the RMS under normal errors (0.52 against 0.55 mrad), and a few wrong members cannot decide
  it. The RMS stays, for diagnosis.
- **Leaving out outliers was the repo owner's first choice, and measuring refused it twice.**
  - The fit's rule flagged 6% to 14% of correct registrations' held-out members, since a held-out
    residual also carries its prediction's error.
  - A self-normalising rule flagged about a fifth on SIFT features, whose held-out residuals are
    heavy-tailed.
  - The median-based residual reads 0.23 to 0.31 mrad on those rigs, where the RMS reads 0.64 to 0.91,
    and 2.9 to 9.0 with a camera turned 5 mrad. The bounds are unchanged.
- **Each camera is judged on its own** once it has ten held-out points (`minimumHeldOutPerCamera`).
- **The resampling spread is median-based**, scaled to read as the RMS.
- **The profiles are amended in place**, provisional until real footage, and
  `registration-targetless/1` holds position to 0.2%.
- **One private routine gathers held-out evidence for both methods**
  (`registration::detail::heldOutEvidence`). `HeldOutCamera` replaces the name-and-RMS pairs.
- **Both goldens change on purpose**: every RMS line holds and no verdict moves.
- **Three new board cases and three sabotages**, each caught by the case aimed at it. The pass-through
  refiner gained an `adjust` hook to move one resample.

### Update 2026-10-20 — M9: the gui binds a camera model and a rig, and saves a model

A review found the gui could not bind a `CameraModel`, a rig's footage or a rig's models (each read
"(no editor)" in the Interface pane) or save a model, while the cli did all four. That blocked the owed
gui-mode checks of the registration nodes. `ctest -j8` **1323/1323** Debug and **1331/1331** Release with
video, camera and Ceres on, **1189/1189** Debug and **1197/1197** Release with all three off (+5 each);
warning-clean, format-check clean. **gui-mode NOT driven:** a six-step checklist is owed. Full notes in
WORK.md's *The gui binds and saves camera values*, under M9.

- **Decided with the repo owner:** editors only, with a `loadCameraModel` node deferred; one reading per
  type in a driver-free flowview unit; a model's Save... beside an image's, in both panes; and `listDir`
  listing folders deferred. Both deferrals are in the Outstanding index with their triggers.
- **`apps/flowview/src/camerafiles.{h,cpp}`**: `loadCameraModel`, `saveCameraModel`, `loadRigModels` and
  `openRigFootage`, each load returning the value or every problem, each problem naming its file. The
  cli's three binders and `run --model` call it, and so do the gui's three new editors and its Save....
  The rig-folder rule (entries not starting with '.', sorted by name) is stated once.
- **`panes/imagesave` became `panes/valuesave`** (`renderValueSave`), which dispatches on the value's type
  (ADR-0008 amended).
- **The cli changed in three small ways**, from sharing the rule: an empty or unlistable rig folder says
  why, every bad entry is logged rather than only the first, and a hidden `.json` in a models folder is
  skipped.
- **Five cases** in `test_camerafiles.cpp` hold the routines and the cli's binders to one answer, and
  **eight sabotages** were each caught as predicted. An unsorted rig folder was caught by the footage
  case only: this file system listed the models case's six files in name order anyway.
- **The checklist's data** (a board calibration's footage and documents, a board rig, a targetless rig
  and a moving camera's footage for the fallback) was rendered by an uncommitted probe through
  `test_cameravertical.cpp`'s writers. Both are in the session notes folder,
  `~/Library/Application Support/Claude/side-session-notes/local_c6e38da9-13ea-47f1-9f44-1213e6108518/`,
  as `gui-check/` and `gui-check-probe.cpp.txt`. Through the real binary the data gives *Ready* for both
  rigs and the calibration, and the model `run --model` saved binds back through `--imported` to an
  *Exploratory* held report, byte-identical out.
- **Another session was editing `register.cpp` and `runmode.cpp` at the same time**, so this change
  touches `runmode.cpp` by one call and one include only, and `register.cpp` not at all.
- **Earlier entries' all-off counts are Release ones**: Debug lacks eight `NDEBUG`-only cases in `core`,
  `image` and `log`, which is why 1184 + 5 here sits beside the recorded 1192.
- **Merged with the registered rig the same day:** `camerafiles` also reads and writes a rig
  document, so `--rig` and `run --rig` go through it, and a `RegisteredRig` output gets the same
  Save... as a camera model. That Save... is gui code; checklist step 7 covers it.

### Update 2026-10-20 — M9: a registration's rig leaves the process

A gap the 2026-10-20 review found, recorded nowhere: every camera's `referenceFromCamera` was in the
registration report and reached no file, since `run` writes a report as its one line. Now it leaves as
a **registered rig**, the way a calibration's model does. ADR-0016 is amended, and CONTEXT.md gains
*Registered rig* and *Rig document*. `ctest` **1322/1322** Debug and **1330/1330** Release with video,
camera and Ceres on, **1188/1188** Debug and **1196/1196** Release with all three off (+4 each).
Warning-clean, format-check clean. **gui-mode NOT eyeballed.** Full notes in WORK.md's *After slice 4:
the registered rig* under M9.

- **Decided with the repo owner:**
  - a value, a node and a document, with the report left as its line;
  - the reference and scale status travel with the transforms, and no verdict or evidence does;
  - a `--rig` binder now, though nothing consumes a rig yet (Outstanding index);
  - `Report::toString()` unchanged.
- **`registration::RegisteredRig` and `Report::rig()`**, set only on success, whatever the verdict.
  The node is **`registeredRig`**, offered when either registration can run.
- **The rig document** (`lain::camera::serialize`, `registeredrig.h`): versioned, rotation as the
  stored unit quaternion `{w, x, y, z}`, read strictly by path. Then it is read as a rig: a camera at
  least, none twice, the reference among them at the identity, unit rotations (a zero quaternion is
  caught before `RigidTransform`'s assert). Its version handling is shared with the camera-model
  reader (`serialize/src/versioned.{h,cpp}`).
- **flowview:** `run` writes a rig output as the document, and `--rig` binds one.
- **Found:** a rotation read back can differ from the one written by one ulp, because `RigidTransform`
  renormalises on construction. Round trips hold rotations to 1e-15, and `lain::math` is left alone.
- **The real binary**, on the verticals' footage written by an uncommitted probe, writes all three
  transforms for both registrations. cam1 comes out turned −0.25 rad and 104 mm across, as drawn.
- **Eleven sabotages, all caught.** Two first failed to build under `-Werror`, so they were redone to
  build before they counted.

### Update 2026-10-20 — the M9 review: what the slice plans left out

Once 12c landed, a review checked M9 against its plan, ADR-0016, ADR-0017 and the code. It found gaps
that nothing recorded, which the repo owner grouped into priorities. They are in WORK.md's new
*The 2026-10-20 review* at the end of M9's build order, and the Outstanding index points at them.
**12c's "M9's build work is done" holds for the slice plans only.** This entry is the P0 pass: no code
changed.

- **P1, in progress in two sessions of their own:**
  - a registration's result cannot leave the process. `Report::toString()` omits the per-camera
    transforms, `run` writes the report as that line, nothing serialises a result, and no node takes
    the transforms out;
  - the gui cannot load or save camera values that the cli can: no editors for a camera model or a
    rig's footage or models, no node reads a model document, the gui saves images only, and
    `listDir` lists files only.
- **P2:** the held-out changes queued on 2026-10-09, and a camera's identity independent of where its
  footage sits, by an interim cli fix.
- **P3:** a footage generator, example documents with a calibrate-then-register composition test,
  then one gui-mode session covering every M9 eyeball.
- **P4:** a CI leg with OpenCV on and Ceres off, and the footprint measured again.
- **Decided with the repo owner:**
  - debug overlays of detection evidence are deferred, with a trigger (ADR-0016 amended);
  - so is bulk projection, which ADR-0016 had promised;
  - a board's measured-length bounds are vocabulary waiting for a consumer;
  - capture manifests, capture datasets, clock domains and timestamp grouping come after M9 (WORK.md's
    prerequisite note and ADR-0016 amended).
- **Out-of-date claims corrected:**
  - M9's status line ("Designed, not started");
  - two prerequisite notes (node kinds register only when they can run; Ceres opt-in);
  - the README's OpenCV and Ceres rows ("today a build probe");
  - ADR-0016's count of node kinds (eleven, not seven);
  - the Outstanding index's "first CI run of the smoke step", which has run since 2026-09-30.
- **Three gui-mode checks recorded nowhere** joined the Outstanding index's *Verification owed*: slice
  1's Camera menu, enum editors and colours; the refusal to open a damaged document; and the Failed
  mark from `NodeEvaluation::fail`.

### Update 2026-10-20 — M9 slice 4, sub-slice 12c: the fallback end to end (**slice 4 COMPLETE**)

The last of sub-slice 12's three commits, and the last of slice 4. `ctest` **1318/1318** Debug and
**1326/1326** Release with video, camera and Ceres on, **1192/1192** with all three off (+5 each), and
**1267/1267** with OpenCV on but Ceres off; warning-clean, format-check clean. **gui-mode NOT eyeballed**:
that is owed now, with footage rendered for it. **CI run 40 (`b952e1d`) is green on all seven jobs**,
1326/1326 on macOS and Windows. Full notes in WORK.md's *Sub-slice 12c built* under M9 slice 4.

- **Five cases run the committed `calibrate-fallback.json` through `runGraph`**, reading which attempts
  were made from the attempts text:
  - board footage: *Attempt 1 of 1 accepted*, the pinhole 0.14% and 0.3 px out, the targetless attempt
    gated off;
  - a moving camera with no board in view: *Attempt 2 of 2 accepted*, 0.18% and 0.7 px out, 20 s in
    Release. It skips in Debug, since drawing takes seconds a frame;
  - `--method board`: one failed attempt, no fallback;
  - no `--method`: nothing attempted, exit 0, the cost of the deferred graph input defaults pinned;
  - a mistyped `--method`: exit 1.
- **Each build's missing backend is reported:** with none, `NoDetector` then `NoFeatureExtractor`. With
  OpenCV but no Ceres, `TooFewViews` then `NoRefiner`, which a new Ceres-off build here checks.
- **The real binary gives the same reports and models to the last digit**, on the vertical's footage
  written into the scratchpad by an uncommitted probe. The mistyped run names the three spellings.
- **Sabotage, caught as predicted:** the targetless gate enabled straight from the method, skipping
  `nextAttempt`, reads *Attempt 1 of 2 accepted* on board footage.
- **M9's build work is done.** What is left is owed by the repo owner (the real-camera capture, the
  held-out judgements, an honest feature covariance) or waits on real footage, plus the gui-mode
  eyeballs.

### Update 2026-10-20 — M9 slice 4, sub-slice 12b: `calibrate-fallback.json`

The second of sub-slice 12's three commits. `ctest` **1313/1313** Debug and **1321/1321** Release with
video, camera and Ceres on, **1187/1187** with all three off (unchanged); warning-clean, format-check
clean. Full notes in WORK.md's *Sub-slice 12b built* under M9 slice 4.

- **The fallback as a committed example**: inputs `footage` and `method`, outputs `attempts` and
  `model`, a 7 x 5 board of 24 mm squares, and `None` models. The report pins are named `board` and
  `targetless`. No other example's bytes moved.
- **`Example::needsInputs`**: `test_examples` loads such an example and requires a binder for each of
  its inputs, rather than running it. The real binary's `list` names the method's choices.
- **Sabotage, caught:** the `Method` binder unregistered fails the example's section.

### Update 2026-10-20 — M9 slice 4, sub-slice 12a: a calibration method binds by name

The first of sub-slice 12's three commits. `ctest` **1313/1313** Debug and **1321/1321** Release with
video, camera and Ceres on, **1187/1187** with all three off (+4 each); warning-clean, format-check
clean. Full notes in WORK.md's *Sub-slice 12a built* under M9 slice 4.

- **`bindEnum<E>`** binds an enumerator by name, ignoring case, `-` and `_`, never by number, and is
  registered for `calibration::Method` by one `addEnum<E>` line.
- **`BoundaryBinders::spellings(type)`** lists an enum's choices in `kebabCase`. `list` prints them
  after the type, and a refused `--method` is told them. Each listed choice is tested to bind back to
  its own enumerator.
- **Found by the first run:** `kebabCase` split an acronym from its digits (`BT709` as `b-t709`).
- **Two sabotages, both caught as predicted.**

### Update 2026-10-20 — M9 slice 4, sub-slice 12 planned: the fallback a user runs (nothing built yet)

The last of slice 4, planned as **three commits, 12a to 12c**, in WORK.md's *Sub-slice 12 planned* under
M9 slice 4. Item 12 of the slice plan and the Outstanding index are amended in place.

- **Decided with the repo owner:**
  - **Graph input defaults are deferred as a feature of their own**, not built here. They were the
    planned answer to 11's finding: an unbound boundary pin is empty, so `calibrationMethod`'s default
    never applies through one. Their design notes are recorded with the plan, and the Outstanding
    index points at them.
  - **The footage is rendered by the test**, never committed.
  - **The fallback nodes keep the camera colour.**
- **Taken as the consequence, not put to the repo owner:** `method` stays a boundary input, and
  `--method` is required. An unbound one attempts nothing and writes nothing, with a warning and
  exit 0, as any unbound input does, and 12c pins that cost. Leaving `method` unwired in the document
  would let its default apply, but the binder would then land with no caller. Either is a one-edge
  change in 12b.
- **12a, the enum binder:**
  - `bindEnum<E>` accepts an enumerator's name ignoring case, `-` and `_`, and is registered for
    `calibration::Method`.
  - `BoundaryBinders` carries each type's accepted spellings, in kebab case. `list` prints them, and
    a failed parse names them.
- **12b, the example:** `calibrate-fallback.json`, with inputs `footage` and `method` and outputs
  `attempts` and `model`. A new catalog flag, `needsInputs`, has `test_examples` load and list it
  rather than run it.
- **12c, the vertical:** the committed document through `runGraph`, on rendered footage.
  - Board footage is accepted at attempt 1.
  - Board-less textured footage from a moving camera is accepted at attempt 2, Release-only if Debug
    is too slow.
  - `--method board`, an unbound `--method`, and a mistyped one.
  - No backend.
  - OpenCV without Ceres, where the targetless attempt reports `NoRefiner`, but only after board
    fails, since board calibration needs no refiner.
- **Found while planning:**
  - The gui needs nothing new, since the Interface pane already binds `method` through 11c's
    `editEnum<Method>`. But until a method is picked nothing runs, and no Issues row says why.
  - `test_examples` could not run this example as it stands: its generic branch requires every output
    non-empty.
  - The gui eyeball owed after 12c needs footage rendered for it.

### Update 2026-10-20 — M9 slice 4, sub-slice 11c: the calibration fallback is a graph (**sub-slice 11 COMPLETE**)

The last of sub-slice 11's three commits: the attempts in the library, and the three small nodes that
switch the gates and combine the reports (ADR-0016). `ctest` **1309/1309** Debug and **1317/1317**
Release with video, camera and Ceres on, **1183/1183** with all three off (+12 each); warning-clean,
format-check clean. **gui-mode NOT eyeballed**; that is owed after item 12. **CI run 39 (`9f37938`) is
green on all seven jobs**, 1317/1317 on macOS and Windows. Full notes in WORK.md's *Sub-slice 11c
built* under M9 slice 4.

- **`calibration/attempts.h`**: `Method`, `includes`, `accepts` (succeeded and Ready, the one rule both
  the gate and the record ask) and `Attempts`, whose `accepted()` is derived, the first that accepts.
- **`calibrationMethod`, `nextAttempt`, `calibrationAttempts`**, offered wherever either calibration
  can run. The two that read attempts take them on "+"-added report pins, every one Optional, the order
  added the priority order, and **share an `AttemptReportsNode` base** (an addition to the plan) so they
  read the attempts one way. `calibrationAttempts` clears `accepted` when nothing is accepted, and both
  outputs when nothing was attempted.
- **`meta::toString` names an enum**, so `method` describes as `BoardThenTargetless` on a port.
- **flowview:** the `calibrationMethod` codec, its editor, two port colours, the catalog's expectations.
- **The fallback test holds both stand-in worlds**, `boardScene()` renamed from `scene()` to let it. One
  deviation: the scene's camera is a pinhole of the board camera's geometry, not the board's true
  camera, since the scene stand-ins' fundamentals ignore distortion. Seven cases over a retained
  Evaluation, each method, acceptance, an Exploratory or failed board falling back, both failing after a
  run that had accepted, an edit re-running one attempt, and footage missing upstream.
- **Four sabotages, all caught.** One caught more than the plan predicted: attempts leaving out a failed
  report fail three cases, since every fallback case after a failed board lists it.

### Update 2026-10-20 — M9 slice 4, sub-slice 11b: `calibrateCameraTargetless`

The second of sub-slice 11's three commits: targetless calibration as a node. `ctest` **1297/1297**
Debug and **1305/1305** Release with video, camera and Ceres on, **1171/1171** with all three off (+5
each); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 11b built* under M9 slice 4.

- **The node runs `targetless::calibrate`** over its footage, the camera named by the footage's source,
  under ten settings each defaulted from `targetless::Settings`, plus `CalibrationNode`'s. Its header
  points at `targetless.h`'s guidance (the HD lens; footage that keeps one point centred).
- **Kinds table:** `canCalibrateTargetless` is the three feature backends and a refiner, as targetless
  registration's is; `cameraModel` now follows `canUseCalibration`, either method. A build with no board
  backend offers both, and `registerCamerasTargetless`.
- **Its own test executable**, over the scene stand-ins: the settings read back from a refusal's record
  as well as a run's, the node's report equal to the method's through the shared printer, empty footage,
  and an imported model.
- **Two sabotages, both caught as predicted.** The first cut of one did not build under `-Werror` (an
  unused capability function), so the old binaries answered until it was redone: a sabotage that does
  not build proves nothing, again.

### Update 2026-10-20 — M9 slice 4, sub-slice 11a: the calibration nodes share a base

The first of sub-slice 11's three commits, with no change in behaviour. `ctest` **1292/1292** Debug and
**1300/1300** Release with video, camera and Ceres on, **1166/1166** with all three off, each unchanged;
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 11a built* under M9 slice 4.

- **`CalibrationNode`**, as `RigRegistrationNode` is for registration: `footage` as input 0, then
  through `declareRequest()` the optional `imported` model and the seven settings every calibration
  takes, read back by `request(evaluation)`. `calibrateCamera` is rebased on it and keeps its inputs;
  `maximumViews`, board calibration's alone, moves from the fourth Inspector row to the eighth.
- **Two private helpers with two callers each**: `detail::identityOf` (a camera named by its footage's
  first source, which `rig()` spelled inline) and `detail::extractionSettings` (the five extraction
  settings both targetless nodes expose). `registerCamerasTargetless` and `rig()` are rebased on them.
- **Sabotage, caught as predicted:** reading `heldOutFraction` without `detail::decimal` fails the
  existing settings case, on its defaults (0.2 is not exact in a float). Its every-parameter section
  alone would not see it, since 0.25 is.

### Update 2026-10-19 — M9 slice 4, sub-slice 11 planned: attempts and the flow nodes (nothing built yet)

Sub-slice 11 is planned as **three commits, 11a to 11c**, in WORK.md's *Sub-slice 11 planned* under
M9 slice 4. It adds a targetless calibration node, the attempts and their acceptance in the library,
and the three small nodes that switch the gates and combine the reports. ADR-0016's fallback bullet
is amended in place, and items 11 and 12 of the slice plan are amended where they disagree.

- **Decided with the repo owner:**
  - **The node goes ahead with the HD lens open.** It is Ready 1.45 to 3.56 px out, and an honest
    feature covariance is left to the repo owner. The node's header points at `targetless.h`'s guidance.
  - **The report pins on `nextAttempt` and `calibrationAttempts` are dynamic, as Merge's branches
    are.** Every pin is Optional, and the order they were added is the priority order. Fixed `board`
    and `targetless` pins were the alternative.
  - **The three commits:**
    - **11a**, with no change in behaviour, adds a `CalibrationNode` base, rebases `calibrateCamera` on
      it, and lifts two helpers out of `registerCamerasTargetless`.
    - **11b** adds `calibrateCameraTargetless`.
    - **11c** adds `calibration::Method`, `Attempts` and `accepts`, plus `calibrationMethod`,
      `nextAttempt` and `calibrationAttempts`, tested through a retained Evaluation.
- **Found while planning:**
  - **A new camera kind reaches flowview in the commit that adds it**, since the menu and the colours
    read the kinds table. Flowview's catalog test holds an expectation per kind and requires a codec
    for every param, so the `calibration::Method` codec and editor move from item 12 to 11c.
  - **The two calibration stand-ins cannot share a translation unit.** `syntheticboard.h`'s
    `testing::scene()` clashes with `syntheticscene.h`'s namespace `testing::scene`, and the two answer
    different footage. The board one is renamed, and the scene's frames take their extent from its
    model, so one footage serves both.
  - **No stand-in case reaches a Ready targetless verdict yet**, so 11c probes for one first.
  - **`calibrateCameraTargetless` asks `canRefine()`**, as registration does. `cameraModel` becomes
    "any calibration".
  - **`Attempts` derives the accepted one** rather than storing its position.
  - **With no attempt made, `calibrationAttempts` produces nothing**, so the absence propagates.
  - **An enum on a port describes as its type name**, so `meta::toString` gains an enum arm.

### Update 2026-10-19 — a refiner covariance does not see the HD lens's error (refused)

The last remedy left for the HD lens (f = 1000 at 1280x720), Ready 1.45 to 3.56 px out after the
second adjustment. Measured and **not built**: a probe computed the free model's marginal covariance
from the final adjustment's Jacobian, the landmarks eliminated and the reduced system pseudo-inverted
over its one null direction, the scale. Nothing in the library changed; WORK.md, ADR-0016 and this
file record the measurement. Full notes in WORK.md's *After sub-slice 10e* under M9 slice 4.

- **It agrees with the landmark bootstrap and is as blind**: 0.6 and 0.7 px per axis on HD (0.95 mrad,
  inside Ready's 2), against errors up to 3.05 px, five of its standard deviations. The f = 250, f =
  500 and board footage show the same understatement, errors of 2 to 4 of its standard deviations, so
  it is not the HD lens's.
- **The data puts the minimum there**: started at the truth, the adjustment ends within 0.3 px of the
  free one on every HD and lens seed, and the truth held costs as much per residual.
- **The observations carry no systematic bias but more noise than they state.** Against each track's
  consensus point through the truth there is no mean offset, trend or radial component, while the
  smallest quarter of HD features errs about 3.5 times its stated sigma and the rest 1.5 times. Both
  the covariance and the landmark bootstrap inherit that.
- **What is left for the repo owner: an honest feature covariance.** The localisation model (0.034 of a
  feature's size) understates the smallest features most, and any precision measure inherits it.
  Resampling frames saw 1.5 to 3.2 mrad on this lens, but was refused earlier for its noise.

### Update 2026-10-19 — targetless calibration adjusts again without what its result does not explain

Item 4 of 10e's recommendations, the principal-point blind spot, answered for the printed board's
footage. After a start's bundle adjustment, a second runs from its result without the observations
whose whitened residual exceeds the loss scale, started through the routine every bootstrap resample
uses. `ctest` **1292/1292** Debug and **1300/1300** Release with video, camera and Ceres on, **1166/1166**
with all three off (+4, +4, +3); warning-clean, format-check clean. Full notes in WORK.md's *After
sub-slice 10e* under M9 slice 4.

- **On the both-methods footage every seed is now within 0.6 px**, where three of six were Ready 2.7 to
  4.4 px out (seed 5: 4.43 px to 0.55, its principal point 4.2 px off to 0.53). The plain scene
  through two lenses improved on every seed too. **Not answered: the HD lens**, which sees no board,
  is Ready 1.45 to 3.56 px out over three seeds (1.5 to 4.7 before). The strongly distorted 1080p
  lens moved the other way, 0.90 to 1.25 px, still Ready.
- **A start is ranked by its first adjustment.** A second keeps whatever agrees with its own start's
  minimum: repeated three times, the long start shed 44% of its observations and fitted the rest at
  0.321 per residual against the right start's 0.340, and would have won.
- **One pass, at the loss scale (3).** That is the outlier definition every method shares, so no new
  number; twice the scale also removed the minimum. Further rounds moved the right minimum by about a
  tenth of a pixel either way. When the result explains every observation it fitted, there is no
  second, so a clean fit is unchanged.
- **Reported:** `TargetlessDiagnostics::pruned` counts what the second left out, each named an outlier
  with its residual under the first's result; one the first's start left out for cheirality is still
  named. The refinement summary is the second's.
- **The stand-in refiner hands back its start, which is no minimum**, so four stand-in cases about
  something else run under an infinite loss scale; three new cases pin the pass. The Ceres
  moving-content case's held-out frames now see none of the moving tracks (0.405 mrad, where 18
  observations were beyond the inlier angle and the rest measured 0.846).
- **End to end:** a new case pins seed 5, and the both-methods case's targetless bounds tighten from
  macOS's 3.05 px minimum to 1.5 px on average. **CI run 38 (`f343276`) is green on all seven jobs**,
  1300/1300 on each Release leg, so macOS and Windows land within them too.
- **Four sabotages, all caught as predicted**, including no second adjustment: seed 5 back at 4.43 px,
  still Ready.

### Update 2026-10-19 — feature/M9 is green on CI, on every platform

The branch had never been built by CI, so slices 2 to 4 and the 10e recommendations were first built on
macOS and Windows here. Five `workflow_dispatch` runs; the first four found eight things, each answered
in its own commit, and **run 37 (`2fe9abc`) is green on all seven jobs**: Linux Release, Debug and
video-off, macOS, Windows, clang-format and the subproject smoke. The Outstanding index's owed CI run is
retired. Full notes in WORK.md's *feature/M9's first CI runs*, under M9 slice 4.

- **MSVC sees what GCC does not without flags lain does not set**: C4456 (a local hiding another; GCC
  only under `-Wshadow`) and C4701 (possibly uninitialised: GLM's default-constructed matrix is
  uninitialised, so a matrix filled in a loop now starts at zero).
- **`cmake --build --parallel` with no count is `make -j` with no limit**, which thrashed macOS's runner
  past its 90-minute timeout. Every build step now names its runner's core count; macOS builds in 7.
- **A golden printed a worst camera chosen among equal variations by rounding noise**, and macOS chose
  another. The printer now writes `-` when the worst variation prints as zero.
- **The 100-camera scale case missed its wall-clock bound beside the end-to-end suites**; the scale
  cases now run serially (item 8).
- **OpenCV's SIFT finds other features on macOS and Windows** while board calibration is bit-identical
  everywhere, so the targetless end-to-end numbers differ by platform, and macOS lands in the
  principal-point blind spot. The cases now pin what holds on all three (item 4's entry).
- **Two tests assumed a platform:** flowview's rig verticals searched for `rig/cam`, which Windows names
  `rig\cam0`; and a case asking where detection ran used a stand-in that returned at once, so on
  Windows the participating caller ran all 40 frames before a worker woke. It now opts into a 2 ms
  detection, as the flow scheduler's several-threads case sleeps.

### Update 2026-10-19 — resampling frames does not see the principal-point blind spot (refused)

Item 4 of 10e's recommendations, measured and **not built**. The recommendation taken was to resample
frames as well as landmarks, so the stability evidence would see a principal point a few pixels off
that the held-out frames' poses absorb. It was built, measured on rendered footage, and reverted. What
lands instead is test code and documentation: the end-to-end cases now pin what holds on macOS
arm64 too, where CI showed the blind spot. The re-pinned cases pass in Release on Linux; the library
is unchanged. Full notes in WORK.md's *After sub-slice 10e* under M9 slice 4.

- **It does not see the wrong minimum.** On the both-methods footage it flagged one of the three seeds
  whose model is 2.7 to 4.4 px out. Across ten draws, one of those seeds measured 0.62 to 1.80 mrad,
  never past Ready's 2. A prototype with another draw seed had flagged two of the three, by luck.
- **It changed verdicts with the draw seed.** The HD lens was Exploratory on two of five draws at 20
  resamples and at 60. The normal lens's right model was pushed out of Ready on one draw of five.
- **Comparing the three starts does not see it either**: the losing starts end in far worse minima, and
  two starts agreeing did not mean right.
- **What differs is the winner's cost per residual**: 1.97 to 2.07 on the wrong seeds against 1.68 on a
  right one. That points at wrong tracks from the printed board's repeated corners. What is left for
  the repo owner: an outlier pass, tracks the board does not alias, or a covariance from the refiner.
- **macOS arm64 shows the blind spot** (CI run of `217deac`). OpenCV's SIFT finds other features there,
  while board calibration is bit-identical across platforms. The both-methods case's targetless model
  is Ready at 3.05 px and the distorted lens Ready at 2.70 px, where Linux gives 0.30 and 0.90; the HD
  lens is Exploratory on 2.38 mrad. Windows' features differ too (1.23 px on the first). The targetless bounds are now that minimum's (5 px and 3.5 px), the
  HD lens may be Exploratory on its principal point alone, and each case records both platforms.
  `targetless.h`'s guidance says a Ready model can still be a few pixels out.
- **The same CI run proved the earlier fixes:** Linux Release, Debug and video-off, clang-format and
  the subproject smoke are green, Windows builds, and the targetless scale case took 38.8 s on macOS.
  Windows' other failure was two flowview tests searching a report for `rig/cam` where Windows names
  the camera `rig\cam0`, fixed in its own commit ahead of this one.

### Update 2026-10-19 — the scale cases run with nothing beside them

The fourth of the commits that take 10e's recommendations (item 8). The 100-camera targetless
registration bounds its wall-clock time at 120 s, and under `ctest -j8` beside the end-to-end suites
it took 154 to 181 s, against 36 s alone. `test-camera-ceres`'s four `[scale]` cases are now
discovered by a second `catch_discover_tests` call with `RUN_SERIAL`, so ctest starts nothing else
while one runs. The whole Release suite at `-j8` passes **1296/1296**, the targetless scale case in
35.5 s. Debug, where the scale cases skip, and the all-off build are unchanged. Full notes in WORK.md's
*After sub-slice 10e* under M9 slice 4.

- **The bound stays where it was.** A larger one would stop catching a real slowdown, and
  `PROCESSORS` reserves slots out of whatever `-j` the caller chose, so it leaves a case alone only
  when its count reaches that `-j`.
- **Two discovery calls on one executable do not collide**: Catch2 names each include file after a
  hash of its arguments. The same 49 cases are discovered, 45 and 4.

### Update 2026-10-19 — targetless calibration judges its held-out residual whitened

The third of the commits that take 10e's recommendations (item 1). A scene feature is located to a
fraction of a pixel whatever the lens, so `reconstruction-targetless/1`'s fixed held-out angle held a
wide lens to a tighter bound than a long one: through f = 250, a model 0.12% out in focal length was
Exploratory on its 2.19 mrad alone. The profile now judges the held-out residual **whitened by each
observation's stated covariance**, RMS per axis, at 4 for Ready and 8 for Exploratory, and leaves the
angle unjudged. `ctest` **1288/1288** Debug and **1296/1296** Release with video, camera and Ceres on,
**1163/1163** with all three off (+2 each); warning-clean, format-check clean. Full notes in WORK.md's
*After sub-slice 10e* under M9 slice 4.

- **Whitened rather than pixels**, because the profile's contract is that every criterion is
  resolution-independent: a pixel bound would hold an 8K camera's features to a VGA camera's.
- **`HeldOutEvidence::rmsWhitened`** goes through the `whitenedSquared` the refinement already uses,
  divided by twice the count, so 1 means as precise as stated. Absent when an observation states no
  covariance; board calibration does not measure it, and `reconstruction/1` does not judge it.
- **A new criterion, `maximumHeldOutWhitened`**, infinite unless a tier judges it, with an override.
  A tier that judges it needs the evidence to be Ready.
- **Measured:** the right models are 1.80 to 2.87 at focal lengths of 250 to 1500, where their angles
  went from 0.61 to 2.19 mrad. They sit above 1 because a small feature states less than its true
  error. Noise at the stated covariance through Ceres measures 1.18.
- **Neither statistic sees a wrong model the reconstruction absorbs**: a principal point held 10 px
  off, or no distortion model on a distorted lens, validated as well as the truth on rendered footage.
  That is item 4's; this commit only stops holding a wide lens to a tighter bound.
- **The board golden changes in eleven lines, one printed field each**, and no number moves.
- **Two sabotages, both caught as predicted.** The RMS taken over residuals rather than axes reads
  each value 1.41 times high: it fails the Ceres noise case's upper bound (1.67) and turns the held
  principal point Rejected (8.53). A judge that ignores the bound fails the stand-in tiers' two
  sections and leaves the held principal point Ready.

### Update 2026-10-19 — a calibration's stability is a robust spread

The second of the commits that take 10e's recommendations. `calibration::detail::stability`, shared by
board and targetless calibration, measured the resampled fx, fy, cx and cy by their standard
deviation, so one bootstrap draw that left too few views to condition the model decided the verdict.
It now takes the median absolute deviation from the median, scaled by 1.4826 so that for normally
spread estimates it equals the standard deviation and the profiles' bounds keep their meaning.
`ctest` **1286/1286** Debug and **1294/1294** Release with video, camera and Ceres on, **1161/1161**
with all three off (+1 each); warning-clean, format-check clean. Full notes in WORK.md's *After
sub-slice 10e* under M9 slice 4.

- **Board calibration on 10e's both-methods footage is Ready on all six seeds**, where it was Rejected
  on two at focal variations of 89% and 32%. Focal variation is now 0.08 to 0.15% and principal-point
  variation 0.9 to 1.5 mrad, and each model is 0.009 to 0.071% out in focal length.
- **Targetless calibration's spread shrank where its resamples agree**: principal-point variation is
  0.025 to 0.43 mrad on the seeds whose model is right, and 0.29 to 0.68 mrad on the three that are
  2.7 to 4.4 px out (item 4, still open). As a standard deviation it was 0.4 to 0.75 mrad.
- **The golden changes in its two resampling lines only**, by a few percent on well-behaved resamples.
- **A new case**: one resample of twenty answered half as long again leaves the result Ready, with a
  variation of 4.4e-6. Sabotage: restoring the standard deviation gives 10.9% and Rejected.
- **The seeded-draws case needed a finer fingerprint**: its stand-in's focal length was a count of
  views, and the median absolute deviation of whole numbers matched across seeds.
- Registration's resampling spread, an RMS about the result per camera, is unchanged and stays queued
  with its robust held-out statistic.

### Update 2026-10-19 — a moving camera keeps 2048 features a frame

The first of the commits that take 10e's recommendations, which the repo owner accepted whole on
2026-10-19. `feature::SequenceExtractionRequest` now defaults `featureCap` to 2048, where the fixed
rig's `ExtractionRequest` keeps 8192. Two other recommendations are recorded and need no code: 40
sampled frames stays the default, and 10c's ranking of starts stays. `ctest` **1285/1285** Debug and
**1293/1293** Release with video, camera and Ceres on, **1160/1160** with all three off (+1 each).
Release ran the three 100-camera scale cases on their own, since the registration one fails its
wall-clock bound under `-j8`; a later commit addresses that. Warning-clean, format-check clean. Full
notes in WORK.md's *After sub-slice 10e* under M9 slice 4.

- **The two extractions now differ in one default, deliberately.** The rig's 8192 is the repo owner's
  decision of 2026-10-09: it never binds on sub-slice 9's footage, and real footage is still owed. A
  derived aggregate cannot give a base member another default, so the moving camera's request sets it
  in a constructor and is no longer an aggregate. Nothing brace-initialised one with values.
- **No case moves:** the default texture never reaches the cap. Across the end-to-end suite, with the
  cases instrumented, no frame brought more than 964 features, and every case that measures the cap
  names the cap it measures. A new case pins the two defaults apart; with the constructor defaulted,
  both of its 2048 checks fail.
- **Found by the first CI run of `feature/M9`, fixed in its own commit ahead of this one:** MSVC's
  C4456. `extraction.cpp` named a keypoint `k` inside a function whose sample count is `k`, so
  `lain::camera` failed to build on Windows and nothing downstream of it compiled. GCC reports this
  only under `-Wshadow`, which lain does not enable. A `-Wshadow` sweep of every camera and flowview
  translation unit found two more, both renamed.

### Update 2026-10-18 — M9 slice 4, sub-slice 10e: calibration end to end, and what the comparison found (**sub-slice 10 COMPLETE**)

The last of sub-slice 10's five commits: `test-camera-calibration-e2e`, four cases through OpenCV and
Ceres on rendered footage, Release only, plus the measurements the plan owed the repo owner and the
operator guidance in `targetless.h`. `ctest` **1284/1284** Debug and
**1291/1292** Release with video, camera and Ceres on (+4 each; they SKIP in Debug), **1159/1159** with
all three off; warning-clean, format-check clean. The Release failure is the 100-camera registration
scale case on its wall-clock bound under `-j8` (154 s against 120; alone 36 s), more often now that this
suite runs beside it. Its cases count as four processors to ctest, which helps but does not stop it. Full notes in WORK.md's *Sub-slice 10e built* under M9
slice 4.

- **The cases:**
  - one camera calibrated both ways from the same 48 frames, both Ready. The targetless model is
    0.033% out in focal length and 0.30 px mean in ray mapping, the board's 0.071% and 0.72 px, and
    the two are 0.090% and 0.93 px apart;
  - the default 40 sampled frames through three lenses. f = 500 and f = 1000 (HD) are Ready. f = 250 is
    accurate but Exploratory, on its held-out angle alone;
  - a rendered pan refused before any start;
  - k1 = -0.25 at 1080p, with no frame unplaced and the result Ready.
- **Deviation:** the plan wanted footage that makes the board Ready on every seed. Over six seeds it
  is Ready on four, so the case pins seed 3 and the spread is recorded.
- **For the repo owner, measured:**
  - **The held-out angle:** the residual is 0.6 to 0.9 px whatever the lens, so a wide lens fails a
    fixed angle. Options: pixels, or whitened by the stated covariance.
  - **Sampled frames:** only 40 of 12, 20, 30 and 40 was Ready on all three seeds. Keep it.
  - **Feature cap:** accuracy is saturated by 1024 to 2048 at 1080p. Exact matching takes 171 s at
    2048 against 477 s at 4096. 2048 is recommended.
- **Found, for the repo owner:**
  - **The targetless verdict does not see a principal point several pixels off.** HD runs are Ready
    at 1.5 to 4.7 px mean. Held-out frames' poses absorb the error, and resamples start from the
    result.
  - **A printed board in close views pulls the targetless fit off** on three seeds of six (2.7 to
    4.4 px mean). Without the board it stays within 1.6 px.
  - **Board calibration's stability is a standard deviation decided by one bad resample:** 2 of 6
    seeds are Rejected at 89% and 32%.
- **10c's deviation never decided anything here:** every start placed the same frames.
- **Three sabotages, all caught.** Without 10d's homography test, case 1's targetless model also goes
  wrong (7.0 px mean), since the board's near-planar close views then seed. With the principal point
  held, the normal and wide lenses fail on ray mapping while still Ready, as 10b found.

### Update 2026-10-18 — M9 slice 4, sub-slice 10d: a pair a homography explains does not seed

The fourth of sub-slice 10's five commits. Targetless calibration tests every pair of frames once, on
pixels, before any start, and a pair whose shared tracks a homography explains may not seed.
`ctest` **1280/1280** Debug and **1287/1288** Release with video, camera and Ceres on (+7 each),
**1159/1159** with all three off (+6); warning-clean, format-check clean. The Release failure is
targetless registration's 100-camera scale case on its wall-clock bound under `-j8` again (132 s
against 120); alone it passes in 36.4 s. Full notes in WORK.md's *Sub-slice 10d built* under M9
slice 4.

- **The geometry seam gains `homographies`**, pure virtual, so every backend answers it here.
  - Its facade `feature::homography` measures each candidate's transfer distance both ways in source
    pixels, and chooses by the facades' rule.
  - OpenCV's producer is `findHomography` with USAC. On a rendered pan it gives the same matrix on 20
    seeds, at 0.025 px over exact correspondences.
- **The rule:**
  - A pair of frames sharing five tracks may not seed when a homography explains 80% of them within
    4 px.
  - When every pair is refused, the calibration fails InitialisationFailed before any start, saying
    the camera turned without moving or saw one plane.
  - The held-out anchor is chosen from the pairs that may seed.
  - `TargetlessDiagnostics` counts the pairs tested and refused.
- **The risk the plan flagged did not materialise.** At 40 samples on rendered footage, 35 to 69 of
  780 pairs were refused, and the seeds moved to frames one or two apart. Every f = 500 run was Ready.
- **A rendered pan is refused**, where it used to be posed as an orbit and judged Ready.
- **Found: distortion bends a homography.** A pan through k1 = -0.2 (51 px at the corners) escapes the
  test on 4 of 66 pairs, and is posed as an orbit and judged Ready, 0.34% out in focal length. It is
  refused through k1 = -0.15. This is recorded for 10e's operator guidance, not fixed: the test needs no
  model, and only a model would undistort.
- **The parallax rule is still reachable:** a pan a quarter of whose landmarks move is explained at
  75%, so it seeds, and is then refused for its points at infinity.
- **Two sabotages, both caught as predicted.**

### Update 2026-10-16 — M9 slice 4, sub-slice 10c: three starts, and the one that reconstructs the most wins

The third of sub-slice 10's five commits. The focal sweep's 21 two-view-scored candidates are replaced
by three starts, each reconstructed and adjusted. `ctest` **1273/1273** Debug and **1281/1281** Release
with video, camera and Ceres on (+2 each), **1153/1153** with all three off (+1); warning-clean,
format-check clean. Full notes in WORK.md's *Sub-slice 10c built* under M9 slice 4.

- **The starts are the centres of the sweep range's geometric thirds** (260.25, 516.41 and 1024.69 for
  a pinhole at 640 wide). Validation and resampling run once, on the winner. An imported model is the
  one start.
- **Deviation, found by the tests and needing the repo owner's confirmation: the start whose
  adjustment kept the most frames wins, and only among those the lowest cost per residual.** A cost
  per residual compares fits of the same footage only: a start that reconstructed 3 frames of 19 cost
  0.45 per residual against the right start's 0.87 over 17. Seeds are already ranked by reach first.
- **A failing start is reported in `TargetlessDiagnostics::sweep`** (focal length, frames placed, cost
  or reason). When all fail, the start that got furthest is reported first, then the others.
- **Found, left for 10d:** the stand-in pan is now reconstructed by the long start and only rejected by
  the verdict.
- **Three sabotages, all caught.**

### Update 2026-10-16 — M9 slice 4, sub-slice 10b: the principal point is freed by default

The second of sub-slice 10's five commits, reversing the 2026-10-12 decision as planned.
`targetless::Settings::freePrincipalPoint` defaults to true. `ctest` **1271/1271** Debug and
**1279/1279** Release with video, camera and Ceres on, **1152/1152** with all three off (no case added);
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 10b built* under M9 slice 4.

- **9c's principal-point case is now (8, -6) px off and recovered by default**, exactly. The stand-in
  geometry places a start 10 px off, which the case had avoided.
- **Held, that offset is Exploratory on exact tracks** (held out 2.17 mrad against Ready's 1.5, the ray
  mapping 10.7 px out). On the probe's rendered footage the held-out frames absorbed such an offset and
  the model was Ready, which is the reason for the change; the case says which.
- **Cases that relied on the held default say so**, and the noisy cases' ray-mapping bounds are
  re-measured: freeing the principal point adds its own 0.14 to 0.56 px error.
- **Sabotage, caught:** the default left held fails the offset case.

### Update 2026-10-16 — M9 slice 4, sub-slice 10a: a resample starts as the adjustment does, and the fit RMS ignores outliers

The first of sub-slice 10's five commits. `ctest` **1271/1271** Debug and **1279/1279** Release with
video, camera and Ceres on (+1 each), **1152/1152** with all three off (unchanged); warning-clean,
format-check clean. Full notes in WORK.md's *Sub-slice 10a built* under M9 slice 4.

- **One routine starts the adjustment and every bootstrap resample** (`startOver` in
  `calibration/targetless.cpp`, over `reconstruction::startFrom`). A resample starts under the refined
  model, whose domain may end at its fold where the adjustment's neutral start's did not, so an
  observation the refined model cannot project is left out instead of failing the resample at Ceres'
  first evaluation. The resample's own view-and-landmark loop is deleted.
- **The fit RMS is over the observations not named outliers**, and its count is a new shared
  `Diagnostics::fitResiduals`. Board calibration fills it with every feature of its calibration views,
  so the board golden changes on purpose in its four diagnostics lines.
- **The new case** folds a Brown-Conrady camera's distortion 44.8 degrees off its axis, just outside its
  image, and matches eight landmarks wrongly in a frame that sees them beyond that fold. 20 of 20
  resamples produce a model, Ready. With every observation kept, none did, and the run was Exploratory:
  the probe's failure, reproduced.
- **Two sabotages, both caught as predicted.**

### Update 2026-10-16 — M9 slice 4, sub-slice 10 planned: rendered footage finds three defects in targetless calibration (nothing built yet)

Sub-slice 10 is planned as **five commits, 10a to 10e**: four fix the method, and the fifth is the
end-to-end comparison. Before planning, a scratch probe ran 9b's method through OpenCV and Ceres on
rendered footage, and it showed what the stand-ins could not. The plan is in WORK.md's *Sub-slice 10
planned* under M9 slice 4. ADR-0016 and CONTEXT.md are amended in place.

- **The focal sweep's score is flat on real tracks.** On the pairs it scores, every candidate from
  f = 342 to 1443 explains the same matches to within two, and ties go to the shortest. The bundle
  adjustment converges from about 0.7 to 3 times the true focal length. At the default 40 samples, two
  of six runs came back about 58% wrong, Rejected, and one failed to initialise.
- **A bootstrap resample can fail on a wrong match.** The match's landmark lies beyond the refined
  model's fold radius, and Ceres fails the resample at iteration 0. A run can lose all its resampling
  evidence, and with it Ready.
- **A held principal point blinds Ready.** The held-out frames' poses absorb its offset. A model 10 to
  18 px out in ray mapping was Ready.
- **A rendered pan is posed as an orbit and judged Ready**, unless its start is exact. 9c deferred this
  question here.
- **Not owed:** a second placement round. Distorted footage up to k1 = -0.25 at 1920 x 1080 left no
  frame unplaced.
- **Decided with the repo owner:**
  - three starts at geometric thirds of the sweep's range, with the lowest final cost per residual
    winning (a wrong minimum costs 3.8 to 8.8 per residual, a right one 0.82 to 1.11);
  - a homography test refusing a seed pair that turns or sees one plane;
  - the principal point freed by default, reversing 2026-10-12.
- **10a** fixes what a resample starts from and the fit RMS. **10b** frees the principal point by
  default. **10c** builds the three starts. **10d** builds the homography test. **10e** adds the
  comparison and the measurements for the repo owner: the held-out bound (near the noise at f = 500,
  past it at f = 250), the default samples, and the feature cap.

### Update 2026-10-15 — M9 slice 4, sub-slice 9c: targetless calibration through Ceres (**sub-slice 9 COMPLETE**)

The last of sub-slice 9's three commits, test code only. It runs the method as 9b built it, refined
by the real Ceres refiner, over 9b's stand-in scene and geometry: eight cases in `test-camera-ceres`.
`ctest` **1270/1270** Debug and **1278/1278** Release with video, camera and Ceres on (+8 each),
**1152/1152** with all three off (unchanged); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 9c built* under M9 slice 4.

- **What the cases pin, each bound set from a measurement written beside it:**
  - a pinhole and a Brown-Conrady camera recovered from exact tracks;
  - noise at the stated covariance calibrates within it and is Ready;
  - a wrong match and moving content are named by track and frame, and leave the model in place;
  - a principal point 3 px off is recovered when freed, and is reported `Held` and missed when held,
    still Ready, which is what the profile cannot see;
  - one report serially and on a pool, resampling included;
  - one report however the tracks are named.
- **Found by the first run, fixed in its own commit ahead of this one:** the Ceres refiner's answer
  depended on where the heap put a free model's block (the entry below).
- **The report printer the two suites compare with is shared** (`targetlesscalibrationtext.h`), so
  "one report" means the same in 9b's cases as in 9c's.
- **The probe's measurements held through the cases, with one exception.** The refiner fix moved the
  wrong match's effect on the focal length from 2.6e-6 to 2.8e-6 of itself.
- **Two sabotages, both caught, one prediction wrong.**
  - The bootstrap drawn inside the parallel loop from one unlocked engine is caught through a race.
    Over three runs the determinism case failed twice and the identities case three times. 9b's
    stand-in refiner cannot see it.
  - Tracks left in identity order were predicted to fail the identities case alone. They fail 6 of
    the 8 cases and 2 of 9b's, leaving frames unplaced. The stand-in geometry scores a pose on 16
    evenly spaced correspondences of its input, so the order decides which 16, and under a start 7%
    short that decides whether a frame is placed.
- Every case but the serial-against-pool one starts a pool of four: 68 s for all eight in Debug,
  0.06 to 0.5 s each in Release.

### Update 2026-10-15 — a free model's solution no longer depends on where the heap put it (fixed ahead of M9 slice 4, sub-slice 9c)

Found by 9c's first run and fixed in its own commit ahead of it. 9c's two cases that compare one
report produced two ways came out different in the last bits: a targetless calibration run serially
against one run on a pool, and tracks named to sort two ways. The Ceres refiner put a free model's
block in the cameras' ordering group. Ceres keeps a group as a set ordered by address, and AMD's
ordering of the reduced system breaks ties by that order. The cameras sit in one array, but a model
block is an allocation of its own, so the answer's last bits followed wherever the heap put it. A
resample on a pool worker allocates from another arena. `ctest` **1262/1262** Debug and
**1269/1270** Release with video, camera and Ceres on (+1 each), **1152/1152** with all three off.
The Release failure is the 100-camera targetless registration scale case on its wall-clock bound under
`-j8` again (134 s against 120); alone it passes in 37.2 s. Also warning-clean, format-check clean.
Full notes in WORK.md's *A fix built ahead of 9c* under M9 slice 4.

- **Each free model now has an ordering group of its own, after the cameras' group**, at `2 + m`. A
  single group for every model would still order two models by address.
- **A new case in `test_freemodel.cpp`**: two models, both freed, through noise. The problem is
  solved once, then under eight heap layouts, each on a thread of its own, and every solution must be
  bit for bit the first's.
- **Two sabotages, both caught.** The models back in the cameras' group fails 4 of the 8 layouts.
  Every model in one shared group fails 2, which is what the second model is for. Which layouts
  catch it depends on glibc's allocator; with the fix nothing depends on an address, so it passes
  under any allocator.
- **No golden moves**: no golden frees a model. ADR-0017 is amended in place.

### Update 2026-10-15 — M9 slice 4, sub-slice 9b: `calibration::targetless`, over stand-ins

The second of sub-slice 9's three commits: the method itself, from the focal sweep to the verdict,
tested over stand-ins (9c tests it through Ceres). `calibration::targetless::calibrate` takes footage
or a track set, plus `Settings` and the shared `Request`. `ctest` **1261/1261** Debug and
**1268/1269** Release with video, camera and Ceres on, **1152/1152** with all three off (+14 each:
13 cases in the new `test-camera-targetless-calibration`, 1 in `test-camera-nobackend`); every
golden untouched; warning-clean, format-check clean. The Release failure is targetless registration's
100-camera scale case on its wall-clock bound, 120.44 s against 120, run beside the two heaviest
cases under `-j8`; every other check in it passed, and alone it passes in 37.1 s. Full notes in
WORK.md's *Sub-slice 9b built* under M9 slice 4.

- **Shared helpers moved:** what both calibration methods decide alike leaves `board.cpp` for
  `src/calibration/common.{h,cpp}` (`failed`, `resolveProfile`, `checkImported`, `initialFrom`,
  `stability`); `whitenedSquared` moves from registration to `camera::detail` (`src/whitening.h`).
  Board calibration's golden and both registration goldens are byte for byte.
- **Deviation: footage that only turns is refused when most of the seed's matches are points at
  infinity.** The planned rule, the seed's median triangulation angle below the minimum parallax,
  cannot fire while anything triangulates, since `triangulate` succeeds only at or above that
  parallax. A pan with every tenth landmark moving put 108 of 120 matches at infinity, but the 12
  moving ones triangulated at a median 64 mrad, so the planned rule let it through.
- **Two findings for the repo owner:**
  - **A pan between two sweep candidates reconstructs as a spurious orbit over the stand-ins.** With
    the truth at f = 500, the sweep starts at 516.4. A pan of another angle plus an arbitrary sideways
    baseline then explains the rays better than the true pan: every seed match triangulates (52 mrad),
    and only the verdict rejects the result (5.3 mrad held out). A rotation-only test would tell a pan
    apart under any model; the open question is whether to add one, or to let sub-slice 10 measure a
    rendered pan through OpenCV first.
  - **Footage whose every frame looks at one point does not say its focal length**, the classic
    critical configuration. `resetMoving`'s camera tied all 960 matches across 11 sweep candidates, so
    the tests use a new `scene::resetWandering` that re-aims and rolls, as sub-slice 8's `arcPath`
    does.
- **Four sabotages, all caught, one prediction wrong.** The sweep scored at a fixed angle fails only
  the case that reads the angles handed, and still starts within one candidate of the truth, rather
  than picking the longest as planned. The first cut of another did not build under `-Werror`, so the
  previous sabotage's binary answered until it was redone.

### Update 2026-10-15 — M9 slice 4, sub-slice 9a: what targetless calibration shares moves where it can reach it

The first of sub-slice 9's three commits, with no change in behaviour. Calibration cannot depend on
registration (ADR-0016), so three things targetless calibration needs move down to `lain::camera`.
`ctest` **1247/1247** Debug and **1255/1255** Release with video, camera and Ceres on, **1138/1138**
with all three off (unchanged); every golden untouched; warning-clean, format-check clean. Full notes
in WORK.md's *Sub-slice 9a built* under M9 slice 4.

- **`holdOut` and `bootstrapDraws`** are `camera::detail`'s (`src/resampling.{h,cpp}`), named in
  registration by using-declarations.
- **`refinement::Summary`** replaces registration's `RefinementSummary`, which stays as an alias.
- **The coverage grid** is `src/calibration/coverage.{h,cpp}`, over pixels. **Deviation:** its size
  moved to `calibration/fitness.h`, beside `minimumCoverage`, rather than staying in `board.h`, which
  names it by using-declarations. A grid both methods share could not otherwise reach it.
- **Two sabotages, both caught.** A 7-column grid fails two cases beyond the predicted golden, since
  board calibration's view selection scores grid cells.

### Update 2026-10-15 — M9 slice 4, sub-slice 8: a moving camera rendered and measured

One commit of test code only. The producer landed in 7a, so this is the moving camera drawn and what
real SIFT and the real solver make of its footage. `ctest` **1247/1247** Debug and **1255/1255**
Release with video, camera and Ceres on (+3 each, two skipped in Debug), **1138/1138** with all three
off; warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 8 built* under M9 slice 4.

- **`RenderedSource` takes a pose per frame** and `drawAll()` draws the missing frames on the pool.
  `synthetic::arcPath(frames)` rises, rolls and re-aims as it goes, since a path at one height turning
  only about the vertical is critical motion for self-calibration. `projectInto` and the OpenCV
  feature helpers each now live once (`texturedscene.h`, `renderedfeatures.h`).
- **A moving camera's tracks, against the truth:** 1028 tracks from 8 of 12 frames, the same in Debug
  and Release, on the pool and serially. All 28 pairs verify. 11 tracks are the disc's, 912 of the
  other 1017 are within 2 px, and **17 are wrong, up to 571 px out along an epipolar line**, mostly on
  the floor. A fundamental matrix cannot see those; sub-slice 9's robust loss and outlier report can.
- **The threshold stays at 4 px.** At a 1920 search it keeps every correct match of both pinhole
  pairs, and refuses a crossing disc between adjacent frames through every lens. Every lens admits
  the disc at 16 px.
- **Distortion costs a wide baseline, not adjacent frames.** At 4 px adjacent frames keep everything
  up to a 317 px corner shift; the path's ends keep 95% of their outer third at 193 px and 84% at
  317 px. ADR-0016 is amended with these numbers. The trigger it names is not reached.
- **All 780 pairs of 40 frames cost 1.2 core-hours Exact** (18 minutes on four cores) and about 6
  core-minutes Approximate, with or without the pre-screen, which skips nothing on a path that always
  sees the scene. The plan's 2.2 core-hours was 1.4 times too high from the machine (slice 3's own
  pair takes 7.07 s here, not 10.2) and 1.3 times from the rows (1.31 a feature here, not 1.49).
- The plan's corner shifts are from the image's outer corner, (-0.5, -0.5), not the corner pixel's
  centre.
- **Two sabotages, both caught, one prediction wrong.** Every frame drawn at the first pose fails the
  moving-camera case alone, as predicted. The threshold handed on times 1000 fails it on the disc and
  on the wrong tracks (6.0% against 1.7%), but not on the share within 2 px, which was predicted to
  fail: admitting everything costs that share only 4 points. It also fails `test-camera-tracks`'
  threshold and moving-content cases.

### Update 2026-10-15 — M9 slice 4, sub-slice 7c: a moving camera's feature tracks (**sub-slice 7 COMPLETE**)

The last of sub-slice 7's three commits. `feature::extractSequenceTracks` turns one moving camera's
footage into feature tracks, every sampled frame its own view. A frame pair is verified by 7a's
fundamental matrix on source pixels, since no model exists yet, and everything else runs through 7b's
module. Nothing calls it yet; sub-slice 8 does. `ctest` **1244/1244** Debug and **1252/1252** Release
with video, camera and Ceres on, **1138/1138** with all three off (+10 each); warning-clean,
format-check clean. Full notes in WORK.md's *Sub-slice 7c built* under M9 slice 4.

- **The threshold is stated in the pixels searched** (4.0, COLMAP's, provisional), and turned into
  source pixels by the frames' scale before it reaches the facade, as the rig's static distance is.
- **Frames are sampled in capture order**, at bin centres, so the order footage lists them in does
  not matter, and a frame given twice is refused.
- **A sequence track's identity has its own versioned text**, naming each observation's frame source
  and ordinal as well as its camera and pixel, pinned against a digest computed outside lain. The
  rig's text and its pin are unchanged.
- **A frame that does not decode stays a view with no observations**, as a rig keeps a camera with
  no tracks. The plan stated this without testing it, so it has a case.
- **The scene stand-in gains a moving pinhole camera** and a `TruthGeometry::fundamentals` that
  proposes the truth's matrices and logs the threshold it was handed.
- **Two sabotages, both caught as predicted**: the frame left out of the identity text fails the pin
  alone, and the threshold handed on unconverted fails the half-scale check alone.

### Update 2026-10-15 — M9 slice 4, sub-slice 7b: the extractions' shared work becomes a module

The second of sub-slice 7's three commits, with no change in behaviour beyond the order in which a
request's several problems are named. The fixed rig's per-frame, per-pair and union work, which the
sequence extraction (7c) shares, moves out of `extraction.cpp`. `ctest` **1234/1234** Debug and
**1242/1242** Release with video, camera and Ceres on, **1128/1128** with all three off (+1 each);
`test_tracks.cpp` untouched; warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 7b
built* under M9 slice 4.

- **`feature::ExtractionSettings`** holds the eight settings both extractions read, and
  `ExtractionRequest` derives from it (the repo owner's call), so the measured defaults are stated
  once and `request.featureCap` reads as before.
- **`src/feature/cells.{h,cpp}`, in `camera::detail`**: an image's features as cells, a pair matched
  before verification, the union with its conflict rule, and the shared checks. Verification and how
  a track is spelled stay each extraction's own.
- **A rig track's identity is pinned** against a digest computed outside lain, and the pin passes on
  the pre-change library too, so "rig identities do not change" is checked rather than argued.
- **Sabotage, caught as predicted; timing unchanged** (3.73 s before and after, Release, alternating).

### Update 2026-10-15 — M9 slice 4, sub-slice 7a: the fundamental matrix of two images

The first of sub-slice 7's three commits. Targetless calibration verifies a frame pair before any model
exists, so the geometry seam gains `fundamentals(pixelPairs, threshold, seed)` and lain a
`feature::fundamental` facade over it, beside the pose facades. OpenCV's producer lands here too, not in
sub-slice 8 (the repo owner's call), since the seam method is pure virtual. `ctest` **1233/1233** Debug
and **1241/1241** Release with video, camera and Ceres on (+5 each), **1127/1127** with all three off
(+4); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 7a built* under M9 slice 4.

- **Producers propose; lain decides, again.** The facade measures each candidate by its Sampson
  distance in source pixels and needs eight usable pairs. It chooses by inliers, then summed squared
  distance, then a canonical order over the normalised matrix, and returns it normalised.
- **The three facades now share one `solve`**, templated on the result and the model.
- **OpenCV on two rendered views through a pinhole:** the same matrix on all 20 seeds, holding every
  correct match, at 0.21 px RMS over its inliers and 0.031 px over exact correspondences it never saw.
- **Two sabotages, both caught as predicted.** The first cut of one did not build under `-Werror`
  (an unused `canonical`), so the old binary answered, and it was redone: **a sabotage that does not
  build proves nothing**, again.

### Update 2026-10-14 — M9 slice 4, sub-slice 6c: the incremental reconstruction becomes a shared module (**sub-slice 6 COMPLETE**)

The last of sub-slice 6's three commits, no change in behaviour. Targetless registration's incremental
reconstruction (placeability, the seed, placement by absolute pose, the cheirality filter) moves out of
`registration/targetless.cpp` into a private module targetless calibration can share in sub-slice 9.
`ctest` **1228/1228** Debug and **1236/1236** Release with video, camera and Ceres on, **1123/1123**
with all three off (unchanged); every golden untouched; warning-clean, format-check clean. Full notes
in WORK.md's *Sub-slice 6c built* under M9 slice 4.

- **`src/reconstruction.{h,cpp}`, namespace `lain::camera::reconstruction`**, private to lain::camera.
  It includes nothing from `registration/` or `calibration/`, which a grep checks.
- **The module speaks of views**, feature's word: a camera in registration, a frame in targetless
  calibration. `Evidence` is `{models, tracks}`, a model per view and no identity, so registration
  keeps its canonical `RigCamera`s beside it for the identities it reports.
  `chooseSeed` and `place` take a `feature::GeometryRequest`.
- **What stays is registration's:** the hold-out guard, scale normalisation, the refinement problem,
  the bootstrap's `registerUnits`, component estimates, the checks and the report.
- **Two sabotages, both caught, each with one failure more than predicted.**
  - A closure counting a unit once ONE placed view sees it fails the chain scene as
    InitialisationFailed, as predicted, and also the spare scene. Its support counts are what the
    hold-out guard spends, so critical tracks get held out and the rig splits.
  - A seed ignoring the parallax floor fails the narrow scene, as predicted, and also the
    seed-selection case.
- **Timing is unchanged**, Release, four alternating rounds of the targetless scale case: 37.3 s
  before against 36.6 s after, with about 3 s of spread within each. Sub-slice 5 found that moving
  code between translation units can change what GCC inlines, so this was measured, not assumed.

### Update 2026-10-14 — M9 slice 4, sub-slice 6b: the observation graph moves down to `lain::camera`

The second of sub-slice 6's three commits, no change in behaviour. The incremental reconstruction 6c
lifts ranks its seeds by the camera observation graph, and calibration may not depend on its sibling,
so the graph moves down first, as `RigCamera` and the refiner seam did. `ctest` **1228/1228** Debug and
**1236/1236** Release with video, camera and Ceres on, **1123/1123** with all three off (unchanged);
every golden untouched; warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 6b
built* under M9 slice 4.

- **`lain/camera/observationgraph.h`** holds `GraphEdge`, `ObservationGraph` and `observationGraph`,
  whose vertices are a method's views (cameras in registration, frames in targetless calibration).
  Registration names them through using-declarations and keeps `weakBridges`, a fitness judgement
  only it makes.
- **`detail::root` and `detail::median` are lain::camera's** (`src/unionfind.h`, `src/median.h`),
  so feature extraction's private copies are deleted. The copies did the same arithmetic.
- **Sabotage, caught, one prediction wrong:** a median taking the lower middle value fails both
  goldens and nothing else. The targetless one differs first at the scale normalisation (line 5),
  not at the seed angle predicted.

### Update 2026-10-14 — M9 slice 4, sub-slice 6a: a golden targetless registration report

The first of sub-slice 6's three commits, none of which changes behaviour. A golden targetless
registration report, produced by the unchanged code, holds the incremental reconstruction still
while it moves to a module calibration can share (6b moves the observation graph down, 6c lifts the
reconstruction). `ctest` **1228/1228** Debug and **1236/1236** Release with video, camera and Ceres on,
**1123/1123** with all three off (+1 each); warning-clean, format-check clean. Full notes in WORK.md's
*Sub-slice 6a built* under M9 slice 4.

- **Seven scenes, not the three first planned**, so every piece that moves is reached: connected,
  noisy (with resamples, so the bootstrap's path prints too), chain, disconnected (a component
  estimate per component), behind (the cheirality filter), narrow (the parallax floor) and spare (the
  hold-out guard reading the closure). Each was checked to reach its piece before the file was
  accepted. Debug and Release give the same bytes.
- **The registration printers both goldens use moved into `goldentext.h`**, and the board golden's
  file is unchanged.
- **Two sabotages, both caught.** One is seen only by the golden: a component estimate that reaches
  every camera starts the second component from cam00 instead of cam02, and the existing test checks
  only the estimates' sizes. Its first cut did not build under `-Werror`, so the stale binary
  answered until it was redone: **a sabotage that does not build proves nothing**, again.

### Update 2026-10-14 — M9 slice 4, sub-slice 5: the refiner frees camera models

A refinement problem's cameras may now share a model, and a model may be free, which targetless
calibration needs: a moving camera's frames are cameras of one lens. Registration changes by no line,
since every new field means today's problem when left empty, and both goldens pass with their files
untouched. `ctest` **1227/1227** Debug and **1235/1235** Release with video, camera and Ceres on
(+13 each), **1122/1122** with all three off (+5); warning-clean, format-check clean. Full notes in
WORK.md's *Sub-slice 5 built* under M9 slice 4.

- **The seam:**
  - `ModelFreedom` (focal length, principal point, coefficients, all held by default),
    `Problem::modelOf` and `modelFreedom`, and `Solution::models`, unchecked for the method to pass
    through `CameraModel::create`;
  - `Status::Unsupported`, `Refiner::canEstimate` and a `canEstimate(model)` facade;
  - each model's `fromCoefficients`, the inverse of `coefficients()`.
- **One addition beyond the plan, `ModelFreedom::frees(model)`.** The facade asks it to choose a
  backend, and the Ceres refiner asks it to decide which models get a block, so the two cannot
  disagree. Freeing only the coefficients of a model that has none frees nothing.
- **The facade picks the first refiner that can estimate every model a problem frees**, as board
  calibration picks its estimator. When none can, it is `Unsupported`, naming the models.
- **In Ceres a free model is one block**, `[fx, fy, cx, cy, coefficients...]`, its held fields on a
  `SubsetManifold`. Every residual goes through one `observe()` that takes a projector, held or
  free. Schur runs when the cameras or any model is free. The refiner declares all six models
  through an exhaustive switch.
- **Found by the timing check: one file slowed the held path.** With the free residuals in
  `ceresrefiner.cpp`, targetless registration's scale case took 32.3 s against 29.1 s, with the same
  10,011 refinements. A variant with the old held `observe()` was as slow, so it was GCC's per-file
  inlining limit, not the residual. Moved to `freemodel.{h,cpp}` (with `observe.h` shared), it took
  29.7 s. The reason is in `observe.h`, so nobody merges the files back.
- **Timings otherwise unchanged**, the pre-change binary built here and run alternately: board
  registration 14.59 s and 14.57 s, landmarks 3.52 s and 3.38 s. An early board reading 1 s slower
  went away once the run order alternated: **alternate the order when timing, or the second binary
  pays for the first.**
- **Exact tracks recover every model**: focal within 7.5e-9 and the ray mapping within 2.8e-6 px; a
  board's corners do the same. The Release scale case (60 frames, 20,000 landmarks, 8 views, 0.5 px)
  takes 6.4 s and 162 MB, and asserts the cost rather than recovery: 8-view tracks left focal 0.81%
  long, the probe's finding about short tracks.
- **Five sabotages, all caught.** One was wider than predicted: free residuals that drop their
  derivatives also fail the unused-model case, which asserts that the model in use was refined.

### Update 2026-10-14 — Newton's solve takes a step before it may stop (fixed ahead of M9 slice 4, sub-slice 5)

Found while planning sub-slice 5, fixed in its own commit ahead of it, as the plan commit decided with
the repo owner. At the neutral model (every coefficient zero, where a targetless calibration starts)
Inverse Brown-Conrady's Newton solve began at the ideal point, found it already within tolerance, and
returned it without a step. The guess does not depend on the coefficients, so a Jet came back with a
derivative of 0 for every coefficient. In a scratch probe a free solve from neutral stalled at cost 193
with fy 6.5% off. `ctest` **1214/1214** Debug and **1222/1222** Release with video, camera and Ceres
on (+1 each), **1117/1117** with all three off; warning-clean, format-check clean. Full notes in
WORK.md's *The fix built* under M9 slice 4's sub-slice 5 plan.

- **`detail::solve` may stop only from iteration 1 on**, for every scalar, with no branch on the
  scalar type. At a converged point one step's derivative is the implicit function's,
  `-J^-1 df/dc`, since the term carrying the finite-difference Jacobian's own derivative is multiplied
  by a residual of 0. For a double at the neutral model the step is exactly 0.
- **The new test was run red first**: 60 failures, all Inverse Brown-Conrady 5 and all at coefficient
  slots, exactly as predicted. After the fix the worst error at the neutral model is 8.0e-9 of the
  derivative's scale for that model and 2.2e-8 overall (Kannala-Brandt 4).
- **The two parameter cases share one helper**, `checkParameterDerivatives(model)`: once at the test
  coefficients, once at `neutral(model)`.
- **Both goldens pass with their files untouched.** The solve also serves every forward model's
  unprojection, and the whole suite is that regression check.
- **Sabotage, caught:** with the forced step removed, the new case fails as it did before the fix.

### Update 2026-10-13 — M9 slice 4, sub-slice 4: projection is generic in a model's parameters

Ceres can now differentiate a camera model's intrinsics and coefficients through lain's one projection,
which sub-slice 5's free models need. No behaviour changed: the double path does the same arithmetic,
and both goldens pass with their files untouched. `ctest` **1213/1213** Debug and **1221/1221** Release
with video, camera and Ceres on, **1117/1117** with all three off (+2 on, +1 off); warning-clean,
format-check clean. Full notes in WORK.md's *Sub-slice 4 built* under M9 slice 4.

- **The models still hold doubles; their formulas became static templates** over `Coefficients<S>`,
  an array in the model's coefficient order. A model is data a report, a document and an editor hold,
  so serialization, the OpenCV estimator and flowview's editors are untouched. Each body unpacks its
  array by a structured binding, so it still reads as its formula.
- **Two scalars:** S for the parameters, T for the point and the result. A held model runs S = double,
  an estimated one S = T = Jet. A coefficient times a constant is now a double literal (`2.0 * p1`),
  since a Jet's `operator*` will not take an `int`.
- **`project<Model>(intrinsics, coefficients, domain, x, y, z)` is the parameter overload**, and
  `project(model, …)` is a visit into it. A test asserts the two agree exactly in every build.
  `unproject` gains no parameter overload, since nothing differentiates one.
- **All six models' derivatives agree with finite differences**, the worst at 4.0e-8 of the derivative's
  scale (Inverse Brown-Conrady, through Newton's method). The tolerance is 1e-6. *(**Corrected
  2026-10-14**: only at the test's nonzero coefficients. At the neutral model, where targetless
  calibration starts, Inverse Brown-Conrady's Newton solve stops before a step and every coefficient's
  derivative is 0. Found while planning sub-slice 5, and **fixed** the same day in its own commit; see
  the entry above.)*
- **Timings, before and after, built and run alternately here: no change beyond noise** (board
  registration 14.94 s and 14.92 s, landmarks 3.67 s and 3.56 s, targetless registration 31.44 s and
  32.11 s, inside its own 2.1 s spread).
- **Two sabotages, both caught as predicted.** Dropping k3's derivative fails only k3's slot, for the two
  models sharing that polynomial. Swapping k4 and k5 in the rational binding fails the pinned pixel and
  OpenCV's agreement as predicted, and two OpenCV estimator cases that were not predicted.

### Update 2026-10-13 — M9 slice 4, sub-slice 3: the refiner seam moves to `lain::camera::refinement`

The seam leaves registration so calibration can refine through it (ADR-0016 keeps the two methods
apart). It is now `lain/camera/refinement/refiner.h`, with the noise model and the robust loss beside
it. No behaviour changed: both goldens pass and the diff touches neither. `ctest` **1211/1211** Debug
and **1219/1219** Release with video, camera and Ceres on, **1116/1116** with all three off (one case
renamed, none added); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 3 built*
under M9 slice 4.

- **Two names lose their stutter**: `refinement::Status` and `refinement::BodyObservation`.
  Registration keeps `RefinementStatus` and `RefinementObservation` as aliases, and its old headers
  keep exporting every name they did, so no registration caller changed.
- **Code that implements the seam, or asks whether a backend exists, spells `refinement::`**: the
  Ceres plugin, the stand-in refiners, the kinds table and the tests that register a refiner. The
  facade's missing-backend text drops "registration", since calibration will read it too.
- **The dependency direction is stated in the header, not guarded.** The seam includes nothing from
  `registration/` or `calibration/`, checked by a grep.
- **Three sabotages:**
  - A copied `canRefine` fails 53 cases, six more than predicted (camera::flow's node cases and the
    rendered end-to-end cases).
  - A copied `refine` does not build, as predicted: argument-dependent lookup on the moved `Problem`
    makes five calls ambiguous.
  - **The seam including `registration/request.h` was predicted to pass every test, and does not
    build where an include goes.** The two headers would include each other, and `request.h`'s using-declarations
    would then precede the seam's declarations. Only an include after the namespace builds and
    passes, and only the grep sees that one.

### Update 2026-10-13 — M9 slice 4, sub-slice 2b: an empty fitness profile is the method's own (**sub-slice 2 COMPLETE**)

The second of sub-slice 2's two commits. `calibration::Request::fitnessProfile` defaults to `""`, and
board calibration resolves it to `reconstruction/1`, as registration's requests have since slice 3's
sub-slice 8a. `ctest` **1211/1211** Debug and **1219/1219** Release with video, camera and Ceres on,
**1116/1116** with all three off (+1 each); warning-clean, format-check clean. Full notes in
WORK.md's *Sub-slice 2b built* under M9 slice 4.

- **The request is recorded exactly as given**, so a report says whether its profile was asked for or
  defaulted, and the thresholds name what it resolved to.
- **The golden changes in exactly the three planned lines**, 55, 117 and 177, each reading
  `profile (method's)`. They were diffed before the file was accepted.
- **The node needed no edit**, since its param default already reads `calibration::Request`'s.
- **Sabotage, caught:** writing the resolved name back into the recorded request fails the golden at
  those three lines, and the new case.

### Update 2026-10-13 — M9 slice 4, sub-slice 2a: the calibration report stops naming a board, and a principal point can be held

The first of sub-slice 2's two commits, chosen by the repo owner so that 2b's three golden lines
show on their own. No change in behaviour: the board calibration golden passes, and 2a's diff does
not touch its file. `ctest` **1210/1210** Debug and **1218/1218** Release with video, camera and
Ceres on, **1115/1115** with all three off (+4 each); warning-clean, format-check clean. Full notes
in WORK.md's *Sub-slice 2a built* under M9 slice 4.

- **The report is method-neutral, as registration's became in slice 3:**
  - `HeldOutEvidence::residuals` replaces `corners`;
  - `Diagnostics` keeps what every method reports, plus `std::variant<BoardDiagnostics> method`,
    which holds the frames examined, the frames usable and the detections;
  - `toString`'s board clause goes through `std::visit`, so a method without a summary does not
    compile.
- **A principal point can be held**: `ResamplingEvidence::principalPointVariation` is
  `std::variant<double, Held>`. Held is not missing evidence (which Ready refuses) and never a zero.
  Board calibration always measures it. Nothing produces a held one until sub-slice 9.
- **The verdict is a public `calibration::judge`**, board's former file-local `meets` with one new
  arm: a Held principal point adds no note in either tier. Its four new cases test that rule now,
  before anything holds a principal point. Every note's text is unchanged, which the golden pins.
- **Callers changed mechanically:** board.cpp, the golden printer (it keeps its old labels), the
  calibration, node and OpenCV estimator tests, and the fixture harness. The harness reads its
  principal-point bound with `std::get<double>`, so a held one would throw rather than become a zero
  bound.
- **Three sabotages, all caught.** One caught more than predicted: counting a held principal point as
  missing evidence also fails the second `judge` case, not only the first.

### Update 2026-10-12 — M9 slice 4, sub-slice 1: the calibration request stops naming a board

Two commits, split by the repo owner so the history shows that the change leaves the golden
untouched. No change in behaviour. `ctest` **1206/1206** Debug and **1214/1214** Release with video,
camera and Ceres on, **1111/1111** with all three off (+3 each across the two commits);
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 1a built* and *Sub-slice 1b
built* under M9 slice 4.

- **1a: a golden board calibration report** (`golden/boardcalibration.txt`, in
  `test-camera-calibration`), produced by the unchanged library.
  - It prints every field of four reports, timings aside, plus each `toString()` line: Ready, Rejected,
    a held model over given detections, and a refusal before detection.
  - The stand-in estimator's answer depends on which views it is handed. One of the two sabotages,
    every resample draw shifted by one index, is seen only through that: it keeps each draw's count of
    distinct views, so the focal term does not move and the principal-point term does.
  - The goldens' printing and comparison moved to `libs/camera/test/goldentext.h`, shared with the
    registration golden, whose text is unchanged. Sub-slice 6's targetless golden is the third user.
- **1b: registration's slice 3 sub-slice 2, for calibration.**
  - `calibration::Request` loses `detection`. The footage overload of `calibration::board::calibrate`
    takes the `board::DetectionRequest` between the board and the request.
  - `Reproducibility` keeps sources, frames, the estimator and the request, and gains
    `std::variant<BoardRecord> method` (`detector`, `poseSolver`, `optional<DetectionRequest>`).
    `estimator` stays shared, since for targetless calibration it will be the refiner.
  - The footage overload records its argument before any check. The detections overload takes no
    search: the record holds what the first detection reports, beside its detector.
- **Six sabotages across the two, all caught.** Two lessons from registration held:
  - a search dropped while the record keeps the argument is seen only per detection, never on the
    record;
  - a sabotage that does not build proves nothing. The node's first cut left `search` unused under
    `-Werror`, and the old binary answered until it was redone.
- **This container needed the dependency workaround again.** GitHub source archives still answer 403,
  so each was cloned over git and handed to configure as `FETCHCONTENT_SOURCE_DIR_<NAME>`. Release
  assets (OpenCV, FFmpeg, clang-format) and gitlab archives (Eigen, libtiff) download normally. The X11,
  Wayland and libva dev packages that CI installs had to be installed too.

### Update 2026-10-09 — M9 slice 3, sub-slice 10: targetless registration is reachable (**slice 3 COMPLETE**)

A `registerCamerasTargetless` node runs `registration::targetless::registerCameras` over a rig's
footage and models, wired into flowview and run end to end through `runGraph` and the real binary.
`ctest -j4` **1203/1203** Debug and **1211/1211** Release with video, camera and Ceres on, **1108/1108**
with all three off (+7 each), and **1145/1145** with OpenCV on but Ceres off. Warning-clean,
format-check clean.
**gui-mode NOT eyeballed.** Full notes in WORK.md's *Sub-slice 10 built* under M9 slice 3.

- **Decided with the repo owner:**
  - **The provisional values sub-slice 9 measured are kept.** Three changes to how held-out evidence
    is judged are queued in the Outstanding index: a robust statistic, a per-camera criterion, and
    translation spread at 0.2%.
  - **The node exposes the extraction settings that describe the rig**: `longestSide`,
    `sampledGroups`, `featureCap`, `matchSearch`, `matchRatio`, `inlierAngleMrad` and
    `minimumPairInliers`. There is no `pixelSigma`, since every extracted observation carries its own
    covariance.
  - **The two registration nodes share a base, `RigRegistrationNode`**: the footage and models
    inputs, the `Request` settings, and the rig (paired, named by source, grouped by position).
    `registerCameras` is rebased on it with no change in behaviour; its `pixelSigma` moved one
    Inspector row.
- **A default `Report` is a board report**, so the node's own refusals set the targetless variants,
  with the extraction it would have run. A sabotage that leaves the default fails both refusal cases.
- **`cameraNodeKeys()`** lists every kind. flowview's colours use it, and the catalog test asks about
  every kind it returns with a per-kind expectation: the board kinds need OpenCV, and both
  registrations need OpenCV and Ceres. The single OpenCV flag could not express that, so
  `registerCameras` had been left off the list the test kept.
- **Measured:** 432 usable tracks, 346 landmarks, 86 held out, Ready. Over 20 runs of the vertical
  the held-out transfer was 0.68 to 1.26 mrad. The real binary took 0.97 s in Release and wrote the
  Debug binary's bytes.
- **Found: where footage sits decides which evidence is held out.** A camera in a graph is named by
  its footage's path, a track's or group's identity digests it, and every k-th one in identity order
  is held out. The same rig in another folder gave 0.966 mrad against 0.795, each repeatable. This
  is board registration's property too. It is recorded for the repo owner.
- **Five sabotages, all caught.** Two first failed to build under `-Werror`, so the old binary
  answered, and were redone: **a sabotage that does not build proves nothing**, again.

### Update 2026-10-08 — M9 slice 3, sub-slice 9: one rig registered both ways, and the provisional values measured

One rig of four cameras is registered by both methods from the same rendered footage, through
OpenCV and Ceres: a textured scene with a printed board moving in front of it. The board method
places the cameras from the board; the targetless method places them from the scene and drops the
board as moving content. Both are validated on the same held-out tracks, compared with each other
and with the truth, and every value sub-slice 10 would expose is measured, with a recommendation for
the repo owner, who decides (one follow-up commit each). `ctest -j8` **1196/1196** Debug and
**1204/1204** Release with video, camera and Ceres on (+4 each), **1101/1101** with all three off
(unchanged: the end-to-end test builds only with both plugins);
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 9 built* under M9 slice 3.

- **Decided with the repo owner:**
  - rendered footage only, since there is no real rig footage;
  - measure, then the owner decides;
  - the comparison at 1280x720 with a focal length of 1000, in Release only, beside a 640x480
    targetless case that runs in Debug.
- **The two methods agree.**
  - **Against each other:** 0.55 mrad and 1.3 mm apart, after a scale-only fit; both pin the same
    reference to the identity, so a similarity fit would only hide that camera's own error.
  - **Against the truth:** the board result 0.34 mrad and 0.61 mm off, metric to 0.004%; the
    targetless result 0.74 mrad and 1.8 mm.
  - **On the same 101 held-out tracks:** 0.70 and 0.72 mrad, although the board's result is twice as
    close to the truth.
  - **What held-out transfer cannot see:** the rig's weak mode, which is what the bootstrap's
    rotation spread measures (the truth error is 1.4 to 2.1 times the spread).
- **The 5 mrad guard depends on the axis.** A tilt shows in every track. A pan stays inside a
  horizontal rig's epipolar planes, so it shows only in tracks of three or more members: 0.71 mrad in
  two-member tracks, 5.07 in longer ones.
- **For the owner, beyond the threshold table:**
  - **`validate`'s RMS has no robust loss.** One verified wrong track 275 mrad out (2 of 503 are
    wrong) makes the truth's RMS over every track 15.3 mrad, against 0.87 over the right ones. About
    one run in three would hold one out and fail Ready on it alone.
  - **Fitness judges the rig-wide RMS,** so one bad camera among seven or more would pass.
  - **Approximate matching** finds the same tracks 11 times faster than Exact.
  - **Features under 4 px, searched natively, state about a tenth of their true error.** A floor on
    the stated sigma made the rig less accurate, not more.
- **Found by a sabotage, fixed before commit.** The check for moving content asked whether an
  observation sat on the moving surface in most sampled frames, and a feature admitted from one frame
  passed it. Validation against the truth cannot see such a track either, because a synchronised rig
  sees a moving feature consistently at one instant. The check now casts each track into the still
  scene behind the moving content. With the static rule disabled, 22 disc tracks got in and that
  registration was still Ready.
- **Six sabotages, all caught.**

### Update 2026-10-07 — M9 slice 3, sub-slice 8b: `registration::targetless`

Fixed cameras with known intrinsics registered from the static scene they share, over an accepted
`TrackSet` or over footage. The footage overload is `feature::extractTracks` and then the track-set
overload, exactly. Nothing calls it from a graph yet; sub-slice 10 does. `ctest -j8` **1192/1192**
Debug, **1200/1200** Release with video, camera and Ceres on, **1101/1101** with all three off;
warning-clean, format-check clean, the board golden untouched. Full notes in WORK.md's *Sub-slice 8b
built* under M9 slice 3.

- **Placeability is a closure from a seed pair**, needing the geometry facades' own minima: 5 shared
  tracks to seed, 4 tracks already seen by two placed cameras to place a camera. A rig no pair
  covers fails `Disconnected`, naming what the best-supported pair cannot reach.
- **The seed** is, of the 16 covering pairs that share the most tracks, the one with the most
  relative-pose inliers clearing a 2° median triangulation angle. When none clears it, the widest
  seeds the rig and `seedBelowFloor` says so (the repo owner's call).
- **The hold-out guard is anchored on the first covering seed.** A track is held out only while every
  camera it helped place keeps its support. The design review's counterexample is a test: cameras 0
  and 1 share the most tracks and place nobody else, and only the guard keeps the 8 tracks that
  place them from 2 and 3.
- **An observation behind its camera at the start is left out**, named as an outlier with an
  infinite residual. Every camera must still see 4 landmarks, since Ceres returns an unobserved one
  unchanged. Without the filter, Ceres fails the whole solve.
- **The bootstrap aligns each resample's scale** by least squares over the camera centres. Without
  it, exact tracks read a 1.43% spread.
- **Measured through Ceres:**
  - **Noisy rig:** 0.454 mrad and 0.039% of depth at worst, Ready.
  - **A wrong match:** named at 110 σ, the rest of its track not named.
  - **Release, 100 cameras × 50,000 tracks:** 40 s (7.0 s refining), 1.26 GB at peak, 0.66 mrad
    worst, Ready.
- **The truth stand-in had to stop proposing every candidate.** Without a top-4 cut it offered up to
  4,373 poses per placement, and seed selection on the scale rig took 7.7 s.
- **In a placeable rig of three or more cameras no edge can be a bridge**, because a placed camera
  joins by two edges. So a weak bridge appears only as a two-camera rig's own edge.
- **Six sabotages, five caught.** Drawing the resamples from one engine under a lock is NOT caught:
  it only permutes the draws among the resamples. The unlocked form the plan named fails 3 runs of 3.

### Update 2026-10-07 — M9 slice 3, sub-slice 8a: `registration::validate`, and the targetless profile

Sub-slice 8 is two commits, chosen by the repo owner: 8a here, the method in 8b.
`registration::validate` predicts each member of each held-out track from the others, with every
camera held, whichever method made the report. That is how sub-slice 9 compares board and targetless
registrations. `ctest -j8` **1172/1172** Debug and **1180/1180** Release with video, camera and Ceres on (+7 each), **1085/1085** with all three off (+6); warning-clean,
format-check clean. Full notes in WORK.md's *Sub-slice 8a built* under M9 slice 3.

- **Decided with the repo owner:**
  - **An empty `Request::fitnessProfile` is the method's own profile**, and is the default. Board
    resolves it to `registration/1` and refuses the new `registration-targetless/1` with
    `IncompatibleFitnessProfile`.
  - **When no pair clears the seed's parallax floor, initialisation seeds from the widest** (8b).
  - **The board golden changes on purpose in exactly four lines** (`profile (method's)`), diffed
    before it was copied over.
- **One fixed procedure, whatever the report's request:** each observation is weighted by its
  track's own covariance, with no robust loss. A report is judged by what it registered, never by
  how it asked to be refined.
- **One refinement per held-out track**, holding only that track's cameras. A start that does not
  project into every predicting camera is never sent, since one failed start aborts a whole Ceres
  solve.
- **A two-member track is scored against its epipolar plane, and that residual counts twice:** it has
  one degree of freedom where a transfer residual has two. `Unavailable` replaces zeros when nothing
  is predicted.
- **Shared with board, and the golden held:** `resolveProfile`, `canonicalCameras`,
  `checkApplicability`, `whitenedSquared` and a guarded stride `holdOut`. `detail::angleBetween`,
  which existed four times, now lives once in `src/angles.h`.
- **The plan had two expectations wrong, and the tests pin what is true:**
  - A camera turned 5 mrad shows 4.96 mrad in its own members; camera 3, which depends mostly on
    camera 2, shows 6.16.
  - A turn about the baseline puts a ray asin(sin t sin f) off its plane, not t. The test works this
    out from the truth.
- **Four sabotages, all caught.** One did not compile at first and was redone: under `-Werror`, an
  unused static function fails the build, and the old binary then answers.

### Update 2026-10-07 — a feature's orientations stay together through matching

The second of sub-slice 7's two recommendations, settled before sub-slice 8. SIFT reports a keypoint
with two strong orientation peaks twice, and extraction used to keep one per 1/64-pixel cell (the
smallest angle) before matching. That angle depends on a view's roll, and a fifth of the correct
matches were lost. Now the cell is the feature, carrying every orientation as its own descriptor
row; every row is matched, and a match between rows is a match between cells. `ctest -j8`
**1165/1165** Debug and **1173/1173** Release with video, camera and Ceres on, **1079/1079** with all
three off (+2 each); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 7
follow-up B*.

- **Real SIFT, rendered footage: 429 tracks against 365**, with pair inliers of 286, 169 and 277
  against 225, 121 and 219. Consistency holds (410 within 2 px).
- **A feature matched to two of the other camera's is dropped from that pair** and counted
  (`PairExtraction::ambiguous`: 3, 2 and 2 on the footage). The union's conflict rule stays the last
  resort; the footage now has one conflict, where 0 had been a measured value rather than a contract.
- **The cost is real and is the repo owner's call.** On the rendered texture a feature carries 1.49
  rows, so an exact match at the 8192-feature cap takes 10.2 s instead of 3.68 s, and a 100-camera
  rig 16.9 core-hours Exact instead of 6.1. Approximate is 0.78 s at 96.8% recall. Counting the cap
  in rows, or lowering it, would restore the old cost.
- **The stand-ins modelled the defect.** A second orientation repeated the first descriptor, a tie
  only collapse-before survived. It now has its own, and two new options model roll and one
  camera's extra orientation.
- **Report names changed with their meaning:** `found`, `orientations`, `duplicates` (static
  collapses only) and `ambiguous`.
- **Three sabotages, all caught.**

### Update 2026-10-07 — the feature covariance follows the keypoint's size

The first of sub-slice 7's two recommendations, settled before sub-slice 8 at the repo owner's call,
each as its own commit. `ExtractionRequest::localisation` (a fixed 0.7 processed px) is now
**`localisationPerSize = 0.034`**, and each observation's σ is that times its static feature's size
in source pixels. `ctest -j8` **1163/1163** Debug and **1171/1171** Release with video, camera and
Ceres on, **1077/1077** with all three off (unchanged); warning-clean, format-check clean. Full
notes in WORK.md's *Sub-slice 7 follow-up A*.

- **The rename is the point**: a change of unit under the old name would compile and silently mean
  something else.
- **The scale is no longer divided out.** The size is already in source pixels, and a coarser
  search finds features larger, so it still weighs them less.
- **The model was checked at half scale before it was trusted.** The error is 6.3% of the size there
  against 4.9% natively, which is inside the bound the plan set, so no floor term. The excess is in
  the smallest features, and the default search of 4K to 8K footage is coarse, so sub-slice 9's
  validation is where a floor would show itself owed.
- **Sabotage, caught:** a σ that ignores the size fails 241 covariance assertions.

### Update 2026-10-06 — M9 slice 3, sub-slice 7: the OpenCV feature producer

The three feature seams get a real backend: SIFT, a matcher with both searches, and a geometry
solver in `plugins/camera/opencv`, tested on scenes a new plain C++ renderer draws, so every match
and pose is judged against the truth. `ctest -j8` **1163/1163** Debug and **1171/1171** Release with
video, camera and Ceres on, **1077/1077** with all three off (+8 each on); warning-clean,
format-check clean. Full notes, every measurement and the recommendations in WORK.md's *Sub-slice 7
built* under M9 slice 3.

- **Decided with the repo owner:** FLANN's index build runs under a mutex, since it draws from the
  process-global `std::rand`; and the defaults stay as they are, with the numbers recorded for the
  repo owner to decide on. Any change is its own commit.
- **Found by measuring: OpenCV's USAC wants pixel-scale coordinates.**
  - On normalised rays with an identity camera, MAGSAC++ proposed essential matrices 0.6 to 15 mrad
    out across 20 seeds; as pixels of a virtual pinhole of focal 1000, 0.25 to 0.47 mrad on every
    seed.
  - Lain's own inlier count could not tell them apart (318 either way at 0.004 rad), so a
    20-seed sweep against the truth is what pins it.
- **Found by measuring: exact matching is 3.9 times faster over float descriptors**, with answers
  identical over 243,798 neighbours, since every partial sum of squared byte differences fits a
  float exactly.
- **The numbers for the provisional defaults:**
  - Exact takes 3.7 s per pair at 8192 features, so a 100-camera rig whose every pair overlaps
    costs 6.1 core-hours. Approximate takes 0.37 s with 96.7% of the matches. The plan's estimate
    was 1.5 core-hours.
  - A search at 1920 px peaks at 489 MB per frame, from SIFT's doubled first octave.
  - SIFT localises to 3.4% of a keypoint's size, so the fixed σ of 0.7 px is three times too loose
    for the commonest features.
  - Collapsing duplicate orientations before matching loses a fifth of the correct matches.
- **Recommended, not done:** keep Exact and 1920 px; make σ proportional to keypoint size; collapse
  duplicates after matching; halve Exact by answering both directions from one distance matrix.
- **The renderer took nine tenths of a Debug render in GLM calls.** Plain arithmetic made it 2.5
  times faster with byte-identical frames, and `RenderedSource` keeps what it draws. The footage
  case went from 70 s to 28 s in Debug.
- **Six sabotages, four caught.** `isParallel = true` and the FLANN mutex removed are not caught,
  as predicted: OpenCV's pool is at one thread, and glibc's `rand()` takes a lock.
- **A stale `LastTestsFailed.log` from an old configure** made `ctest --rerun-failed` "rerun" two
  placeholder targets. Read failures from the run itself, not from that file.

### Update 2026-10-06 — M9 slice 3, sub-slice 6: track extraction, `feature::extractTracks`

Footage and capture groups in, accepted static feature tracks and a report out, through sub-slice
5's facades. Nothing calls it yet; sub-slice 8 does. `ctest -j8` **1155/1155** Debug and
**1163/1163** Release with video, camera and Ceres on, **1077/1077** with all three off (+12 each);
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 6 built* under M9 slice 3.

- **Five steps:**
  1. checks that decode nothing;
  2. evenly spaced sampling of the capture groups;
  3. one task per camera: decode, extract, release, collapse and cap each frame, then consolidate
     the static features;
  4. one task per pair: pre-screen, match and verify;
  5. a serial union in which a component holding two features of one view is rejected whole.
- **Decided with the repo owner:**
  - **Consolidation goes through `match()`** (mutual, ratio both ways), gated at 1.5 processed px
    and a 1.5× size. This replaces a descriptor bound, which lain cannot measure across kinds.
  - **Sampling is by capture order**: median timestamp, then median ordinal, then identity, at bin
    centres.
  - **The covariance is `(σ / scale)²`**, with σ = 0.7 processed px, provisional.
  - **`RigCamera` / `RigFootage` moved down to `lain/camera/rig.h`**, so feature names no
    registration header.
- **One feature per 1/64-source-px cell** (largest response, then smallest angle, then smallest
  size), per frame and among static features. It collapses SIFT's duplicate orientations and makes
  track identities (SHA-256 over cameras and pixels on that grid) unique by construction.
- **Deviations:**
  - `TrackSet::groups` for `frames`;
  - a new `Status::InvalidInput`;
  - the Approximate-under-DeterministicDebug refusal lives here, since `match()` has no execution
    policy;
  - board's frame index became a shared `detail::FootageIndex`, with the golden byte-equal.
- **Found by a test: an orbit about the scene still verifies.** A camera rendered 0.05 rad and
  0.2 m off still left 37 to 47 of 60 matches on their epipolar lines. A 0.2 rad tilt gives 0, and
  the case pins that.
- **Six sabotages, five caught.** Running the union in pair completion order is **not caught, as
  predicted**: a union-find partition does not depend on the order of its unions.

### Update 2026-10-06 — M9 slice 3, sub-slice 5: feature contracts, seams and facades

`lain::camera::feature` lands in `libs/camera` (std and lain only). It holds:
- the feature values;
- three backend seams (extractor, matcher, geometry solver), each with a registry;
- four facades (`extract`, `match`, `relativePose`, `absolutePose`);
- a public `triangulate`.

Producers propose and lain decides (ADR-0016, amended in place): lain runs the matching tests,
measures every candidate pose itself and recomputes the inliers. Nothing calls it yet. `ctest -j8`
**1143/1143** Debug and **1151/1151** Release with video, camera and Ceres on, **1065/1065** with all
three off (+25 each); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 5 built*
under M9 slice 3.

- **One `Status` across the facades**, adding `BackendMisbehaved` to the planned set on `render()`'s
  precedent. A malformed answer (descriptor bytes, an off-image keypoint, an out-of-range neighbour)
  is refused rather than indexed.
- **`match` tests both directions**: mutual nearest, and the ratio test on each side. So
  `match(a, b)` is `match(b, a)` swapped, and an exact tie is never a match. CONTEXT.md gains
  *Feature match*.
- **The pose facades measure every candidate and choose regardless of their order**: by inlier
  count, then squared residual, then a canonical pose order. Inputs are pair types (`RayPair`,
  `PointRay`), so a length mismatch cannot be written.
- **Decided with the repo owner:**
  - the geometry seam carries no inlier mask (ADR-0016 says one is never read);
  - `Unsupported` comes from the extractor and the matcher only;
  - a pair with parallax below the inlier angle is a point at infinity, an inlier when its rays
    agree under the rotation.
- **Found by a test: a wrong rotation can hide on far points.** Turned about an axis in the
  epipolar plane, a far pair is re-triangulated nearer and stays an inlier, which is correct
  geometry. Turned about the baseline, it explains nothing, and that is what the case now asserts.
- **Five sabotages, all caught.** The first cut of one did not compile and so proved nothing, until
  it was rewritten to compile; the comments' guessed measurements were replaced by measured ones.

### Update 2026-10-06 — M9 slice 3, sub-slice 4: the refiner gains landmarks

The refiner's seam now carries scene points whose positions are unknown, beside the bodies, which
targetless registration (sub-slice 8) refines and triangulates. Board registration is unchanged
except for one new count in its summary. `ctest -j8` **1118/1118** Debug and **1126/1126** Release
with video, camera and Ceres on (+9 each), **1040/1040** with all three off (+0); warning-clean,
format-check clean. Full notes in WORK.md's *Sub-slice 4 built* under M9 slice 3.

- **The seam:**
  - `Problem::landmarks` and `landmarkObservations` (`LandmarkObservation {camera, landmark, pixel,
    covariance}`);
  - `Solution::landmarks`;
  - `RefinementSummary::landmarks`.

  A board problem sets none of them.
- **One projection path in the Ceres refiner.** `whitening()` and `observe()` are shared:
  - `CornerResidual` is the body's transform followed by `observe()`;
  - `PointResidual` (`<…, 2, 6, 3>`) is `observe()` alone.

  A problem is refused only with no observation of either kind.
- **The linear solver follows the problem's shape** (ADR-0017):
  - free cameras → `SPARSE_SCHUR`, with landmarks in elimination group 0;
  - held cameras with at most one latent block → `DENSE_QR`;
  - otherwise `SPARSE_NORMAL_CHOLESKY`.

  **`Solution::detail` now names the solver that ran**, the only way to observe the rule short of
  timing, and how its test reads it.
- **The scale gauge was measured, and the `SphereManifold<3>` trigger did not fire.**
  - Eight cameras and 2,000 landmarks started 5 mrad and 5% out converged in 4 or 5 iterations over
    thirteen seeds.
  - The scale came out between 0.980 and 1.022, from starts within 0.2% of 1. It was the same at
    0 px of noise as at 0.3 px, so the path from the start sets it, not the noise.
  - No factorisation failed, and the method normalises the scale afterwards.
- **Measured, Release, one thread:**
  - 100 cameras × 50,000 landmarks × 8 views: 4.2 s and 324 MB at peak, in 4 iterations;
  - 20,000 landmarks against held cameras: 0.54 s.

  Both cases run in Release only; the second took 26 s in Debug.
- **A landmark behind a camera at the start fails the solve**, which pins why sub-slice 8 filters
  by cheirality first. Ceres prints a miniglog WARNING when it happens; warnings are kept on purpose.
- **The golden changed on purpose for the first time**: three `refinement` lines gain
  `landmarks 0`, and the diff was checked to be exactly those before the file was accepted.
- **Three sabotages, all caught.** Whitening's sign flipped fails both the landmark and the corner
  covariance cases, which shows the helper is shared.
- **Practical:**
  - this container has no GNU `time`, so peak memory came from Python's `getrusage` on the child;
  - a Catch2 test spec reads a comma as "or", so escape it (`name\, rest`);
  - `pkill -f <binary>` matches the shell running it and kills that too.

### Update 2026-10-06 — M9 slice 3, sub-slice 3: the report, its diagnostics and fitness stop naming a board

No change in behaviour; the golden board report passes with its file untouched, which ends the
byte-equal gate of sub-slices 1 to 3. `ctest -j8` **1109/1109** Debug and **1117/1117** Release with
video, camera and Ceres on, **1040/1040** with all three off (+1 each); warning-clean, format-check
clean. Full notes in WORK.md's *Sub-slice 3 built* under M9 slice 3.

- **`Diagnostics` holds what every registration reports** plus `std::variant<BoardDiagnostics>
  method`, which takes board's own: groups examined and usable, observations, those without a pose,
  flips and detections.
- **The shared fields are neutral:**
  - `Outlier::unit`, `CameraEvidence::shared`, `HeldOutEvidence::units`, `heldOutUnits`;
  - `residuals` for every corner-level count;
  - `translationVariationAbsolute`.

  So are the observation graph's (`GraphEdge::shared`, `sharedPerCamera`) and the thresholds'
  (`minimumSharedPerCamera`, `minimumBridgeShared`).
- **`FitnessProfile::unit` (`EvidenceUnit {CaptureGroup, Track}`) decides the wording** of the
  fitness notes and `toString`'s held-out count. `toString`'s method clause goes through
  `std::visit`, so a new method without a summary does not compile.
- **Deviations:**
  - `residuals`, not the planned `observations`, because an observation here is already one
    camera's view of one unit;
  - `RefinementSummary::landmarks` moves to sub-slice 4, beside what it counts;
  - the unit-word test runs on a real stand-in report, because GCC 13 at `-O3` raises a false
    `-Wmaybe-uninitialized` on a hand-built `Report` whose `heldOut` variant is reassigned.
- **Three sabotages, all caught**, and one prediction wrong: the cli vertical does not see the unit
  word, since it pins only board's own "groups usable" clause.

### Update 2026-10-06 — M9 slice 3, sub-slice 2: the registration request stops naming a board

No change in behaviour; the golden board report passes with its file untouched. `ctest -j8`
**1108/1108** Debug and **1116/1116** Release with video, camera and Ceres on, **1039/1039** with all
three off (+1 each); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 2 built*
under M9 slice 3.

- **`registration::Request` loses `detection`.** The footage overload of
  `registration::board::registerCameras` takes the `board::DetectionRequest` as an argument (board,
  then search, then request), and the node passes it.
- **`Reproducibility` holds what every registration has** (sources, frames, refiner, the request) plus
  `std::variant<BoardRecord> method`, where `BoardRecord {detector, poseSolver,
  optional<DetectionRequest> detection}` lives in `report.h`, since `board.h` includes it.
- **The detections overload takes no search argument**: it searches nothing, so one could only be
  recorded and could contradict its detections. The record holds what the first detection reports,
  as `detector` already did, and stays unset with no detection. It used to record whatever the caller
  left in `Request`, unchecked.
- **The footage overload records its argument before any check**, so a refusal that decodes nothing
  still says what it would have searched with.
- **`calibration::Request` keeps `detection`** until slice 4 gives calibration a second method.
- **Owed to sub-slice 8:** `Request::fitnessProfile` defaults to `"registration/1"`, a capture-group
  profile, so a targetless call with a default `Request` would refuse it. Each method needs its own
  default.
- **Three sabotages, all caught**, and one prediction wrong: the footage overload searching with the
  default is caught only by the per-detection check (60 failures), not by the node's settings test,
  because the record holds the argument whatever the detections were made with.

### Update 2026-10-06 — M9 slice 3, sub-slice 1: the shared registration helpers leave board.cpp

No API change and no change in behaviour, held to a golden board report committed in the same
commit. `ctest -j8` **1107/1107** Debug and **1115/1115** Release with video, camera and Ceres on,
**1038/1038** with all three off (+1 each, the golden); warning-clean, format-check clean. Full notes
in WORK.md's *Sub-slice 1 built* under M9 slice 3.

- **The golden** (`test_registrationgolden.cpp`, `golden/boardregistration.txt`) prints every field of
  four noisy board reports (connected over 40 groups, flipped with a requested reference, stray with
  covariance and Huber, disconnected), timings aside. Numbers snap to a 1e-9 grid before nine
  significant digits, so last-bit maths-library differences do not show. On a mismatch it writes
  what it produced beside the build. Debug and Release give the same bytes; it holds sub-slices 2
  and 3 still too.
- **`src/registration/common.{h,cpp}`** (`registration::detail`): `rotationBetween`, `median`, one
  `root`, `failed`, `meets`, and four lifted from inline code: `holdOut` (the sole-bridge guard
  kept), `chooseReference`, `bootstrapDraws`, `judge`. Evidence units are lists of cameras, so a
  capture group and a feature track are the same to them.
- **`RigCamera` / `RigFootage` → `registration/rig.h`, `ScalePolicy` → `camera/scalepolicy.h`**, with
  using-declarations at the old names.
- **Inside `registration`, a `registration::detail` hides `lain::camera::detail`.** `forEach` is
  re-exported there by a using-declaration, since spelling the call sites `camera::detail::forEach`
  made clang-format re-indent 150 lines of lambda bodies in a commit that should read as a move.
- **Two sabotages, both caught by the golden** (`kInitialisationGroups` 24 → 12; `holdOut` starting
  one index late).
- **Not run:** `flowview run` on a registration document, since none is committed; the cli vertical
  through `runGraph` passed.

### Update 2026-10-06 — M9 slice 3 planned: targetless registration with known intrinsics (nothing built yet)

Slice 3 of M9 is planned as **eleven sub-slices (0 to 10), one commit each**, in WORK.md's M9 build
order. ADR-0016 and ADR-0017 are amended in place. CONTEXT.md gains *static feature*, *held-out track*,
*scale normalisation* and *placeable camera*, and amends *Targetless registration*, *Camera
observation graph*, *Weak bridge*, *Feature track* and *Feature-track extraction*.

- **Decided with the repo owner:**
  - **A track is a static landmark across the whole capture.** A feature a fixed camera sees at one
    pixel across its sampled frames is static, and moving content is dropped before matching. Each
    camera pair is matched once, and held-out validation and the bootstrap resample tracks. Capture
    groups only choose frames, so *Targetless registration* no longer says "synchronized".
  - **Methods are compared on held-out feature tracks**, through a public `registration::validate`
    that triangulates each held-out landmark from the other members, cameras held. It is
    scale-invariant, so nothing is fitted on the validation data.
  - **A scale-ambiguous result is normalised** so the median depth seen from the reference is 1.
  - **SIFT** in the OpenCV producer.
  - **Matching is a selector, Exact (default) or Approximate (FLANN).** The repo owner prefers exact
    but is concerned about time: about 1 s per pair at 8k features, so about 1.5 core-hours for a
    100-camera rig whose every pair overlaps. Sub-slice 7 times both and measures Approximate's
    recall; those numbers are the trigger to revisit the default. `DeterministicDebug` refuses
    Approximate, whose FLANN trees seed from the process-global `std::rand`.
- **Found by the design review:**
  - **Producers propose; lain decides.** Lain runs the matching tests, builds the tracks, and measures
    and accepts candidate poses itself, the `board::pose` facade's rule again. ADR-0016 had given
    track construction and verification to the producers.
  - **Placeability, not connectivity.** Overlaps A–B and B–C with no track seen by all three keep two
    scales, so C cannot be placed. A rig with an unplaceable camera fails `Disconnected`.
  - **Initialisation is incremental** (seed pair, then absolute pose camera by camera), because
    board registration's pairwise chaining cannot carry one scale across edges.
  - **The scale gauge stays free in Ceres**, normalised afterwards; sub-slice 4 measures it.
  - **The "shared" registration contracts are board-shaped** (`Request::detection`, the report's
    diagnostics, the fitness count names). Sub-slices 1 to 3 make them method-neutral with no change
    in behaviour, held to a golden board report committed first.

### Update 2026-10-05 — M9 slice 2 planned: fixed-camera board registration (nothing built yet)

Slice 2 of M9 is planned as **seven sub-slices (0 to 6), one commit each**, in WORK.md's M9 build
order. ADR-0016 and ADR-0017 are amended in place, the README's Ceres and Eigen rows carry their
pins, and CONTEXT.md gains *pose solver*, *by-position grouping*, *held-out group / transfer
residual* and *weak bridge*.

- **Decided with the repo owner:**
  - **Ceres 2.2.0 is built from source**, with Eigen 3.4.0 and Ceres' bundled miniglog. Only
    unreleased Ceres master needs Abseil (checked against both trees' CMakeLists), so **Abseil
    leaves the inventory**. Everything optional is off, the build is static, and `EIGEN_MPL2_ONLY`
    is defined for Ceres' own sources too.
  - **Capture groups are explicit**, plus by-position grouping for frame-locked footage. Timestamp
    grouping is deferred until the first footage that is not frame-locked.
  - **A graph supplies cameras as collections**, footage and models paired by position.
  - **"Ambiguous" is three tested cases**: a weak bridge, a planar pose flip, and a conflicting
    capture group.
- **Taken by precedent:**
  - Ceres runs serial and lain supplies the parallelism, as OpenCV does (ADR-0024).
  - `LAIN_CAMERA_CERES` defaults to `${LAIN_NOT_SUBPROJECT}`.
  - The single-view board-pose solve moves from `calibration::Estimator` to a `camera::board` seam,
    since registration must not depend on its sibling.
- **The sandbox needed the X11/Wayland/libva dev packages** (`libxrandr-dev` and friends) before the
  Vulkan loader would configure. A fresh container does not have them.

### Update 2026-10-05 — M9 slice 2, sub-slice 6: the registerCameras node and the cli vertical (**slice 2 COMPLETE**)

Fixed-camera board registration is reachable: a `registerCameras` node, wired into flowview, run
end to end through the real binary. `ctest -j8` **1106/1106** Debug and **1114/1114** Release with
video, camera and Ceres on, **1037/1037** with all three off (+4 each); warning-clean, format-check
clean. **gui-mode NOT eyeballed.** Full notes in WORK.md's *Sub-slice 6 built* under M9 slice 2.

- **`registerCameras` takes the rig as two collections**, footage and models paired by position, and
  groups them frame by frame. Each camera is named by its footage's source uri until a capture
  manifest assigns identities. It is offered only with a detector, a pose solver and a refiner.
- **The cli binds a rig from folders**: `--footage <folder>` (one entry per camera, by name) and
  `--models <folder>` (one `.json` per camera, by name). That is a deviation from the planned
  `listDir → map(openSequence)`: `ListDir` lists regular files only, so it cannot list per-camera
  still folders.
- **Through the real binary**, from another folder: `flowview run --graph register.json --footage rig
  --models models --report report.txt` writes `Ready: 3 cameras relative to …/rig/cam0, metric, …`.
- **M9 slice 2 is complete.** Still owed: the gui-mode eyeball and a CI run.

### Update 2026-10-05 — M9 slice 2, sub-slice 5: the Ceres refiner

`CeresRefiner` fills registration's refiner seam: a sparse bundle adjustment of cameras and board
poses, every residual through lain's own `camera::project<T>`. `ctest -j8` **1102/1102** Debug and
**1110/1110** Release with video, camera and Ceres on (+10 each), **1033/1033** with all three off;
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 5 built* under M9 slice 2.

- **The scale target registers in 19.7 s at 663 MB** (Release, four cores): 100 cameras, 4,000
  groups and 768,000 corner residuals, with a single-threaded refinement of 16.1 s. That was
  ADR-0017's trigger to revisit running Ceres serially, and it does not fire. Debug is about 65
  times slower, so the scale test runs in Release only.
- **End to end with both real backends** (rendered footage, OpenCV detection and poses, Ceres): Ready,
  with the worst camera 0.28 mrad and 0.15 mm out.
- **The default robust loss is now Cauchy**, on a measurement: a stray view moved a camera 8.4 mrad
  under Huber and 1.6 under Cauchy. ADR-0017 records it.
- **The test rig was too weak for a real refinement**: at 1 m a 168 mm board's tilt barely shows, and
  a chain accumulated 22 mrad. The Ceres tests stand at 0.5 m, and exact corners converge to 1e-10
  on every rig shape.
- **The refiner returns Ceres' values, never the start copied back**, so a constant block is shown to
  stay put rather than restored. A pose-only problem whose only camera was the reference could not
  tell a freed camera from a held one; a two-camera case now does.

### Update 2026-10-05 — M9 slice 2, sub-slice 4: the board registration method module

`registration::board::registerCameras`, over footage or over stored detections. The pose solver and
the refiner are seams, and everything that decides what a registration means is here. Tested over
stand-ins, including a pass-through refiner, so the Ceres refiner's accuracy is sub-slice 5's to show.
`ctest -j8` **1092/1092** Debug and **1100/1100** Release with video, camera and Ceres on,
**1033/1033** with all three off (+16 each); warning-clean, format-check clean. Full notes in
WORK.md's *Sub-slice 4 built* under M9 slice 2.

- **Initialisation chooses, it does not trust.** Each spanning-tree edge's relative pose is chosen
  from every proposal its shared groups make (both of each planar board's poses), scored by the
  median transfer over at most 24 sampled groups. That one rule resolves a camera whose every view
  is flipped, and outvotes a group with a frame from another instant.
- **A group that alone joins two parts of the camera graph is never held out.** What is held out
  must not decide whether a registration can happen. CONTEXT.md's *Held-out group* is amended.
- **Outliers and flips are reported after the refinement**: an observation whose RMS whitened
  residual is above the loss scale, and an observation whose result agrees with the plane's other
  pose.
- **A sabotage that passed at first:** removing the hold-out guard broke nothing, because the
  weak-bridge case's stride never landed on its bridging group. A seven-camera chain in which every
  group is critical now pins it.
- **`cmake --build … --target format | tail -0` formats nothing**: tail exits at once and SIGPIPE
  kills the formatter. Send the output to `/dev/null` instead.

### Update 2026-10-05 — M9 slice 2, sub-slice 3: capture and registration contracts

The types and pure functions registration will stand on, with nothing using them yet: capture
groups, the registration request, report, refiner seam and `registration/1` profile, and the camera
observation graph. `ctest -j8` **1076/1076** Debug and **1084/1084** Release with video, camera and
Ceres on, **1017/1017** with all three off (+14 each); warning-clean, format-check clean. Full notes
in WORK.md's *Sub-slice 3 built* under M9 slice 2.

- **`camera/method.h`** now holds `Verdict`, `Unavailable` and `ExecutionPolicy`, which the method
  modules share. Calibration keeps its spellings through using-declarations, and registration never
  includes a calibration header.
- **A capture group's identity is canonical**: SHA-256 over a versioned, length-prefixed text of its
  camera-sorted members. It is pinned against a digest computed outside lain. `create` also refuses
  one frame named by two cameras, which CONTEXT.md already called invalid and the first cut missed.
- **The weak-bridge threshold is a fitness criterion** (`minimumBridgeGroups`), not a request field
  as planned, because it decides what a result is fit for. CONTEXT.md's *Weak bridge* is amended.
- **Bridges are found iteratively**, and a 200,000-camera chain is the test.
- **A sabotage that proved nothing at first:** the case meant to catch names running together used
  an example the space separator already told apart. Now it uses `"a b"` + `"/c"`, which collides
  without the length prefixes.

### Update 2026-10-05 — M9 slice 2, sub-slice 2: the board pose seam leaves calibration

Calibration's behaviour is unchanged. A board's pose in one view, under a model held fixed, is now
`camera::board::pose`, so registration (sub-slice 4) can use it without depending on calibration.
`ctest -j8` **1062/1062** Debug and **1070/1070** Release with video, camera and Ceres on (+7 each),
**1003/1003** with all three off (+2); warning-clean, format-check clean. Full notes in WORK.md's
*Sub-slice 2 built* under M9 slice 2.

- **`board::PoseSolver` proposes poses, and the facade measures them** with lain's projection
  (`board::measure`, which used to be calibration's inline residual loop). `PoseResult` carries a
  `pose` and, for a planar target's second solution, an `alternative`. The status says why there is
  none, `NoBackend` included.
- **`OpenCVPoseSolver` is `OpenCVEstimator::boardPose` moved**, now on `solvePnPGeneric` with IPPE,
  so both solutions come back, each refined by LM. **`Estimator::boardPose` is deleted.** A held
  model needs a pose solver, not an estimator, which adds `Failure::NoPoseSolver` and
  `Reproducibility::poseSolver`.
- **Measured: on a far, nearly square-on view the backend's FIRST pose can be the wrong one.** At
  2.5 m, 0.07 rad off square, with 0.5 px noise, the two poses are 0.33 rad apart and both fit within
  the noise, and the first is 0.22 rad from the truth against the alternative's 0.13. That is why
  the choice is the caller's.
- **A second pose within 1e-3 rad of the first is dropped**, since LM often refines both IPPE
  solutions onto one (3e-5 rad apart on a clean view).
- **Test noise is Box-Muller over `mt19937_64`**, because `std::normal_distribution` is
  implementation-defined, and recorded angles must not belong to one standard library.
- **Three sabotages, all caught; the first cut of one failed to build**, since deleting the
  comparison left a helper unused under `-Werror`. It was redone so that it compiles.

### Update 2026-10-05 — M9 slice 2, sub-slice 1: Ceres and Eigen land, with nothing depending on them

The FFmpeg and OpenCV slice-0 shape, for registration's solver.
- `cmake/addEigen.cmake` brings Eigen 3.4.0's headers.
- `cmake/addCeres.cmake` builds Ceres 2.2.0 from source: static, with miniglog and Eigen's sparse
  Cholesky; SuiteSparse, METIS, LAPACK, CUDA, Accelerate, gflags and the Schur specialisations off.
- `plugins/camera/ceres` (`LAIN_CAMERA_CERES`, default `${LAIN_NOT_SUBPROJECT}`) holds a build probe.
`ctest -j4` **1055/1055** Debug and **1063/1063** Release with video, camera and Ceres on (+5 each),
and **1001/1001** Release with all three off (unchanged); warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 1 built* under M9 slice 2.

- **The one-implementation proof is a test.** `camera::project<ceres::Jet>` instantiates for all six
  distortion models, and its derivatives match finite differences of `project<double>`, through
  the inverse Brown-Conrady model's Newton iteration and Kannala-Brandt's on-axis branch.
- **`EIGEN_MPL2_ONLY` is on the `Eigen3::Eigen` target**, so it reaches all 115 of Ceres' compiles
  and building Ceres proves no LGPL Eigen code is used. Ceres' own `config.h` still calls Eigen's
  sparse Cholesky LGPL; that is stale.
- **Eigen's CMakeLists is never run.** The archive is only populated, and a config and version file
  in `CMAKE_FIND_PACKAGE_REDIRECTS_DIR` answer Ceres' `find_package(Eigen3)` with this copy.
- **Measured:** Ceres builds in 1m13s wall / 4m04s CPU (Debug, four cores), 2.8 times that with
  the Schur specialisations, which is why they are off. `libceres.a` is 7.3 MB in Release.
- **The aggregator test assumed every camera plugin is a detector**, and Ceres is not. It now asks
  per plugin.
- **Inside `lain::camera`, write `::ceres::`.** The plugin's namespace is `lain::camera::ceres`, so
  an unqualified `ceres::` finds it first. The mistake is a compile error, not a wrong lookup.
- **A sabotage that proved nothing:** a qualified `std::sqrt` in `detail::ray` still compiled,
  because only unprojection reaches it and no Jet is unprojected. The same sabotage on the
  projection path fails the build.

### Update 2026-09-30 — M9 slice 1, sub-slice 8c: a board to print and the capture guide (**fixture machinery COMPLETE**)

The last third of the real-camera fixture's machinery. What remains of sub-slice 8, and of slice 1,
is the capture itself, which only the repo owner can make. `ctest -j8` **1035/1035** Debug with
video and camera on, **1042/1042** Release on, **985/985** Release off; warning-clean, format-check
clean. Full notes in WORK.md's *Sub-slice 8c built*.

- **`apps/flowview/examples/render-board.json`** prints the board: 9 x 6 squares of 30 mm, ARUCO
  5x5_100, at 300 dpi, sized for A4 landscape. `flowview run -g render-board.json --board board.png`
  was driven through the real binary. The example catalog gains **`needsCamera`**: a build without
  a backend must refuse it, naming each camera kind. The builder writes the camera kinds whatever
  the build, so every build writes the same bytes.
- **`plugins/camera/test/fixtures/README.md`** is the capture guide:
  - print, then measure;
  - two sessions of about 40 poses, with a remount between them;
  - commit about 14 frames a session (12 at the least, checked at the code);
  - fill in `fixture.json`, including how RealSense's intrinsics and distortion names map onto
    lain's;
  - pin the full capture as the extended tier.
- **`camera-fixture-tool template`** writes a `fixture.json` to fill in, so the person starts from
  a document that already has the right shape.
- **ADR-0016 is amended in place**: the two-part repeat check, board-geometry reconstruction
  recorded as not built, storage, and extended-tier addressing.
- Two sabotages in the camera-off build, both caught.

### Update 2026-09-30 — M9 slice 1, sub-slice 8b: the fixture harness, and a repeat check in two halves

The second third of the real-camera fixture: a fixture folder's format and loader, calibration of
each session, the repeat check, the manufacturer comparison, and the hashed extended tier. It is test
code in `plugins/camera/test` (`camera-fixture`, `camera-fixture-tool`, `test-camera-fixture`), run
here on synthetic two-session fixtures written in the real on-disk format. `ctest -j8` **1035/1035**
Debug with video and camera on (+9), **1042/1042** Release on, **985/985** Release off;
warning-clean, format-check clean. Full notes in WORK.md's *Sub-slice 8b built*.

- **Cross-validation alone cannot see an intrinsics error, and measuring it is what said so.** A held
  model's board poses are recovered per view with the model fixed, and on a planar board a pose
  absorbs most of a focal or principal-point error. A second camera 3% longer and 12 px off-centre
  predicted the first's views at 0.27 mrad against Ready's 0.6, and a factory model 2% long validated
  as well as the estimate. So the repeat check keeps **prediction** (it sees a wrong lens) and adds
  **agreement**: two Ready sessions' intrinsics compared directly.
- **Agreement is bounded by the sessions' measured spread, three combined deviations**, not by the
  Ready stability bounds: held to those (0.5% focal), two sessions of ONE camera failed at 0.544%,
  because they bound one capture's resamples and two captures also differ by their poses. Ready caps
  each deviation, so the bound never exceeds about 2.1% and 8.5 mrad.
- **A fixture needs two Ready sessions**, so the compact tier's second session needs about 14 frames,
  not the 6 planned. 8c's capture guide carries that.
- **The manufacturer comparison never gates**, and the test proves it with a model beyond any
  agreement bound.
- **`data::fromValueStrict` / `data::shapeDifferences`** are promoted from camera::serialize, since
  the fixture document and its manifest are the second and third strict readers.
- Thirteen sabotages, all caught; one first failed to build and was redone.

### Update 2026-09-30 — M9 slice 1, sub-slice 8a: camera values as documents, model files in flowview

The first third of the real-camera fixture, which the repo owner chose to have built ahead of the
capture (compact tier committed as PNG, lain's own board printed and measured, two sessions with a
remount). New **`libs/camera/serialize`** writes and reads camera models, board specifications and
the new generic **`camera::CaptureRecord`**; flowview binds `--imported model.json` and writes a
`CameraModel` output as a camera-model document. `ctest -j8` **1026/1026** Debug with video and
camera on (+5), **1033/1033** Release on, **976/976** Release off; warning-clean, format-check clean.
Full notes in WORK.md's *Sub-slice 8a built*. 8b (the fixture harness) and 8c (the capture guide and
a board to print) follow.

- **Each distortion model has a stable key** (`brownConrady5`, `inverseBrownConrady5`, ...), ADR-0016's
  serialization identity, pinned by a test.
- **The readers are strict where `lain::data` is best-effort, deliberately.** They write back what
  they read and compare shapes, so a misspelled coefficient, an unknown distortion name or a dictionary
  typo is a problem named by its path instead of a silent zero or default. A hand-typed manufacturer
  model is exactly where that matters.
- **The vertical round-trips a model file:** calibrate to `model.json`, then bind it with `--imported`
  into a document that holds it; that run reports Exploratory and outputs the identical model.
- Five sabotages, all caught.

### Update 2026-09-30 — M9 slice 1, sub-slice 7: camera nodes, flowview wiring, a cli vertical

The camera work becomes reachable. New **`libs/camera/flow`** (`lain::camera::flow`, namespace
`lain::camera`) holds five node kinds: `boardSpecification`, `renderBoard`, `detectBoard`,
`calibrateCamera` and `cameraModel`. flowview registers the backends, then the kinds a backend can run,
and offers them under a **Camera** menu category. `ctest -j8` **1021/1021** Debug with video and
camera on (+12), **1028/1028** Release on, **971/971** Release off; warning-clean, format-check clean.
**gui-mode NOT eyeballed.** Full notes in WORK.md's *Sub-slice 7 built*. Sub-slice 8 (the real-camera
fixture) is the only one left, and it waits on a capture.

- **Capability-driven registration is ONE table** (key, needed capability, registrar), read by both
  `availableCameraNodeKeys()` and `registerCameraNodes()`, so the menu and the factory cannot
  disagree. It is tested from empty registries one backend at a time, and held to the configuration
  both ways in flowview: an OpenCV build that offers no camera kind fails, as does a build without
  OpenCV that offers any. **Port types register always.** *(Superseded 2026-10-09: flowview's
  expectation is now per kind, since both registration kinds also need Ceres, and every kind the
  library has must have one. See M9 slice 3, sub-slice 10.)*
- **The flowview catalog is built per call** rather than held in a static, because its Camera
  category depends on backends that register after first use would have fixed it.
- **The cli vertical runs through `runGraph`:** 24 frames of the production render seen by a known
  pinhole (drawn in plain C++, supersampled and bilinear, so flowview's tests need no OpenCV) calibrate
  to **Ready**, with the model within 0.14% and 0.3 px. It was also driven through the real binary. A
  build without a backend refuses the same document and names each missing kind.
- **Found: `flow::serialize::toValue` silently drops a param whose type has no codec.** Removing the
  `distortionModel` codec made the vertical calibrate the default model with nothing logged. A census
  test now requires a codec for every param of every palette kind. The serializer fix is left for
  its own change.
- **Found on CI (fixed in `f8ddc2b`, ahead of this):** test-camera did not link on MSVC, since
  Catch2's `std::string_view` StringMaker is compiled out there. Compare a view as `std::string`.
- **A float parameter means the decimal typed:** `markerToSquare` and `heldOutFraction` are rounded to
  millionths, `squareLengthMm` to whole micrometres. A seed keeps its bits.
- **Serialization of camera values is re-deferred** to sub-slice 8's imported model, the first thing
  that reads one from disk; `run` writes each value's one-line `toString()`.
- Nine sabotages, all caught, listed in WORK.md.

### Update 2026-09-30 — M9 slice 1, sub-slice 6: the OpenCV estimator

`OpenCVEstimator` joins the plugin (registered as "opencv"): `calibrateCamera` for no distortion,
Brown-Conrady 5 and rational 8; `cv::fisheye::calibrate` for Kannala-Brandt 4; inverse and modified
Brown-Conrady are refused, never substituted. `ctest -j4` **1009/1009** Debug with video and camera on
(+7); Release and camera-OFF builds pass. Full notes in WORK.md's *Sub-slice 6 built*.

- **End to end, it works:** rendered, distorted (by lain's own model), detected and calibrated
  footage returns its Brown-Conrady camera within 0.3% and 2 px, with held-out residuals under
  0.25 px, and is judged Ready.
- **`boardPose` is model-agnostic**: corners are unprojected with lain's model and the pose is solved
  on normalised coordinates, so validation works for models OpenCV lacks (a held inverse
  Brown-Conrady camera validates to 1e-6 px).
- **New entry point `calibrate(detections, image, board, request)`**, which the footage overload
  wraps: re-calibrate stored detections without decoding frames.
- **The rational model's radial coefficients are not identifiable** (a common factor in numerator and
  denominator changes all of them and no projection). The test pins the projection, not `k4`.
- **Two real slips found by tests:** a rational model reported 14 coefficient deviations, because
  OpenCV returns 14 elements and the count came from that vector; and a swapped p1/p2 readout or a
  rational fit without its rational terms went unseen by the accuracy checks. A noiseless
  coefficient-recovery case now catches both.
- The rational model is Exploratory on this synthetic scene: accurate, but its principal point moves
  2.2 mrad across resamples against Ready's 2.0, which is the stability criterion working.

### Update 2026-09-30 — M9 slice 1, sub-slice 5: the board calibration method module

`lain::camera::calibration` (Request, Report, the `Estimator` seam, the `reconstruction/1` fitness
profile) and `calibration::board::calibrate(footage, board, request)`: detection on the process pool,
a canonical held-out split, greedy view selection by coverage and tilt, estimation through the
backend, validation through lain's own projection, seeded resampling and the verdict. No real
estimator yet (sub-slice 6). `ctest -j4` **1002/1002** Debug with video and camera on (+11); Release
warning-clean. Full notes in WORK.md's *Sub-slice 5 built*.

- **Every criterion in `reconstruction/1` is resolution-independent**: held-out RMS angle, coverage
  fraction, relative focal and principal-point variation. Ready requires all evidence present, and a
  held model (HoldAndValidate) is therefore at most Exploratory.
- **The tests found a real bug:** bootstrap resamples were not seeded like the estimate, so under the
  Initial policy the stability evidence described a different procedure. Now they start where the
  estimate did.
- **The parallel path is proven, not assumed**: a test starts a real pool and requires detection on
  several threads, and DeterministicDebug on exactly one. With no pool started, "parallel" runs
  inline, and an equal-results check cannot tell.
- Resampling draws come from one seeded `mt19937_64` via a plain modulo, because the standard's
  distributions are implementation-defined.
- Stand-in backends answer from a known truth in their own test executable, since registries only
  grow.
- Two refactors in unpushed code: `Provenance` moved to `lain::camera`, and `distortion.h` gained
  `DistortionModel`. Inside `calibration`, a bare `board::` now means `calibration::board`, so the
  headers spell `camera::board::`.

### Update 2026-09-30 — M9 slice 1, sub-slice 4: the OpenCV renderer and detector

`lain::camera::opencv` renders and detects ChArUco boards behind `lain::camera::board`'s seams,
registered by the new generated `lain::camera::registerCameraBackends()` (a no-op without a plugin,
like `registerVideoCodecs`). Registering calls `cv::setNumThreads(0)`. `ctest -j4` **991/991** Debug
with video and camera on (+15); Release warning-clean; the camera-OFF build registers nothing and
fetches no OpenCV. Full notes in WORK.md's *Sub-slice 4 built*.

- **Measured accuracy, not asserted.** Native detection on ~60 px squares, 20 degrees off axis:
  mean 0.068 px, worst 0.136. Half-scale detection refined against source pixels: mean 0.064, worst
  0.100, i.e. native accuracy. A flat rendering is exact. Tolerances are set from these numbers, and
  each number is written beside its check.
- **The tests found a real defect in the first refinement window.** A quarter of the corner spacing
  reaches into the markers, and "refined" half-scale detection measured 0.80 px against 0.24 px
  unrefined. The window now stays inside the corner-to-marker gap, as OpenCV's own ChArUco refinement
  does.
- **Corners map back centre to centre** ((x + 0.5) / s - 0.5). The detection-scale-only case is what
  catches a corner-to-corner mapping, because native refinement would hide the half-pixel offset.
- **lain's projection matches `cv::projectPoints` / `cv::fisheye::projectPoints` to 1e-6 px** for
  the three shared distortion models, so the coefficient orders are OpenCV's.
- **A sabotage that passed, and why.** A wrong dictionary mapping round-trips, because renderer and
  detector share it. Only OpenCV's dictionary, named independently in the test, can tell, so every
  dictionary is now rendered and checked that way, from ids high enough to separate a dictionary
  from its smaller prefixes.
- Any pixel format detects: wider formats are narrowed per channel by lain first, and never reduced
  to grey by lain, whose grey is a luminance that needs linear light.

### Update 2026-09-30 — M9 slice 1, sub-slice 3: board values and the detection seam

`lain::camera::board`, in `libs/camera`: ChArUco patterns with a pinned, versioned fingerprint,
physical instances and specifications, observations, detection requests and reports, renderings,
and the `Detector` / `Renderer` backend registries behind `detect()` / `render()` facades. No
backend yet (sub-slice 4). `ctest -j4` **976/976** Debug with video and camera on (+11); Release
warning-clean. Full notes in WORK.md's *Sub-slice 3 built*.

- **The fingerprint hashes a versioned canonical text**, with the ratio as whole millionths so no
  double is ever formatted. One pattern's digest is pinned in a test, so a format change has to bump
  the version line.
- **The board frame is the camera frame's convention** (X right, Y down, Z into the board), so a
  camera squarely facing an upright print sees the board's axes as its own.
- **The facades do what every backend must do identically:** `detect()` resolves the scale policy,
  records the request and timing, sorts features canonically and keeps status and observation
  consistent; `render()` builds the description and fingerprint and checks the raster's size and
  format rather than trusting the backend. No backend is a Failed report with
  `Rejection::NoBackend`, from the same value every other outcome arrives in.
- **An empty registry gets its own test executable**, since a `core::Factory` only grows.
- The generated `registerCameraBackends()` aggregator moved to sub-slice 4, beside its first
  backend.

### Update 2026-09-30 — M9 slice 1, sub-slice 2: camera models (`libs/camera`)

`lain::camera` exists: immutable validated camera models, the closed distortion set, and ONE
scalar-generic projection implementation. `ctest -j4` **965/965** Debug with video and camera on
(+14); Release warning-clean. Full notes in WORK.md's *Sub-slice 2 built*.

- **Six distortion models** in a `std::variant`, each with its formula, coefficient order and
  direction written in `distortion.h`. `BrownConrady5` and `InverseBrownConrady5` hold the same
  five numbers and are tested not to be interchangeable.
- **`project<T>` / `unproject<T>` are scalar-generic**, so slice 2's Ceres Jets will run the same
  code a report measures with. Coefficients convert to `T` before use. A `float` instantiation is
  tested as the witness that nothing is secretly double.
- **A model knows where it is defined.** `create` finds where the radial mapping first folds, and
  projection past it is `OutsideDomain` rather than a plausible pixel on the wrong branch.
- **`create` is the only door**, and it refuses a distortion that folds before an image corner
  (`FoldsInsideImage`) as well as the plain malformations. The unchecked `CameraModelParameters` is
  what an importer or solver holds until then.
- **Pixel (0, 0) is the centre of the top-left pixel** (OpenCV's and librealsense's convention),
  now in CONTEXT.md.
- **Deviation:** serialization moved to sub-slice 7, as its own `libs/camera/serialize` target
  beside its first reader, on `media` and `flow`'s precedent of keeping `lain::data` out of the core
  library.
- **Eight sabotages, all caught.** Worth remembering from them: the first fold-scan sabotage failed
  to build (it left `foldOf` unused under `-Werror`) and so proved nothing; the rewrite placed the
  sabotage inside the function.

### Update 2026-09-30 — M9 slice 1, sub-slice 1: camera foundations (`Length`, `Sha256`, `RigidTransform`, `AxisConvention`)

The std-only and GLM-only pieces the camera code stands on, with no consumer yet (sub-slices 2 and 3
are theirs). `ctest -j4` **951/951** Debug with video and camera on (+20), plus the release-only
saturation case in a Release build; warning-clean, format-check clean. Full notes in WORK.md's
*Sub-slice 1 built*.

- **`core::Length`** is exact int64 nanometres behind named units, as `core::Time` is for seconds, so
  one quantity is one value and 0.1 mm + 0.2 mm == 0.3 mm. A NaN or out-of-range double is a
  precondition (asserted in debug, made defined in release), on `core::Range`'s precedent: the door
  that turns data into a Length refuses bad input first.
- **`core::Sha256`** is a streaming FIPS 180-4 hash plus a one-shot `sha256()`, for the
  board-pattern fingerprint and the fixture dataset's content hashes.
- **The `core::hex` deferral fired and is built.** Its trigger was "a second consumer", and the
  digest's text was it. `hexDigits` / `hexDigitValue` in `core/hex.h`; `Uuid` uses both.
- **`math::RigidTransform<T>`**: composition reads the way `aFromB` field names chain
  (`aFromC = aFromB * bFromC`), and `fromMatrix` refuses scale, shear, reflection and a projective
  row rather than extracting the nearest rotation.
- **`math::AxisConvention`** is valid by construction (`create` refuses two axes on one physical
  line), and `basisChange<T>(from, to)` is an exact signed permutation whose determinant is -1 exactly
  when handedness differs. The camera frame is `AxisConvention::XRightYDownZForward()`.
- **Seven sabotages, all caught**. The instructive one: reversing `basisChange` fails only the
  vehicle-frame case, because camera to graphics is its own inverse and cannot tell the directions
  apart.

### Update 2026-09-30 — develop's CI after the example-documents push: two platform bugs, fixed

The example-documents push (`09b1b94`, the entry below this one) went red on every Linux leg and on
Windows, one failure each, and neither is in the examples themselves. Both are fixed in their own
commits. `ctest -j4` **931/931** Debug with video and camera on.

- **Linux: a uri in a path param lost its scheme on save.** `lain::data` wrote a
  `std::filesystem::path` through `generic_string()`, and **libstdc++ collapses repeated
  separators**, so `s3://bucket/a.png` saved as `s3:/bucket/a.png`. libc++ and MSVC keep them,
  which is why macOS never showed it. This dates from graph serialization (2026-07-12), and the new
  document-paths test was simply the first thing to look. `documentpaths.cpp` had the same round
  trip twice (the scheme check, and the write-back of a path it left alone). Both halves are fixed,
  and undoing either one fails the case.
- **Windows: the byte comparison of committed examples read every one as stale.** GitHub's Windows
  runners check out with `core.autocrlf`, and there was no `.gitattributes`. Now
  `apps/flowview/examples/*.json text eol=lf`. Reproduced here with a `core.autocrlf=true` checkout:
  153 CR bytes in `tint-blur.json` without the attribute, none with it, byte-identical to the
  committed file.

### Update 2026-09-30 — M9 slice 1 planned; sub-slice 0: `run` / `list` refuse a lossy load

M9 slice 1 (camera geometry + ChArUco calibration) is planned as **nine sub-slices (0 to 8), one
commit each**, in WORK.md's M9 build order. Four decisions came from the repo owner while planning, all
recorded by amending [ADR-0016](docs/adr/0016-camera-calibration-method-modules.md) in place:
- **The real-camera fixture is last and generic.** The D455 is its first capture, not a type in the
  code, and its provenance is a generic record. Everything before it lands on synthetic evidence.
- **A camera node kind registers only when a backend can run it**, so `LAIN_CAMERA_OPENCV=OFF`
  registers none. This is the opposite of video's rule, on purpose: video's reader seam always
  exists and only a format may be missing, while a calibration node with no backend cannot
  function. Port types still register, since they are vocabulary. *(**Reversed 2026-10-02** — every
  camera kind registers; only what the menu offers follows the backends. See that update.)*
- **OpenCV runs serial; lain parallelises across frames** (`cv::setNumThreads(0)`, one frame per
  task on the process pool). This settles ADR-0026's recorded thread-pool question, marked there.
- **The OpenCV plugin renders boards**, so a rendered and a detected board are one backend's idea of
  the pattern.

**Sub-slice 0 is built**: `run` and `list` refuse any document whose load reported a
`Severity::Error`. It is the registration rule's consequence (a document saved with camera nodes,
opened by a build without them, loses those nodes), and it was a real hole already. `ctest -j4`
**913/913** Debug with video and camera on (+3); warning-clean, format-check clean. Full notes in
WORK.md's *Sub-slice 0 built*.

- **The old check had been dead since M5 slice 2.** It tested `nodeCount() == 0`, and every `Graph`
  is born with its boundary pair, so nothing reached it. Measured before the fix, through the real
  binary: `run --graph missing.json` ran an empty graph and **exited 0**, and so did `list`.
- **Carrying on was data loss, not just a wrong answer.** A node whose kind the build lacks is
  dropped with both of its edges, and `run --save` then wrote that smaller graph to the path it
  named. A refused run writes nothing, and the test asserts it.
- **A Warning still runs** (an unknown param), so one stale param does not make a document
  unrunnable. The gui is unchanged: it already lists every load issue. *(**Superseded 2026-10-02**:
  the gui's guard was the same dead check, and it now refuses too; see that update.)*
- **Two sabotages, both caught:** the old `nodeCount()` test fails both refusal cases; refusing on
  any issue fails the warning case.
- **The full tree now builds in this sandbox.** GitHub source archives still answer 403, so each
  pinned GitHub dependency is cloned over git and handed to configure as
  `FETCHCONTENT_SOURCE_DIR_<NAME>`. libtiff (gitlab) downloads normally, and FFmpeg and OpenCV are
  release assets. The ctest baseline before this change was 910/910, matching CI's Linux Debug leg.

### Update 2026-09-30 — example documents: media paths become document-relative, gradient records its size, the examples land, CI runs one (commits 1–4, COMPLETE)

The repo owner asked for example graph documents for testing. **No graph document or media file had
ever been committed**, so nothing tests that a document written by an older build still loads.
Grilled 2026-09-30; the plan is in WORK.md's *Example documents*. It calls for about 20 focused
examples, a composition "bagel", `broken-*` documents, and a small self-generated `data/`, all
written by one C++ builder with deterministic ids.

**Commit 1 of 4 is built:** document-relative media paths
([ADR-0027](docs/adr/0027-media-paths-relative-to-the-document.md)).
- A path param was never interpreted. A relative one was read against the working directory, and
  the file dialog saved absolute paths, so real documents broke when moved.
- Now a path is absolute in memory, and on disk relative when inside the document's folder tree,
  never `../`.
- The format owns the rule: `flow::serialize::relativizePaths` / `resolvePaths` over a document
  Value, keyed on `kPathTypeKey`. flowview applies them where a document meets a file, so undo
  snapshots are never rebased.
- `ctest -j8` **919/919** Debug with video on, **925/925** Release with video on, **892/892**
  Release video-off; warning-clean, format-check clean. Verified through the real binary run from
  `/`.
- Two other shapes were tried and dropped; see WORK.md's commit 1 notes.

**Commit 2 is built:** `GradientNode`'s size is now `width` / `height`, Int inputs with defaults, so
it is saved with the document and drivable from the graph. It used to be a construction value taken
from `--size`, so one file rendered at a different size per session. `--size` is only where a new
gradient starts, and older documents keep behaving as before. `ctest -j8` **921/921** Debug with
video on, **927/927** Release with video on, **894/894** Release video-off.

**Commit 3 is built:** `apps/flowview/examples/` holds 22 documents, a README and ~28 KB of
synthetic `data/`, written by `flowview-examples` from ONE table that `test_examples.cpp` also reads.
Every example loads, runs and raises what its entry says, and the committed files are byte for byte
what the builder writes (`examples` / `examples-data` targets regenerate them). They found
`gaussianKernel`'s sigma-0 NaN (a black image; fixed in its own commit) and a `LoadImage` gap
inside maps (recorded). `ctest -j8` **924/924** Debug with video on, **930/930** Release with video
on, **897/897** Release video-off. **gui-mode not yet eyeballed.**

**Commit 4 is built:** CI's new *Smoke an example document* step runs `folder-average.json` through
the shipped binary from a folder that is not the document's. It ends with `test -s` on the output
file because `flowview run` exits 0 when a bound output is empty. The step was verified locally, not
yet on CI.

**Owed:** a gui-mode eyeball of every example (their primary purpose), and the first CI run.

### Update 2026-09-29 — M14 slice 8a built: a pull of many targets, cancellable and parallel

The flow half of Run Selection, on the pull fix below.
- **`Scheduler::evaluate(definition, evaluation, targets, control)`** pulls several root-level targets
  at once. The single-target form forwards to it.
- **A pull's stages execute through `executePlan`**, so ParallelScheduler runs a pull on the pool
  instead of walking it serially.
- **What a pull leaves owed is reported through a new `RunObserver::leftOwed`.**

Nothing in flowview calls it until 8b. `ctest -j8` **901/901** Debug with video on, **907/907**
Release with video on, **874/874** Release video-off (+7 each); warning-clean, format-check clean;
`[pull],[observer],[cancel]` (36 cases) swept 100× in Debug and Release; `flowview run --example`
unchanged. Full notes in WORK.md's *Milestone 14 › Slice 8a landed*.

- **A pull is a run with a cone, and nothing else.**
  - `Scheduler::Mode` is deleted, and `buildRunPlan` / `buildEvalPlan` became one `buildPlan` over an
    optional cone (null for a run). One staging loop serves both entry points.
  - The lease, cancellation, progress and the observer were already `run()`'s.
  - The cone is collected once per pull, since the definition cannot change under a run.
- **`leftOwed` is its own method, not a flag on `owed`.** A host's copy must mark those nodes stale,
  or a node fed the new value past the cone reads current (7a's argument). But the runner turns `owed`
  into **Queued**, and a left node will never compute, so the two must not share a spelling a host
  could forget to split.
- **A target the definition does not hold contributes nothing.** A host's selection may outlive a
  node, since Run Selection is captured at the click and may start frames later. No targets pulls
  nothing.
- **Three sabotages, all caught:**
  - a serial pull fails the new threads case;
  - no `leftOwed` report fails both the report case and the replay-exactness case;
  - reporting left nodes as `owed` fails the report case.

### Update 2026-09-29 — a pull finishes what it starts (fixed ahead of M14 slice 8)

Found while planning slice 8 (Run Selection, which is `Scheduler::evaluate` with many targets), fixed in
its own commit ahead of it. The pull planned only the nodes of its target's cone that were stale **on
their own account** — the M1 semantic, *"a clean node keeps its cached value even if something
upstream recomputes"*. Once anything has been computed that is unsound: edit U in U → D → T, and a pull
of T recomputed U alone, after which the stale closure called D and T **current** while they showed
values built from U's old output. A node outside the cone that U feeds went the same way. It is slice
3's *"staleness assumes a run finishes its closure"*, from the pull side. `evaluate` had no production
caller, so nothing on screen was ever wrong; slice 8 would have been the first. `ctest -j8` **894/894** Debug with video on,
**900/900** Release with video on, **867/867** Release video-off (+4 each); warning-clean, format-check
clean; `[pull]` swept 100× in Debug and Release; `flowview run --example` unchanged. Full notes
in WORK.md's *Milestone 14 › A pull finishes what it starts*.

- **A pull plans the part of the stale CLOSURE in its target's cone.** The cone is closed upstream, so
  nothing the plan recomputes waits on a value it will not produce.
- **What it leaves of the closure stays owed** (`Scheduler::Plan::left`, requested beside
  `Plan::closure`): a node outside the cone that the closure **feeds** and whose **own record is
  clean**. Both halves were settled by a test that fails without them:
  - a group stale inside AND fed by the edit has to be requested, or it never republishes the new input
    (21 instead of 22);
  - a group stale ONLY inside must not be, since a request would make it republish and lose its inner
    incrementality. That is the closure's own rule from slice 3.
- **Every existing pull test missed it for one reason:** each started from a fresh evaluation, where
  "stale on its own account" and "in the closure" are the same set. The four new cases
  (`libs/flow/test/test_pull.cpp`) each run once, edit, then pull. Four sabotages, all caught.

### Update 2026-09-29 — M14 slice 7b built: results land node by node; Queued and Computing (**slice 7 COMPLETE**)

The flowview half of slice 7, on 7a's `RunObserver`. Each node's value and thumbnail now appear the
moment it finishes, not when the run ends. The canvas shows every node the run owes as **Queued** (a
filled dot), the node computing as **Computing** (a turning arc, and an outline that pulses), and then
6b's green *just updated* fade as its result lands. A group, map or loop shows the most active state
inside it. `ctest -j8` **890/890** Debug with video on, **896/896** Release with video on,
**863/863** Release video-off (+9 each); warning-clean, format-check clean; `[runner],[freshness]` (23
cases) swept 100× in Debug and Release; `flowview run --example` identical to the 7a binary; the
`--frames` smokes exit 0, including a quit mid-run. **gui-mode checked by the repo owner 2026-09-29.**
Nine sabotages, all caught. Full notes in WORK.md's *Milestone 14 › Slice 7b landed*.

- **The runner turns reports into two things the frame loop takes each frame**, together with the
  publication and under one lock, so a node that has just finished is never shown idle before its
  result has landed:
  - **folds**: per node, the latest record plus whether it has been owed again since. A loop's next
    pass owes a node whose last pass has just landed, and both reach the host.
  - **activity**: Queued or Computing, per node.
- **A publication at the START of every run** (after prepare + bindings), replacing an untaken one, as
  decided in 7a. A run's records then always fold onto a copy of its own clone. It also means the
  structure appears at once after New or Open, instead of staying empty until the first run ends.
- **Storing a publication drops the folds it contains; a SKIPPED stage publication drops none.** A
  cancel ends every Queued at once (the run will start none of them), while a compute already running
  stays Computing until it finishes. The run's end clears all activity.
- **The landing order is a driver-free function, `land()`** (`runner.h`), which `pollRun` calls. It
  applies the publication, then the folds, then the pending bindings laid back over them. That last
  step has to come last: a folded boundary node carries what its RUN bound, and a newer binding may
  still be queued. A test pins it.
- **Precedence Computing > Failed > Queued > Stale > Current** (decided with the repo owner), applied
  to a node's own state and to the roll-up alike. `-Wswitch` found the Preview pane's header switch
  the moment the enum grew.
- **Found while sabotaging, twice.**
  - A failing runner case HUNG instead of failing. A `REQUIRE` that throws past the release leaves a
    held compute waiting forever, and the runner's destructor joins it. The held computes now wait at
    most 10 s, the bound `waitEntered` already had.
  - Not clearing activity at the run's end passed every test. A run that ends normally has already
    emptied its activity node by node; only nodes it never starts (a throw's successors) need the
    clear. The throw case now has a node downstream of the thrower.

### Update 2026-09-29 — M14 slice 7a built: the run observer, the node record, `flow::EvalPath`

The flow half of slice 7 (per-node publication). A run now TELLS a host what it does as it goes, through
**`flow::RunObserver`**:
- `stageFinished()` — the old stage observer, now one of its methods;
- `owed(path, node)` — each node a stage owes;
- `started(path, node)` — a node compute beginning;
- `finished(path, node, const NodeRecord*)` — a step finishing, with **`flow::NodeRecord`**, a copy of
  that node's record.

A host folds records into its copy with **`PublishedEvaluation::fold`** and marks owed nodes with
**`owe`**. Nothing in flowview reads the reports until 7b; the runner only moved its stage publication
onto the new seam. `ctest -j8` **881/881** Debug with video on, **887/887** Release with video on,
**854/854** Release video-off (+9 each); warning-clean, format-check clean;
`[observer],[published],[cancel]` (33 cases) swept 100× in Debug and Release; `flowview run --example`
identical to the pre-change binary; the `--frames` smokes exit 0. Full notes in WORK.md's *Milestone 14
› Slice 7a landed*.

- **Found while planning: folding one node's record makes what is downstream of it look CURRENT.** D's
  own record is clean; it was stale only because U was. Fold U's record and nothing holds D, though it
  still shows the value built from U's old output. `owed` is the fix: the scheduler already persists
  each stage's closure as recompute requests (slice 3), so it reports each one, and a copy that mirrors
  them stays as sound as the working evaluation. A test shows both halves (with the marks the sink is
  stale; without them it reads current while showing 1 instead of 7).
- **Decided with the repo owner:**
  - ONE observer interface rather than a second hook beside the stage observer. The worker-side
    methods are `noexcept`, since an exception escaping one would leave inside a step.
  - A publication at the START of every run (lands in 7b), so records always fold onto a copy of
    their own clone.
  - The roll-up order for 7b: **Computing > Failed > Queued > Stale > Current**.
- **`EvalPath` is flow's now**, because a working evaluation's address names nothing in a copy of it.
  `Plan::levels` holds one path per evaluation a stage touches: `{owner, 0}` for a group or loop,
  `{map, i}` for an element. flowview's `PathStep`/`GraphPath` are deleted, not aliased, with every use
  renamed. The compiler found three unqualified `loopAt` calls in a test that had been found by ADL
  through the old flowview type.
- **What reports what.** `started` fires for node computes only. `finished` carries a record after a
  compute that got through, after a throw (carrying the failure, before the exception leaves), and after
  every crossing, for its owner. It carries null for a give-up; nothing is reported for a compute a
  cancel skipped. A fold GROWS a map's elements the copy lacks, never shrinks, and refuses another
  lineage.
- **Exactness is the headline test:** a copy taken at run start, with every report replayed in order,
  shows exactly what the run-end copy shows. That holds at every level: the same payload object per port,
  `needsRecompute`, failures and child counts. It is checked for a group plus a map that grew 3 → 4,
  and a loop, under both strategies. Six sabotages, all caught.

### Update 2026-09-29 — M14 slice 6b built: freshness on screen (**slice 6 COMPLETE**)

The UI half of slice 6. New `freshness.{h,cpp}`: `Freshness { Current, Stale, Failed }` and
`levelFreshness(document, published, path)`, which walks to the level on screen asking 6a's
`StaleClosure` at each step and carrying `reseeds()` down. MainWindow works it out once a frame
(`AppContext::freshness`). It drives:
- the canvas: a title glyph with a tooltip worded by the trigger, and a green outline that fades when
  a value lands;
- a thumbnail badge in the Inspector and Interface, and words in the Preview header;
- a **status bar across the bottom of the window** (a new `lain::gui` seam, `beginStatusBar()` /
  `endStatusBar()` in `dock.h`, over ImGui's internal `BeginViewportSideBar`);
- an **Issues row per failed node at any level** (`collectFailures`). The run-level row is kept only
  for a throw no node owned (`RunReport::failedNodes == 0`).

`ctest -j8` **872/872** Debug with video on, **878/878** Release with video on, **845/845** Release
video-off (+10 each); warning-clean, format-check clean; 20 cases swept 100× in Debug and Release;
`flowview run --example` identical to the 6a binary; `--frames` smokes exit 0, including a quit
mid-run. **gui-mode eyeballed by the repo owner 2026-09-29** (a quick pass); the full checklist in
WORK.md's *Slice 6b landed* has not been walked item by item.

- **The status bar's separators are raw UTF-8 `·`, not `\u00b7`.** MSVC builds here without
  `/utf-8`, so a universal-character escape becomes a lone code-page-1252 byte, while raw bytes survive.
- **One sabotage is NOT caught, on purpose:** the walk reusing `resolveEvaluation`'s tolerance gives
  the same answer, since an ancestor's records are of another lineage and never pair. Recorded, and
  the comment claiming otherwise was corrected.
- Two deliberate deviations: the mark colours live in `freshness.cpp` (one mark, two surfaces), and
  the Preview pane says "stale" in words rather than badging the picture being inspected.

### Update 2026-09-29 — M14 slice 6a built: the stale closure, and the failure record

Slice 6 (freshness) is two commits, and this is the flow half. **`flow::StaleClosure`** (new
`staleness.h`) is the stale closure as a public const query, and the scheduler plans from it:
`Scheduler::stale()` / `runOrder()` are deleted, so what a run recomputes and what a host will draw as
Stale are one statement. **`Evaluation::failure(id)`** records what a node's last compute threw, where
it threw, with `hasFailure()` for a group's roll-up, and **`RunControl::failed()`** counts them.
Nothing in flowview reads either yet (6b). `ctest -j8` **862/862** Debug with video on, **868/868**
Release with video on, **835/835** Release video-off (+13 each); warning-clean, format-check clean;
`[staleness],[cancel],[published],[failure]` (30 cases) swept 100× through the Catch2 binary in Debug
and Release; `flowview run --example` identical to the pre-change binary. Full notes in WORK.md's
*Milestone 14 › Slice 6a landed*.

- **Failed is RECORDED, not attributed** (decided with the repo owner). WORK.md offered slice 7's
  per-step hook first, or flow naming the node in the exception. Instead `runNode`'s existing catch
  stores the message against the node in the evaluation it threw in, and the exception leaves
  unchanged. That lands a failure in the right map element or linked-group instance by construction,
  which a NodeId alone could not say. It reaches the published copy for free and is cleared by the
  node's next compute that gets through, suppression included. `failed()` tells a throw some node
  owned from one none did.
- **Staleness crosses into a group, and a per-level comparison misses it.** After an edit upstream of
  a group, nothing inside changed, yet every value past its boundary came from the old input. So a
  level is asked with `boundaryStale`, answered by the level above (`reseeds(owner)`).
  - A group or map reseeds under the plan's own republish rule, now one function the plan calls
    (`StaleClosure::republishes`).
  - A loop reseeds whenever it is stale at all, since it re-folds from its seeds.
- **The query compares the DOCUMENT against an evaluation prepared on a CLONE**, so it cannot use
  `needsRecompute` (which reads the clone and says clean). A node with no record is stale. A
  definition of another lineage is all stale rather than a throw: a query reports, `prepare` acts.
- **Nine sabotages, all caught, and two verification lessons that each produced a wrong result
  first.** A sabotage restored with `mv` from its backup keeps an OLDER mtime than the object built
  from it, so the build silently keeps the sabotage. Touch after restoring. And
  `--list-test-names-only` is Catch2 v2 and prints 0 in v3, while the sweep itself runs every case.
  Count with `--list-tests --verbosity quiet`.

### Update 2026-09-26 — one process task pool; `lain::task` becomes an alias for `multi`

The wrapper moved from Taskflow to `multi` on 2026-09-11 with `libs/flow` unchanged by a single line
of production code — which proved its one claim and exhausted its reason to exist. `lain::task` is
now **`namespace task = multi;`** and nothing else, the pool is the **process's** (owned by
`lain::app`, sized by `--threads`), and `flowview run` becomes the parallel scheduler's first
production caller. Decisions in **[ADR-0024](docs/adr/0024-one-process-task-pool.md)**. `ctest -j8`
**797/797** Debug with video on against a pre-change baseline of **793** (measured, not assumed — the
old tree was stashed, rebuilt and run twice), **803/803** Release with video on, **770/770** Release
in the default video-off configuration; warning-clean, format-check clean, and the parallel path swept
**100x** across five binaries. **gui-mode live-verified by the repo owner 2026-09-28** — nothing
draws differently, but `Application`'s run cycle is restructured and `main` is rewritten, so what the
eyeball actually confirms is that the three phases are ordered right around a real window.

- **A tag sweep must invoke the Catch2 BINARY, not `ctest -R`** — found the hard way here. The first
  two attempts reported *"0 failures / 100"* while matching **zero tests**: `catch_discover_tests`
  registers a case by its NAME, so a Catch2 TAG is invisible to `-R`, and a loop checking only exit
  status never notices *"No tests were found"*. Any sweep claim that does not say how it was run is
  worth re-checking.

- **`multi` changes by ZERO lines, and BOTH things I proposed adding to it were refused — correctly.**
  A scoped-pool RAII duplicates what `WorkerPool::~WorkerPool` already does (it asserts, then stops
  defensively); the only real defect is *where* it complains — a static destructor after `main` — and
  that is fixed by giving the global an owner, not by a second mechanism. And a synchronous
  `run(Recipe&&)` goes against multi's intent that **a thread is never blocked unless the caller
  really means it**: a blocking call reachable from a worker halts the system, so blocking must stay
  conspicuous rather than get a short safe spelling.
- **`lain::app`'s run cycle IS its interface now.** `run(int, char**)` is replaced by three public
  phases, `initialise` / `run` / `shutdown`, so the header states the model where it carried a
  six-step prose description. `initialise` answers `std::optional<int>` because it has three
  outcomes and not two — carry on, stop with 0 (`--version` / `--licenses`), stop with a failure —
  and it advances the phase only when it returns nullopt, so an early exit leaves nothing started.
  `main` calls all three; `~Application` asserts the stop happened and then does it.
- **THE SPLIT CAUSED A REAL DEFECT, and its shape is the lesson.** `cli::App` had been a LOCAL of
  `run(int, char**)`, so it outlived the parse by accident of scope — and a delegate keeps the
  `cli::App*` from `add_subcommand` and reads it later, which `flowviewapp.h` states in a comment
  (*"Their pointers stay valid through Application::run()"*). Splitting `initialise` out destroyed
  the parser at the end of phase one. **The symptom was a deterministic HANG**, with `--version`
  still working because it exits before anything dereferences a pointer; a crash would have named
  the line. The parser now lives on `impl`. Sabotage-verified by a new `[app]` case that stores a
  subcommand pointer in `onInit` and reads it in `onProcess`. General shape: **a lifetime held only
  by a scope is a lifetime nothing states**, and splitting the scope is what collects on it.
- **One vocabulary, not two.** `executePlan` had been writing `multi::Recipe` / `Step` /
  `RecipeResult` / `RecipeHandle` and then calling `task::async` on them — both spellings of one
  namespace in one function, which is the second name the alias exists to prevent. Swept to
  `task::` at both sites; `multi` is now named only where the sentence is about the library rather
  than about a type.
- **A namespace alias cannot be reopened, and that is the mechanism rather than a limitation.**
  `lain::task` can hold no owned name, which is what forced every behaviour the wrapper carried to
  find a real owner — and what makes it impossible to grow a second vocabulary back one name at a
  time. It is also why `scheduler.h`'s `namespace lain::task { class Executor; }` had to go: you
  cannot forward-declare into an alias. `flow`'s public header now names `task` nowhere.
- **lain does not wrap what it owns, and `acm::` was already the precedent** — it appears raw in the
  public headers of `lain::app` and `lain::gui`. Locked decision #1's *"external deps enter as thin
  wrapper libs"* was written about third-party code and is amended to say so.
- **One pool forced a choice two pools hid: multi's primitive families wait in OPPOSITE ways.**
  `parallel`/`each`/`range` have the caller steal (W workers + 1), `async(Recipe)` + `wait()` has it
  sleep (W). No single W is right for both, so the scheduler now waits with **`waitAll(handle)`**,
  which spins through `waitUntil` running stolen tasks — `hw - 1` workers plus a participating caller
  is exactly one runner per core for every primitive. `waitAll` is duck-typed over `.complete()` and
  is what multi's own README teaches on this handle, so this needed no new code.
- **It also retired two undocumented deviations that happened to cancel out.** `Executor::run()` used
  `handle.wait()` — the one wait that does *not* steal — and compensated by starting
  `hardware_concurrency()` workers instead of multi's `hw - 1`. Neither was recorded as deliberate.
- **`ParallelScheduler` had NO production caller** — flowview ran `SerialScheduler` in gui-mode *and*
  in headless `run`, so it was built, heavily tested and reached by no shipped binary. The ninth
  compiled-linked-unreachable instance in this file. `flowview run` now takes it (a cli is where
  blocking is free), gui-mode stays serial until the async milestone, and **`--threads 0`** is the
  escape hatch: the pool stays inactive and the same plan executes inline in dependency order.
  Verified through the real binary — `--threads` at -1/0/1/4 produces output identical to the
  pre-change *serial* binary once the timestamp and minted uuids are normalised.
- **THAT VERIFICATION WAS HOLLOW THE FIRST TIME, and driving the binary is what said so.** It was
  run as `flowview run --save f --threads 0`, and a reserved flag typed AFTER a subcommand is
  swallowed by that subcommand's `allow_extras` — so all four runs matched because all four
  silently used the default worker count, with only an unmatched-argument warning to show for it.
  Re-run as `flowview --threads 0 run --save f` the outputs are identical for the real reason.
  Pre-existing and shared with `-v`; `--threads` merely joined it. CLI11's `fallthrough()` is the
  obvious fix and was **tried and reverted**: it hands a subcommand's unmatched options up to the
  parent, which then refuses them, so flowview's `--<boundary>` bindings stop working altogether.
  The help text and ADR-0024 now state the position instead.
- **The test suite could have gone silently serial, and one case exists to stop it.** Sixteen sites
  lost their `Executor`; a parallel test that starts no pool runs the INLINE path and passes, faster,
  covering nothing — including the 100x sweeps that are this repo's only race cover. Every other
  parallel case asserts a result the serial path produces too, so none of them can notice. *(flow)the
  parallel scheduler really runs on several threads* asserts the mechanism instead: 32 independent
  nodes must have run on more than one thread. Sabotage-verified by deleting the pool from it.
- **`lain::testing::ThreadPool`** is the start/stop pair for a test, which has no `Application` to own
  the pool. Test-only by construction; deliberately NOT a production type, for the same reason multi
  did not get one.
- **`--threads` gets a test of its own**, because nothing else would notice it being disconnected: an
  option can parse cleanly into a member that reaches no one. One `[app]` case asks
  `task::threadCount()` from inside `onProcess` — the default starts `hw - 1`, `--threads 3` starts
  three, `--threads 0` starts none and `task::parallel` still runs. Sabotage-verified both ways:
  dropping the argument, and not starting the pool at all.
- **A second sabotage found the discrimination that matters.** Dropping `waitAll` from the `[task]`
  helper fails the two pooled cases and correctly leaves the unstarted-pool case passing — its work
  is already done inline by the time the handle is read. Which is exactly why the exception case has
  to start a pool: without workers it would pass with the bug in it.
- **`Services` was considered and refused, because it already had been.**
  [ADR-0004](docs/adr/0004-static-linking-service-shaped-seams-over-thorax.md) names *"a shared
  service registry, one process-wide logger/executor/factory across DSOs"* as machinery to repair
  fragmentation that DSOs themselves introduce. lain is one static binary.
- **Still out, and it is a milestone rather than a follow-on:** asynchronous graph execution and a
  non-blocking gui. The blocker is not the task layer — `Scheduler::run` is a staging loop, and ten
  pane files make 23 direct reads of an evaluation every frame. Payloads are shared and immutable
  since M5 slice 1 so the pixels are safe, but the **slot** is rebound non-atomically and a pane
  reading mid-rebind **tears**. *(Designed 2026-09-28 as **M14** — see that entry above. The slot
  turned out to be the smaller race.)*

### Update 2026-09-26 — the image type tables stop being `detail::`

`image`'s two enum-indexed type tables are now public and each sits beside the thing whose order it
must match: **`ChannelTypeList`** under the `ChannelType` enum (it had been below the *other* enum)
and **`ColorTypeList`** under the Color family it enumerates (it had been in `details/traverse.inl`).
Both invariants are asserted together in `image.cpp`. No behaviour change and no test added or
removed, so the existing suite is the regression test: `ctest -j8` **794/794** Debug with video on,
warning-clean, format-check clean. Full landing notes in WORK.md.

- **It REVERSES a recorded refusal**, which is the only reason it is worth an entry. WORK.md's
  §Refused, not deferred held *"a public `ChannelTypeList` … exposing it invites indexing with a raw
  `std::size_t`"*, and **nothing in `pixelformat.h` pointed at it** — so it was rediscovered in
  review, not by the author. That is the same shape as `core::Uri` and `core::hex`: *a queued
  decision with nothing pointing at it from the code it governs stops being a decision.* The bullet
  is deleted rather than amended, because a reversed entry in a "do not re-raise" list stops the
  list discriminating.
- **The refused argument survives as a stated cost.** `Color<F>`, `Color<F>::value_type` and
  `visit(img, fn)` already reach everything a consumer wants, enum-keyed; all 17 in-tree uses of the
  format table go through `visit`. The public aliases add `visitAt` with a raw index, and have **no
  consumer outside `lain::image`**. The locality win is real and was independent of visibility.
- **`ChannelTypeList` is a misnomer, kept deliberately.** Four lines under `enum class ChannelType`
  while holding `uint8_t`/`uint16_t`/`float`, it reads as "a list of ChannelType"; the intended
  parse is (Channel)(TypeList). `ChannelStorageTypes` / `ColorTypes` were proposed — `...List`
  appears nowhere else in `libs/` and names an alias after its own type, and "storage type" is
  already the phrase three comments use. **Trigger: a consumer outside `lain::image`.**
- **A comment that had gone from true to false:** `image.cpp` claimed both tables were *"guarded
  reorder-proof at their definitions"* — true before the move, false after, since the guard moved
  in with it. It also overclaimed for `ChannelTypeList`, which has no order guard and **can have
  none**: only the enumerator's name says `U8` means `uint8_t`.

### Update 2026-09-26 — a ColorSpace's transfer becomes one value

`lain::image` runs one conversion verb overloaded on its target, and the one place it did not was
the scalar transfer: a **public pair named for a direction**, `image::toLinear(ColorSpace, float)` /
`fromLinear(ColorSpace, float)`. The pair is now **one value** — **`TransferCurve {toLinear,
fromLinear}`**, answered by **`transferCurve(ColorSpace)`** — with the four curves private in
`details/colormath.inl`. `ctest -j8` **794/794** Debug with video on (+1, was 792), **767/767**
Release in the default video-off configuration, **800/800** Release with video on; warning-clean,
format-check clean, `flowview run --example` identical to the pre-change binary once the timestamp
and minted uuids are normalised. Full landing notes in WORK.md.

- **ADR-0003 promised the opposite of what it shipped.** Its *adding a color space is additive*
  bullet says `convert` expresses every pairing *"without a `toX`/`toY` explosion"*, and its own
  2026-09-01 amendment names `toLinear` / `fromLinear` as the mechanism that makes it so. One
  document, both claims — which is what made this a correction rather than a preference.
- **The deliverable is an INVARIANT, not a spelling.** The pair is what a colour space *is* on this
  axis, so it is a type you get whole or not at all, where two free functions could be half-written,
  wired backwards, or drift apart across two switches that had to independently agree.
- **Three arrangements sit on top of the type, each closing a different hole.** (1) **A `constexpr`
  pair per space, beside the curves it pairs** — so `transferCurve`'s switch maps a space to a
  *curve* and cannot get a direction wrong at all, and the one line where a pair could be reversed
  sits under the two bodies it names. A struct-per-space was refused: it would be the tree's first
  struct-as-namespace (verified — there is none in `libs/`) and leaves each half callable anyway.
  (2) **Space-first curve names** (`srgbToLinear` / `srgbFromLinear`). A `transferLinear*` prefix was
  measured against the goal and **still splits each pair** in a sorted listing, because it puts the
  invariant part first. Space-first clusters each pair and makes the suffixes mirror the member
  order, which turns three miswirings from test-only into readable: reversed, doubled, and
  cross-standard. (3) **Its own header** — `transfercurve.h` + `details/transfercurve.inl`, not a
  place beside the enum the way `formatDescriptor` sits with `PixelFormat`, because that descriptor is
  `constexpr` arithmetic needing nothing while these curves need `math::pow`, and `colorspace.h` has
  **zero includes** and reaches every consumer of the tags through `image.h`. The payoff is API
  SURFACE, not compile cost: consumers are nearly disjoint (`tiffcolor.cpp` wanted the transfer and
  nothing else; `operations.cpp` the reverse; only `convert.cpp` both), so the TIFF plugin no longer
  sees `luminance` / `saturate` / `convert<Dst>` / `mapColorChannels` — but it still pulls **311
  transitive headers against 313**, since both reach `math/types.h` for `math::pow` and GLM behind it
  is the whole count. **An earlier version of this note claimed 356 -> 62, which was fabricated** —
  measured against a file that did not exist yet. The split stands on separation and on housing the
  type and its trigger block, the same idiom as `imageview.h` / `traverse.h`, not on include weight.
- **`detail::identity` was the last generic name, and it took three attempts to place.** At
  `image::detail` scope it was the `image::descriptor` -> `formatDescriptor` problem one level down.
  **Two inline lambdas** were tried and reverted — clang-format's Allman braces put a lambda body on
  its own line, so `{[](float c) { return c; }, …}` comes out unreadable. **A nested
  `detail::transfer`** was tried and also removed, and the reason it was wrong is worth keeping: a
  file does NOT scope names, so the header split would not have retired the nest by itself; what
  retires it is removing the one name that needed protecting. `TransferCurve::identity()` is the
  answer — scoped to the type, nothing added to `image::detail`, with a `static_assert` pinning that
  static members cost the type neither aggregate init nor trivial copyability.
- **`toUnit` / `fromUnit` are deliberately NOT given the same treatment**, and the rule is worth
  carrying: **a pair earns being a type only when its halves are assembled at runtime.** A
  `ColorSpace` comes from a file, so a switch picks the halves and can pick the wrong pair — that is
  the invariant `TransferCurve` buys. `toUnit<T>` / `fromUnit<U>` are selected by template argument:
  two instantiations, not two things anyone pairs. They are also the *original* of the pattern and
  were always private, which is why they never became the complaint the public pair did. Trigger, at
  the code: a runtime channel-type dispatch (`PixelFormatDescriptor` already carries a `ChannelType`).
- **MY PERFORMANCE ARGUMENT FOR THIS SHAPE WAS WRONG, and measuring is what said so.** The case for
  hoisting was that it removes a per-channel switch and so pays for itself with more than tidiness.
  Benchmarked A/B in one TU over 50M colour channels: **0.995x-1.012x across four runs — a wash**,
  because two `pow` calls dominate everything around them. The hoist stays for clarity, not speed;
  the overclaiming comments were corrected to say so, with the number.
- **Six sabotages, six DIFFERENT failures, and the differences are the finding.** A **reversed**
  pair fails 4 cases but **not** the round-trip one — correctly, since a reversed pair is still a
  mutual inverse; the mid-grey case catches it. **One direction twice** fails 5, including the round
  trip, because a double-decode is not an inverse. A **cross-standard** pair fails 3. So all three
  miswirings a paired type still permits are caught, by different cases — the round-trip test alone
  would have looked sufficient. A **null `kIdentity`** segfaults 18 tests while the census case fails
  cleanly by name, which is exactly why `transferCurve` is total. And giving `TransferCurve` a
  data member fails the new aggregate-init `static_assert`, which the brace-built pairs rely on.
- **The `from == to` exactness moved rather than vanished** — to `convert(img, ColorSpace)`'s early
  return, now its only user, carrying the measurement that makes it load-bearing: **314 sRGB and 412
  BT709 values of 1001** do not survive a decode/encode round trip.
- **A wrong control, caught by running it:** the first cut asserted the round trip at 0.77 differs.
  It does not — 0.77 survives exactly. Which values survive is a property of the platform's `pow`,
  so the claim became *"inexact somewhere in a 1001-point sweep"* rather than *"inexact at this
  value"*, which was a guess dressed as a test.
- **Three triggers recorded in WORK.md, pointed at from `transfercurve.h`** (scattered notes are how a queued
  decision becomes a rediscovery): an X-macro table — trigger a fourth space; a `ColorSpaceDescriptor`
  wrapping the curve — trigger a second per-space fact; qualifying `transferCurve` — trigger a second
  module exposing transfer curves. The first two fire on the same event and should land together.
- **`convert<Dst>` was examined and deliberately NOT renamed to `cast<Dst>`** — only a name change,
  and CONTEXT.md already named `image::convert` as its example of Convert, so the vocabulary's owner
  was on the keep side. CONTEXT.md now owns the answer and WORK.md points at it.
- Found in passing: `color.h` pointed at `image::toRGB`, a function that does not exist.

### Update 2026-09-23 — Windows builds again, and the Test step finds a real bug

Both Run 10 fixes worked. **Windows Release BUILDS for the first time since 2026-09-10** and its
Test step ran for the first time in thirteen days: **797/798**. multi's own CI went fully green in
parallel — all four Windows jobs, with `/wd4324` deleted — which **retires the instantiation-scope
risk** Run 10 flagged: a pragma around a class does cover a warning raised when its templates are
instantiated, and lain's leg proved it crosses the header boundary into another TU. Full notes in
WORK.md's *Run 11*.

- **The one failure is a REAL PRODUCTION BUG**, in the test whose own name is about Windows:
  `pattern.extension()` expanded to `"<frame" == "png"`. **`Uri::extension()` delegated to
  `std::filesystem::path`, and on Windows that splits a component on `:`** (`C:foo` is a real
  drive-relative path), so `shot.<frame:04>.png` reported `<frame` there and `png` everywhere else.
  `io::sequence` dispatches a uri to a medium BY EXTENSION — so on Windows a render pattern reached
  no medium at all, silently. Latent since M13 slice 1 (2026-09-11).
- **The fault is the delegation, not the platform.** A `Uri` deliberately carries no platform's
  path semantics (ADR-0023) — borrowing `std::filesystem`'s for one accessor let them in through
  the back door. The last component is now scanned here, on both `/` and `\`, so the answer is
  identical everywhere; a format-keyed registry cannot be built on anything else.
- **A test that fails on every platform now exists**, where the old one could only fail on Windows:
  `Uri{"C:\\footage.old\\clip"}.extension()` must be empty, the dot in the DIRECTORY being what
  discriminates. Sabotage-verified on macOS, where the original bug was invisible.
- **The second half of Run 10's lesson:** a red build had been hiding a red test for eight days,
  and no amount of reading would have found this one — the code is correct on two platforms of
  three.

### Update 2026-09-22 — Windows CI: red since 2026-09-14, and neither cause was recent

The push of the two body-placement commits came back red on **Windows Release alone**, at Build.
**Neither failure came from those commits** — the run of 2026-09-14 carries the same two errors, so
the leg had been broken since the 2026-09-11 work shipped and stayed broken for eight days across
two pushes. Last green: 2026-09-10. That is Run 9's *"only a push runs CI"* lesson getting worse:
the check existed, ran, and reported correctly; nobody opened it. Full notes in WORK.md's *Run 10*.

- **C4324 from `multi`, and multi ALREADY SUPPRESSED IT — wrongly scoped.** MSVC reports that the
  `alignas(CACHE_LINE_SIZE)` on `ChaseLevDeque` / `MpmcQueue` / `WorkerPool` padded the struct,
  which is the entire point of a deque that must not false-share, and `/W4 /WX` makes it fatal.
  multi's CMakeLists already carried `/wd4324` with a correct comment — inside
  `target_compile_options(... PRIVATE ...)`. **PRIVATE covers multi's own sources and not the
  headers a consumer includes**, which is exactly why multi's own Windows CI (two images, Debug and
  Release) was green while every consumer tripped over it: the library that owned the problem was
  the one configuration that could not see it. The general shape — **a suppression for something
  declared in a public header belongs at the declaration**, because a build flag stops at the
  target boundary and a header does not.
- **Fixed in multi:** `MULTI_BEGIN_CACHE_ALIGNED` / `MULTI_END_CACHE_ALIGNED` in
  `details/platform.h`, beside the `CACHE_LINE_SIZE` it is about and already included by all three
  headers, wrapping each class. **The `/wd4324` flag is deleted rather than kept alongside**, which
  is what turns multi's own Windows leg into a test of the pragma instead of a test of the flag.
  Two CIs now each prove one half. `task.h`'s `alignas(SBO_ALIGN)` is deliberately not covered — it
  is `alignof(void*)` on a char buffer, so it cannot pad and does not warn.
- **A first fix was built in lain and BACKED OUT:** re-exposing multi's headers as SYSTEM from
  lain's root CMakeLists. It worked and was wrong twice — it silences every warning from an owned
  library rather than the one that is noise, and it fixes one consumer while leaving the next, and
  every non-CMake consumer, to hit it again. One finding survives it and is worth keeping: MSVC's
  `/external:I` does nothing to warnings without `/external:W<n>`, so a SYSTEM re-expose can be a
  silent no-op there; CMake emits both for MSVC >= 19.29.30036.3, which CI's VS 2022 satisfies.
- **`test_rect.cpp(44) C2131`, and only line 44** — `STATIC_REQUIRE(lm::Rect2i{}.empty())`, the
  DEFAULT-constructed case, while every `Rect2i{x, y, w, h}` beside it passed. GLM stores a vec's
  components in **anonymous unions** behind a defaulted default constructor, and MSVC's constexpr
  evaluator does not treat a value-initialised union member as initialised; the four-argument path
  reaches GLM's two-argument constructor, which names the members. `Rect2`'s NSDMIs now say
  `{T{0}}`. **`GLM_FORCE_XYZW_ONLY` also fixes it and is refused** on a checked ground: it removes
  the `.r`/`.g`/`.b` spellings that `image::ColorRGBf` is read through in `graphio.cpp` and
  `parameditors.cpp`.
- **Neither fix is verifiable on this machine.** CI is still the only Windows compiler this project
  has, so the next push is the test; multi passing standalone (103/103) and lain's three
  configurations staying green (792 / 765 / 798) says the fixes break nothing, not that they work.
  The residual risk is real and stated: C4324 is raised at template INSTANTIATION while the pragma
  sits at the definition — MSVC attributes it to the member's own line, which the wrap covers, and
  this is `concurrentqueue`'s and `folly`'s idiom for this exact warning, but it is unproven here.
  Fallback: multi's `/wd4324` moved from PRIVATE to INTERFACE. And the Test step has not run on
  Windows since 2026-09-10, so everything from the `multi` swap onward is unproven there rather
  than merely unreported.

### Update 2026-09-21 — one rule for where a body lives (and the hidden-friend spelling refused)

The repo owner inlined `Time::from` / `Time::as`, and the reason generalises: those bodies were out
of line **in the same header**, which buys nothing — a reader scrolls past them either way, and the
declaration and definition become two places. Surveying for that shape found three things and **no
rule written down anywhere** (not AGENTS.md, not CONTEXT.md, not CLAUDE.md). The rule is now in
*Conventions to inherit* below. `ctest -j8` **791/791** Debug with video on (+2), **764/764**
Release in the default video-off configuration (+2), **797/797** Release with video on (+2);
warning-clean, format-check clean, and `flowview run --example` **identical** to the pre-change
binary once the timestamp and the minted uuids are normalised. Full landing notes in WORK.md's
*Where a body lives*.

- **The first cut made every comparison a hidden friend, and that was REFUSED on review — the
  refusal is the more useful half of this entry.** Three arguments were offered and none survived.
  (1) *Conversion symmetry*: a member cannot convert its LEFT operand, so `data::Value` — with six
  implicit constructors — accepted `value == 42` and rejected `42 == value`. True, measured, and
  worthless: the only thing it enables is a Yoda comparison this codebase does not want written,
  nothing here does a heterogeneous `upper_bound`, and it is **C++17-only** besides, since C++20
  synthesises the reversed candidate for `==`. (2) *ADL scoping*: measured and came to nothing —
  GLM's operators live in `glm`, so ADL never associates `lain::math` for a `Vec2i`, and `media`'s
  four overloads are non-viable on sight. The argument only bites for a GREEDY free template, and
  none of these is one. (3) *The body sits with the data*: **a member does that too**, which is
  what actually killed it. What remained was uniformity for its own sake, which does not pay for
  `friend` in eleven public structs.
- **So the rule covers a body's HOME and says nothing about its spelling.** One-line bodies move
  INTO their class — as members, where they were already members — and the six types that were
  hidden friends before this work (`Uri`, `Uuid`, `NodeId`, `PortId`, `PortAddress`, `Frontier`)
  are left exactly as they were. Two spellings remain and that is deliberate: a rule needs a
  benefit to justify the churn, and this one had none.
- **What the survey found and the change kept:** `core::Version`'s and the four `media` types'
  comparison bodies move out of their `.cpp` files into their classes (a one-line `operator!=`
  defined a translation unit away is the `time.h` complaint across a TU); `core::Range`'s and
  `math::Rect2`'s move in from namespace scope.
- **Two byte-identical `lexical_cast` adapters became `core::parseInto` + `LAIN_CLI_OPTION`.**
  `Range` and `FrameRate` each carried the same six-line `T::parse` → bool hook, and `framespec.h`
  already *said* it was the same hook — the `core::hex` trigger (*a second consumer*) fired
  unnoticed again, one commit after `core::parse` recorded that exact lesson. Each is now **one
  line beside its type**, `LAIN_CLI_OPTION(Range)`, from the new **`core/clioption.h`**.
- **The macro is the whole hook, not a declaration needing a `.cpp` half.** Spelled out, `bool
  lexical_cast(const std::string&, FrameRate&);` is an anonymous-looking free function with nothing
  connecting it to a command line — the macro is what makes the line say *this type is a
  command-line option*. It cannot be a member (CLI11 finds it by ADL) and needs no `friend`
  (nothing private). **One macro serves both jobs** because it expands to the whole inline
  definition, so there is no signature written twice and nothing to keep in step.
- **A single generic hook is impossible, and that was measured rather than assumed.** ADL requires
  the hook in the TYPE'S namespace, so it cannot be declared once like `lain::string`'s fmt
  formatter. One constrained template plus a per-namespace `using` DOES work — ADL reaches a
  function introduced by a using-declaration, verified through real CLI11 on three types across two
  namespaces — but at two types it saves nothing, trades a greppable per-type line for an implicit
  rule, and leaves a `using` that looks deletable and silently breaks conversion. Refused with a
  trigger, recorded in `clioption.h`: a third bindable type, or a third namespace.
- **The shared body guarantees what the two copies happened to honour: on a refusal the output is
  untouched.** Sabotage it and **three** cases fail, including the *pre-existing* `lexical_cast`
  case — which is what proves the shipped path runs through it. **`FrameRate`'s hook had never
  been tested at all.**
- **The adjacent finding, in its own commit: an EIGHTH spelling of the number parser.**
  `FrameRate::parse` hand-rolled a digit loop with its own overflow check, twenty lines from the
  file that now calls into `core::parseInto`. The `core::parse` commit claimed *seven
  implementations in four spellings*, and this one was missed because it is a `for` loop over
  chars — none of the greps that found a `from_chars` or a `stoi` would see it. Now
  `core::parse<std::uint32_t>`. It had **no direct test at all** (it is reached only through
  `lexical_cast`), so the refusal list was characterised against the OLD loop first — 36
  assertions green — and the swap left all 36 green, which is what makes the equivalence measured
  rather than argued. Sabotage: prefix-only `parseAt` instead of whole-field `parse` fails exactly
  4 of 36, both decimals plus `"24fps"` and `"24 "` — the full-consumption test, which is what
  keeps `"29.97"` refused across a change of mechanism.
- **Every `.inl` is included by its own header, so a split buys no include and no rebuild** —
  readability only. `valuecodecs.inl` claimed otherwise and was wrong; its comment is corrected.
  The three holding one-to-four-line forwarders (`graph.inl`, `param.inl`, `data.inl`) are
  **deleted** and folded into their classes; the other twelve stay.
- **`task::Task::precede` is the one sanctioned same-file out-of-line body and now says so** — it
  reaches through `m_flow` into a `Flow` that is only forward-declared where `Task` is defined, so
  moving it up is a build break rather than a tidy.

### Update 2026-09-21 — `core::parse`: the number parser stops hiding

`version.cpp`'s `parseNumber` was a file-local `static`, and the hiding was the symptom rather than
the problem. A survey found **seven implementations of one job** — text to a number, whole field,
refuse trailing junk — in **four spellings**: `version.cpp`'s `parseNumber`, `range.cpp`'s
incremental `readNumber`, inline `from_chars` in `io/image/open.cpp` and `flow/serialize/version1.cpp`,
and `stoi` / `stof` / `stoull` inside a try/catch at four flowview sites. **`lain::core` now declares
it** (`core/parse.h` + `details/parse.inl`, header-only), and **every one of them is a caller**.
`ctest -j8` **789/789** Debug with video on (+14, was 775) and **762/762** Release in the default
video-off configuration (+14, was 748); warning-clean, format-check clean, and
`flowview run --example` identical to the pre-change binary once the timestamp and the minted uuids
are normalised. Full landing notes in WORK.md's *`core::parse`*.

- **This is the `core::hex` deferral's own criterion — *trigger: a second consumer* — fired, and
  nobody noticed it had.** `hexValue` genuinely has one consumer, so it stands and **`uuid.cpp` is
  untouched**; `parseNumber`'s second consumer was **20 lines away in the same library**. The general
  shape: a rule written down for one case is not self-applying, and the case it fires on next is the
  one nobody re-reads it for — the `core::Uri` rediscovery from the other end.
- **`graphio.cpp`'s comment reads *"One policy, two callers"* while being the third spelling of that
  policy**, in an app, above a core library hiding two of its own. That is what settled it: the
  policy was already understood to be worth stating once, and had been stated once three times.
- **`lain::string` is the obvious home and is REFUSED on a dependency direction.** It PUBLIC-links
  `fmt::fmt` + `lain::meta`, and `libs/core/CMakeLists.txt` says *"No lain or third-party deps, so
  any library above may build on it"* — so putting it there buys `lain::core` a dependency on fmt to
  parse `"1.2.3"`. Core is the only home serving every caller, and is where every existing `parse`
  already lives.
- **No new verb, and deliberately not a fifth io verb.** `Version` / `Range` / `Uuid` / `Uri` already
  answer text with `parse`; this is that verb one level down. CONTEXT.md's *The four verbs* warns
  against *"a fifth verb for a fourth thing"* — those name what a **loading** function hands back, and
  this sits below all of them. Recorded beside that entry, not in a section of its own.
- **Not ADL-reachable, checked rather than assumed** — the `image::descriptor` lesson. `parse` takes
  a `std::string_view`, which associates namespace `std`, so a caller must qualify.
- **`parse` is implemented OVER `parseAt`**, so the strict reading is stated once and the two cannot
  drift about what `"1x"` means. `version1.cpp`'s **prefix-only** behaviour was silent and is now a
  stated decision (`parseAt` with the position ignored), **kept rather than tightened**: changing what
  an existing v1 document comes back as is its own decision, not a side effect of naming a parse.
- **A REAL BUG in the flowview binders, found by measuring rather than by reading.** `std::stoull`
  does not refuse a negative — **it negates**: `stoull("-12")` is `18446744073709551604`, consuming
  the whole string, so `bindFramePosition`'s trailing-junk check passed it through and
  `--position -12` bound a frame no footage has. The exact failure `core::parse` exists to prevent,
  sitting in the code the parser was being extracted for. **Nothing had ever asked** — all 781 tests
  passed with it, and `clibinders.cpp` was already compiled into `test-flowview`, so it was reachable
  and simply untested. Plumbing proven on the real binary (`flowview run --example -12` reports
  `value '-12'`, so a negative token survives CLI11 and reaches the binder).
- **Deliberate tightening, and the first pass's "the integral ones convert verbatim" was wrong**:
  `"+12"` and `" 12"` were accepted by `stoi` / `stoull`, which skip whitespace and take a leading
  sign, and are now refused. Shell oddities rather than values anyone means.
- **Floating point is IN, and it took two wrong answers to get there — both worth keeping.** The
  first pass refused it on SEMANTICS (*"`stof` accepts hex floats and leading whitespace"*), and
  measurement shrank that to nearly nothing: `1e5`, `inf`, `nan`, `.5` and `5.` agree exactly. The
  second refused it on PORTABILITY and wrote a `parseFloat` in flowview — which the repo owner
  rejected on the sharper ground: **the `static_assert` was ours, not the API's**, so the answer was
  a second name in an app dodging a restriction core had imposed on itself. The argument for putting
  `parse` in core at all is that a caller should not carry the mechanism, and the workaround handed
  one back to two callers.
- **The portability fact is real, worse than first recorded, and constrains the MECHANISM rather
  than the interface.** libc++'s floating-point `from_chars` is not "wait for the CI runner" — it is
  marked **introduced in macOS 26**, so no consumer with an ordinary deployment target can reach it.
  Measured, not assumed: `-mmacosx-version-min=15.0` fails with *"'from_chars' is unavailable:
  introduced in macOS 26.0"* on the same toolchain where the default target compiles. So
  **`core::parse<T>` is one policy over two mechanisms** — `from_chars` for integral, `strtof` /
  `strtod` / `strtold` for floating, chosen by an `if constexpr` inside the one function — and a
  caller writes `core::parse<float>` and is told none of it. When from_chars becomes reachable that
  arm is deleted and **no caller changes**, which is the whole reason the seam is in core.
- **The first cut of that arm was rejected for CONVOLUTION, and the cause generalises.** It read
  `parse<T>` → `parseAt<T>` → three `parseFloating` overloads → `parseFloatingImpl<T>` → three
  `callStrto` overloads: two overload sets doing the same dispatch plus a forwarding layer, to reach
  one call. Every bit of it existed to keep the arm **out of line** in a `src/parse.cpp`, which was
  buying almost nothing — `<cstdlib>` and `<cerrno>` are two of the lightest headers there are.
  Dropping that requirement collapsed seven functions into one `if constexpr` and deleted the .cpp.
  **An out-of-line split is a cost, and a template forced out of line pays it in dispatch
  machinery**: worth it for a heavy dependency, not for two C headers.
- **Nothing makes the two arms agree except a test, so there is one.** `strtof` takes three things
  `from_chars` refuses (a leading `+`, leading whitespace, a hex float), and those are refused
  explicitly; *"the integral and floating arms refuse the same forms"* runs ONE list through `int`,
  `float` and `double` rather than a copy per type. Sabotage: drop that check and three cases fail,
  in core and in flowview. `ERANGE` is the float twin of the integral wrap — without it `strtof`
  hands back an infinity for a finite number someone wrote — and is sabotage-verified too.
- **Two costs stated in the header rather than found later:** the floating arm is locale-dependent
  through `LC_NUMERIC` (the same exposure `std::stof` always had; lain never calls `setlocale`) and
  copies its field to null-terminate it. Both end with the mechanism. **`bool` stays refused**, and
  not for portability: it is not a numeral, and whoever reads `"true"` is deciding a spelling.
- **Two sabotages, both caught, and the second corrected a test.** Dropping the full-consumption test
  fails the new strict case, the coupling case **and Version's own existing
  `parse rejects malformed input`** — which proves the production path still runs through it. Writing
  `pos` before the error check fails *"parseAt does not move pos on a refusal"* only because that case
  was strengthened first: the original refused with `"x"` and at end-of-text, and `from_chars`
  advances its `ptr` on **neither**, so it passed with the guard deleted. The arm that buys it is
  `result_out_of_range`, where `from_chars` *does* advance past the digits it refused.
- **Two more on the binder half.** Putting `std::stoull` back fails *"a negative frame position is
  refused, not wrapped"* alone; giving the Cast its own `stoi` fails *"a bound value and a Cast read
  text the same way"* **together with** the pre-existing *"text converts only when the whole string is
  a number"*. That agreement case compares the two doors' verdicts over one string list rather than
  keeping a copy of the list, since a duplicated list is the drift it exists to catch.

### Update 2026-09-13 — M13 slice 5 built: encoder options say the job (**M13 COMPLETE**)

notes.txt note 29 — *"libs/io/image/include/lain/io/image/writer.h — Feels like this needs some kind
of 'Quality' setting."* The last slice of Milestone 13. `lain::io::image::Compression` +
**`ImageWriterOptions`**, `ImageWriter::isLossy()`, and the `encode`/`save` that carry them — with a
cli spelling (`--compression` / `--quality` on `run`) and the Save... panel's two controls, so the
knob is reachable rather than merely present. `ctest -j8` **774/774** Debug with video on (+14),
**780/780** Release with video on (+14), **748/748** Release in the default video-off configuration
(+14); warning-clean, format-check clean. **gui-mode NOT eyeballed** — the two controls are new UI
and need a Metal session. Full landing notes in WORK.md M13.

- **The knob was deferred since M3 WITH A CONDITION, and both halves of it had been met for a
  while.** *"The three surfaces are heterogeneous — decide with a real caller."* The part that made
  it overdue is that **`VideoWriterOptions` cited that deferral as its own precedent**, so one
  decision was load-bearing in two seams while existing in neither. That citation is amended in
  place: the judgement it pointed at now has an answer, and the answer is what keeps GOP length,
  ProRes profile and FFV1 level out of the video options too.
- **The shape is `VideoCodec`'s rule applied to stills: name THE JOB, never an encoder's knob.** A
  zlib level 0-9, a tiff *algorithm* and a jpeg quality are one question in three incompatible
  spellings, so a caller says `Compression` (`Default` / `None` / `Fast` / `Small`) or `quality`
  (1-100, lossy only) and each codec maps it. No per-format options struct, no string bag.
- **THE DEFAULTS CHANGE NOTHING, CHECKED BY BYTES.** A png and a tiff written by the pre-change
  binary and by this one are byte-identical through the real binary. That is what
  `std::optional<std::uint8_t> quality` buys over a `0` sentinel: the seam never repeats the codec's
  number back at it, so "a caller who says nothing gets what it always got" is structural rather
  than a coincidence of two 90s agreeing.
- **`Default` means what this codec already wrote, not what the library defaults to** — read the
  second way, a TIFF falls back to uncompressed and every file lain writes silently grows. Both
  lossless cases now assert `Default` is distinct from `None` and `Small`, because folding it into
  one of them is the change no round-trip assertion would notice.
- **PackBits was measured and rejected, which is why `Fast` is deflate.** The obvious cheap tiff arm
  is RLE, and on a jittered gradient it produced a file **1% LARGER than storing the pixels
  uncompressed** — RLE working correctly on data with no runs. An arm named `Fast` may cost size; it
  may not cost more size than `None`, or the enum's own ordering is a lie. Found by the test failing.
- **libtiff spells its deflate LEVEL "ZIPQUALITY", and that is exactly where the two fields would
  have blurred.** It is an effort knob, so it belongs to `Compression`; `quality` stays fidelity and
  every lossless codec ignores it. One field meaning fidelity in one codec and speed in another is a
  value that disagrees with itself — the shape `editableAt` and ADR-0014's *"no flag, nothing
  stored"* each deleted.
- **`isLossy()` is a SECOND question, not a synonym for `canEncode`.** `canEncode` asks whether the
  format can HOLD this image (no knob moves it, so it takes no options); `isLossy` asks whether
  encoding costs fidelity — which is what the gui gates its slider on and what the cli warns from. A
  census case asserts exactly one still format answers true, because a wrong answer there is silent
  in both hosts. It also retired the panel's *"no lossless format"* string, which with a quality
  slider a line below was two meanings of one word.
- **Found by driving the binary: the quality warn only fired on the SWEEP path**, because it had been
  folded into `encodableHere`, which a single run never calls. Extracted as `noteIgnoredQuality` and
  called from both — and the split is the better shape anyway, since a predicate that also warns
  about an unrelated thing is two jobs.
- **`lain::gui::enumCombo` has a live production caller again**, three months after its only one was
  retired with the preview-size dropdown (2026-07-31). An untouched quality slider reads **"codec
  default"** rather than a number, because the handle must sit somewhere and a number there would be
  the pane claiming to know the codec's default — the duplication the optional exists to prevent.
- **The adjacent finding, in its own commit: `VideoWriterOptions::bitsPerSecond` was read by the
  FFmpeg writer and set by NOTHING in the tree** — no caller, no test. The eighth instance of the
  compiled-linked-unreachable shape, and it was sitting inside the precedent this slice mirrors. It
  now has a test (24 frames of noise at 50 kbps against 5 Mbps: 8648 bytes against 169384); the
  missing `--bitrate` spelling is recorded in the Outstanding index rather than guessed at inside a
  still-image slice.
- **M13 is COMPLETE**, and **M9 is now the only milestone from 5 onward that is not built**.

### Update 2026-09-13 — M13 slice 4 built: the verb + layering pass

notes.txt notes 27 and 28 — *"load/save vs read/write vs encode/decode — This needs some
consistency"* and *"sequence.h — Not sure if this should be here? Sequence feels like a 'type' of
Video rather than something Image based."* The tree already ran a coherent **four-verb rule** and had
never written it down, which is why the functions disagreeing with it read as normal.
**`io::image::openSequence` → `io::image::open`** (files and test renamed with it) and
**`VideoWriter::write(const Image&)` → `encode`**; the rule and the three-layer sequence picture go
into CONTEXT.md. Nothing behaves differently and nothing on disk changes, so every baseline is
unmoved: `ctest -j8` **760/760** Debug with video on, **766/766** Release with video on, **734/734**
Release in the default video-off configuration. Warning-clean, format-check clean. Full landing notes
in WORK.md's *Milestone 13*.

- **The triage's "one genuine outlier" claim did not survive.** It surveyed the free-function facades
  and not the interface classes. `VideoReader::decode(ordinal)` already followed the rule and says so
  in its own comment (*"exactly as ImageReader::decode does"*), while its write-side peer did not —
  and the sharp form is the COLLISION rather than the asymmetry: **a VideoWriter owns an
  `io::WriteStream` whose `write()` takes bytes**, so inside `ffmpegwriter.cpp` one word meant two
  different things one layer apart. ADR-0018's writer-handle triple is amended in place.
- **The file rename is what makes note 28's answer structural.** `io::image` now reads `load.h` /
  `save.h` / `open.h` / `reader.h` / `writer.h` — the rule visible in a directory listing — and there
  are three media-level `open.h` files meaning one verb. A frame sequence was never video's *or*
  image's: **`lain::media` owns the model and depends on no io**, which is precisely why it cannot
  also own opening; each medium's io seam opens its own, and `io::sequence` dispatches. After the
  rename the file no longer claims the noun at all.
- **The node factory key `"openSequence"` did NOT change, and the code now says why.** It is a
  serialization key — an unknown `kind` drops the node **and its edges**, so every saved document
  holding footage would come back structurally damaged — and it is the name ADR-0019's amendment
  settled on. The reason sits on `kOpenSequenceKey` itself, because the next tidy pass would
  otherwise "fix" it, and that fix is data loss.
- **The renamed test's calls are QUALIFIED deliberately**: `using lain::io::image::open;` at
  namespace scope collides with POSIX `::open` from `<fcntl.h>` as soon as any header in the TU pulls
  it in — a Linux-leg break this machine would never see. Said in the head comment so it is not
  tidied back.
- **CONTEXT.md is now the OWNER of the rule**, and WORK.md's triage drops its table for a pointer:
  the audit owns the finding, the glossary owns the rule. The entry **states its two deliberate
  exceptions** rather than hiding them — `createStream` is an open named for being destructive,
  `openWriter` opens a handle whose pushes are `encode` — because a rule that quietly excuses its
  own exceptions stops discriminating.
- **Surveyed and kept, so it is not re-derived:** `media::FrameSource::decodeFrame` is the same job
  as `VideoReader::decode` one layer up, but `lain::media` is not an io seam and `decodeFrame` is a
  *more specific* name rather than a conflicting one; `gui::openFile` / `saveFile` are file dialogs
  returning a path, in a library with its own vocabulary.
- **The `encode` rename only compiles in a video-ON build** — the override lives in the FFmpeg
  plugin, so the video-off leg builds the seam and never the implementation. Both video-ON
  configurations were run for that reason.

### Update 2026-09-12 — M13 slice 3 built: `transport.h`, and io's top level follows its own rule

notes.txt — *"libs/io/include/lain/io/read/write.h — Why split these? Also should probably have
openStream here. **So Stream is just a class.**"* Every seam beneath `lain::io` splits one way:
`reader.h` / `writer.h` declare the interface a backend implements, `load.h` / `save.h` the entry
points a caller calls. **The one layer that defined that pattern was the only layer not following
it.** Now **`transport.h`** holds `read` / `write` / `openStream` / `createStream` and **`stream.h`**
holds `Stream` / `ReadStream` / `WriteStream` and nothing else; `read.h`, `write.h`, `read.cpp` and
`write.cpp` are deleted and `src/transport.cpp` takes their bodies plus the scheme dispatch. Nothing
behaves differently and no function is renamed, so the existing suite is the regression test and
every baseline is unmoved: `ctest -j8` **760/760** Debug with video on, **766/766** Release with
video on, **734/734** Release in the default video-off configuration. Warning-clean, format-check
clean; `flowview --version` / `run --example` / `list` unchanged and save ⇒ load ⇒ save still
byte-idempotent through the real binary. Full landing notes in WORK.md's *Milestone 13*.

- **The cost was in the SOURCES, not only in the header names.** `src/stream.cpp` included
  `localstream.h` and `uri.h`, because the interface file's implementation also owned backend
  selection. It now includes neither — **the interface stops naming the backend**, which is the
  property `reader.h` has had all along.
- **The include runs ONE WAY, and that is the layering claim**: `transport.h` includes `stream.h`,
  never the reverse. Five files depend on `stream.h` alone — `io::video`'s `reader.h` / `writer.h`
  and the FFmpeg plugin's `aviobridge.h` / `ffmpegreader.h` / `ffmpegwriter.h` — and **none of them
  appears in the diff**, which is how the claim is checked rather than argued. A `stream.h` reaching
  back would hand every codec plugin a whole-asset API it must not have.
- **`ReadWriteStream` is refused with a trigger, and the record it was already pointing at now
  exists.** The Outstanding-work index said *"see M13's own record"* and there was **no record** —
  the dangling-pointer shape ADR-0023 opens with, three slices after that lesson was written down.
  The note asks something broader than a third class (*"a singular stream + access perms"*), and the
  answer is that the two differ in **contract, not rights**: a `WriteStream` is open-push-**finish**
  with nothing valid before `finish()`, a `ReadStream` has `atEnd()` and no finish, so one type with
  an access flag is a type whose members refuse for half its instances — the shape M7 slice 1's
  `editableAt` and ADR-0014's *"no flag, nothing stored"* each deleted. What the two genuinely share
  is already shared on the base (`seek`, `size`), which is why the FFmpeg bridge's ONE seek callback
  takes an `io::Stream*`. Nothing asks for the type today, because a `WriteStream` already seeks
  back and overwrites what it wrote. **The trigger is a muxer that reads back its own output**
  (`-movflags +faststart`) — stated as the class of need, since the prebuilt FFmpeg ships headers
  only and the requirement inside the mov muxer is not checkable from the tree; what is checkable is
  that `makeWriteContext` hands `avio_alloc_context` a null `read_packet`. And the cheaper answer
  may serve it anyway: a post-pass through `openStream` + `createStream` is what `qt-faststart` is.
  Recorded in `stream.h`'s own refusals list, where the question gets asked.
- **The no-scheme-registry decision gets a pointer from the code that provokes it.** notes.txt also
  asked why the local backend is not a registry *"like other IO paths"* — already answered in M10
  slice 3 (*"one member is not a registry"*) and recorded nowhere the asker would look.
  `transport.cpp`'s dispatch block now says it, and says where a second scheme would attach.
- **Tests follow the sources exactly**, the mapping `io::image` already uses: `test_read.cpp` +
  `test_write.cpp` become `test_transport.cpp` (one duplicated `bytesEqual` deleted on the way), and
  the two `openStream` / `createStream` refusal cases move across with them because scheme dispatch
  is `transport.cpp`'s. `test_stream.cpp` keeps the thirteen Stream-contract cases and still reaches
  them through the production door.
- **No new test case, and nothing to sabotage** — the claim is that nothing moved, so the count had
  to move by zero and did. Three configurations at their existing baselines is the verification;
  inventing a case to have one would have been the dishonest version.

### Update 2026-09-12 — M13 slice 2 built: `####` retires for a `<key>` pattern system

notes.txt note 21 — *"The entire NumberField approach sucks. I want some type of replacement
dictionary"*. `io::NumberField` / `numberField` / `substituteNumber` are **deleted**;
**`lain::string::Pattern` + `Dictionary`** replace them, formatting and matching literal text with
named holes (`shot.<frame:04>.png`). `ctest -j8` **760/760** Debug with video on (+12), **734/734**
Release in the default video-off configuration (+12), **766/766** Release with video on (+12);
warning-clean, format-check clean, and the render sweep re-driven end to end through the real binary
— the same four output names as the `####` run, read back as a sequence by the opener, both
`--on-missing-frame` policies unchanged. Full landing notes in WORK.md's *Milestone 13*.

- **The mechanism is a rewrite, so fmt's spec language comes free.** A key the dictionary holds
  becomes `{key:spec}`, literals are brace-escaped, and one `fmt::vformat` runs over a
  `dynamic_format_arg_store`. A key it does **not** hold survives as its own token — which is what
  makes partial resolution work, and why an unknown key is not an error. Sabotage-verified.
- **An agreement that was STRUCTURAL became a property, and now something asserts it.**
  `io::numberField` lived in `io` rather than in either caller for a stated reason: *"two
  implementations of 'find the # run' means what a sweep writes cannot be read back, and the
  disagreement is silent."* One function, both directions. Now the grammar is `lain::string`'s and
  the key is **`io::image::frameKey`**, declared once — so a new `[sweep]` case sweeps four frames
  to `out.<frame:04>.png` and then opens **that same pattern string** through
  `io::image::openSequence`, finding the four frames in order. Nothing tested that before, in either
  spelling.
- **The shared-key sabotage is worth knowing for what it does NOT catch.** Giving flowview its own
  `"frame"` literal passes everything: the constant buys a **compile error on a rename**, not a test
  failure. A *divergent* spelling fails **8 of 17** cases — which is what drift actually looks like.
- **ADR-0023 predicted its own amendment wrongly, and is corrected in place.** It said slice 2
  *"removes the first collision"* with RFC 3986. `<` and `>` appear in no production of that grammar,
  so a conforming parser rejects `shot.<frame:04>.png` as surely as it misreads `shot.####.png`
  (where `#` is the fragment delimiter). The collision **changed shape rather than disappearing** —
  and is now deliberate: neither character is legal in a Windows filename, so a real file can never
  be mistaken for a pattern.
- **`format` returns `std::optional`**, because `lain::string` cannot log (the dependency to
  `lain::log` runs one way) and a pattern is text a user typed. Exactly one caller pays. The
  alternatives were worse in the same direction: an exception escaping a string helper, or a token
  rendered literally — which would write every frame of a render to one filename, the precise
  failure the frame-field rule exists to prevent.
- **It is NOT a regex, and match() is where that shows.** A key captures the shortest non-empty run
  its neighbouring literals allow, the spec is not consulted, and what a capture *means* belongs to
  the caller — `io::image` is what rejects `shot.x.png`, since it parses the number to order the
  sequence anyway.
- **The delimiters are two of fmt's three alignment characters, and the first cut lost them.**
  Ending a token at the first `>` left `{:>8}` and `{:*<8}` unspellable; that shipped as a stated
  limitation and review rejected it. `findSpecEnd` now applies fmt's own rule — an alignment sits at
  spec index 0 or 1 and nowhere else — but only **assumes** one when that reading still leaves a
  closing `>` behind, so `<name:8>` remains a width and no pattern changes meaning. The fallback is
  the half that matters: without it the fix failed *"width alone"* on the first run. Both halves
  sabotage-verified. `[]` was reconsidered and refused again — a bracketed pattern is a shell
  **glob** that can silently expand to an existing wrong filename, where `<>` fails loudly.
- **`filesMatchingPattern` came out net-negative and lost a second defect**: the hand-rolled
  prefix/suffix compare became `Pattern::match`, and `std::all_of(std::isdigit)` + throwing
  `std::stoull` became **`std::from_chars`** — the tidy pass's own idiom, removing a locale-dependent
  digit test and an overflow throw in one move. The Pattern is built once, outside the scan.
- **The break is compiler- and test-caught** (no stored `.json` in the tree holds a `####`; seven
  test spellings failed on the first run), and the one place it cannot be gets a directed message:
  handed a `####`, `openSequence` now says *"'####' was retired for the named form, so write it as
  'shot.<frame:04>.png'"*.
- **`lain::string` is STATIC now** where it was INTERFACE, and `pattern.h` is deliberately separate
  from `format.h`: `log.h` includes that one at every log site in the tree and must not start
  carrying a parser.

### Update 2026-09-12 — M13 slice 1b built: the signatures take a `core::Uri`

The half [ADR-0023](docs/adr/0023-uri-as-an-identity-not-a-path-algebra.md) called *the more valuable
one*. Slice 1 built the type and typed nothing with it, so the tax stayed visible in the code it left
behind: `io/image/src/sequence.cpp` built a `core::Uri canonical` and then called `.toString()` on it
**five times in one function**, and `io::video::open` was
`const std::string canonical = canonicalise(Uri{uri}).toString();` — a `Uri` that lived for the
length of one expression. Now every `lain::io` entry point, `Stream`, `media::FrameSource`,
`media::FrameRef::source` and flowview's `graphio` take or answer one. `ctest -j8` **748/748** Debug
with video on, **754/754** Release with video on, **722/722** Release in the default video-off
configuration; warning-clean, format-check clean. Full landing notes in WORK.md's *Milestone 13*.

- **The log question was answered first and separately** (see the entry below), so the **29** log
  sites that print a uri — 13 of them in the FFmpeg plugin — changed by **zero lines**. Had it gone
  the other way, this commit would have added 29 `.toString()` calls to diagnostics.
- **The compiler found the bug ADR-0023 recorded as "not urgent".** `graphio::loadGraph` did
  `std::filesystem::path(uri).parent_path()` on its document argument; for `s3://bucket/scene.json`
  that builds a relative directory called `s3:` and resolves every linked template against it — the
  exact bug `io::localPath` was introduced to fix, one level up. Typed, the line stops compiling; it
  now asks `uri.path()` and falls back to no document directory. **That is the concrete value of the
  type**: not refusing bad input at runtime, but making a caller which cannot serve a remote resource
  say so at build time.
- **A half-done rename fails at LINK, not at compile**, because the implicit `Uri(std::string_view)`
  constructor lets an un-migrated definition compile as a separate overload. Worth carrying into
  slices 3 and 4: build to a link, not to a compile.
- **`.toString()` now marks exactly one seam, deliberately.** `flow::serialize`'s `TemplateCache` and
  `ResolvedTemplate::key` stay `std::string`: to flow a template key is an **opaque identity token**,
  and ADR-0013's amendment says flow must not interpret a `source` path — `scheme()` / `path()` /
  `extension()` are precisely the verbs it must not call. The four `.toString()` calls there are
  where a name stops being one. `media::serialize::ManifestFrame::source` stays a string for the
  matching reason on the wire side, which is what keeps ADR-0023's "no serialize arm, no wire change"
  true.
- **Twelve `std::string(uri)` casts are gone**; `io/image/sequence.cpp` went from six `.toString()`
  calls to one, and that one is `numberField(...)` — a **pattern**, not a uri, which slice 2 deletes.
- **`Uri::fromPath` is the named door at nine production sites** that used to flatten a
  `std::filesystem::path` with `.string()`. **Test sites were deliberately left alone**: their
  fixtures are `TempPath` / `TempFile` / `Fixture` objects whose own `.string()` is the accessor, not
  `fs::path`s, so `fromPath` does not apply without giving each a `path()` — found by the compiler
  after a first sweep tried it, and reverted rather than worked around.
- **A `static_assert` pins the property the type rests on**: a bare `std::filesystem::path` must NOT
  convert, while a `std::string` and a literal must. Asserted rather than reviewed, because the
  implicit constructors sit right beside `fromPath`. Sabotage-verified both ways — adding an implicit
  path constructor fails the assert, and handing a bare path to `io::image::load` fails to compile.
- **Found and fixed in passing:** `libs/io/src/localstream.h` still described itself as private
  *"exactly like scheme.h"*, a file slice 1 deleted.
- **gui-mode was not eyeballed, and does not need to be**: nothing here touches ImGui or a pane's
  drawing, and the `graphio` / `session` / `menubar` changes are covered by `apps/flowview/test`.

### Update 2026-09-12 — `lain::log` carries the toString formatter

The decision M13 slice 1b was blocked on, taken as its own commit ahead of the typing sweep that
needs it. `lain::log` now PUBLIC-links `lain::string` and `log.h` includes its generic
`fmt::formatter`, so a bare `{}` at any log site renders any type exposing `toString()`.
`ctest -j8` **748/748** (+1), warning-clean, format-check clean.

- **The measurement is what decided it: the formatter had ZERO production consumers.**
  `libs/string/test/test_string.cpp` was its only exercise. Eleven TUs included the header, and the
  production sites where it was genuinely in scope — `media/framespec.cpp`, `media/framesequence.cpp`,
  `app/application.cpp` — all wrote `.toString()` anyway. So the mechanism this file describes as
  *"a type is formattable just by having `toString()`, no per-type registration"* was reachable from
  **no log site in the tree**, which is the one place a lain type is most often rendered by name.
  That is the compiled-linked-unreachable shape, a seventh time — and the alternative on the table
  (slice 1b's ~39 uri log sites each spelling `.toString()`) would have entrenched it.
- **Two production sites converted in the same commit**, so the mechanism does not ship with only a
  test consumer: `lain::app`'s startup banner (`core::Version`) and `io::image::encode`'s refusal
  (`image::Image`). `flowview run --example` prints `flowview 0.1.0` through the formatter.
- **The dependency runs ONE WAY, and both CMakeLists say so:** `lain::string` must never link
  `lain::log`. A diagnostic wanted inside `string` goes to its caller instead. `lain::string` is
  INTERFACE over `fmt` (already log's PUBLIC dep) and `lain::meta`, so the added weight is a trait
  header.
- **A forward reference in CMake is fine, and this proves why it is worth knowing:** `libs/log` is
  added before `libs/string` in the root `CMakeLists.txt`, but a name containing `::` is resolved at
  **generate** time — `libs/string` already forward-references `lain::meta`, which is added after it.
  No reordering was needed.
- **The caveat becomes tree-wide, and was measured rather than assumed:** a type that is both a range
  and has `toString()` is ambiguous with fmt's range formatter. Of the ten headers declaring
  `std::string toString() const`, **none declares `begin()`**. `core::Range` is the one to watch —
  it is named for a range and is not one, and a future `begin()`/`end()` on it would fire this
  everywhere rather than in eleven TUs.
- **Sabotage-verified:** removing the include from `log.h` fails the new `[log]` case with fmt's
  *"Cannot format an argument"* static assertion — a hard compile error, not a silent fallback to an
  address or a type name. The case asserts through `fmt::format` rather than log output (the sink is
  not capturable there) precisely because `test-log` links `lain::log` and nothing else, so if the
  formatter renders there, `log.h` is what brought it.

### Update 2026-09-11 — M13 slice 1: `core::Uri`, an identity rather than a path algebra

The first of Milestone 13's five slices (io coherence). `lain::core::Uri` lands with
**[ADR-0023](docs/adr/0023-uri-as-an-identity-not-a-path-algebra.md)**, absorbing `io::localPath`
(as `Uri::path()`), `io::extensionKey` (as `Uri::extension()`) and the private
`libs/io/src/scheme.h` (as `scheme()` / `isLocal()`), which is **deleted**. `io::canonicalUri`
becomes **`io::canonicalise(Uri) -> Uri`** and stays in `io`, because it touches the filesystem and
`lain::core` does not. `ctest -j8` **747/747** (+5 net), warning-clean, format-check clean,
`flowview run --example` unchanged, linked-group and `templateKey` cases still green. Full landing
notes in WORK.md's *Milestone 13*.

- **The decision was written down on 2026-09-01 and rediscovered on 2026-09-11.** WORK.md's
  *Queued: `core::Uri`* carried the whole argument, and its "build after slice 5" condition was met
  on 2026-09-02 — then three separate notes in a reading pass re-derived it from scratch, because
  nothing in the code pointed at it. That is the ADR's opening paragraph and the general rule worth
  carrying: **a queued decision with nothing pointing at it from the code it governs stops being a
  decision and becomes a rediscovery.**
- **It is NOT an RFC 3986 parser, and a test now enforces that** rather than a review. Two of lain's
  own strings collide with the standard head-on: `shot.####.png` is not a valid URI reference (`#`
  is the fragment delimiter, so a conforming parser reads path `shot.` + fragment `###.png`), and
  `C:\footage\clip.mp4` parses as **scheme `C`** on a platform lain ships. The opaque `://` split
  is immune to both.
- **`media` needed no new dependency** — the ADR's "home is `core`" argument confirmed at the CMake
  level rather than argued: `lain::media` already links `lain::core` for `core::Time`, and it links
  no `io` by rule, which is exactly why the type could not live there.
- **`extension()` is the one piece of "path algebra" that survives**, because "which codec, or which
  medium" is a real question for every scheme — `parent_path` / `filename` mean nothing for one with
  no implementation. It reads the whole text rather than `rest()`, so a remote uri still has a
  format.
- **Found while writing the tests: Catch2 here cannot stringify a `std::string_view`** — its
  `StringMaker` is compiled out, so comparing a `scheme()` result fails to LINK rather than to
  compile. Wrapped in `std::string` at the assertion sites; worth knowing before the next test
  compares one.
- **Slice 1b is owed, not dropped.** The transport signatures and `media::FrameRef::source` still
  speak `std::string`. The implicit constructor makes the change compile every caller untouched, but
  `Stream::uri()` is printed at ~15 `lain::log` sites in the FFmpeg plugin and `lain::log` does not
  link `lain::string`, so the `toString()` formatter is not in scope there. That costs either
  `.toString()` at each site or a new plugin dependency — a decision worth making on its own rather
  than inside a slice about naming. ADR-0023 calls it the more valuable half.

### Update 2026-09-11 — `lain::task` moves from Taskflow to `multi`

The swap the triage decided, built. Locked decision #2 read *"Taskflow is the substrate (drops
`multi` for now)"* and its own prose invited this — *"`multi` … can return later behind the same
`lain::task` seam."* `ctest -j8` **742/742** (+3 `[task]` cases), warning-clean, format-check clean,
`[map],[loop],[group],[scheduler]` swept **100×** on the parallel path with no failures. **gui-mode
not eyeballed.** *(**Discharged 2026-09-28** — gui-mode was
driven on a tree where `lain::app` starts and stops the pool on every run, so the question this
raised (does the viewer work with multi underneath) is answered. The tidy pass's File ▸ Quit
gesture is a separate claim and is NOT covered by it.)* Full landing notes in WORK.md's *`lain::task` moves from Taskflow to
`multi`*.

- **`libs/flow` changed by ZERO lines of production code.** `src/`, `include/`, `serialize/` and
  `example/` are byte-identical across a change of concurrency substrate — which is the claim the
  seam was built to make, and the only real measure of whether it earned its place. It uses exactly
  three calls plus a constructor, and none of them moved. (Two stale *comments* in `libs/flow/test`
  naming the old backend were corrected in the same commit; that is drift, not adaptation, which is
  why the claim is scoped to production code.)
- **A DUPLICATE EDGE is success, and finding that out is what the swap cost.** The first run failed
  one case of 739 on an assert treating any refusal as an error. **Probed, not assumed:** the result
  was `DuplicateEdge`, not `WouldCycle`. flow emits one plan edge per GRAPH edge, so two inputs of
  one node fed by one producer ask for the same ordering twice — Taskflow absorbed that by counting
  dependents, multi refuses the second and keeps the single constraint. Identical order, which is
  the only thing an edge in a task graph means. A `[task]` case now pins it.
- **multi VALIDATES the graph**, so `WouldCycle` turns what was a Taskflow *hang* into a reported
  refusal. The remaining assert is about the lowering inventing a cycle, since `flow::Graph` already
  rejects one when the edge is made.
- **`Executor{0}` runs INLINE, in dependency order** — a parallel plan executed deterministically
  and single-threaded with no second scheduler, and a platform whose `hardware_concurrency()`
  answers 0 now degrades to correct-but-serial rather than to a made-up thread count. **The test
  asserts thread IDENTITY, not just order**, because the order is 1, 2, 3 whether or not a worker
  ran it — order alone would not have reached the claim.
- **`wait()` then `get()`, and the pair is load-bearing.** multi's `get()` is a NON-BLOCKING fetch
  that rethrows; `wait()` blocks and deliberately does not. Calling only `get()` returns while the
  work is still running and swallows the exception with it — sabotage-verified. flow relies on
  propagation: a `compute()` that throws must leave its node stale and reach the caller.
- **Costs, stated:** `lain::task` is still INTERFACE but `multi::multi` is a static library, so a
  consumer links one where header-only Taskflow linked nothing; and **Tier B #4 (streaming / live
  execution) loses its named mechanism** — `tf::Pipeline` was it, and multi has chunked parallel-for
  but no pipeline. Amended at both sites in WORK.md.
- **Buys beyond dogfooding:** multi's CI is broader than lain's (ASAN, TSAN, Android, iOS, Windows
  2022 + 2025, Linux across compilers) — TSAN on a work-stealing pool is real cover for
  `ParallelScheduler`. It also retires Taskflow's `v3.7.0` pin and the reason for it: 4.x needs
  C++20, which would have propagated `cxx_std_20` to every consumer in a C++17 set.

### Update 2026-09-11 — notes.txt triage: tidy pass built, M13 and the substrate swap decided

A reading pass had collected 25 tidy/adjustment observations in `notes.txt`. Grilled one at a time,
they split three ways: a **tidy pass built here**, a designed **M13 "io coherence"** (five slices,
nothing built), and one isolated **`lain::task` → `extern/multi`** swap (decided, not built). Six are
deferred with recorded triggers. `ctest` **739/739** (+4), warning-clean, format-check clean,
`flowview --version` / `run --example` / `--frame` unchanged through the real binary. **gui-mode not
eyeballed** — File ▸ Quit changed call, so it needs a Metal session. Full landing notes in WORK.md's
*notes.txt triage*.

- **`Application::exit(int)` had no callers — the compiled-linked-unreachable shape, a sixth time.**
  `quit()` had four; nothing reached `exit`. Merging them into `exit(int code = EXIT_SUCCESS)` is
  what gives the status path its first reachability, since every former `quit()` site can now pass
  one. `run()`'s bare `return 1` / `return 0` became `EXIT_FAILURE` / `EXIT_SUCCESS` — not an enum,
  because a process exit code IS an `int` and `run()` returns one.
- **`core::Range` becoming a class closed a latent infinite loop, which is not what the note asked
  for.** The complaint was that it "feels like scrap code"; the cause was **invariants not enforced
  by the type**, as `count()`'s own comment admitted (*"a hand-built Range can hold them"*). With
  `last >= first` and `step >= 1` now guaranteed by construction, `runmode.cpp`'s sweep —
  `for (f = first(); f <= last(); f += step())` — can no longer be handed a zero step and spin
  forever. **Not templated:** one caller, and `Bounded<T>` is already reserved for the other meaning
  of range; a `Range<float>` would make `contains()` an exact-modulo test on accumulated floats.
- **Two hand-rolled digit parsers became `std::from_chars`**, net negative code, removing a
  locale-dependent `std::isdigit`. Sabotage-verified: dropping the `ptr == end` full-consumption
  test fails *(core)parse rejects malformed input*.
- **`math::Rect2i` is the first OWNED type in `lain::math`** — GLM has no rectangle, so it is the
  one thing there that is not a typed name over something else, which is why it is `rect.h` and not
  a line in `types.h`. `image::crop` and both `ImageView::subview` overloads took four loose ints,
  where transposing x with y or w with h compiles silently. A **negative extent is empty rather than
  reversed**: a rect quietly meaning `[x+w, x)` would pass `crop`'s bounds check and read backwards
  off the buffer.
- **`image::descriptor` → `formatDescriptor`** (49 sites, 16 files) — checked first that every
  `descriptor(` in the tree is this one, with no Vulkan descriptor-pool collision. It is a free
  function found by ADL on `PixelFormat`, so the bare noun was reachable unqualified from any scope
  holding one. Also inlined `Time` (deleting `libs/core/src/time.cpp`) and moved `std::hash<Uuid>`
  into `details/uuid.inl`, the split `colormath.h` already uses.
- **The io cluster was mostly ALREADY DECIDED and nobody could find it.** Six of the nine io notes
  are downstream of WORK.md's *Queued: `core::Uri`*, whose "build after slice 5" condition has been
  met since 2026-09-02; notes 22 and 26 **are that section, restated by someone who had forgotten it
  existed**. `libs/io/include/lain/io/uri.h` now carries a pointer back to it. The lesson is general:
  a queued decision with nothing pointing at it from the code it governs stops being a decision and
  becomes a rediscovery.
- **Note 27's consistency question answered itself.** The tree already runs a four-verb rule nobody
  had written down — `read`/`write` = bytes at the transport, `load`/`save` = uri to a whole typed
  asset, `encode`/`decode` = typed value ↔ format bytes in memory, `open` = a lazy handle. It goes
  into CONTEXT.md with M13 slice 4, whose one rename (`io::image::openSequence` → `io::image::open`)
  is its only outlier.
- **Note 1's premise inverts on inspection, and is refused permanently.** Prebuilt is **C-ABI only**
  (Catch2/fmt/spdlog can never qualify), GLFW is the sole dependency with official upstream binaries
  and is not a hog, and zlib/libpng/libtiff — the ones the note names — are among the *cheapest*
  things lain builds. The expensive ones are all structurally un-prebuildable. ccache refused too:
  state between builds is hard to debug, and the saving is single minutes. Trigger to revisit: tens
  of minutes.
- **`cast` vs `convert` is deferred whole, with its survey recorded** rather than a header edited.
  The distinction the call sites already draw — `cast<To>(from)` when the TYPE changes,
  `convert(value, target)` when the type stays and a tagged PROPERTY changes — plus the standing
  preference behind it (no proliferation of `toXXX`/`fromXXX`) and the counter-evidence any design
  must answer: `porttyperegistry.h` has already refused the automatic model, because WHICH
  conversions a graph offers is a policy.

### Update 2026-09-10 — a fourth em dash, caught by the Windows leg

The first push after the `-j` fix went red on Windows alone: one case of 741, *"an unpaired inner pin
mirrors as one port — invariant in, last out"*, reporting **"No test cases matched"**. Not a test
failure — `catch_discover_tests` hands a test's NAME back to the executable as a filter, and a UTF-8
em dash does not survive that trip through argv on Windows (the log shows `G��`). It is the same
finding the port recorded on 2026-09-05, a fourth time; the test was written that same day, in M11
slice 2. Renamed to ASCII. `ctest -j8` **735/735**, format-check clean. Full notes in WORK.md's
*Run 9*.

- **The guard fired at its first opportunity; the latency is the push cadence.** Run 4 chose the
  Windows leg as the guard against a fourth rather than building a local check, and that is what
  caught this. But the em dash landed 2026-09-05 and the last CI run was on the commit *before* it,
  so **16 commits accumulated** — M11 slices 3–6, M12 and four fixes — before anything asked Windows.
  The measured cost is therefore not the ~8 minutes of runner: it is that a pure text fact,
  checkable in milliseconds anywhere, went unchecked across sixteen commits because only a push
  checks it.
- **A local guard is available and deliberately not built here** — a scan of `TEST_CASE("…")` for
  bytes above 0x7F, in the `format-check` gate that already polices text the compiler does not. One
  red run is not evidence against a decision that was made deliberately, so it is recorded in WORK.md
  as an option with a known price rather than taken unilaterally.
- **SECTION names are unaffected, and three keep their em dashes.** Discovery registers TEST_CASEs
  only, so a section name never crosses a process boundary. The rule the port wrote — *prose keeps
  its em dashes; a test NAME is an argument that crosses a process boundary* — is what says where the
  constraint stops, and it stops before sections.

### Update 2026-09-10 — `ctest -j` is green, and CI runs it that way

The last known defect, cleared: `ctest -j8` failed ~7 `io` / `io::video` cases, nondeterministically,
and passed serially every time. `ctest` **735/735** at `-j16` (×3), `-j8` (×5), `--parallel 4` (×3,
CI's setting) and serially; warning-clean, format-check clean; the headless/subproject configuration
(`LAIN_BUILD_TESTING=OFF`) still configures and builds.

- **`catch_discover_tests` runs every TEST_CASE as its own PROCESS** — it re-invokes the executable
  with a filter. Fourteen test files had each grown the same private fixture uniquifying its path
  with a `static int counter = 0`, under a comment promising *"each test owns an isolated fixture on
  disk"*. That counter restarts at 0 in every process, so two concurrent cases of one executable
  took the same name; the failure output showed a single file holding two tests' interleaved frames.
- **A counter cannot fix it, because the processes cannot see each other's.** What can is identity
  minted without coordination, which is what `core::Uuid` is already in the tree for. So the new
  header-only **`lain::testing`** (`libs/testing`, built only under `LAIN_BUILD_TESTING`) puts the
  uniqueness in a per-process **directory** and a plain counter inside it — sufficient, because
  Catch2 runs one case at a time within a process.
- **The uuid buys the CLEANUP as much as the name**, and the sabotage is what showed it: fixing the
  directory name to a constant fails **10–19** tests, *more* than the 7 it replaces, because each
  finishing process then `remove_all`s the directory out from under every other one still running.
  Recorded in the header, since it is not what you would predict.
- **Measured bonus:** a full run now leaves **zero** scratch directories behind, where the old
  scheme had left 13 stray files in the temp dir.
- **CI turns `--parallel 4` on**, which is part of the fix, not a follow-on: the step's comment
  read *"Serial on purpose: twenty test files write into the shared `fs::temp_directory_path()`"* —
  a workaround whose reason is now gone. A serial CI also could not catch this coming back, since
  the collision is invisible to a serial run.
- **`lain::testing` has no test of its own, deliberately.** The property that matters is
  cross-process, and no in-process fixture can assert it — a case checking "these two paths differ"
  passes just as happily under the bug. The suite at `-j` is the test, which is why CI had to change.
- **All four known defects are now fixed, and two of the four were larger than their one-line
  description in the same way**: the recorded symptom was the visible end of a decision that was
  duplicated (this one, fourteen times) or unreachable (defect 3's dead string). Worth remembering
  when writing the next entry on that list.

### Update 2026-09-10 — a map is not a linked group, and `ungroup` stops saying so

Known defect #3, cleared — and the recorded defect was wrong about the part that mattered. It read
*"reachable only by keyboard shortcut, since the menu greys the item out"*. The shortcut runs the
same `ungroupSelection`, which pre-filtered with `soleSelectedOfType<InlineGroupNode>` and answered
*"Select a single inline group to ungroup"* before `edit::ungroup` was ever called — so the wrong
string was unreachable from **all four** of its call sites, and `NotAGroup`'s was dead beside it.
Correcting the wording alone would have edited text nobody could see. `ctest` **735/735** (+1),
warning-clean, format-check clean, `flowview run --example` unchanged. **gui-mode not eyeballed.**

- **One class test was answering two questions**, which is why it could only ever report one reason.
  `dynamic_cast<InlineGroupNode*>` conflates *is this interior mine to take?* with *does it run
  once?*, so a map — which owns its interior outright — was told that interior belonged to a
  template. `ungroup` now asks the two SEAMS instead: `editableInner()` for the first,
  `interiorEvaluation()` for the second. It names no node kind at all now, the way the scheduler
  does not, so the next interior kind needs no edit here.
- **`NotInline` became `LinkedInterior`**, saying the fact rather than the failed test. With the
  map/loop case split off it means exactly "linked" — and the old name would have misled a reader,
  since a map is *also* not inline yet no longer returns it. Its partner **`InteriorRepeats`** is
  worded around the consequence: the spliced nodes would run **once**, so the graph would go on
  evaluating and quietly compute something else. Unlike a linked group there is no first step that
  makes it possible — repetition is what the node IS.
- **The host stopped duplicating the decision.** `ungroupSelection` now asks only *is exactly one
  node selected* and lets `edit::ungroup` say why not, so a map, a loop, a linked group and a plain
  node each get their own reason where all four used to get one message that was true and said
  nothing. The menu item stays greyed (`canUngroupSelection`) — greying out what cannot be done is
  right; the shortcut is where the explanation was owed.
- **Nothing caught it because nothing asked.** `test_groupedit.cpp` covered the linked kind and the
  plain-node kind, and neither of the two kinds added since (M8's map, M11's loop). The new case
  asserts both refuse as `InteriorRepeats` and that the refusal is atomic; sabotage — put the single
  class test back — fails it.

### Update 2026-09-09 — an unwired DEFAULTED input is not a missing required input

Known defect #2, cleared. The Issues pane reported *"required input 'radius' is not connected"* on
every input that was `Required` with no incoming edge — and a **defaulted** input is `Required` by
design. That is exactly what keeps a default from swallowing suppression (ADR-0007: a Gate wired in
and turned off must leave the slot empty rather than fall back), and `populateInputs` seeds such a
slot from its param whenever nothing is wired. So the input always carries a value and can never be
why a node cannot run. `ctest` **734/734** (+3), warning-clean, format-check clean, `flowview run
--example` unchanged. **gui-mode not eyeballed.**

- **Wider than the defect list recorded, and visible on startup.** Twelve defaulted inputs exist
  today — Blur `radius`/`sigma`, Gate `enable`, Select `selector`, Loop `count`, a loop interior's
  `continue`, ClipSequence `position`/`count`, FrameAt `position`, OpenSequence `path`, LoadImage
  `path`, ListDir `directory`/`extension` — and the **default example scene contains a Blur**, so
  flowview opened with two false warnings before the user touched anything.
- **The one-line fix was the smaller half. It lasted because nothing could test it.**
  `collectIssues` was a `static` function inside `issuespane.cpp`, which the driver-free
  `test-flowview` does not compile — so the panel's entire validation (this rule, the map-hole row,
  the Cast row) had never been under test, and the defect survived two milestones and twelve
  declarations. It now lives in **`apps/flowview/src/validation.{h,cpp}`**, compiled into the test
  binary beside `groupnav.cpp` / `undo.cpp` / `canvasids.cpp`. It moved cleanly because it touches
  no ImGui at all: a pure `Graph` + `Evaluation` → `vector<Issue>`, with the pane keeping only the
  drawing.
- **Asked of the NODE, not the port.** `Port::required()` cannot answer this — a port does not know
  what stands behind it — so the check is `node.defaultOf(in.id()) == nullptr`, the same public
  accessor the Inspector already uses to show *"driven by input"*. No new API.
- **The positive control shares the case with the rule**, because "reports nothing" would pass just
  as well if the check had been deleted outright: a bare `BlurNode` carries both kinds at once
  (`image` required with no default, `radius`/`sigma` required *with* one) and must report exactly
  one row. The loop case asks both levels, since `count` is the loop's own port while `continue` is
  an input of its interior's `GroupOutput`.
- **Found by writing the third test, and it corrected the test rather than the code:** an output is
  called a dead end once its node is READY, which is not the same as "has run" — a node with no
  inputs is ready from the start, and a node whose required input is unfed never becomes ready
  however many times the graph runs. The case now runs the graph on both sides, so what separates
  them is readiness alone.
- **Left for its own commit, recorded in WORK.md:** there is no row for *a node that ran and produced
  nothing*. A palette-added `LoadImage` defaults its `path` to an empty one, so it loads nothing and
  suppresses everything downstream while correctly reporting no missing connection. The old row said
  the wrong thing about it; saying nothing is better, and saying the right thing needs care, because
  a suppressed node is ORDINARY under ADR-0007.

### Update 2026-09-09 — `GroupSync::refused` has readers (both of them)

Known defect #1, cleared. M11 slice 2 built the refusal — an inner boundary pin a group cannot mirror
onto its own face, reported **by name** because that is the part a user can act on — and nothing read
it. The symptom was total silence: the pin is simply absent from the group's interface, nothing
outside can wire to it, and no channel says why. `ctest` **731/731** (+3), warning-clean,
format-check clean, `flowview run --example` unchanged. **gui-mode not eyeballed** — the Issues row
is new UI and needs a Metal session.

- **It needed TWO readers, not the one WORK.md named.** `edit::syncGroupPorts` has exactly two
  production callers and *both* dropped the field: `groupnav::syncPathGroups`, and **the loader**.
  Fixing only the host would have left the defect half-alive, and the half left behind is the more
  important one — the loader syncs **every** group in a document, where a host reconciles only the
  groups the user is currently INSIDE. So a refusal now survives navigating away, arrives on a
  document authored elsewhere, and reaches headless `run` / `list` (through `ctx.warn`'s log line;
  `runmode` reads `LoadResult::issues` for nothing else either). Proved on the real binary against a
  hand-collided `count-loop.json`, with the unmodified document silent as the control.
- **`syncPathGroups` returns a `PathSync {changed, refused}`**, and a `PinRefusal` carries the path
  to the group's **INTERIOR**, not to the graph the group sits in. That is where the pin lives and
  where the Interface pane can rename it, so it is where the row has to lead — and only a path more
  than one step deep tells the two levels apart, which is why the test nests one. A row naming the
  level already drawn is a `note` rather than an `inside`: navigating costs a preview-cache clear,
  and spending one to arrive where you already are is not a link.
- **The refusals are AppContext data, not Issues**, rewritten whole each frame. Not folded into
  `loadIssues`, though it is the same panel: an edit CLEARS those, and the edit that creates a
  colliding pin is precisely the edit whose refusal must survive it. Held as the sync's own record
  because the wording belongs to the pane that shows them.
- **The row states the CONSEQUENCE, not the cause** — *"inner pin 'count' could not be mirrored onto
  its face — nothing outside can connect to it"*. There are two causes (a loop's name collision, a
  map's type with no registered collection form) and `refused` carries names alone by decision; the
  host *could* infer which from the node's class, and that would be a second answer to a question
  core already owns.
- **`refused` stays out of `changed()` at the host level too**, and a test pins it: a refused pin is
  retried on every pass and flowview syncs every frame, so counting one as a change would bump the
  recipe version continuously and re-run every evaluation forever. A second case pins the other half
  — a plain loop reports **nothing**, because its reserved pins are *not candidates*
  (`mirrorsPin`) rather than refused, and conflating the two would make every loop in every document
  report two problems from the frame it was created.
- **CONTEXT.md needed no edit** — its *reserved pin* entry already said a refused pin is one "a user
  can act on **and a host names**". That sentence was aspirational until this commit.

### Update 2026-09-09 — the linked map and linked loop are REFUSED, not deferred (decision only)

ADR-0014 deferred a **linked map** (one shared template mapped over N streams) and ADR-0021 deferred a
**linked loop** behind the same sentence — *"it needs the workload in front of it to decide how an
instance's interface reconciles against a shared template's."* That question is retired by refusing
the cell rather than answering it: **compose instead — a map (or loop) whose interior holds a linked
group.** Nothing is built; this is a decision commit, and the argument is written up once in
[ADR-0014](docs/adr/0014-map-nodes-staged-planning.md).

- **The composition is not a workaround, it is the better shape.** A linked map's interior would BE
  the template, so there is nowhere for a per-element pre-tint, an index tag or a second link, and no
  way to wrap more than one thing. It degrades better too: a failed link cannot touch a face derived
  from the map's own boundary, so the arity contract and the parent's wiring survive, where a linked
  map would need ADR-0010's cached interface a second time and lifted.
- **It dissolves the blocker rather than dodging it.** The template owns the recipe; the map's own
  boundary owns the lifting (`exposePort` vs `exposeBroadcast`) — one owner per fact. A linked map
  would make the template and the instance two owners of one interface, which is the shape M7 slice
  1's deleted `editableAt` and ADR-0014's own *"no flag, nothing stored"* rule each refuse.
- **The extra level costs nothing measurable.** One `shared_ptr<const Graph>` definition backs N
  evaluations either way; the level adds one entry/exit pair per element; a boundary crossing is a
  refcount bump (M5 slice 1); and the stage count is unchanged, because a linked group is `Once` and
  raises no frontier — exactly the nesting depth of 2 ADR-0014 already sizes the target workload at.
  `groupnav::hasLinkedGroups` is already recursive, so `File ▸ Reload Linked Groups` already reaches
  a link inside a map.
- **The one real cost is a GESTURE, not a class**, and it is now WORK.md's Tier A #10: plumbing a
  map's boundary to the link's face by hand. `edit::groupSelected` already computes the cut-set and
  builds the boundary pins, and hardcodes the kind at exactly one site; the lifting would fall out
  for free, since `syncGroupPorts` calls the **virtual** `exposePort` a `MapNode` overrides. Its one
  genuine decision — a lifted outer port will not type-check against a parent source producing `T`,
  so it cannot reconnect everything the way grouping does, and at least one input must be lifted or
  the map has no arity to size its children from — is itself part of why the cell is refused: the
  split/broadcast bit is the instance's to make and cannot be derived.
- Amended in place: ADR-0014's and ADR-0021's deferred items (ADR-0021 points at ADR-0014 rather than
  restating it), WORK.md's four historical mentions (M8 ×2, M10, M11) plus the new Tier A item, and
  the three comments in `libs/flow/include/lain/flow/group.h`. **No code changed** — comment-only, so
  the build and the suite are unmoved.

### Update 2026-09-08 — M12 live-verified; loop timing documented (do-while, not while)

**gui-mode was eyeballed by the repo owner 2026-09-08**: a loop built from const + compare + cast
works. One crash was found and fixed on the way (the stale-value `bad_any_cast`, in the M12 entry
below), and one **oddity turned out to be a documentation gap rather than a bug**.

- **`continue` is a POST-test, and nothing said so.** It is a value the BODY produces, so it can only
  be read after a pass, and it is asked about the pass that just ran. A loop is therefore a
  **do-while**: with any positive `count` the body runs at least once, and `count == 0` is the only
  pre-test (the fold identity). A condition of `index < 4` runs **five** passes, because the pass at
  index 4 is the first whose answer is false and by then it has already run.
- **`count` is the mechanism for "exactly N".** Conditioning on `index` re-implements it with that
  off-by-one built in. The condition exists for what a count cannot express — stop when the work
  stops changing, where one extra pass is meaningless.
- **A Gate must NOT be used to end a loop**, which is the first thing a user reaches for.
  `continue == false` stops it cleanly; a **suppressed** `continue` is an iteration FAILURE and the
  exit clears every output. That split is deliberate (`GateNode::enable`'s resolution) — it is what
  distinguishes "the loop finished" from "the fold broke" — but it makes the obvious gesture wrong.
- **No pre-test is available, and that is not an omission:** it would require the condition to be
  computed outside the body, and the whole reason it lives inside is that it is computed FROM the
  body's work. The same constraint the owner identified visually.
- Recorded where a reader looks: **ADR-0021 amended in place**, CONTEXT.md's *reserved pin* entry,
  and `LoopNode::continuePin`'s own documentation. Pinned by a test — *a condition on `index` is
  asked about the pass that JUST RAN* — so the arithmetic cannot drift. `ctest` **728/728**.

### Update 2026-09-07 — M12 built (all four slices): a node's payload type is data

M11's loop could not be driven from the GUI: `LoopNode`'s `index` is an `int`, `CompareNode`'s inputs
were `float`, and `Graph::connect` type-checks exactly — with **no conversion anywhere in `flow`**.
That is an instance of a wider gap: a node's payload type was baked into its **factory key**, so
`ConstantNode<T>` cost one palette entry per type (five, against ten registered port types and
growing), `Gate`/`Merge`/`Select` existed for `image::Image` alone, and a **Cast** is impossible in
that model at any size — it needs one key per *pair*. Grilled 2026-09-07; decisions in
**[ADR-0022](docs/adr/0022-payload-types-as-data.md)**, build order in WORK.md M12, vocabulary in
CONTEXT.md's new *Payload type* section. **All four slices are built** — the mechanism + Constant,
the conversion registry + Cast, the ordering capability + Compare, and the control nodes. `ctest`
**727/727** Debug with video on (+30 from 697) and **701/701** Release in the default video-off
configuration (+30 from 671); warning-clean, format-check clean, and `flowview --version` / `list` /
`run` unchanged through the real binary. **gui-mode NOT eyeballed** — the Inspector's payload-type
dropdown and the Issues row are new UI and need a Metal session. *(**Superseded** — eyeballed
2026-09-08; it found the stale-value crash recorded at the end of this entry, and nothing else.
**M12 is COMPLETE as of 2026-09-08** — see the 2026-09-08 update above.)*

**The blocker, resolved, is a test**: `a loop's own index drives its condition and its body` wires
`index → Compare<Int> → continue` (no cast needed — slice 3) *and* `index → Cast(Int→Float) → sigma`
(slice 2), through the production factory and the production retype gesture. It reports **4
iterations against a bound of 100** — it stopped on the condition, which is what ADR-0021 built
`iterations` to make readable — and the Cast is proved to feed the blur by cutting its edge and
checking the fold produces a **different picture**, since every weaker assertion passes either way.

- **A payload type is a `const PortType*` a node declares its ports and params FROM**, named because
  one is not always enough (a Constant has `value`; a Cast will have `from` and `to`). It is the
  flyweight and not a registry key, so a payload-typed node is still constructible with no registry
  populated — which `libs/flow`'s own tests need; the key is only the on-disk form.
- **It is emphatically NOT a param, and the reason is structural.** A `Node` cannot reach its
  `Graph`, so it cannot disconnect its own edges — a retype can therefore never be `setParam`. Stored
  as a param, the Inspector's generic loop would reach it and commit through the one writer that
  cannot cut an edge; **nothing re-checks an edge after `connect`** (`populateInputs` copies into the
  slot blind), so the failure would arrive as a `bad_any_cast` inside a worker task.
- **Retyping is IN PLACE**, so `PortId`, name, order and presence survive — sabotage-verified:
  remove-and-re-add draws fresh ids and **aborts** two cases. The writer is
  `Graph::setPayloadType` (a primitive that REFUSES while an edge would be left with ends that
  disagree) under `edit::setPayloadType` (the gesture that cuts exactly those and reports them) —
  precisely the `removePort` split.
- **Corrected during the build:** *"edges that still typecheck survive a retype"* describes a case
  that cannot occur — `connect` is the only edge-maker and it type-checks, so a genuine retype breaks
  every edge on every port it moves. What survives is edges on the node's **untagged** ports.
- **Per-type behaviour followed ADR-0014's own split:** **ordering** is a new `PortType::compare`
  field (derivable from `T`), **conversion** is a registry keyed by the type pair (a relation between
  two types). So a Compare's accepted types will be *derived* from `isOrderable()`, not listed.
- **`meta::is_less_comparable` asks a container about its ELEMENT** — found by the build: before
  C++20 `std::vector`'s `operator<` is declared for every element type and fails only when
  instantiated, so plain detection answered true for `std::vector<image::Image>`.
- **The conversion registry landed here rather than in slice 2**, because `edit::setPayloadType` is
  its first caller: a retype carries a param's value across through **the same conversion a Cast
  will**. Deferring it would have shipped a retype that loses the number the user typed.
- **No schema version bump**: one optional `types` section per node, and **absent means the factory's
  preset stands**. The five keys `constant` replaced stay registered through `Factory`'s
  **string-creator** form, which records no reverse type → key entry — load-bearing, since `keyOf`
  maps one class to one key and the typed form would make a Float constant **save as `constInt`**.
- **Six sabotages, all caught by named cases**; and a bug in one of the new tests, found by running
  it, where the conversion menu was checked without the port types registered.
- **An unconvertible Cast PAIR is representable, and the loader forced that.** Refusing one through
  `acceptsPayloadType` is the shape this repo normally prefers and is wrong here: payload types are
  applied **one at a time** on load, so a document saying `Path → String` would have its `from`
  refused while `to` was still the factory preset, and come back as a different node than it was
  saved as. So `canConvert()` answers the pair whole and the **Issues pane** reports it — the one
  place the adapter asks a CLASS rather than a structural fact, because there is none to ask.
- **A DYNAMIC pin has to be TAGGED as it arrives** (`Node::tagAs`, from `onDynamicPortAdded`). A
  Merge's branches are added through the port-type registry, whose creator has a compile-time `T` and
  knows nothing about payload types — without the tag a retype moves the declared `out` and leaves
  every branch behind, so the node forwards nothing while looking correctly configured.
  Sabotage-verified, and the whole reason Merge/Select were in scope.
- **Presets keep every existing document loading unchanged**: Gate/Merge/Select preset `image::Image`
  (what they were fixed to), Compare presets `Float`, Constant presets `Int`. None of them writes a
  `types` section a v2 reader would choke on, and absent means the preset stands.
- **Deviation, deliberate and recorded:** no generated per-pair palette entries (`Cast ▸ Int →
  Float`). They want `NodeCategory` to carry a preset and three render sites to apply it — a refactor
  for ergonomics, where the Inspector's two dropdowns already reach every pair in two clicks.
- **First gui-mode bug, found by the repo owner and fixed 2026-09-08: a STALE value crashed the
  panes.** Adding a Constant (Int) and changing its type to Image threw `std::bad_any_cast`. A retype
  makes a port's declared type disagree with the value the last run left in the evaluation — a state
  that could not exist before, since a port's type was fixed at declaration. The retype **cannot**
  clear it (invalidation is pulled, ADR-0012), and a host draws before the next run corrects it. So
  the per-type bridges are now TOTAL in the value they are handed (`holds<T>()`, not `empty()`), and
  `describe` answers **"(stale)"** — not `"(empty)"`, which means SUPPRESSED. **The same bug was in
  `dump.cpp`**, unreported: it tested the port's DECLARED type then read the value, so `flowview run`
  would have thrown identically. Found by auditing for the shape. The rule, now in the ADR: ask what
  the VALUE holds, never what the port declares.
- **Noted for its own commit:** `ctest -j8` fails ~7 io/io::video cases on shared scratch paths.
  Pre-existing, unrelated, invisible serially (724/724 passes).

### Update 2026-09-05 — CI: lain builds and tests on Linux, macOS and Windows (**GREEN**)

`lain` had **no CI at all**, and all 210 commits were built and verified on one macOS arm64 machine.
`.github/workflows/ci.yml` (modelled on archimedes') now builds and tests on **Linux x64, macOS arm64
and Windows x64**. Full landing notes in WORK.md's *Continuous integration* section. **The first run
is a discovery run, not a gate** — CI is the only Windows and Linux compiler this project has, so the
workflow is the porting *tool*; "CI is done" means three green platforms, not a merged yml, and the
fix-forward backlog lives in WORK.md.

- **Six jobs.** Four build legs — Linux Release, Linux Debug, macOS Release, Windows Release — plus a
  `clang-format` job (over the repo's pinned clang-format 20, which exists precisely so CI and every
  machine format identically) and an `add_subdirectory` smoke. `fail-fast: false` is load-bearing: a
  red leg must not hide what the other three would have said.
- **Release everywhere, Debug on the cheapest runner.** Release is what a fresh clone gets and is the
  configuration that has rotted before (M10 slice 4 found every `#ifdef NDEBUG` enforcement test in
  the repo had been unreachable for some time); Debug is a genuinely different test set.
- **The default configuration gets its own Linux leg** (revised 2026-09-05, five legs). Video
  defaults OFF, so one leg must prove that configuration builds. macOS carried it at first, on the
  reasoning that a platform built by hand daily needs CI least — **which inverted once the timings
  arrived**: macOS is the slowest leg by 3–4×, and the daily build has video ON, so CI was spending
  seventeen minutes on a combination nobody builds while never exercising the one built daily. Linux
  does the same work in three. Each axis now varies alone; Debug stays video-ON to match the daily
  build, and the video-off leg is Release because that is what a fresh clone gets.
- **The `add_subdirectory` smoke proves two claims with one job**, because `LAIN_BUILD_TESTING` /
  `_APPS` / `_FORMAT` all default to `${LAIN_NOT_SUBPROJECT}`: lain is consumable as a subdirectory,
  *and* a headless consumer of `lain::flow` is not forced to vendor GLFW or ImGui. Verified locally
  before pushing — it builds, runs, and pulls no GLFW, no ImGui and no Vulkan loader. It also makes
  visible that `add_subdirectory(extern/archimedes)` is unconditional, so a headless consumer builds
  a Vulkan renderer regardless.
- **The GPU is deliberately not covered.** The one `[gpu]` test self-SKIPs behind `LAIN_GUI_SMOKE`
  and `catch_discover_tests` sets `SKIP_RETURN_CODE=4`, so it reports skipped rather than failed;
  `ensureGlfw`/`ensureInstance` are lazy, so the headless `[app]` tests need no driver. **gui-mode
  remains eyeball-verified on a Metal machine and nothing about that changes.**
- **`flowview` is smoked as a BINARY** (`--version`, `list`, `run`, `--licenses` on the video legs),
  because `apps/flowview/test` compiles `runmode.cpp` into the test binary and so ctest structurally
  never exercises the shipped executable — including whether it launches at all.
- **Found and fixed here: `flowview` and `test-flowview` staged no FFmpeg DLLs.**
  `lain_ffmpeg_stage_runtime()` was called by the two video test executables and by neither of them,
  though both link `lain::io::video::codecs`; on Windows neither would have started. For
  `test-flowview` that fails the **build**, not the tests, since `catch_discover_tests` runs the
  executable at build time to enumerate cases. The same compiled-linked-unreachable shape as M5's bug
  six — and the one thing fixed ahead of the discovery run, since it is a known omission rather than
  anything a compiler had to tell us.
- **GREEN on all three platforms 2026-09-05**, five runs after the first, and green again on the
  reshuffled five-leg matrix: Linux Release 658/658, Linux Release video-off 626/626, Linux Debug
  652/652, macOS Release 658/658, Windows Release 658/658, plus the format and subproject jobs.
  **The Debug/Release split earned its place immediately** — Release runs six more tests than Debug,
  and those six are the `NDEBUG`-guarded enforcement cases this file recorded as having gone
  unreachable unnoticed.
- **The port took ten findings, and the prediction was wrong about their kind.** Not one MSVC
  narrowing warning and not one `<windows.h>` `min`/`max` collision — the two predicted loudest. Six
  of ten were instead **something libc++ supplies that libstdc++ and MSVC's STL do not**: a
  transitively-included `<string>`, `__STDC_CONSTANT_MACROS`, and three diagnostics clang does not
  implement (`-Wclobbered`, `-Wcomment`, GCC's rejection of a declared-but-undefined
  internal-linkage function). The rest were each invisible in their own way: two languages claiming
  the `.asm` extension (libpng's `project(LANGUAGES C ASM)` silently taking the Vulkan loader's MASM
  trampolines, which then failed to link with 750 unresolved symbols), a DLL nothing put on the
  path, a link order only GNU ld's `--as-needed` enforces, and an em dash that could not survive a
  trip through argv into a ctest filter.
- **Two were real bugs, not portability noise, and both predate this work.** `rows` crossed a
  `setjmp` without `volatile` in both PNG codecs — a `longjmp` could have handed `free()` a stale
  pointer on *any* platform; and `flowview` never staged the FFmpeg DLLs it links. Neither is a
  Windows or Linux problem. They had simply never been asked about.
- **Standing consequence worth knowing: a Linux consumer of the video plugin needs `libva`
  installed.** The prebuilt is configured `--enable-vaapi`, so `libavcodec.so` carries a link-time
  dependency every consumer must satisfy.
- **Still not covered, deliberately: the GPU.** gui-mode remains eyeball-verified on a Metal
  machine, and the `[gpu]` test still self-SKIPs. Nothing in CI should be read as covering it.

### Update 2026-09-04 — M10 slice 7b built: the sequence player and the palette (**M10 COMPLETE**)

The second of slice 7's two commits, and the end of Milestone 10. Footage is now something you can
see and drive in the gui. `ctest` **652/652** with video on (+4), **626/626** in the default
video-off configuration, warning-clean, format-check clean. **gui-mode live-verified by the repo
owner 2026-09-04** — no crashes. Full landing notes in WORK.md M10.

- **A sequence's poster is its first frame, decoded** — the second customer 7a's registry was shaped
  for, and where the `PortValue` return type earns itself: this one cannot be aliased, so producing
  it is work, and it happens once per edit rather than once per drawn frame.
- **The player decodes by FRAME IDENTITY** (`FrameRef`, which `sequence.frame()` answers without
  decoding), not by position — a re-run can rebind a different sequence while the transport sits
  still, and position alone would keep showing the old footage. It records the attempted frame
  whether or not the decode succeeded, so a broken file is not re-decoded 60 times a second, and a
  frame that will not show **clears** the texture (slice 6c's `ConvertNode` lesson: a stale frame
  presented as the current one is worse than a visible hole). Playback **drops frames** rather than
  sliding behind the clock — ADR-0018's "best effort" made literal; an unspecified rate plays at
  24 fps with a tooltip saying so.
- **An INSPECTION player, structurally**: it decodes from the value on the pin and never re-runs the
  graph. Driving a render is binding a `FramePosition`, which is the Interface pane's new editor; the
  transport that does it for you is the follow-on.
- **`FrameSequence` binds through `ParamEditors`** (File... / Folder... through `io::sequence::open`,
  both modes because the opener dispatches by what the uri IS), which is what 7a's branch removal was
  for — before it, such a boundary input read "(no editor)". **`FramePosition`** edits as a plain
  drag, deliberately unbounded: an editor cannot know which sequence a position indexes.
  **ADR-0005 is amended in place** with what actually landed: siblings, not one registry, and a view
  is an object because it owns state.
- **The dialog filters were hiding video** — Images-only, with a comment already admitting the gap,
  so an `openSequence` path could not be pointed at an `.mp4`. They now come from
  `io::video::videoExtensions()`, the seam's own list, plus All files.
- **`openSequence` / `frameAt` / `clipSequence` are on the menu** (a new **Sequence** category, which
  reaches the menu bar, the right-click palette and the Nodes pane at once), with canvas colours for
  the three payload types. A new `[catalog]` test pins the invariant a hand-added entry breaks
  silently: every catalog key must be creatable by the factory, or the menu item does nothing.
- **Milestone 10 is complete.** What was deliberately left out is listed in WORK.md's *Not in this
  milestone*: the driving timeline, a handle pool, a decoder pool, BT.601 / BT.2020 / PQ / HLG,
  device capture, realtime playback of processed output. **`core::Uri` is the queued follow-on.**

### Update 2026-09-04 — M10 slice 7a built: the GUI view registry, with no new behaviour

The first of slice 7's two commits. CONTEXT.md's deferred **"GUI view"** seam is built — flowview's
**`ValueViews`**, a `type_index` registry, sibling of `ParamEditors`: that one is how a type is
WRITTEN, this is how it is SHOWN. Only `image::Image` is registered, so behaviour is unchanged and the
existing suite is the regression test. `ctest` **648/648** (+3), warning-clean, format-check clean,
both headless paths unchanged. Full landing notes in WORK.md M10.

- **Two halves**, because a value appears in two places with two lifetimes: a **poster** (the still
  standing for it in a list, uploaded into the edit-refreshed `PreviewCache`) and a **view** (the
  Preview pane rendering, owning zoom — and at 7b a playback position and a decoded frame, state that
  changes with **no edit at all**, which is why it cannot live in that cache).
- **A poster is a `PortValue`, not an `Image`** — so the image case **aliases its own payload**
  (`PortValue::alias`), because a thumbnail of an image IS that image and a by-value poster would
  charge every image port a deep pixel copy per edit. Sabotage-verified on an address comparison.
- **Five hardcoded `typeid(image::Image)` branches are gone.** Every port is one line of
  `Evaluation::describe()` plus a thumbnail iff the cache has one. `ValueView::summary` was designed
  and **dropped**: `describe()` already answers it for every type, and a per-view string would be a
  second answer to a question flow already answers.
- **New `gui::Texture::extent()`, and it is not a convenience**: the panes sized thumbnails from the
  value's image extent, which 7b's sequence pin does not have. The aspect belongs to what was uploaded.
- **The image bind gesture moved into `ParamEditors`**, which deleted the last type branch from
  `interfacepane.cpp` — it has to land here, or removing the branch would regress the gesture.
- **`ImageCanvas`** is the extracted zoom/pan/fit widget, split into `drawImage` + `drawToolbar` so a
  view owns its layout; `previewFit` / `thumbnailBox` moved out of `AppContext` to sit beside it,
  which is also what keeps the new tests driver-free.

### Update 2026-09-03 — codec colour-tag policy audited and settled (ADR-0020)

The question was what each reader and writer actually *thinks* it reads and writes, and whether that
is a fixed assumption or genuinely from the file. The audit found **four policies for one question, a
WORK.md claim matching none of them, and no ADR owning any of it** — ADR-0003 owns `ColorSpace` and
said nothing about codecs; ADR-0018 governed video only. Settled in
**[ADR-0020](docs/adr/0020-codec-colour-tag-policy.md)**: *a reader states only what the file states;
a writer records the tag when the format can state it, and refuses when it cannot.* `ctest`
**642/642** (+23), warning-clean, format-check clean. Full landing notes in WORK.md.

- **Only PNG and the video reader read anything.** JPEG hardcoded `sRGB` (stb_image discards every
  APP marker, so nothing was consulted); TIFF hardcoded `Unspecified` without reading `ICCPROFILE` or
  `TRANSFERFUNCTION`. **No still-image writer recorded a colour tag at all** and no image `canEncode`
  consulted `colorSpace()`, so lain's own round trip lost the tag — which is why the stills→video path
  needed a hand-placed `ConvertNode` — and `Linear` → JPEG → `sRGB` was accepted silently.
- **Each codec's colour decision is now ONE file holding both directions** (`pngcolor`, `jpegcolor`,
  `tiffcolor`), on the `colorpolicy.{h,cpp}` precedent whose header says why: splitting them is how
  the halves drift, and a reader reading three chunks beside a writer writing none *is* that drift.
- **JPEG scans its own APP markers**: Exif `ColorSpace` above the ICC profile above JFIF — Exif first
  because that is the specification's own arrangement, so a camera JPEG carrying both keeps the one
  statement lain can represent. The JFIF arm survives as the single conventional answer, **logged**.
- **TIFF states its curve** (`TransferFunction`, sampled from `image::toLinear`, compared **exactly**),
  making it the one still format that round-trips all four values. **PNG refuses `BT709`**: its only
  handle is `gAMA`, and lain's own reader reads 0.45 back as `sRGB`.
- **Two invented claims removed**, same defect on the alpha axis: PNG accepted `Premultiplied` and
  handed it back `Straight` (corruption `canEncode` exists to prevent), and TIFF wrote
  "unassociated" for an alpha mode it was never told.
- **`BlurNode` stopped overwriting a stated fact** — `setColorSpace(sRGB)` ran unconditionally, so a
  BT709 frame was linearised with the wrong curve and relabelled. **`Image::toString` names both
  tags**, because every codec refusal prints it and the refusals are about the tags.
- **A real memory-corruption bug, caught only by building Release**: libtiff's TransferFunction
  getter always consumes three `uint16_t**` while its setter takes one or three, so the obvious
  mirror of the setter let libtiff write two NULLs past the argument list — absorbed in Debug,
  SIGSEGV in Release on the first grayscale image. Both configurations are now verified
  (**643/643** Debug, **619/619** Release).
- **The loss matrix is a test** (`plugins/io/image/test/test_colorroundtrip.cpp`), and every image
  codec now switches exhaustively on `ColorSpace` — none did, which is why `BT709` reached none of
  them when M10 added it.
- **Three video items inside ADR-0018's territory, fixed in the follow-up commit.** The **refusal
  lists were denylists**, so everything unlisted was claimed as BT709 — the LOG transfers,
  `AVCOL_TRC_LINEAR` (already refused on the write side, so the directions disagreed), DCI-P3 /
  Display P3 / XYZ primaries. Now **allowlists**: default to refusing, not to claiming, which is
  the same lesson the codec capability tables encode. The **log line** is now one per axis, since
  the old condition needed both transfer and matrix unspecified and so said nothing about a file
  tagged only by its matrix. And the **untagged decode matrix finding was right but my proposed fix
  was wrong**: measurement showed lain's own untagged fixture decodes back to its source colour
  exactly under BT.601 and 10 counts out under BT.709 (at 720p too), because an untagged encoder
  used BT.601. The transfer and the matrix are separate questions — the OETFs are the same curve,
  the coefficient sets are 17% apart — so the behaviour stayed and became a stated, tested, logged
  decision (`decodeMatrixFor`) instead of an inherited swscale default. `ctest` **645/645**.

### Update 2026-09-02 — M10 slice 5b built: the FFmpeg reader (**slice 5 COMPLETE**)

`FFmpegVideoReader` in `plugins/io/video/ffmpeg` — a custom `AVIOContext` over slice 3's `Stream`
(so no `fstream` enters a codec plugin, ADR-0004), the demux-scanned frame table, keyframe seek +
pts matching, `swscale` to RGB8 with range expansion, and the colour policy. **`--video src.mp4`
works**: a real mp4 sweeps to `out.####.png` through the production binary. `ctest` **589/589**
(+15) with video on, **578/578** in the default configuration, warning-clean, format-check clean.
See WORK.md M10 for the full landing notes.

- **A container's index is KEYFRAME-ONLY, so the frame table is always demux-scanned.** ADR-0018
  and CONTEXT.md both said the index was read "when present" with the scan as a fallback — not
  achievable: an index answers *where do I start decoding*, never *what is frame 412*. Both are
  corrected in place; the index keeps the one job it is good for (seek points). Cost, stated rather
  than found later: one sequential header-only read at open.
- **The table is sorted into DISPLAY order — the slice's sharpest edge.** With B-frames the packets
  arrive pts 0, 1536, 512, 1024 … so a table left in arrival order answers `image(1)` with the frame
  stored second: a **wrong image, not an error**. Sabotage-verified, and the reason a second fixture
  (MPEG-4 `-bf 2`, since videotoolbox will not emit B-frames) exists at all.
- **A seek matches by PTS, never by counting** — after seeking to a keyframe the decoder hands back
  earlier frames, reordered. Sequential access never seeks (`m_nextOrdinal`), and a failed decode
  resets that to *nowhere* rather than trusting a decoder whose position is unknown.
- **Sabotage found a weak test and it was replaced.** "Random access equals sequential" opened a
  fresh sequence and asked for frame 7 — which decodes forward and never seeks, so it passed with
  seeking removed entirely. It now reads frame 11 first, forcing a backwards seek past the ring, and
  compares byte for byte.
- **The colour rule refuses at OPEN, and examines all three axes.** Not theoretical: the PQ fixture
  came out of the encoder with its **matrix** tag surviving and its transfer stripped, so a policy
  reading only the transfer would have accepted BT.2020 as BT709. An explicit **sRGB transfer is
  believed**; untagged is BT709 **with a log line**.
- **`container()` / `codec()` landed with their consumer** — `io::video` logs *"opened clip.mp4 —
  mov,mp4,m4a,3gp,3g2,mj2/h264, 64x48 RGB8 BT709 · 24 fps, 12 frames"*. The container/codec
  distinction as **data**, per this session's decision; an accessor with no caller is how an
  unreachable feature stays unreachable.
- **Live proof:** 12 frames rendered from an mp4, the stills tracking the fixture's per-frame
  colour, and **the file opened exactly once for the whole range** — ADR-0018's "one Evaluation
  keeps the decoder warm", observed. And in a video-OFF build the same document still lists its full
  interface and reports *"this build has no video codec plugin"* when run: capability, not
  vocabulary.

### Update 2026-09-02 — M10 slice 5a built: the `io::video` seam + the opener registry

Slice 5 lands as **two commits** — the seam and the registry first (testable in the *default*,
video-off build), the FFmpeg reader second. **5a is built.** `lain::io::video` (the codec-free seam:
`VideoReader`, the reader registry, the container-extension claim, `VideoSource`, `open()`), the
extension-keyed `io::sequence` registry, the generated `registerVideoCodecs()` aggregator, and the
`io::extensionKey` hoist. `ctest` **574/574** (+14), warning-clean, format-check clean, and the
default configuration builds and passes with it. See WORK.md M10 for the full landing notes.

- **The seam is unconditional; only the CODEC is opt-in** — which is what makes ADR-0019's amendment
  true rather than aspirational. With no plugin, `.mp4` still routes to a video opener and reports a
  missing **capability**, instead of falling to the still opener to be told it is not a directory.
  So `registerVideoCodecs()` registering nothing, correctly, is the default build's whole video
  story, and the aggregator test asserts the count in **both** configurations.
- **The container-extension list lives in the SEAM, not the plugin.** A list owned by the plugin
  disappears with it, taking `.mp4`'s meaning along — the vocabulary loss arriving by another door.
  What a file is *called* is a claim about which **medium** it belongs to, and that claim survives
  having no codec.
- **The video reader registry is keyed by BACKEND NAME, not format** — the one place this seam
  deliberately does not mirror `io::image`. An image codec claims a format; a demuxer claims a family
  and identifies containers by **content**, so an extension key would be a second, worse answer to a
  question FFmpeg already answers better.
- **Container and codec stay ONE interface, and not because FFmpeg does both.** A video file has two
  aspects — the container (mp4/mov/mkv) and the codec (h264/prores/vp9) — but the obvious split
  (`Demuxer → Packet`, `Decoder(Packet) → Image`) cuts through the middle of one thing: a demuxer's
  output is **not** codec-neutral (AVCC in an MP4 vs Annex-B in a TS — the reason FFmpeg needs a
  bitstream filter), and "decode frame 412" is one algorithm spanning demuxer knowledge and decoder
  state. The distinction surfaces as reported facts and, at the writer, as options. WORK.md M10
  records the trigger to revisit: the day lain owns a demuxer, or a second backend covers a disjoint
  set — and `Packet` is the type it would have to build first.
- **Dispatch is by extension with one default, and the asymmetry is load-bearing.** Video is
  addressed by name; the image medium is addressed **structurally** (a folder has no extension;
  `shot.####.png`'s extension names the still format, not the sequence's). The registry lives in
  `open.cpp` and the wiring in `openers.cpp` — the only file naming a medium — so the media seams do
  not know the dispatcher exists and the dependency stays acyclic.
- **`VideoSource` is in the seam, not the plugin**: a plugin implements `VideoReader` and nothing
  else, since the ring, the lock, the range check and spec enforcement are `media::FrameSource`'s.
  Its `timestampOf` override is what makes VFR free, pinned by a test reporting timestamps **no rate
  could produce** — the base's default is rate × ordinal, so rate-derived values could not tell the
  two apart. And **`VideoReader::open` takes an open `ReadStream`, never a uri**: ADR-0004's split
  made structural, since a plugin that is never given a name cannot open a file.
- **`io::extensionKey` hoisted into `lain::io`, collapsing TWO copies.** `io::image` and `io::data`
  each had a private `formatkey.h`, the second explaining it was duplicated *"so neither seam depends
  on the other"* — a reason already expired, since both link `lain::io`. Three seams now ask this
  question (a codec key, a container claim, a medium dispatch), and a format decided in three places
  disagrees about `clip.MP4` silently: the wrong opener, or none.
- **A `core::Factory` only grows**, so the empty-registry case got its own tiny executable rather
  than a rule about the order Catch2 runs cases in.

### Update 2026-08-11 — group authoring gestures built (Group / Ungroup / Save as Template / Make Local)

The four gestures M5 designed and deferred, whose hold M6 step 5 lifted. See WORK.md's "Group authoring
gestures" for the full landing notes. `ctest` **430/430**, warning-clean, format-check clean; headless
`run --example` unchanged; **gui-mode live-verified by the repo owner 2026-08-11** — every gesture
reaches the canvas, which is where all ten of M5's bugs lived, and this time it produced none.

- **`Graph::extract(NodeId) -> unique_ptr<Node>`** — removing a node while keeping it, the primitive
  both directions needed. `removeNode` is now this with the result dropped. The node **keeps its
  NodeId**, safe only because ADR-0011 made that a uuid: identity survives a move between graphs, so
  layout entries, canvas ints and preview keys all keep pointing at the right node.
- **`edit::groupSelected`** computes the cut-set (one pin per distinct SOURCE port, so a fan-out is one
  pin carrying one value; names from the mirrored port, uniquified `_2` since port names are
  identifiers). Two refusals beyond the designed boundary-node one, both atomic and checked before the
  first node moves: **`WouldCycle`** (a value that leaves the selection and re-enters — the contraction
  is impossible, not the graph) and **`UnnamedPinType`** (a crossing type with no port-type registry
  key: the serializer skips a dynamic pin it cannot name, so the group would work until saved and come
  back missing that pin and its wiring — reject rather than degrade, as the image encoders do).
- **`edit::ungroup`** resolves each boundary pin back to the direct edges it stood for. Refuses a
  LINKED group (its interior is the template's, shared with every instance). Contrary to the M5 sketch
  it does **not** mint fresh ids — with uuids there is nothing to avoid colliding with.
- **`edit::replaceGroup`** is what Save as Template and Make Local share: swap what backs a group,
  keeping the group **and its NodeId** (from the document's point of view it is the same group,
  differently backed). Parent edges carry across by **pin name**; a pin the new interface lacks is
  reported in `dropped`, not silently lost.
- **Make Local reads the template from disk**, not from the shared definition in memory — the honest
  meaning of the gesture, and the only way, since a definition is a `shared_ptr<const Graph>` precisely
  so no instance can take it. Loaded with **no cache**: a private body is not another sharer.
- **Layout migration lives in `groupnav`** (`descendLayout` / `ascendLayout` / `liftedPositions`), not
  beside the gestures, so it is unit-tested driver-free. Lifted nodes are translated so their centre of
  mass lands on where the group sat; verbatim inner coordinates would fling them off-canvas. Split out
  because a layout mistake here is **silent** — a node whose entry did not travel just lands in a
  default column — and that paid off at once: a moved node may itself be a GROUP, and the first cut
  carried only its position, so grouping a group discarded everything below it.
- **Wiring:** the **Edit menu** + `Ctrl+G` / `Ctrl+Shift+G`, each greyed by its own availability query.
  Not a Group menu — M5 retired that one because a menu greyed out almost always is the worse discovery
  path. New generic `AppContext::noteMessage` carries refusals to the transient row.
- **Found and fixed here:** **`File ▸ Reload Linked Groups` had no menu item.** M7 slice 3 built and
  tested `reloadTemplates` + `hasLinkedGroups` but never added the `MenuItem`, so the gesture compiled,
  linked, and was unreachable — M5's bug six exactly. Also corrected four stale claims in WORK.md (M3's
  "only loose end", the `saveFormat` residual M6 step 5 fixed, cli named-binding, and the settled names
  question), and dated M5 slice 6's last two unconfirmed gui items — **no gui work is now unverified**.

### Update 2026-08-15 — M8 COMPLETE: map nodes (designed 2026-08-11, built + live-verified)

M6 split definition from evaluation so one definition could back N evaluations; M7 made that sharing
real for linked groups. **Neither has ever had the caller both were built for** — running one subgraph
once per element of a collection. **Milestone 8** brings it. Grilled 2026-08-11; decisions in
**[ADR-0014](docs/adr/0014-map-nodes-staged-planning.md)**, build order in WORK.md, vocabulary in
CONTEXT.md. **M8 is COMPLETE (2026-08-15)** — all six slices built, and **gui-mode live-verified by
the repo owner**: maps run, persist, and are reachable from the UI. It settles all four questions
ADR-0012 listed as *deliberately unsettled*, which it could only do once a concrete caller fixed them.
`ctest` **472/472**, warning-clean, format-check clean.

Both bugs the milestone produced came from the **gui seam**, none from the engine — the same
distribution M5 saw, and the reason that surface is driven rather than assumed.

- **First vertical: a folder of images, listed in-graph** (`listDir → map(load → tint) → combine`).
  Runnable headless on the existing `io::image`, and the smallest caller that still makes arity
  **data-dependent** — the collection is computed by a node *during* the run, which is the fact every
  decision turns on.
- **A collection is read through a `PortType` capability**, not a payload type flow knows: `element` /
  `size` / `at` / `gather` beside `describe`, filled from `meta::traits::is_vector_v` through the same
  per-type function-pointer bridge. `at()` uses `shared_ptr`'s **aliasing constructor**, so splitting a
  vector across N children copies nothing; `gather` cannot alias and costs N element copies.
- **The scheduler plans in STAGES.** `expand()` refuses to descend into a map whose arity is unknown —
  that map is a **frontier** — and `run()` loops plan → execute → prepare the now-known children →
  plan again. Each stage is still one flat DAG, so `lain::task` is untouched and ADR-0009's "no new
  substrate surface" holds; **the gap between stages is the second coordinator point** ADR-0012 left
  open, which keeps *"a worker task never grows evaluation storage"* literally true. A Taskflow
  subflow would have broken both rules at once.
- **`EvalPath` finally becomes `{NodeId, index}`** — the change CONTEXT.md has been holding a note for.
  A group is index 0; a map's index is **positional**, because position is the only identity a
  `std::vector` has. That reaches every flowview pane through `PinKey`, and the breadcrumb gains an
  element stepper.
- **Split vs broadcast is the port's own declared type** (`vector<T>` against an inner `T` splits, `T`
  against `T` broadcasts) — no flag, nothing stored. The same move M7 slice 1 made when it deleted
  `editableAt`: a remembered bool can disagree with what it describes.
- **One suppressed element clears the whole output.** A `std::vector<T>` has no hole, and gathering
  the survivors into a shorter vector would silently break the positional correspondence between input
  and output. `N == 0` is different — empty in, empty vector out, which is a value.
- **A map serializes its interface**, unlike a group whose ports are re-derived: its mirroring is
  under-determined by exactly one bit per input pin, so M5's "a group's own ports are never stored"
  does not extend to it. Reconciled on load by the rectification pass linked groups already use.
- **Accepted costs, all named in the ADR:** per-element incrementality is unavailable (any change
  rebuilds the whole vector, so all N children recompute), child state is retained per element, and
  the gather copies. The escape from the **first and the third** is the `Collection` payload behind
  the same `size`/`at`/`gather` interface — which is why that seam is an interface rather than a
  type. (Retention is untouched by it: that is N retained child evaluations of computed interior
  values, unrelated to how the input collection is stored.)
- Six slices, the two structural refactors landing as **no-behaviour-change** commits before the map
  exists: (1) the `PortType` capability, (2) staged planning with zero frontiers, (3) N children per
  node, (4) `MapNode` + the map steps, (5) serialization, (6) flowview + the example nodes.

**Slice 1 is built (2026-08-11).** The capability landed as designed — `element` / `size` / `at` /
`gather` filled by `if constexpr (meta::is_vector_v<T>)`, plus `PortValue::alias` over `shared_ptr`'s
aliasing constructor. Core only: no scheduler, node or serializer changed. `ctest` **443/443** (+13),
warning-clean, format-check clean, headless `run --example` unchanged.
- **`meta::vector_element_t`** joined `is_vector`, which had only answered half the question. Its
  header invites exactly this ("add traits here as a real consumer appears"); flow is the consumer.
- **A proxy container has no element to alias** — found while building, not in the ADR.
  `std::vector<bool>` packs bits, so `operator[]` yields a value and aliasing it would dangle. `at`
  branches on `std::is_reference_v<decltype(items[index])>` and copies for a proxy: general rather
  than a `vector<bool>` special case, and cheap. Refusing the type would have surprised whoever first
  registers a per-element flag list.
- **`gather` decides nothing** — a hole or a mistyped element yields an empty result, and what that
  *means* stays the map's call in slice 4. `gather({})` is an empty **vector**, not an empty slot, so
  "no elements" stays distinguishable from "no collection".
- **Both load-bearing properties are sabotage-verified.** Making `alias` copy fails the zero-copy test
  on its address comparison; making it non-owning fails the outlives test by reading freed memory,
  deterministically in both sections. The second is the one that matters: without the aliasing
  constructor, a child reading element 3 after the producer rebinds is a use-after-free.

**Slice 2 is built (2026-08-13).** `run` / `evaluate` are the plan → execute → re-plan loop, with
nothing yet able to raise a frontier — so behaviour is identical and the existing suite is the
regression test. `ctest` **444/444** (+1), warning-clean, format-check clean, headless unchanged.
- **`run` moved to the base and stopped being virtual.** A strategy now overrides only
  `executePlan(const Plan&)` — one stage, already built and ordered — so the staging loop, the run
  lease and all planning live in one place. A backend never plans, never takes the lease and never
  decides when the run is over. Nothing held a `Scheduler&` and only the two backends subclass it, so
  no call site changed.
- **The loop terminates on "nothing was DEFERRED", not on "the next plan is empty",** and that is the
  whole slice. A mapless run must build exactly one plan, or every run pays a second planning walk to
  learn there is nothing left — and an on-request source re-arms itself inside `compute()`, so a
  freshly built plan is *never* empty and the invocation would never return. A new `[staging]` test
  pins it over both strategies; sabotaging the rule makes it **hang**, which is precisely the failure
  it exists to prevent.
- `Plan::frontiers` (deferred maps, addressed `{definition, evaluation, node}` like a `Step`) is the
  signal, empty until slice 4; `runSteps` is the extracted serial walk shared by `SerialScheduler`
  and the pull path.

**Slice 3 is built (2026-08-13).** `Evaluation`'s children are now `map<NodeId,
vector<unique_ptr<Evaluation>>>`, addressed by *which node* **and** *which evaluation of it* —
`child(node, index = 0)`, `hasChild(node, index = 0)`, plus `childCount(node)`. A group is index 0.
`ctest` **446/446** (+2), warning-clean, format-check clean, headless unchanged.
- **All 25 call sites changed by zero lines**, which is what the default index buys.
- **`prepare` does NOT impose a count** — it keeps what is there and guarantees at least one. The one
  non-rename in the slice, and load-bearing: `prepare` runs at the top of every invocation, so
  "one child per graph-containing node" would reset a map's N children, and every element's retained
  values with them, on each run. Sabotage-verified — imposing a count makes a group's interior
  recompute on the second run, i.e. inner incrementality gone.
- Children are held indirectly because an `Evaluation` must not move when the vector grows: the
  scheduler holds child pointers in plan steps for a whole invocation, and slice 4 grows that vector
  between stages.
- **Deviation: `GraphPath` did not gain its index here**, as the plan said — it moves to slice 6.
  `child()` defaulting to 0 means flowview resolves unchanged, so the index has no caller until the
  element stepper varies it; adding it now would thread an always-zero field through `PinKey`, the
  layout tree and the breadcrumb, across the surface that produced M5's ten bugs, for nothing
  observable.

**Slice 4 is built (2026-08-14) — MAPS RUN.** `MapNode` beside `InlineGroupNode` / `LinkedGroupNode`,
lifted mirroring, per-element children sized between stages, the gathering exit, and the suppression /
ragged / `N == 0` rules — all driven through the production schedulers, serial and parallel.
`ctest` **456/456** (+10), warning-clean, format-check clean, headless unchanged.
- **`Node::evaluatesPerElement()`** is the structural seam's third question, beside `innerGraph()` and
  `innerPin()`. The scheduler asks the fact, not the class: the first draft used
  `dynamic_cast<MapNode*>` and was corrected, since ADR-0009's whole point is that a future
  graph-containing node needs no scheduler change. *(Renamed `interiorEvaluation()`, returning an
  enum, in M11 slice 1 — see the 2026-09-05 update.)*
- **Lifting needs the port-type REGISTRY — an addition the ADR did not anticipate.** A `PortType`
  knows its element type, but nothing walks that backwards (naming `std::vector<T>` needs `T` at
  compile time, and mirroring has only a runtime type). `registerPortType<std::vector<T>>` now also
  records itself as `T`'s list form, and `listTypeFor` is what `MapNode::exposePort` lifts through.
  **A type is mappable exactly when its list form is registered** — already required by ADR-0014 for
  a collection pin to serialize. An unregistered one is refused, not silently mirrored un-lifted.
- **`GroupNode::exposePort` is virtual**, so `edit::syncGroupPorts` needs no idea which kind it holds.
- **There is no MapEntry step.** Binding happens where the children are sized — between stages, on the
  coordinator thread — so a map's plan is N × its interior plus a `MapExit`. A map is never consumed
  within a stage it is expanded in, because it would have been deferred if a predecessor were running.
- **A map defers exactly when a group would republish**, plus "not already prepared this invocation".
  The first half keeps an edit *inside* a map at one stage; the second is what terminates the loop.
- **`exitMap` re-asks whether the map could run** instead of remembering: that is what keeps zero
  children unambiguous — an empty collection gathers to an empty vector (a value), an undeterminable
  arity produces nothing.
- **Four sabotages, all caught**, one of them by hanging (dropping the prepared guard makes a map
  defer forever). **Nested maps tested, not assumed** — each row is its own evaluation of the inner
  map with its own element count, which is why a frontier is addressed `{definition, evaluation,
  node}`.

**Slice 5 is built (2026-08-14).** A map writes its recipe as a nested body like an inline group,
**plus its own `interface`** — the one group kind whose ports are stored, because M5's "a group's
ports are re-derived" rule is under-determined by one bit per input pin, and derivation alone would
turn every broadcast back into a split. `ctest` **460/460** (+4), warning-clean, format-check clean.
- **The stored thing is the port's TYPE, not a flag.** On load the stored key is compared against the
  inner pin's own key and its list form's — the pin's type means broadcast, the list type means split
  — so no second field can disagree with the port it describes.
- **Restoration runs BEFORE `syncGroupPorts`**, which is the whole mechanism: sync mirrors by
  `PortId`, so a restored pin is left alone while a pin that appeared since is added at the default.
  The document wins, the interior fills the gaps.
- **Rectified like a linked group's cache:** a stored port whose pin has vanished is dropped *and
  reported*; a type matching neither form is re-mirrored at the default and reported. A bad entry
  costs its own port, not the node. A document with no `interface` mirrors at the default.
- Verified on **values**, not structure: the round-trip runs to the same result (a broadcast that came
  back lifted would fail to reconnect and suppress the map), save ⇒ load ⇒ save is byte-identical, and
  ignoring the stored interface fails 3 of 4 cases.

**Slice 6a is built (2026-08-14)** — the last slice is three commits (the mechanical widening, the
driver-free proof, then the gui), as WORK.md recommended. A path step is now
`PathStep {NodeId node; std::size_t element = 0;}`: the `{NodeId, index}` step ADR-0012 named and
CONTEXT.md had been holding a note for. Everything resolves at element 0, so behaviour is unchanged
and the existing suite is the regression test. `ctest` **461/461** (+1), warning-clean, format-check
clean, headless unchanged.
- Deferred from slice 3 so it would land beside its first caller: 16 files touch `GraphPath`, and
  mixing that sweep with the UI is what slice 2 exists to avoid.
- **Only the EVALUATION walk uses the element.** The graph walk reaches one definition however many
  elements run over it, and the **layout tree stays keyed by node alone** — an arrangement describes
  the definition, and every element of a map shares one interior. Keying layout per element would give
  each element its own canvas positions.
- `GraphPath` is runtime-only (the session persists file paths, not this), so nothing on disk changed.

**Slice 6b is built (2026-08-14) — the milestone's end-to-end claim is PROVEN.** `flow-example` gains
`ListDirNode` (a directory → one sorted `std::vector<path>`) and `CombineNode` (N images → their
pixelwise mean), and `flowview run` over a real folder lists the files, maps `LoadImage` over them and
averages — through the production save/load facade. `ctest` **464/464** (+3), warning-clean,
format-check clean.
- **`ListDir` is what makes arity genuinely data-dependent**, which is the fact the whole staging
  design rests on. A missing folder yields no value (suppression); an empty one yields an empty list
  (a value) — ADR-0014's `N == 0` distinction, arriving from the other end.
- **`LoadImageNode` gained an Optional `path` input** overriding its param: a param is per-node
  configuration and every element of a map shares one definition, so a per-element path must arrive
  as a value. Same shape as a Select's connectable `selector`.
- **`CombineNode` needed no engine support** — it just declares a vector-valued input, exactly as
  CONTEXT.md's "Port arity" always said. Only looking *inside* a collection ever needed anything new.
- A collection now describes itself as **"3 items"** instead of its bare type name — what the
  Inspector, tooltips and cli dump show for every port a map has.
- **A real bug, found only by the end-to-end scene:** on a later run where only the INPUT changed, a
  map whose new arity is **zero** was never selected in the next stage, so its exit never ran and it
  kept serving last run's collection. `prepareMap` assumed a recompute request was still standing —
  true on a fresh evaluation, false afterwards. With elements left, binding them drags the map in as
  stale through the recursive check, so **only the drop to zero exposes it**; every unit test had
  masked it by dirtying everything or running once. Now requested explicitly, with a regression test
  at that exact shape.

**Slice 6c is built (2026-08-14) — gui-mode NOT eyeballed, no Metal in this sandbox.**
*(**Superseded** — that eyeball happened; see "Live-verified 2026-08-15" at the end of this entry,
after which no gui-mode work was outstanding for M8.)* `ctest`
**466/466** (+2), warning-clean, format-check clean, both headless paths unchanged.
- **Found first, and it would have made `Add ▸ Map` useless:** `resolveEditable` and `syncPathGroups`
  both `dynamic_cast<InlineGroupNode*>` to mean "owns a mutable interior". A map owns one but is not
  an inline group, so **every pane would have been read-only inside a map** — one you could add,
  descend into, and never build — and an interface edit inside one would never have reached its outer
  ports. M5's bug three, map-shaped. Answered structurally with **`GroupNode::editableInner()`**, a
  virtual the host asks instead of testing for a class.
- **`Add ▸ Groups ▸ map`**; **a breadcrumb element stepper** (`< [3/12] >`) on a map crumb, reading
  `Crumb::perElement` (the definition says it is a map) and `groupnav::pathElementCounts` (the
  evaluation says how many) — two owners, because a map's arity is not in the recipe; and **an Issues
  row for a hole that navigates to the failed element** ("3 of 12 elements produced no 'image' — the
  whole output is cleared (first: element 2)"). One row per output, not per element, so a broken
  folder cannot bury the panel.
- **`Issue` grew named constructors and lost its public one.** Adding a fourth field to a plain
  aggregate produced six positional `{…, {}, {}}` sites across four files — the symptom, not the
  problem. An Issue has three shapes: `note` (nothing to point at — seven of ten sites), `at` (a node
  on this level), `inside` (another level: a map's failed element). The last two are ALTERNATIVES, and
  as an aggregate a caller could set both and have the node silently ignored; now that shape cannot be
  written, and the next field touches no call site.
- **First live bug — a SEGFAULT leaving a map via the breadcrumb** (2026-08-14). The strip computes
  its crumbs from `ctx.activePath`, then draws them — but a crumb click calls `navigateTo`, which
  **replaces the path mid-loop**. Clicking the document crumb empties it, and the map crumb behind it
  read `activePath[0]` of an empty vector. The stepper was the first code in that loop to READ the
  live path rather than only slice it on click, which is why the pattern had never bitten. Fixed
  structurally: **`GraphPane::draw` snapshots the path once** and draws the whole strip from it, so a
  pane cannot read navigation state it may itself have changed — the lesson `MainWindow` already
  learned one level up.
- **Found auditing for the same shape:** `MenuBarPane::documentToSave` captured the canvas layout into
  `ctx.activePath` while `MainWindow` correctly used the drawn path — two paths doing one job.
  Navigating and saving in one frame would file one level's positions under another's key.
  **`AppContext::drawnPath`** is now the single answer to "which level is on screen".
- **Second live report — `ListDir` could not pick a folder, and its settings were params only.** The
  path editor **hard-coded an image filter**, with a comment already admitting the gap ("a per-value
  filter hint is a future refinement if a non-image path value appears"); a `std::filesystem::path` is
  legitimately a file OR a folder and the type does not say which, so it now offers **both `File...`
  and `Folder...`**. New **`gui::selectFolder`** (pfd had it, lain never exposed it). `ListDir`'s
  `directory` and `extension` became **Optional input pins** overriding their params, with
  **`constPath` / `constString`** added to the palette so there is something to wire — a param cannot
  be driven by the graph, and "which folder" is exactly what a caller supplies from outside. The rule
  is stated once in **`flow/example/setting.h`** (*wired wins, unconnected the param stands*), shared
  with `LoadImage::path`.
- **Input defaults (2026-08-15)** — the `setting.h` helper the ListDir fix introduced was a symptom:
  a node wanting both spellings declared a param and an Optional input of the same name and reconciled
  them by hand in three places. Now one declaration in core, **`addInput<T>(name, Default{value})`**,
  with `Node::defaultOf(port)` pairing the halves — **amending ADR-0005 in place** with the reason
  maps supply (one definition, N evaluations, so a per-element setting cannot be a param). The default
  IS a param underneath, so it serializes and **existing documents load unchanged**. It seeds an input
  with **no incoming edge**, and such an input stays **Required** — sabotage-verified, because a
  default that filled any empty slot would let a Gate turned off be replaced by it instead of
  suppressing (ADR-0007). No new noun: CONTEXT.md already listed "setting" among the words to avoid
  for this family. The Inspector shows *"driven by input"* where the editor would be while a pin is
  wired.
- **Live-verified 2026-08-15**: `Add ▸ Map`, descending, building a map's interior, the element
  stepper, Issues rows navigating to a failed element, the folder picker, and the Inspector's
  *driven by input*. No further gui-mode work is outstanding for M8.
- **`GateNode`'s `enable` now defaults to true** (2026-08-15). An unwired gate is TRANSPARENT rather
  than a dead end — before defaults existed it was a plain required input, so a gate dropped on the
  canvas was never ready and suppressed everything downstream until something fed it, which read as
  broken. `value` deliberately has no default: it is the data, and one would let a gate emit a value
  nothing gave it. **Changes an existing document**: a saved graph with an unwired gate now passes
  through rather than suppressing — accepted, since such a gate could do nothing useful before.
  **`SelectNode`'s `selector` became `Default{0}`** in the same pass, moving "unwired means branch 0"
  out of a `compute()` presence check into the declaration — and changing one case for the better: a
  wired-but-suppressed selector now suppresses rather than quietly routing to branch 0.
- **`BlurNode`'s `radius`/`sigma` became defaulted inputs** (+ `constFloat` in the palette to drive
  `sigma`). **The data input now declares FIRST**: an input's position is how tests and hand-built
  graphs address it, so declaring settings first silently retargeted `connect(src, 0, blur, 0)` onto
  `radius` — caught by the suite. Documents were never at risk (edges serialize by port NAME).
- **Param audit (2026-08-15).** Four params existed in production nodes; two converted (Blur's).
  **`ConstantNode::value` stays a param permanently** — a Constant's job is to BE a source, and an
  input would make it a pass-through needing a source of its own. **`TintNode::tint` stays for now
  for a concrete reason**: `image::ColorRGBf` is not a registered port type, so the pin could not be
  wired by anything and would be dead; it wants `registerPortType<ColorRGBf>` plus a colour Constant
  first. **Merge/Select branch pins keep `setRequired(false)`** and must — a Merge forwards the first
  LIVE branch, so defaults on all of them would defeat it. The rule: convert when the value plausibly
  varies per element AND something can drive it; each conversion costs a pin on the canvas. That surface produced all ten of M5's bugs while the engine slices produced
  none, and has now produced its first here — expect more there rather than in the engine.

### Update 2026-08-19 — M10 designed: video + frame sequences (nothing built yet)

The ask was "load and save video the way images already load and save". Grilled 2026-08-19; decisions
in **[ADR-0018](docs/adr/0018-frame-sequences-and-host-driven-rendering.md)** (the model) and
**[ADR-0019](docs/adr/0019-ffmpeg-lgpl-for-video-codec-support.md)** (the backend + licence
exception, with ADR-0015 amended), build order in WORK.md's **Milestone 10**, vocabulary in
CONTEXT.md's new *Frame sequences* section. **Numbered after M9 but built before it** — M9's
prerequisite note already pointed here. **Nothing is built.**

- **The image parallel holds at the registry/plugin/facade level and breaks in exactly two places**,
  which is most of the design. Transport: `io::read` is a whole-asset slurp and `ImageReader::decode`
  takes the whole `Buffer`, so M10 builds the **`Stream`** transport WORK.md has had queued for
  precisely this reason. Writing: an encoder is open-push-finalise and its file is invalid until
  finalised, so the writer is a **stateful handle**, not `encode(asset) → Buffer`.
- **A `FrameSequence` is a LIST of frame references over one or more sources**, not a decoder chain.
  Clip/concat/select become list operations with exactly one class decoding, and a multi-file timeline
  is free — which is CONTEXT.md's *media segment* / *camera-sequence definition* realised rather than
  a new concept. **Position and frame identity separate**: a clip re-bases the first and preserves
  the second.
- **A processing graph structurally CANNOT output a `FrameSequence`.** A frame ref names a source and
  a computed frame has no source — so sequence outputs are limited to *selection* (which is exactly
  **calibration view selection**) with no rule to police. Carrying processed frames would need a
  graph-backed source whose decode re-enters the scheduler: the fire-and-join deadlock.
- **The host owns the frame loop; a render is a FOLD, not a map.** A map over 500 frames at 4K is
  ~16 GB gathered plus ~50 GB retained per-element interior (ADR-0014's own recorded costs; M9's
  prerequisite note reached this independently). The frame position arrives as an **ordinary boundary
  input** — M4 binding, ADR-0012 recompute, incremental eval, unchanged. An ambient time cursor was
  rejected: it breaks *"a node reads only its evaluated inputs"*, needs a per-node time-varying flag
  that can disagree with what the node reads (M7/M8 both deleted exactly that shape), and has no
  answer inside a map. **The map stays right for small-N analysis** — calibration's selected views,
  feature tracks.
- **`FramePosition` is a distinct port type**, so the cli finds the loop counter with no naming
  convention and an editor can render a timeline *by type* (ADR-0005's rule). Deliberately **not**
  `FrameIdx`: nothing here abbreviates, and *index* is a spent word (CONTEXT.md's *Position*).
- **No decoded unit aggregate.** `FrameAt` emits `image : Image` + `frame : FrameRef` on separate
  pins. The tempting `Image + intrinsics` pairing is refused by the glossary itself — camera-model
  stability says one model applies *throughout a sequence*, so that value belongs to the sequence.
  `FrameRef` is `{canonical uri, ordinal, timestamp}`: a **name you look up**, never a back-pointer
  that would pin a decoder for as long as any evaluation holds any frame.
- **A sequence is homogeneous (one frame spec, mismatches refused at composition), frame-tabled at
  open** (container index, else a decode-free demux scan → exact count, exact seek, VFR for free,
  deterministic identity), and **lazily decoded behind a mutex with a fixed ring**, blocking. The
  cache/pool wording in CONTEXT.md was already written to permit the later swap.
- **`ColorSpace` gains `BT709`**; BT.601/BT.2020/HDR are **reported and refused**, not relabelled.
  Tagging footage `sRGB` is a silent error in every blend; `Unspecified` makes it unprocessable under
  op-class enforcement. Governing principle the owner set: **no silent lossy conversion; codec loss
  is expected.**
- **FFmpeg, LGPL configuration only, found-not-fetched, opt-in** — and **enforced**: configure fails
  if the linked library reports `--enable-gpl`/`--enable-nonfree`, since a GPL-configured build
  relicenses the combined work whether or not a GPL codec is called. Delivery codecs use **platform
  hardware encoders through FFmpeg's wrappers** (LGPL-safe); never x264/x265. Platform-native backends
  were rejected mainly because **frame-accurate seek differs per backend**, and "frame 412" must mean
  frame 412 everywhere.
- **Slices 1–2 (`lain::media` + the image-sequence source, then the flow integration) are a complete
  dependency-free vertical** — a folder of stills swept to `out.####.png` in bounded memory exercises
  every decision before FFmpeg or `Stream` exist. flowview is last, and lands the deferred **GUI view**
  registry with two customers (`FrameSequence` → player, `Image` → the hardcoded thumbnail branch).

### Update 2026-08-31 — M10 build order revised: FFmpeg is fetched, and slice 0 is new

[`luckyneko/ffmpeg-prebuilt`](https://github.com/luckyneko/ffmpeg-prebuilt) now publishes
**tier-verified LGPL shared** FFmpeg archives for `macos-arm64` / `linux-x86_64` / `linux-arm64` /
`windows-x86_64`, each with a `MANIFEST.txt` stating tier, full configure string and
corresponding-source url. That changes three things in M10's plan and nothing in its model. Build
order in WORK.md; amendments in ADR-0019, ADR-0018 and ADR-0004. **Slices 0, 1 and 2 are built
(2026-08-31) and slice 3 on 2026-09-01 — the milestone's central claim is proven end to end; slices
4–7 are not.**

- **Fetched, not found — so ADR-0019's ordering risk is retired.** The objection was to building
  *autotools*, not to fetching; a release **archive** fits the `cmake/addXXX.cmake` FetchContent
  idiom exactly. The macOS gap that ADR flagged as "should be confirmed early" is why the prebuilt
  repo exists, and is now closed. **New slice 0** lands `addFFmpeg.cmake` with nothing depending on
  it yet: it is now the cheapest way to retire the last unknown in the back half.
- **The tier gate parses `MANIFEST.txt` instead of a `try_run`** — it executes nothing, so it works
  when cross-compiling, which is exactly when a consumer can least inspect what it linked. The
  compiled probe survives as a permanent `[video]` **runtime** test, and as the only cover for a
  system FFmpeg someone points `LAIN_FFMPEG_ROOT` at.
- **lain's first shared-linked dependency** (ADR-0004 amended). LGPL relinking is satisfied by
  dynamic linking alone; a static tier would additionally owe relinkable object files. Nothing of
  lain's crosses the boundary, so the ADR's fragmentation concern is untouched — the costs are an
  rpath, and a DLL copy on Windows. **The LGPL obligations are discharged in slice 0, not deferred
  to a packaging story lain does not have:** notices staged beside the binaries, a `THIRD-PARTY.md`
  checked against the fetched manifest at configure time so it cannot go stale, and `--licenses` on
  `lain::app` beside the `--version` it already owns.
- **One `openSequence` node over an opener registry**, settling a disagreement already in the docs
  (WORK.md said `OpenSequenceNode`, ADR-0018's prose said `OpenVideo`). Disabling video must remove
  a *capability*, not a graph's vocabulary: an unknown `kind` drops the node **and its edges**, so a
  video document would come back structurally damaged on a build without the plugin. The registry
  is a small `libs/io/media` facade, since `lain::media` depends on no `io`.
- **Two colour details settled in ADR-0018.** `convert(img, Linear)` applies the **inverse Rec.709
  OETF** (the curve the encoder applied; what "Rec709 to linear" means in Nuke/OCIO) — not the
  BT.1886 2.4 EOTF, and no OOTF. And since real footage very often carries **no colour tags at all**,
  the BT.601/2020/HDR refusal applies to **explicitly tagged** material only; unspecified is treated
  as BT709 with a log line, rather than guessed as BT.601 by frame size and rejected.
- **Slice 0 built 2026-08-31.** `cmake/addFFmpeg.cmake` (hash-pinned fetch + imported SHARED
  targets), `plugins/io/video/ffmpeg` (the licence probe today, the reader at slice 5), the
  generated `--licenses` notice, staged licence texts, README row. `LAIN_IO_VIDEO_FFMPEG` defaults
  **OFF**; `ctest` **476/476** with it on. The configure gate is **sabotage-verified** on all three
  refusals, and a manifest-less root warns rather than passing quietly. Two things worth carrying
  forward: **CMake derives the build rpath from linked imported shared libraries**, so macOS/Linux
  needed no manual rpath — proven by the test running at all, since an executable that cannot
  resolve `@rpath/libavutil` does not launch (Windows still needs an explicit DLL copy); and the
  licence test **caught a trap in itself** — `avutil_license()` returns `"LGPL version 2.1 or
  later"`, which *contains* `"GPL version"`, so only a **prefix** test separates the five strings it
  can return. Known limitation, recorded in WORK.md: the notice is build-level, so `flowview
  --licenses` names FFmpeg before flowview links it (true from slice 5).
- **Slice 1 built 2026-08-31 — `lain::media` + `io::image::openSequence`.** The frame-sequence
  model (`FrameRate`, `FrameSpec`, `FrameRef`, `FrameSource`, `FrameSequence`, and clip / concat /
  reverse / stride / select), plus a folder or `####` pattern of stills opened as a real sequence.
  No new third-party dependency. `ctest` **506/506** (+30). Three things worth carrying forward:
  **`log::ensure` is a PRECONDITION helper that debug-aborts**, so it is wrong for every refusal in
  media — a graph binding a position past the end, a folder holding one stray odd-sized still — all
  of which are data, not broken invariants, and all of which ADR-0018 answers with an invalid Image;
  `lain::image` uses `ensure` correctly for op-class enforcement, and the two look alike.
  **The rate is the one spec axis that tolerates absence** (geometry and colour tags must match
  exactly; an unspecified rate adopts the other side's), which is what makes ADR-0018's "a video
  file and a folder of stills" true rather than a refusal. And **`lain::io::canonicalUri` was
  hoisted out ahead of its second caller**, because a FrameRef names its source by uri alone, so
  two spellings must give one string — the `graphio::templateKey` rule, kept to one function before
  the image and video openers could each grow their own. No public `FrameTable` type yet: offset
  and keyframe are video-shaped, so slice 5 builds one privately behind `frameCount()` /
  `frame(ordinal)`.
- **Slice 2 built 2026-08-31 — a folder of stills renders to `out.####.png` in bounded memory.**
  `FramePosition`, `lain::io::sequence` (the opener seam), `OpenSequence` / `FrameAt` /
  `ClipSequence`, the port types + codec + binders, the manifest writer, and the `run` sweep with
  `--frame` and `--on-missing-frame`. `ctest` **526/526**; the sweep, the stop policy (exit 1, four
  of six frames) and skip (exit 0, a visible gap) were driven through the real binary. Carry
  forward: **the seam is `lain::io::sequence`, not `io::media`**, because inside a namespace called
  `media` that name shadows `lain::media` everywhere; **a thin facade now, the registry at slice 5**
  (the repo owner's call), so no registry-with-one-member exists; **`FramePosition` must be a
  struct**, since every payload registry is `type_index`-keyed and an alias for `std::size_t` would
  collide in all four at once; and **there is deliberately no `serialize(Archive&, FrameSequence&)`**
  — `Archive` is direction-agnostic, so one would make `fromValue` compile and silently yield an
  empty sequence, hence a one-way `sequenceManifest()` instead.
- **Two hazards the slice exposed and fixed.** `bindDefaultInput` bound `inputs[0]` whatever pin it
  was asked about, and `Evaluation::bind` type-checks nothing — so a second boundary input would
  have let an unbound frame position silently re-bind the image pin. And `--frame` collides with the
  natural name for a frame-position pin; it does not matter, because **the sweep finds the pin by
  TYPE** — which is the entire reason ADR-0018 made `FramePosition` distinct. `--frame 5` is a
  one-frame range, and is the only cli spelling for a position.
- **New in `lain::app`: `onProcess` returns its status, and `Application::exit(code)`.** The stop
  policy needs a non-zero exit. The first attempt added `setExitCode` and argued that changing a
  public virtual cost more than it was worth; **review overturned that** — only 2 of 6 delegates
  override `onProcess`, and a one-shot work routine returning its own status cannot be forgotten.
  `quit()` already existed, so `exit(code)` is that request with a reason and `quit()` is now
  `exit(0)`.
- **Accepted cost, not claimed around:** `bind` marks the `GroupInputNode`, not the pin, so every
  node fed by *any* boundary pin recomputes each frame — harmless for this shape (a `FrameSequence`
  copy is a refcount bump), but it is not "only the changed cone recomputes" until measured.
- **Slice 2 review pass (2026-09-01), four objections upheld.** `ctest` **534/534**; the sweep and
  both missing-frame policies re-driven through the real binary unchanged. **`--frame` is now a
  typed option** — `core::Range` carries its own literal form the way `core::Version` does and
  provides CLI11's `lexical_cast` hook, so core names nothing of CLI11 yet a malformed range is
  refused *by the parser* before a graph loads. `Range` needed the name, so the planned param-editor
  slider is renamed **`Bounded<T>`** in CONTEXT.md. **The manifest moved to a new
  `libs/media/serialize` target and became declarative** (`LAIN_SERIALIZE`), which is what ADR-0018
  asked for all along — *"not a bespoke writer"* — with `lain::media` still linking no `data`.
- **Three traps worth carrying forward, now in CONTEXT.md as the boundary rule's corollary.** A
  `serialize` for `math::Vec2i` written in `lain::math` **compiles and is never found**, because
  `Vec2i` is a `using` alias for `glm::ivec2` and ADL associates `glm`; one in `namespace glm` is
  global to the program instead. A **namespace cannot share a name with a function in the same
  scope**, so `lain::media` keeps the `serialize()` free function and only the TARGET is
  `lain::media::serialize` (as `lain::io::image::codecs` already does). And **`media::select(seq,
  Range)` was built and backed out**: it makes `select(seq, {1, 10, 2})` ambiguous, for an operation
  with no production caller.
- **Verified against the published artifacts, not assumed.** Delivery encoding is genuinely
  platform-conditional (macOS videotoolbox; Windows Media Foundation + NVENC; Linux VAAPI /
  V4L2-M2M / NVENC), while **decoding is uniform** — so slice 6 selects an encoder by availability,
  never by a hardcoded name. Also: the prebuilt is `--disable-network`, which makes the `Stream`
  seam *more* load-bearing than ADR-0018 argued, since a future `s3://` reaches the decoder through
  lain's transport or not at all.

### Update 2026-09-01 — M10 slice 3 built: the `Stream` transport

`lain::io::Stream` — the **incremental, seekable** transport a lazy frame sequence needs, since
`read(uri) → Buffer` is a whole-asset slurp. `ReadStream` / `WriteStream` over one local backend
(`libs/io/src/localstream.{h,cpp}`, private like `scheme.h`), and **`io::read` / `io::write`
reimplemented as the whole-asset use of it** — so a scheme has one backend, not two that can drift,
and the slice is on the production path three slices before its intended consumer (slice 5's
`AVIOContext`). `ctest` **549/549** (+15), warning-clean, format-check clean; slice 2's sweep
re-driven through the real binary with its four outputs byte-identical to their four inputs, in order.

- **The backend hook is POSITIONAL** — `onRead(at, …)` / `onWrite(at, …)` are handed the absolute
  position because the BASE owns the logical one. That makes *"transparently reopen and re-seek"*
  structural rather than a rule each backend remembers, which is what lets a future LRU **handle pool**
  evict between two reads with nothing above noticing; a seek touches no backend at all, so a released
  stream stays released through one. Sabotage-verified: a backend trusting its own physical position
  fails the release-and-resume test and two seek tests.
- **The write direction landed here, not at slice 6** (settled with the repo owner). Slice 6's muxer
  needs an AVIO *write* callback for exactly ADR-0004's reason the reader needs a read one, and
  `io::write` gives it a caller now. What its consumer would have pinned is pinned anyway: a muxer
  **seeks back to patch its header**, so a `WriteStream` is seekable and not an append-only sink.
- **A write re-acquire must NOT truncate** — the one silent data-loss failure in the slice, and
  sabotage-verified. `createStream` creates/truncates once (`io::write`'s own contract); every
  re-acquire after `release()` opens `in|out`, which is also what lets a positioned write land back
  over earlier bytes.
- **`finish()` is explicit and status-returning** (ADR-0018: a failure in a destructor has nowhere to
  go) and idempotent; an unfinished stream still flushes and closes, so it loses the *report*, not the
  bytes — pinned by its own test rather than left as a comment. **EOF and failure stay distinct** for
  the same reason: a decoder that reads a lost handle as a clean end truncates the asset and reports
  success.
- No `flush()`, no scheme registry — one member is not a registry (slice 0's aggregator, slice 2's
  opener facade, same judgement). `uri()` is `io::canonicalUri`, because a stream that reopens by a
  relative path is one `chdir` from a different file.
- **`io::localPath(uri)` — one conversion, and it fixed a latent bug.** Turning a uri into a path is
  a one-liner, which is why it kept being written by hand; the copies had already drifted, and
  `openSequence` built its `fs::path` from the **whole** uri, so `s3://bucket/frames` became a
  relative directory named `s3:` that the opener asked the working directory about. It returns
  `std::optional<std::filesystem::path>`, so a caller that cannot serve a remote resource has to say
  so, and `openStream` / `createStream` now treat "no local path" AS the unsupported-scheme branch
  rather than deciding the same thing twice.
- **Queued, not built: `core::Uri`** (WORK.md M10, *Queued: `core::Uri`*). It must **not** be an RFC
  3986 parser — `shot.####.png` is a fragment to a conforming one and `C:\clip.mp4` is scheme `C` —
  its home is `core` (because `media::FrameRef` needs it and media links no io), and it carries no
  path algebra. After slice 5, with an ADR.

### Update 2026-09-01 — M10 slice 4 built: `ColorSpace += BT709`

`lain::image::ColorSpace` gains **`BT709`**, so slice 5's decoder has an honest tag for footage
whose transfer is not sRGB — the alternative being a silent ~20% error at mid-grey in every blend
(tag it `sRGB`) or frames op-class enforcement refuses to touch (tag it `Unspecified`). The curve is
the **inverse Rec.709 OETF** per ADR-0018 with the spec's **rounded** constants
(4.5 / 0.018 / 1.099 / 0.099), matching how sRGB is already spelled here. `ctest` **560/560** (+11),
warning-clean, format-check clean. No gui surface, no FFmpeg involvement — this builds and passes in
the default configuration.

- **The public surface is two functions, and the commit is net-negative in code.**
  `image::toLinear(ColorSpace, float)` / `fromLinear(ColorSpace, float)` in `colormath.h`, with the
  four curves private in `details/colormath.inl` (`inline`, not a new `.cpp` — they run per colour
  channel of every converted image). `convert` **composes through Linear**, so the ordered pairwise
  if-chain and its `ensure(false, "…not supported")` bail arm were **deleted** rather than grown
  from two arms to six; that arm is now unreachable by construction. It is also one pass, not two —
  the intermediate stays a float inside a single channel visit, so an 8-bit sRGB→BT709 conversion
  quantises once where chaining two `convert` calls quantises twice. **ADR-0003's "adding a color
  space is additive" bullet is amended in place** with what actually made it so.
- **`toLinear`/`fromLinear` deliberately echoes `detail::toUnit`/`fromUnit`** in the same file — the
  same "convert to and from a canonical intermediate" job. `encode`/`decode` was rejected because
  CONTEXT.md has both words spoken for by the codec seam (`ImageReader::decode`,
  `encode(asset) → Buffer`).
- **They are the enum's ONLY exhaustive `switch` in the tree, deliberately.** Every other
  `ColorSpace` site is an `==`, so before this a new enumerator was invisible to the compiler and
  had to be found by reading. Adding one now fails with `-Werror,-Wswitch` — verified by adding one
  and watching it break, not assumed.
- **`Unspecified` passes values through:** `colormath.h` is public and `lain::log` is PRIVATE to the
  `image` target, so it cannot assert. A total-function fallback, not a meaning — `image::convert`
  rejects it one level up, where the logger exists.
- **Op-class enforcement needed no code change** (`ensureBlendable` tests `== Linear`, which already
  excludes BT709); it needed tests. "value-blending ops require Linear" now loops over every
  non-Linear space instead of naming sRGB, so the next standard joins the enforced set rather than
  slipping past a check written before it existed.
- **Three sabotages, all caught.** Aliasing the BT709 curve to sRGB's fails the load-bearing
  "decodes differently" case (0.2596 vs 0.2140 at mid-grey); making `fromLinear` the identity fails
  every round-trip **including the pre-existing sRGB one**; letting BT709 past `ensureBlendable`
  fails the `[ops]` case under `NDEBUG`.
- **Two pre-existing traps now stated rather than left silent.** `convert(src, ColorSpace)` never
  reads `alphaMode()` — a nonlinear curve on Premultiplied colour is wrong, unguarded, and dodged
  only by the convention `BlurNode` follows; it is now a documented limitation on `convert.h`, since
  changing it would move existing results inside a commit whose claim is that sRGB↔Linear did not.
  And `pngreader`'s gAMA ≈0.45 branch still returns `sRGB` though 0.45 is the Rec.709 exponent — a
  gamma hint is not a Rec.709 tag.
- **Found while verifying, NOT fixed (pre-existing, unrelated): the tree does not build in
  Release.** `libs/memory/src/alloc.cpp:11`'s `isPowerOfTwo` is used only by a debug assert, so
  `NDEBUG` makes it dead and `-Werror,-Wunused-function` fails. That means **every `#ifdef NDEBUG`
  enforcement test in the repo is currently unreachable**, so that whole class has not run in some
  time. This slice's guarded cases were exercised in a throwaway Release build with the warning
  downgraded and pass. The fix is a one-liner and wants its own commit.

### Update 2026-08-03 — M6 step 5 built: host keys carry their level (**M6 COMPLETE**)

The last step of Milestone 6. `PinKey` — the key the preview cache, Inspector, Interface, Preview
pane and `saveFormat` all share — became `{GraphPath path, PortAddress port}`: *which evaluation,
which node, which port*, three real axes instead of a key that said less than it meant.
`ctest` **398/398**, warning-clean, `format-check` clean, and **gui-mode live-verified by the repo
owner 2026-08-10** (the panes that build these keys all changed). **M6 is complete.**

- **Why it matters even though node ids are already unique.** ADR-0011 stopped ids repeating across
  *levels*, which is what caused M5's bug nine (previews showing another level's images). But two
  **evaluations of one definition** — the target workload, N streams through one subgraph — have the
  same node and port ids *by design*. So the level belongs in the key however unique NodeIds get.
- **The direction bool is gone.** A `PortId` is minted per node across both sides (step 2), so a
  `PortAddress` names one port unambiguously and the flag was redundant.
- **`EvalPath` is spelled `GraphPath`.** A group has exactly one child Evaluation, so the graph walk
  and the evaluation walk are the same sequence of group NodeIds; a parallel alias for an identical
  type would be two names for one thing. A **map node** (one child per element) is what makes them
  differ — noted in `pinkey.h` so the absence reads as a decision rather than an oversight.
- **Behaviour is deliberately unchanged.** The cache still holds only the level on screen and still
  clears on navigation — but as an explicit MEMORY choice (a texture per image port per visited
  level, for a cache only one level reads), not as the thing standing between the user and a wrong
  image. Retaining every level is now available; it would want `refreshIfDirty` to walk all levels
  rather than prune everything outside the active one.
- The hold on leftover group GUI work is **lifted**: it was waiting for these panes to settle.

### Update 2026-08-03 — M6 step 4 built: `Evaluation` — the recipe apart from its runtime state

**The milestone's centre of gravity.** A `Graph` was simultaneously a document, an evaluation cache
and dirty bookkeeping; now it is only the first. Runtime state lives in a host-owned `Evaluation`, and
the scheduler takes both: `run(const Graph& definition, Evaluation& evaluation)`. `ctest` **394/394**
(the flow suite repeated 5x clean), warning-clean, `format-check` clean, headless round-trip still
byte-identical, and **gui-mode live-verified by the repo owner 2026-08-03** — every value-reading pane
changed where it reads from, so that was the slice's real risk; it produced no bugs. See WORK.md
step 4 for the full landing notes.

- **`libs/flow/evaluation.h`** — `Evaluation` (move-only, core-only, needs just `PortValue`) owns
  per-node values, `computedAt` versions, recompute requests, boundary bindings, and one child
  Evaluation per group node. `NodeEvaluation` is the per-node view `compute(NodeEvaluation&) const`
  gets: its own inputs (read-only) and its own outputs, and nothing else.
- **`Port` is pure declaration** — `{id, name, direction, PortType, presence}`. `value()`,
  `set`/`get`/`holds`, `ready()` and `describe()` all left, taking `details/port.inl` with them.
  Readiness followed the values (`evaluation.ready(node)` / `nodeEvaluation.ready()`, and
  `hasValue(PortAddress)` for port presence), which also retired the collision where `Port::ready()`
  meant *has a value* and `Node::ready()` meant *all required inputs do*. Rendering stayed a
  `PortType` capability, applied to an evaluation value: `evaluation.describe(port)`.
- **Staleness is a per-node version comparison**, not a shared `m_dirty` bool — a bool could only ever
  describe one run, so it could not serve two evaluations. `Node::version()` is bumped by `setParam`
  and by Graph's structural primitives; an evaluation records what it computed each node at.
  **Invalidation is PULLED**: the definition holds no list of its evaluations, so an edit cannot walk
  them — it bumps a version and each notices when it next runs, which keeps an edit O(1) in the number
  of streams. `dirty()` / `selfDirty()` / the group override / `Graph::markAllDirty` are all gone;
  `Evaluation::requestRecompute(node)` and `requestRecomputeAll()` replace the last, addressed to ONE
  evaluation rather than to a definition that cannot know which to refresh.
- **`const Graph&` now means CONCURRENTLY READABLE**, so `topoOrder()` stopped being a lazy `mutable`
  cache and is rebuilt by the mutators. Two runs after an edit would otherwise have rebuilt it at
  once, behind a signature advertising safety.
- **A bound value IS the boundary pin's output value in the evaluation.** `GroupInputNode::compute`
  became a no-op carrying what was bound, which deleted `m_bound`, `setValue` and
  `GroupOutputNode::value` together — and made group entry literally the host's operation
  (`enterGroup` calls `child.bind(...)`). `BoundaryInput`/`BoundaryOutput` collapsed into one
  `BoundaryPin` of `{PortAddress, name, type, typeName}`: pure recipe metadata, with no way to touch
  a value.
- **The version bookkeeping is two halves, in this order** — clear the recompute request BEFORE
  `compute()` (so an on-request source that rearms itself keeps its new request), record `computedAt`
  AFTER it (so a `compute()` that **throws** stays stale and is retried, not remembered as done). The
  lease test caught the original single-step version. A *suppressed* node still records: ADR-0007
  relies on clean-and-empty together, so a stable-off subtree drops out of future closures.
- **One evaluation runs once at a time.** Scheduler entry takes a non-blocking RAII lease and throws
  `std::logic_error` immediately if it is held — a mutex would hide the caller error and can deadlock
  on recursive entry. `Scheduler::Session` bundles the lease with `prepare` so a backend cannot do
  half the ritual.
- **A host owns a definition and its Evaluation as one replaceable unit.** `FlowviewApp::replaceGraph`
  swaps both or neither; `runmode` creates both together. `prepare` compares the recorded definition
  as a guard rail — address identity, so it catches a mispaired call but not a Graph rebuilt where the
  old one stood, which is why ownership is the mechanism.
- **`resolveEvaluation`** joins `resolvePath` in `groupnav`: an Evaluation is a tree with one child
  per group node, so the same `GraphPath` walks it and every pane gets a definition and its values in
  step.
- **Known cost, accepted:** `prepare` rebuilds its node/port maps every run rather than noticing that
  nothing changed. A graph revision counter would make the common case O(1); worth doing if a large
  graph ever feels it.

### Update 2026-08-02 — M6 step 3 built: `Node::setParam` is the one recipe-mutation seam

A param is **recipe**, so changing one is a document edit that must invalidate its node — and while
the write and the invalidation were separate calls, remembering the second was the caller's job in
three places. `Node::setParam(PortId, value)` now type-checks, commits and invalidates as one
operation, returning `bool` and changing nothing on failure. `ctest` **384/384**, warning-clean,
`format-check` clean, headless round-trip still byte-identical.

- **Mutable `Param&` is gone from the public surface.** `param()` returns const only; `Param::set<T>`
  is private (it seeds the declared default from `addParam`) and the non-const `value()` is removed,
  so the seam cannot be routed around. `grep 'Param& '` over the tree finds only `const Param&`.
- **Two overloads.** The type-erased `(PortId, PortValue)` is the primary, for a caller holding a
  runtime-typed value — a decoded document value, a value an editor widget just wrote. A
  `template <typename T>` convenience builds the erasure for a caller with a compile-time type
  (`ConstantNode::setValue`, tests). The non-template still wins for an actual `PortValue` argument.
- **"The type is the schema" is enforced here.** A value whose payload type is not the param's
  *declared* type is refused rather than quietly retyping the param; so is an empty one, since a
  param always holds a value. A refusal changes nothing **and does not dirty the node** — a spurious
  dirty would cost a re-run of the whole downstream cone.
- **`paramFromValue` changed shape**: `(const Param&, Value, codecs) -> std::optional<PortValue>`
  instead of writing into the param. Decoding and committing are now visibly separate, and the commit
  goes through the node. `paramToValue` already took a `const Param&`, so the pair is symmetric. A
  serialize test caught the exact hazard this creates — it decoded and never committed, and the
  round-trip assertion failed.
- **The Inspector edits a detached copy** and commits only on change. Free, because a `PortValue`
  copy is a refcount bump and `set` rebinds rather than writing through — so an in-progress edit
  cannot disturb the live param; only a successful commit does.
- `markDirty` disappeared from three call sites (the Inspector, `ConstantNode::setValue`, two node
  tests whose comments described the old two-step contract). That is the point of the change.

### Update 2026-08-02 — M6 step 2 built: every declaration returns a `PortId`; `PortIndex` retired

The gap M4b slice 1 left, closed and then widened on review. `addInput` / `addOutput` /
`addInputLike` / `addOutputLike` return the minted **`PortId`** instead of a position; every fixed
node stores named `PortId` members; and `input(PortId)` / `output(PortId)` / `param(PortId)` join the
positional accessors. `ctest` **377/377**, warning-clean, `format-check` clean, headless round-trip
still byte-identical. Purely a rename — no gui-mode surface changed, so nothing new is waiting on a
Metal session beyond step 1's.

- **A param is a declaration too, so it gets an id.** The plan (and ADR-0012) had params keeping
  positional indices, because nothing declares one dynamically and the on-disk key is the name — an
  argument that only holds while params *can't* become dynamic. Future-proofing it cost one field on
  `Param`, so `addParam` now returns a `PortId` and step 3's seam becomes `setParam(PortId, value)`.
  ADR-0012's bullet is amended in place with the reasoning.
- **Ports and params draw from ONE per-node counter**, so a param id can never equal a port id on
  that node. That is what keeps the shared handle type safe: passing a param id to `input()` finds
  nothing rather than silently finding the wrong port. A `[param]` test pins it down.
- **`PortIndex` is deleted, not renamed to `ParamIndex`.** Once every durable handle is a `PortId`,
  what remained was a *position*, doing three unrelated jobs — a count (`inputCount`), an iteration
  cursor, and a param address. `std::size_t` says all of that, and naming the type was what invited
  storing one. `grep PortIndex` over `libs/` and `apps/` returns nothing.
- The by-identity accessors are **unchecked**, like the positional ones: a node's named PortId always
  names something that node declared, so a miss is a programming error, asserted in debug through a
  shared `Node::checked` (flow core stays log-free). `findInput` / `findOutput` / `findParam` remain
  the answer when the id came from elsewhere and may be stale.
- `PortId` being a distinct type is what keeps the overload sets unambiguous — `input(0)` still
  resolves to the positional accessor.
- `DynamicPortsNode::addDynamicPort` and `GroupNode::exposeInput` / `exposeOutput` / `exposePort`
  each lost their declare-then-look-the-index-back-up dance.
- `flow-example`'s `imagePort()` / `inputPort()` were **deleted**, not converted: nothing in the tree
  called them, and an unexercised accessor is how a nested `Edit Template…` stayed broken through a
  whole slice (M5 update, bug six).
- New tests: a `[dynamic]` case contrasting the accessors (remove the first of three pins — `input(id)`
  still names its port, the same position now names a different one), plus two `[param]` cases for id
  addressing and the shared counter.

### Update 2026-08-02 — M6 step 1 built: `core::Uuid`, UUID `NodeId`, schema v2, `CanvasIds`

**Milestone 6 step 1 is built** (see WORK.md for the landing notes and the three deviations). Node
identity is now a UUID, the graph document is schema **v2**, and flowview's imnodes boundary is a
document-lifetime mapping. `ctest` **374/374**, warning-clean, `format-check` clean, and **gui-mode
live-verified by the repo owner 2026-08-03** — every pane's id plumbing changed, so that was the
slice's real risk. It produced one bug, fixed separately: undo left a linked group unresolved (see the
note at the end of this section).

- **`libs/core/uuid.h`** (`lain::core::Uuid`) — std-only RFC 9562 **v7**: `generate`, a deliberately
  liberal `parse` (32 hex, either case, hyphens tolerated, no version/variant policing — a pasted
  `uuidgen` v4 must simply work), canonical `toString`, `shortString`, comparison, `std::hash`.
  `shortString()` truncates from the **tail**: v7 leads with a millisecond timestamp, so a whole
  graph's ids share their prefix — the cli dump printed all four example nodes as `[019fc0ab...]`
  before this was caught. ADR-0011's `[019fbafb…]` illustration is corrected accordingly.
- **`flow::NodeId` wraps a `Uuid`**, minted at admission and immutable after. `PortId` stays a
  per-node counter, since `{NodeId, PortId}` is globally unique once `NodeId` is. `flow` now links
  `lain::core` PUBLIC. `Graph` keeps **insertion order** in its own `vector<NodeId>` beside the
  id-keyed owner (a uuid order means nothing); `nodeIds()` returns it, and `topoOrder` seeds from it
  — which reproduces the old ascending-counter order exactly, not merely a deterministic one.
  New: `Graph{BoundaryIds}` (the invariant pair born with a document's saved ids) and
  `add(node, requestedId)`; one private `usableId` makes "null means mint" and "a duplicate is
  re-minted" the same rule, so identity is never overwritten.
- **Schema v2.** `toValue` writes the node's real uuid — **the canonical `1..N` renumbering on save
  is gone**, so a node keeps its identity across saves and a diff shows what changed. `fromValue`
  became a **version router** over the sole v2 body decoder, with a private, deletable-whole
  `serialize/src/version1.{h,cpp}` migrating a v1 `Value` DOM (ids, edge endpoints, editor keys,
  recursively through inline bodies, a fresh id map per body). A linked template enters the router
  independently, so a v1 template under a v2 parent still opens. The body decoder now **returns** a
  Graph rather than filling one — boundary ids are fixed at construction, so an inline group's inner
  graph is move-assigned from the load rather than re-keyed. A missing `version` is now **fatal**
  (guessing "current" on a v1 file drops every node, and a save then writes that over the original).
- **`IdPolicy::Preserve` / `Mint`** — the plan's "load preserves, paste mints", with today's paste
  named: a **linked template**. One file may back several linked groups in one document, so a
  resolved template is instantiated with fresh ids. Nothing is lost — a linked group's interior is
  never written to the parent, only its source path and interface cache.
- **flowview `CanvasIds`** (`src/canvasids.{h,cpp}`, owned by `AppContext`) — bidirectional,
  monotonically allocated, never-recycled `int`s for `NodeId`, pins (`PortAddress` + direction, since
  a node's first input and first output are both `PortId{1}`) and edges (an edge *is* its destination
  `PortAddress`). `pinId` / `decodePin` / edge-index link ids / `NodeId`→`int` casts are all gone;
  `panes/canvasids` became `panes/canvasstate` (the imnodes-reading half). It resets only when
  DOCUMENT identity changes, which is exactly `pendingBaseline`. **The per-navigation position
  re-seed and selection clear stay** — imnodes destroys an unsubmitted node's data and frees
  selection-pool indices without pruning them, hazards below the id layer.
- **Undo's positional identity is retired.** A restore preserves ids, so `groupnav::pathOrdinals` /
  `pathFromOrdinals` are deleted and `pendingReselect` / `pendingPath` carry plain ids.
- Verified headlessly: save ⇒ load ⇒ save is still **byte-idempotent** *and* now preserves ids; a
  hand-written v1 document migrates with params, names, edges and editor blobs intact and re-saves as
  a stable v2. Two flowview test files are new (`test_canvasids.cpp`) or reworked (`test_groupnav.cpp`
  lost its ordinal case), plus `libs/core/test/test_uuid.cpp` and
  `libs/flow/serialize/test/test_identity.cpp`.
- **Found here, FIXED separately (2026-08-02):** `restoreGraph` (the undo/redo path) passed no
  `TemplateResolver`, so undoing in a document containing a linked group rebuilt it as an unresolved
  placeholder — the interior emptied and the group stopped producing output. Pre-existing and
  unrelated to identity. `restoreGraph` now takes the document directory as a **required** parameter
  (a defaulted one is exactly what invited the omission) and builds the same resolver `loadGraph`
  uses; `applyRestore` passes `ctx.currentPath.parent_path()`. Nothing warned about it because
  loading unresolved is a legitimate mode — it is what a headless `flowview list` wants — so the
  regression test lives in the new `apps/flowview/test/test_graphio.cpp`, which compiles the real
  `graphio.cpp` and round-trips a snapshot against a template on disk. Verified by reintroducing the
  bug and watching the test fail. Landed with it: **`Graph::boundaryInputNode` / `boundaryOutputNode`
  gain const overloads**, which deleted the `const_cast<Graph&>` in `flow::serialize`'s
  `interfaceToValue` — reading a graph's interface is a const operation, and under ADR-0012 (where
  `const Graph&` comes to mean *concurrently readable*) casting past it is the wrong habit to leave
  lying around.

### Update 2026-08-01 — M6 designed: definition & evaluation (nothing built yet)

A `GraphId` was added to fix a preview-cache bug, then **reverted** on review: it patched an ambiguous
key by adding a scoping field, and put process-unique runtime identity into core's vocabulary to serve
a UI concern. The pushback — *"this feels like you are trying too hard to avoid anything having a
unique identifier… perhaps a symptom of keeping structure/data mixed with live state"* — was right, and
grilling it produced a milestone rather than a patch. **See WORK.md's Milestone 6** — this section is
the design; step 1 has since been built (see the 2026-08-02 update above).

- **[ADR-0011](docs/adr/0011-node-identity-is-a-uuid.md)** — `NodeId` wraps a `core::Uuid` (RFC 9562
  **v7**), minted at creation; `PortId` stays a per-node counter. UUID rather than 64-bit random because
  there is no coordination-free 64-bit standard (that space is Snowflake-style *coordinated*) and the
  savings do not survive the scale. UUID comparison is **not** graph order: `Graph` retains an explicit
  insertion-order vector alongside its id-keyed owner, and the JSON `nodes` array carries that order.
  **Load preserves identity; paste mints it** — which is what retires undo's positional ordinals.
  UUID strings make the graph document schema **v2**: one private v1 `Value`→v2 `Value` migrator feeds
  the sole v2 graph decoder, including recursively migrated inline bodies; each linked template enters
  that document-version router independently. Removing v1 later deletes its migrator, dispatch case
  and fixtures, not a second loader. flowview's imnodes boundary becomes a document-lifetime
  **`CanvasIds`** in `AppContext`, mapping nodes, pins and edges (an edge *is* its destination
  `PortAddress`) bidirectionally to monotonically allocated, never-recycled `int`s, so one int names
  one object for the document's life. It persists through navigation and UUID-preserving undo/redo and
  resets only when document identity changes; no canvas integer is serialized. It fixes **cross-level
  id collisions only** — the per-navigation position re-seed and selection clear stay, because imnodes
  destroys an unsubmitted node's data and frees selection pool indices without pruning them.
  Node identity is immutable after admission: the loader stages headers, constructs
  `Graph{BoundaryIds}` with the first valid saved boundary pair (minting + reporting either the
  document lacks), replays those definitions, then adopts other nodes through `add(node, requestedId)`
  in array order; ordinary add mints.
- **[ADR-0012](docs/adr/0012-definition-and-evaluation.md)** — runtime state moves off the structure
  into a host-owned `Evaluation` tree; `compute()` takes its context (parallel map makes an implicit
  one a data race); staleness is a per-node version comparison, *pulled* by evaluations rather than
  pushed by edits; an Evaluation is located by coordinate, not by minted id. A host owns a definition
  and its Evaluation as **one replaceable unit** — that, not a pointer compare, is what stops a
  rebuilt Graph's restarted version counters being read against old observations. And `const Graph&`
  now means *concurrently readable*, so `topoOrder()` stops being a lazy `mutable` cache. **Supersedes
  ADR-0010's "a link references a recipe, never a runtime share"** — that constraint was a consequence
  of the mixing, so one definition can safely back N Evaluations. M6 proves that through the real
  schedulers, including N Evaluations running *simultaneously* over one definition, but does **not**
  yet change LinkedGroupNode's copied-inner-Graph ownership; shared template ownership/caching, reload
  propagation and file watching remain a later vertical.
  ADR-0009's flat plan remains, but each step is now addressed by
  `{const Graph*, Evaluation*, NodeId}`; group staleness and boundary publication follow the matching
  child Evaluation rather than `Node::dirty()` on the definition.

The four id-collision bugs of the previous week were all one cause: ids are unique only *within* their
container, and group nodes made "within a container" stop being the whole world.

### Update 2026-07-31 — preview pass: self-sizing thumbnails, zoom/pan in the Preview pane

A small flowview UI pass, **live-verified by the repo owner**. The Inspector's **Preview size**
dropdown is gone: thumbnails now fit the pane they sit in via one shared
`previewFit(extent, box)` (`appcontext`), the box being `{pane width, 160px}` — a height cap bounds
the size, width follows from the aspect ratio, and the pane width catches a panoramic image. That
also **fixed a long-standing distortion**: the Inspector and Interface drew `gui::Image(tex, {side,
side})`, squashing every non-square image into a square; only the Preview pane preserved aspect, and
its hand-rolled fit is now the shared helper. `PreviewSize` / `previewExtent` /
`AppContext::previewSize` are gone with it — and with them `enumCombo`'s only live exercise, which
was a deliberate trade (UI shouldn't be kept alive to dogfood an API).

The **Preview pane gained zoom + pan**, which WORK.md had deferred to a future multi-window viewer;
doing it in-pane is cheap and doesn't preclude that. `minZoom = min(fitScale, 1.0)` so actual size is
always reachable from either direction (max 16x); **Fit is a mode**, re-fitting on resize until the
user zooms deliberately, and reset when the previewed asset changes. The image sits in a child window
so ImGui owns clipping/scrolling, panning is a drag translated to `SetScroll*`, `NoScrollWithMouse`
keeps the wheel for zoom, and **wheel zoom is cursor-anchored**. Toolbar (`Fit` / `1:1` / a
logarithmic percent slider) sits **below** the image, where the convention puts it. Known limit: the
texture keeps the app's sampler, so past 1:1 magnification is filtered rather than blocky.

### Update 2026-07-29 — M5 (group nodes) designed and built (slices 1–6; gui-mode needs an eyeball)

**Group nodes / subgraphs were grilled and designed** — see the new **Milestone 5** section in
[WORK.md](WORK.md), **[ADR-0009](docs/adr/0009-group-nodes-flattened-into-one-execution-plan.md)**
(the scheduler flattens all nesting into one execution plan; `Node::innerGraph()`; virtual `dirty()`),
**[ADR-0010](docs/adr/0010-inline-vs-linked-groups-no-prefab-overrides.md)** (inline vs linked groups,
the interface cache, links are recipe references and never runtime shares, overrides deferred), and
the vocabulary added to [CONTEXT.md](CONTEXT.md). **All six slices are built.** Groups execute, sync,
persist, and are reachable from the UI — but see the gui caveat on slice 6:

**Slice 6 — flowview navigation.** New `groupnav.{h,cpp}`: a `GraphPath` of descended group nodes, a
tolerant `resolvePath` (a stale path degrades to an ancestor rather than dangling), `breadcrumb`,
`editableAt`, and the `EditorTree` subtree accessors. `MainWindow` resolves the **active graph** once
and hands it to every pane, so descending retargets canvas + Inspector + Interface + Preview + Issues
together. The canvas gains a breadcrumb + Back, **double-click to descend** (same gesture for both
kinds), a `[linked - read only]` marker, and per-gesture mutation gating so read-only affordances keep
working inside a linked group. `Add ▸ Groups ▸ group`, `Add ▸ Linked Group…` (template path stored
relative to the document, resolved immediately), `Group ▸ Edit Template…` through the existing guarded
swap. Interface pane below the root edits the interface and hides binding. Canvas layout is now an
`EditorTree` in `AppContext` — imnodes only knows the level on screen, and **ids repeat across levels**,
so every navigation re-seeds positions and clears the selection. Verified headlessly end-to-end: a
linked group resolves, runs, and delivers through the parent boundary; a missing template loads
unresolved and re-saves byte-losslessly; a recursive one is refused. Two bugs the first live click exposed, both fixed: `Add ▸ Group` **crashed** on ImGui's
"SetCursorPos used to extend boundaries" assert, because a fresh group is the first node kind with
**zero pins** and imnodes needs the body to submit *something* (the pinless boundary nodes only escaped
it via their `±` buttons) — the canvas now draws a placeholder for any pin-less node, guarded by a new
**headless imnodes test** in `libs/gui/test` (ImGui + imnodes need no backend, so a real frame runs
in-suite); and `syncGroupPorts` was only called when *adding* a linked group, so editing a group's
interface from inside would never have reached its outer ports — `groupnav::syncPathGroups` now
reconciles every group on the active path each frame. A third live bug: adding an input *and* an output pin inside a group left the
**output port missing**, because `syncGroupPorts`'s "already mirrored" set was direction-agnostic and
the two boundary nodes both start at `PortId{1}` — the very collision documented on `portMap()` one
slice earlier. Sets are now per direction. The suite missed it because every test added its pins before
a single sync; the bug needs a sync *between* adds, which is the interactive ordering the per-frame sync
produces, and there is now a test in that shape. A fourth: a node inside a group jumped to the *group's* position when navigating by
breadcrumb, because `GraphPane::draw` consumed `pathChanged` in the same frame the breadcrumb raised it
— seeding the level being left and leaving the level being entered un-seeded, so it inherited imnodes
state by node id. `MainWindow` now consumes it at the start of a frame, before the path resolves.
`apps/flowview/test/test_groupnav.cpp` (9 cases) now covers the navigation model. A sixth:
`Add ▸ Linked Group…` / `Group ▸ Edit Template…` were implemented but **never called** — the edit
adding the menu items silently no-op'd, and an out-of-line member definition does not warn as unused,
so the feature compiled and linked while being unreachable. Reviewing that dead code before it ran
found a real bug: `editTemplate` looked its node up in the **root**, but a link can be nested — and
since ids restart per `Graph`, that lookup silently found a *different* node rather than failing.
`enclosingLinkedGroup` now returns the node itself. A seventh, and the worst: **Save wrote the VIEW,
not the document** — `MenuBarPane` gets the active graph, so saving while inside a group overwrote the
file with just that group's interior (data loss). Save now always writes the root graph + the whole
layout tree. An eighth: a linked group's nodes sat in default columns because the template's `editor`
section was discarded on load; it now travels with the template. The **return stack** ADR-0010 called
for is now in (`Group ▸ Return to <file>` + a breadcrumb button), so `Edit Template…` is a round trip
rather than a one-way door — and since returning re-opens the parent, every linked group re-resolves,
which is what makes a template edit reach all instances. `Add ▸ Linked Group…` also dropped the
template's layout (the load path kept it), so a fresh group sat in default columns until a reload —
the third bug of the form *two paths do the same job, one got updated*, and a sign the app-side and
serialize-side halves of "resolve a template" want collapsing into one routine. Previews also showed
the WRONG level's images inside a group — `PinKey` has no level in it and navigation never marked the
cache dirty, so inner pins resolved against root-built entries (a colliding key returned another
graph's image, a non-colliding one nothing); navigation now clears the cache and the preview target.
The breadcrumb bar was then reworked to carry document identity too — `parent.json < boof.json * /
denoise`, where `<` precedes a document (a guarded swap) and `/` a graph level (a free view change),
the first crumb naming the document (`Untitled` + `*` when dirty) instead of `root`, and a right-aligned
`Edit <template>` button whose tooltip states the blast radius. The `Group` menu is retired: both its
actions were contextual and now live in the strip.
`[linked - read only]` then left the breadcrumb for a muted canvas watermark plus a transient message
when an edit is actually attempted — and closing that loop exposed a hole: read-only was enforced ONLY
on the canvas, so the Interface pane's ± / rename and the Inspector's param edits were silently lost on
save inside a linked group. Both now use `BeginDisabled(!editable)`.
**Slice 6 was live-driven by the repo owner throughout (2026-07-29 → 07-31), each fix exercised as it
landed — which is where all ten bugs came from; the engine slices produced none. The two changes made
after the last report — the watermark's restyle and the read-only node-layout change — were confirmed
2026-08-11, so slice 6 is fully live-verified.**

**Slice 5 — nested serialization.** The format splits as designed: a **body** is `{nodes, edges,
editor}`, a **document** is a body plus `{version}`; `toValue`/`fromValue` are thin wrappers over
recursive `bodyToValue`/`loadBody`. An inline group embeds a body under `"graph"`; a linked group
writes `"source"` + `"interface"`. **A group's own ports are never stored** — `edit::syncGroupPorts`
re-derives them from the rebuilt inner boundary *before* that level's edges resolve, which is what lets
the parent's name-addressed edges land. The **`TemplateResolver`** (`source → {key, document}`) is
injected since core does no file I/O, and having **no** resolver is a legitimate mode: every linked
group then loads unresolved from its interface cache (what `flowview list` wants). An unresolved link
rebuilds its cached pins onto the inner boundary nodes and mirrors them outward, so a placeholder is a
real graph with the right face — and re-saving it is **byte-lossless**, making a broken link repairable
rather than destructive. Rectification diffs the cache against the resolved template and reports a
vanished or retyped pin. `LoadResult::editor` is now an **`EditorTree {nodes, groups}`** (an implicit
conversion from `EditorData` kept every save-side call site unchanged). `ctest` **326/326**; the cycle
guard was verified by deletion — a self-linking template **SIGSEGVs** without it.

**Slice 4 — `edit::syncGroupPorts`.** Reconciles a group's outer ports against its inner boundary
pins (**remove → rename → add**, so a name freed in the pass is reusable), returning
`GroupSync {added, removed, renamed, disconnected}` — the last being the *parent's* edges a vanished
pin took with it, which a host must surface. A renamed inner pin keeps its outer port and its wiring;
only the label moves. Mirroring needs **no `T` and no port-type registry**: a `Port`'s type is a shared
`PortType` flyweight, so `Node::addInputLike/addOutputLike` + `Port::portType()` mirror *any* type,
including one registered nowhere — correct, because a group's ports are derived rather than
user-chosen. A hazard surfaced here and is now pinned by a test: `PortId`s are minted **per node**, so
an inner GroupInput pin and an inner GroupOutput pin can share an id — the map's value is only
meaningful with the outer port's *direction*, and a test wires the inner graph crossed so a mix-up
shows as swapped values. `ctest` **320/320**.

**Slice 3 — the execution plan.** `Scheduler` now plans before it runs: a `Plan` is a
dependency-ordered list of `Step`s (`Node` / `GroupEntry` / `GroupExit`, each carrying the `Graph*`
it belongs to) plus step-index edges, built by an `expand()` that recurses through anything
answering `Node::innerGraph()`. **A group is never run as a node** — it becomes *entry → its inner
graph's steps → exit*, so a nested graph is one flat DAG: `SerialScheduler` walks the steps,
`ParallelScheduler` emplaces a task per step and a `precede` per edge across every level at once,
and no scheduler is ever invoked from inside a task. `evaluate` plans the dirty upstream cone
through the same mechanism, so there is one expansion, not two. Suppression crosses a boundary with
**no new machinery** exactly as ADR-0009 predicted (an unready group publishes empty, ADR-0007's
readiness gate does the rest). Two subtleties earned their own tests by being deliberately broken
first: **publish gating** (`Node::selfDirty()` — republish only when the group's *inputs* changed,
else an inner edit re-runs the whole inner graph) and **pre-marking the inner boundary dirty at plan
time** (else a re-bind serves a stale value). `Node::innerPin()` joins `innerGraph()` as the second
half of the structural seam, so the boundary steps need no cast to a concrete group class.
`ctest` **312/312**; `[group]` run 100× for races.

**Slice 2 — the group seam + the boundary-pair invariant.** New **`flow/group.h`**: `GroupNode` owns
an inner `Graph` and answers the new `Node::innerGraph()`; `LinkedGroupNode` is a thin subclass adding
the template `source()` + the `PinSpec` interface cache. `Node::dirty()` is virtual so a group is dirty
when anything inside it is (recursing through nesting). **Every `Graph` is now born with exactly one
`GroupInputNode` + one `GroupOutputNode`** — `removeNode` refuses either, `add` refuses a second, and
`boundaryInputNode()`/`boundaryOutputNode()` return **references**, which retired both the RTTI scans
and every null check. That invariant forced the loader-reuse rule up from slice 5: `fromValue`
**adopts** a document's boundary pair onto the graph's own rather than adding duplicates. `ctest`
**306/306**; a loaded example scene holds 4 nodes, not 6; round-trip still byte-idempotent.
(Port mirroring is declared here; `GroupNode::exposeInput/exposeOutput<T>` — the primitive slice 4's
`edit::syncGroupPorts` gesture builds on — arrived with slice 3.)

### Slice 1 — `PortValue` payloads are shared + immutable

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

**Milestone 1 is complete (2026-06-30) — the engine core, the `flow` decoupling, and the
app stack + viewer. What each piece is, and what it decided:**

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
- ✅ **`libs/string`** (`lain::string`) — string utilities behind a lain:: face (header-only
  until M13 slice 2 added `pattern.cpp`). `format(fmtStr, args...)` wraps `fmt::format`
  (C++17 has no `std::format`) with compile-time-checked format strings. A generic `fmt::formatter`
  for any type exposing `toString()` (detected via `lain::meta::has_to_string`, which
  requires a string-convertible return) renders through it, inheriting
  `fmt::formatter<std::string>` so width/fill/align specs apply — so a type is formattable
  just by having `toString()`, no per-type registration, and the fmt dependency stays out
  of `core`. (Caveat: a type that is both a range/tuple and has `toString()` would be
  ambiguous with fmt's range formatter; none of lain's are.) Depends on `fmt::fmt` +
  `lain::meta`. Tests cover format, the Version-via-`toString` path, specs, and the pattern
  system.
  **`lain::log` carries that formatter** (`log.h` includes it; `lain::string` is PUBLIC on
  `lain::log`) — see the 2026-09-12 update: until then it was reachable from no log site in
  the tree, which is the one place a lain type is most often rendered by name. The
  dependency runs one way: **`lain::string` must never link `lain::log`.** `pattern.h` — the
  `<key>` **name pattern** (`format` / `match` / `keys`, see the 2026-09-12 slice 2 update) — is
  deliberately a separate header from `format.h`, so `log.h` carries the formatter without the
  parser.
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
  tested; it had a live exercise in flowview's inspector until 2026-07-31, when the
  preview-size dropdown it drove was retired — see the 2026-07-31 update).
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
  pins. (A `lain::gui::enumCombo` also drove a `PreviewSize` enum here; retired 2026-07-31
  when thumbnails became self-sizing, taking `enumCombo`'s only live exercise with it.)
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
  *(**Superseded 2026-09-26** — [ADR-0024](docs/adr/0024-one-process-task-pool.md) brought the
  process-wide pool back deliberately, with an owner this time: `lain::app` starts and stops it
  and exposes `--threads`, and `ParallelScheduler` takes nothing. The forward declaration is
  gone too, necessarily — you cannot forward-declare into a namespace alias — so `flow`'s public
  header no longer names `task` at all.)*
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

The engine core (skeleton, `PortValue`, `Port`/`Node`/`Graph`, `Scheduler`,
example node + tests) is already built. Follow WORK.md's "Remaining work" in order:
(1) decouple `flow` from archimedes (`PortValue` → `std::any`), (2) `libs/math`
(typed GLM), (3) `libs/app` (GLFW + CLI11 harness), (4) `libs/gui` (ImGui + imnodes
wrapper), (5) `apps/flowview`. Don't build the backlog (Tier A/B) speculatively.

## Build & verify

Out-of-source, standard for `lain`:

```sh
git submodule update --init --recursive   # pull extern/ (archimedes, multi; both track develop)
cmake -B build                            # FetchContent pulls GLM, GLFW, CLI11, ImGui, imnodes, ...
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
