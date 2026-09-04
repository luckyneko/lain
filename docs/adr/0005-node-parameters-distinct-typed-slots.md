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
  `m_x = addParam<T>("name", default)` in its ctor and reads `param(m_x).get<T>()` in `compute()` — the
  same shape as `addInput` / `input().get<T>()`.
- **Params reuse `PortValue`'s typed `std::any` slot.** One value-erasure mechanism for both, so
  **promoting a param to a connectable input is a definition change, not a data change** — that is
  how a config becomes graph-driven (wire a constant/source node), rather than an editable-pin model.

  **Amended 2026-08-15 (M8).** "Promoting is a definition change" assumed the node's author picks one
  or the other, once. The **map node** broke that assumption: every element of a map evaluates ONE
  definition (ADR-0014), so a setting that must differ per element cannot be a param at all — while
  the same node, used outside a map, still wants a configured value with no wiring. Both, from one
  declaration, is now a first-class shape: **`addInput<T>(name, Default{value})`** declares an input
  and the param holding its default together, and `Node::defaultOf(port)` pairs them.

  The param is still non-connectable and still the only editable, serialized half; what changed is
  that a port may name one as its fallback. Two rules keep it honest: the default seeds an input with
  **no incoming edge** (never one whose upstream produced nothing, or a Gate turned off would be
  silently replaced by a default instead of suppressing — ADR-0007), and such an input therefore
  stays **Required**, so a connected-but-empty upstream still fails the readiness gate. The editor is
  still chosen by type; an inspector simply shows no editor while the pin is wired, because the value
  would have no effect.
- **The editor widget is chosen by the param's TYPE**, never by metadata/hints — and we **prefer
  an existing type, inventing a bespoke one only where none fits**: `std::string`→text,
  `std::filesystem::path`→file-picker, `image::ColorRGBf`→colour swatch, `int`/`float`→drag,
  `bool`→checkbox. Only where no standard type carries the meaning do we add one — `Choice` (an int
  + label list, for enums, filled from `meta::enums` at declaration so the adapter never needs the
  concrete enum) and `Range<T>` (a value + bounds, for a bounded slider). Such bespoke helper types
  live in `flow` (pure data). `flow` core never names these — only nodes (`flow-example`) do, and
  it already links `image`; `std::filesystem::path` is std.
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
  Rejected: a parallel hints system when the *type* can carry the intent (`std::filesystem::path`, `image::ColorRGBf`,
  `Range<T>`, `Choice`) — one rule ("the type says what it is") instead of two.

## Consequences

- **`flow` gains a small param surface**; params use existing types (`std::filesystem::path`,
  `image::ColorRGBf`, scalars) where they fit, and a bespoke `Choice`/`Range<T>` only where none
  does. flowview gains a param **editor registry** and a **palette + params panel**.
- **A read-only ("Debug") param *kind*** (display-only, orthogonal to the type) is deferred — it
  slots in without disturbing this.
- **Serialization-friendly**: a typed slot with a `type_index` is already the thing a graph
  save/load walks; this design doesn't add a member-pointer indirection that would fight it.
- The editor registry **unifies later with the texture-preview "GUI view" seam** — both are
  `type_index → gui rendering`.

  **Amended 2026-09-04 (M10 slice 7), with what actually landed.** They are **siblings, not one
  registry**: `ParamEditors` is how a type is written, `ValueViews` is how it is shown, and they are
  asked at different moments by different panes. Merging them would have made every viewable type
  also declare an editor and vice versa. What DID unify is the rule — the type chooses, in both
  directions — and the unification paid off in the direction this bullet did not anticipate: the
  Interface pane's *bind* gesture was an `if (type == image::Image)` branch sitting beside a
  fall-through to this registry, so a `media::FrameSequence` boundary input had no editor and could
  not be bound at all. Moving the picker into an `image::Image` editor deleted the branch and gave
  the sequence its own.

  Two shapes are worth carrying forward. A **view is an object, not a function**, because it owns
  state (zoom, a playback position, a decoded frame) that an editor's `(label, type, value)` call
  never needs. And a view's **poster** — the still that stands for a value in a list — is a
  `PortValue`, so the image case aliases its own payload rather than deep-copying it per edit.

  The **per-value hint** this ADR names as a refinement is still not built, and slice 7 met it twice:
  a `std::filesystem::path` editor cannot tell an image from a folder from a clip (it offers all
  three), and a `media::FramePosition` editor cannot know which sequence it indexes (so it is an
  unbounded drag, not a slider).
