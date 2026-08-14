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

### Update 2026-08-11 — M8 designed: map nodes (nothing built yet)

M6 split definition from evaluation so one definition could back N evaluations; M7 made that sharing
real for linked groups. **Neither has ever had the caller both were built for** — running one subgraph
once per element of a collection. **Milestone 8** brings it. Grilled 2026-08-11; decisions in
**[ADR-0014](docs/adr/0014-map-nodes-staged-planning.md)**, build order in WORK.md, vocabulary in
CONTEXT.md. **Slices 1–5 of 6 are built** (see the end of this section) — **maps run and persist**;
only the flowview slice remains. It settles all four questions ADR-0012 listed as *deliberately
unsettled*, which it could only do once a concrete caller fixed them.

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
  the gather copies. The escape from the first two is the `Collection` payload behind the same
  `size`/`at`/`gather` interface — which is why that seam is an interface rather than a type.
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
  graph-containing node needs no scheduler change.
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
   definition and `prepare` compares it as a guard rail, not as the mechanism.
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
   `ParallelScheduler` (lowers to a `tf::Taskflow` — one task per node calling
   `compute()`, edges become `precede` — and runs it to completion on an
   **injected, caller-owned** `lain::task` executor; no default/hidden pool).
   Taskflow owns the push scheduling, so `ParallelScheduler` stays thin.

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
