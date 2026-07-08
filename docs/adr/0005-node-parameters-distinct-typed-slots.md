# Node parameters as distinct typed slots, not editable input pins

---
Status: accepted
---

`flow` nodes need **configuration** — a `LoadImageNode`'s file path, a `BlurNode`'s radius/sigma,
a future `ImageWriteNode`'s format + quality — that the gui can display + edit and that a graph
can eventually serialize. `flow` must stay **UI-free** (it names no `lain::gui`), so a node
cannot draw its own ImGui. We model config as **params**: a named, typed, **non-connectable**
value declared on a Node as pure data, which the **adapter** (flowview) renders an editor for.

The decisive choice was **params as a distinct concept vs. the modern node-editor idiom of
"editable input pins"** (an unwired input pin shows an inline widget; wiring an edge overrides
it). We chose distinct params: config and dataflow are genuinely different, and the "drive a
config from the graph" power of editable pins is recovered *without* the extra concept by
**promoting a param to a real input port** and feeding it a `ConstantFloatNode` — cheap precisely
because params and ports share one internal value machinery.

## Decision

- **Params are distinct from ports and non-connectable.** Config, not dataflow. A node declares
  `m_x = addParam<T>("name", default)` in its ctor and reads `param<T>(m_x)` in `compute()` — the
  same shape as `addInput` / `input().get<T>()`.
- **Params reuse `PortValue`'s typed `std::any` slot.** One value-erasure mechanism for both, so
  **promoting a param to a connectable input is a definition change, not a data change** — that is
  how a config becomes graph-driven (wire a constant/source node), rather than an editable-pin model.
- **The editor widget is chosen by the param's TYPE**, never by metadata/hints — a closed set
  (`string`→text, `FilePath`→file-picker, `int`/`float`→drag, `bool`→checkbox, `Choice`=int+labels
  →combo). Richer widgets are richer *types* (`Range<T>` slider, a `Color` swatch). Enum params
  flatten to a `Choice` (labels from `meta::enums` at declaration), so the adapter never needs the
  concrete enum type. The helper types live in `flow` for now (pure data), promotable to `core`/`io`.
- **The type→widget mapping is a type-keyed editor registry in the adapter** (the reader-registry /
  "GUI view" shape): built-ins registered once by flowview, a custom type is a
  `registerParamEditor<T>` registration. `flow` stays UI-free; `Node::onInspect` (a vestigial ImGui
  hook) is removed.
- **Add-node UX: a persistent palette panel.** A left-click list of node types from
  `Factory::keys()` (trackpad-native, unlike right-click, which stays as a secondary), sitting
  beside the selected-node params panel. Keyboard-search add is a deferred fast-follow.

## Considered options

- **Editable input pins (the Blender / imgui-node-editor idiom).** One unified concept: an unwired
  input shows an inline widget, a connection overrides it. Rejected: it overloads the port model
  (a value that's "either the edge or an editable default", per-type inline widgets on the canvas)
  for a unification we get more simply — a param OR a port, bridged by a constant node, since both
  are the same typed slot underneath. A path or an enum choice as a danging connectable pin is also
  semantically odd.
- **Member-bound params** (`addParam("radius", &m_radius)`, gui writes the member). Rejected: a
  second type-erasure system (member pointers) with gui↔internals lifetime coupling, versus reusing
  the `PortValue` slot the codebase already has.
- **Metadata/hints-driven widgets** (`isPath` flag, `min`/`max`, enum value-map on each param).
  Rejected: a parallel hints system when the *type* can carry the intent (`FilePath`, `Range<T>`,
  `Choice`) — one rule ("the type says what it is") instead of two.

## Consequences

- **`flow` gains a small param surface + helper value types** (`FilePath`, `Choice`, `Range<T>`),
  all UI-free data; flowview gains a param **editor registry** and a **palette + params panel**.
- **A read-only ("Debug") param *kind*** (display-only, orthogonal to the type) is deferred — it
  slots in without disturbing this.
- **Serialization-friendly**: a typed slot with a `type_index` is already the thing a graph
  save/load walks; this design doesn't add a member-pointer indirection that would fight it.
- The editor registry **unifies later with the texture-preview "GUI view" seam** — both are
  `type_index → gui rendering`.
