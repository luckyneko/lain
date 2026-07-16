# flowview pane architecture: plain per-pane structs over a shared AppContext

---
Status: accepted
---

flowview's window delegate grew from an "inspector" into the app's **main window** driving every panel —
Graph canvas, Inspector, Interface, Preview, Issues, the menu bar, and the docking layout — plus the
cross-pane state they all touch (the gui `Context`, the per-pin preview cache, param editors, canvas
style, dirty/layout flags, load issues, the preview target). At 1369 lines in one `.h/.cpp` pair it was
a single mountain: every panel's draw code, every shared helper, and every piece of shared state in one
class. This ADR records how it was split — not *whether* to (that was a given), but the shape of the
split, since that shape is the standing pattern for future panes.

## Decision

Split into **one plain-struct pane per file** under `apps/flowview/src/panes/`, orchestrated by a thin
`MainWindow`, over a **shared `AppContext` model** the window hands panes by reference.

- **A pane is a plain struct with a `draw(...refs...)` method the window calls explicitly** — no virtual
  `Pane` base, no registration, no pane manager. ImGui + the docking layout already *are* the panel
  manager; a pane is just a code-organisation unit. `MainWindow::onRender` calls each pane's `draw` in a
  fixed order (canvas → menu → re-eval → refresh previews → inspector → interface → preview → issues →
  confirm-modal → deferred load → render). A pane owns its own `gui::Begin("Name")/End()` where it maps
  to a dock window (the window name is the dock key); the menu bar and the image-save widget draw inline.
- **`AppContext` is the shared *model* — plain data + trivial setters, not a manager.** It holds three
  things: graph-adjacent metadata (`previewSize`, per-pin `saveFormat`, the add cascade `addCounter`),
  cross-pane **signals** each with its setter (`previewAsset`/`previewTarget`, `locateNode`/`locateTarget`,
  `noteRejectedConnect`/`recentIssue`, the `loadIssues` queue), and document / pending-load state
  (`currentPath`, `dirty`, `loadRequested` + `loadedGraph` + `pendingLayout`, `confirmNew`). It also carries
  a `FlowviewApp* app` (set once in `onInit`) so panes reach `graph()`/`nodeFactory()`/`reevaluate()`.
- **The window owns the GPU / GUI resources; the model does not.** `gui::Context`, the `PreviewCache`, and
  the `ParamEditors` registry live on `MainWindow` and are passed to panes as separate references — they are
  device resources with a lifetime tied to the window, not model state. (Steered by the user: "the gui
  Context should be owned by the window, and perhaps passed in by reference.")
- **`PreviewCache` is its own class**, not three loose members: the `map<PinKey, gui::Texture>` + a dirty
  flag behind `markDirty()` / `refreshIfDirty(graph, ctx)` / `find(key) -> const Texture*` (folds the
  repeated found-and-valid check). Uploads stall the GPU queue, so it refreshes only on edits, never per
  frame.
- **Cross-pane needs become small shared units, extracted only when a second pane needs them** — `pinkey.h`
  (the port-addressing key), `panes/canvasids.{h,cpp}` (`pinId` + `selectedNodes`, shared by the canvas and
  inspector), `panes/imagesave.{h,cpp}` (the format-dropdown + Save widget, shared by inspector + interface),
  and `previewExtent` / `addCatalogNode` on `AppContext`. A helper used by exactly one pane stays private to
  that pane's `.cpp` (e.g. the canvas's `decodePin`/`tryConnect`, the interface's `renderAddPin`).
- **The single `edited` flag stays window-owned.** Multiple surfaces (canvas, menu Add, Nodes palette)
  contribute edits in a frame; `onRender` owns the one `if (edited) { reevaluate; markDirty; … }` response
  rather than each pane re-running the scene. Panes accumulate into a `bool& edited` out-param.

## Why

- **It matches the actual coupling.** The panes are genuinely independent *views*; what they share is a
  *model* (the graph and its adjacent metadata) plus a handful of *signals* (a thumbnail click routes to
  Preview; an Issue row locates a node on the canvas). A shared data struct + explicit calls expresses
  exactly that, with the dependency graph visible in each `draw` signature.
- **No framework where ImGui already is one.** A virtual `Pane`/manager would re-implement panel tiling,
  focus, and tab management that the ImGui docking branch owns. Plain structs keep the pane layer a pile of
  view code, not a second windowing system (the user's framing: "we are not trying to make a GUI pane
  manager… just keep the code in neat piles rather than a single mountain").
- **The resource-vs-model split has teeth.** GPU handles (`Context`, `PreviewCache`) must be created/destroyed
  in a specific order relative to the device and the ImGui backend; keeping them off the freely-copied model
  and on the window (with `onShutdown` ordering) keeps that lifetime explicit and away from the panes.
- **Incremental + reviewable.** Scaffolding first (rename → `PreviewCache` → `AppContext`), then one pane
  per commit, each keeping the build warning-clean and `ctest` green — so every step is a small, behaviour-
  preserving diff. Nine commits, no big-bang rewrite.

## Consequences

- **`mainwindow.cpp` is ~130 lines of orchestration** (`onInit` wires the Context + panes, `onRender` calls
  panes in order, `onShutdown` tears down, `buildDefaultLayout` stamps the dock arrangement). Each pane file
  is 30–470 lines. The delegate was renamed `InspectorWindow` → `MainWindow` to match what it is.
- **Boundaries shuffled during implementation, as expected** (the user: "it's a lot of moving parts with
  few chairs… may need adjustment after implementation"). Two moved off their grilled home once a second
  consumer appeared: `addCounter` → `AppContext` (menu Add *and* Nodes palette cascade from it), and the
  imnodes-position bridge split by usage (`collectLayout` is save-side/menu-only, `applyLayout` is
  seed-side/canvas-only). The rule that settled it: **shared-by-two → shared unit; used-by-one → private.**
- **`AppContext` couples the panes to a growing shared struct**, the accepted cost of the "shared model"
  choice — a new cross-pane signal is a field + a setter here, not a new parameter threaded through call
  sites. It stays plain data; anything with behaviour or a resource lifetime does **not** belong on it.
- **A pane is reached only through `AppContext` + the passed resource refs, never through `MainWindow`** —
  so panes don't include each other or the window. Cross-pane actions go through `AppContext` setters
  (`previewAsset`, `locateNode`), keeping the panes decoupled from each other.
- **Not verified interactively here.** The split is behaviour-preserving by construction and passes the
  warning-clean strict build + `ctest` 289/289, but the panes are gui-only — a live Metal-session eyeball of
  each pane (menu save/load, issues click-to-locate, preview click-through, interface bind/add/remove,
  inspector params, canvas connect/delete) is the remaining check, per AGENTS.md rule 9.
- **Considered + rejected:** a virtual `Pane` base with a registry / pane manager (re-implements what ImGui
  docking already does; the user rejected it outright); putting the gui `Context` on `AppContext` (conflates
  a device resource's lifetime with model state); each pane owning its own re-evaluate on edit (multiple
  surfaces edit per frame — one window-owned response is simpler and coalesces them); and a big-bang rewrite
  (unreviewable; the incremental peel keeps each step green).
