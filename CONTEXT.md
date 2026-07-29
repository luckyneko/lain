# CONTEXT.md — domain vocabulary

The ubiquitous language for `lain`. Names here are load-bearing: use them exactly
in code, comments, and reviews. Architecture terms (module, interface, depth, seam,
adapter, leverage, locality) come from the `/codebase-design` skill; this file names
the *domain*. `WORK.md` owns the build plan; `CLAUDE.md` owns how to build it.

## The `flow` engine — three layers over one data model

`flow` is deliberately split into three peer layers, public → private. Keep them
apart: logic that belongs to one must not swell another.

- **Graph** *(data model)* — owns nodes + edges and the invariant-preserving
  **primitives** that mutate them: `connect` (type-checked, single-source,
  acyclic), `disconnect`, `removeNode`, `add`. Each primitive is total and keeps
  exactly one data-model invariant. Graph does **not** execute and does **not**
  carry editor policy. `connect` *reports* (`Connection::InputInUse` /
  `WouldCycle` / …) — it does not decide.

- **Scheduler** *(execution layer)* — consumes a Graph and evaluates it (push
  `run`, pull `evaluate`). Execution lives here, never on Graph. See `scheduler.h`.
  - **Execution plan** — what a Scheduler builds before running: an ordered list of
    **steps** with dependencies between them, expanded across *every* level of group
    nesting at once, so a nested graph runs as one flat DAG and never as a nested run
    (ADR-0009). Both `run` (dirty-closure) and `evaluate` (upstream-cone) build one.
  - **Entry / exit step** — the two **boundary steps** a group expands into, around its inner
    graph's own steps: the entry hands the group's outer input values to its inner
    `GroupInputNode`, the exit publishes its inner `GroupOutputNode`'s values onto the group's
    outer output ports. A group is never *run as a node*; these steps are what a group "does".
  - **Dirty closure** — the nodes a `run` must recompute: every dirty node plus everything
    downstream of one. A group node counts as dirty if anything inside it is.

- **edit** *(editing layer)* — `lain::flow::edit`, a **stateless free-function
  module** over Graph (`edit.h`), peer to Scheduler. Owns editor **policy and
  gesture orchestration** that composes Graph's primitives:
  - **connectReplacing** — replace-on-occupied-input. Because `connect` checks
    `InputInUse` *before* `WouldCycle`, a naive replace can destroy the existing
    edge and then fail the cycle check. `connectReplacing` is **atomic**: it
    captures the edge feeding the target input, disconnects, tries the new
    connect, and **restores the original if the new one is rejected**.
  - **remove** — batch delete-a-selection: removes a set of nodes and a set of
    edges as one gesture, one result. Edges are identified by **value / stable
    destination** `(to, inPort)`, never by list position, so the shifting-index
    hazard cannot arise inside the module.
  - **disconnect** / **addNode** — thin routes to Graph's primitives, kept so the
    adapter talks to a single mutation seam (a future undo/command log has one
    choke point).

  Policy varies across front-ends; primitives don't. That is why editing is its
  own layer and not methods on Graph.

## Adapter

A front-end (e.g. flowview's imnodes canvas) is an **adapter**: it decodes its own
input (pins, selection, cursor, key gestures) into calls on `edit::` and `Graph`.
It owns everything GUI — `pinId`/`decodePin`, gesture detection, selection
resolution, placement — and no graph-mutation policy. The editing layer is the
**test surface**: edit logic is tested against a plain Graph with GPU-free nodes,
never by driving a live GUI.

## Node parameters — configuration, distinct from dataflow

- **Param** — a **named, typed, non-connectable configuration value** on a Node (a
  `LoadImageNode`'s path, a `BlurNode`'s radius), **distinct from a Port** (which is dataflow,
  edge-driven). A Node declares params in its ctor (`addParam<T>(name, default)`) and reads them
  in `compute()` (`param(idx).get<T>()`) — the same shape as `addInput` / `input().get<T>()`. Params
  reuse **PortValue**'s typed, type-erased slot, so a param and a port share one internal value
  machinery: **promoting a param to a connectable input is a definition change, not a data change**
  — that is how `flow` gets "drive a config from the graph" (wire a `ConstantFloatNode` to the
  promoted input) without a second concept or an editable-pin model. `flow` stays **UI-free**: a
  param is pure data; the **adapter** (flowview) renders an editor for it (the vestigial
  `Node::onInspect` ImGui hook is removed — a node never names `lain::gui`). _Avoid_: property,
  setting, attribute, field (for the concept).

- **Param editor** *(the adapter's rendering)* — the **widget is chosen by the param's type**, not
  by metadata, and we **prefer an existing type, inventing one only where none fits**:
  `string`→text, **`std::filesystem::path`**→file-picker, **`image::ColorRGBf`**→colour swatch,
  `int`/`float`→drag, `bool`→checkbox. A bespoke helper type appears only where no standard type
  carries the meaning — **`Choice`** (int + labels, for enums; labels from `meta::enums` so the
  adapter never needs the concrete enum) and **`Range<T>`** (value + bounds, a slider) — and those
  live in `flow` (pure data). The type→widget mapping is a **type-keyed editor registry** in the
  adapter (same shape as the reader registry / the deferred texture "GUI view" seam): built-ins
  registered once by flowview, a custom type is a `registerParamEditor<T>` registration, not a core
  edit. Editing a param writes its slot, then `markDirty` + re-evaluate (all main-thread).
  **Deferred:** a read-only ("Debug") param *kind* — display-only, orthogonal to the type.

## Payload-agnostic

`flow` names no GPU/UI types. A **PortValue** is a type-erased slot carrying any
payload — a CPU value or a GPU handle (`acm::Texture`). A consumer that cares
compares `type()` against `typeid(...)`. See `CLAUDE.md`.

The payload is **shared and immutable**: `set()` moves the value into a shared const
allocation, so copying a `PortValue` is a refcount bump, never a payload copy. This is
load-bearing rather than an optimisation — the scheduler copies a `PortValue` **per edge,
per run**, and payloads are large (an `image::Image` copy is a deep pixel copy). It is safe
because nothing mutates a value in place: `get()` hands out a `const&`, a node writes only
its own outputs, and `set()` *rebinds* the slot rather than writing through the pointer, so
a recompute never disturbs a payload another slot is still reading. A node that wants to
modify a value copies it out (`image::Image src = input(i).get<image::Image>()`), which is
the one place a deep copy is paid — deliberately, by the node that needs it.

## Graph boundary & host binding — the pipeline I/O model

How a graph gets its inputs and yields its outputs, without load/save *nodes*. A
graph declares an **interface**; a **host** supplies the inputs and consumes the
outputs. (M4 — being built; the vocabulary is settled, the mechanics are in
`WORK.md`.)

- **Boundary node** — a node that *is* part of the graph's interface, distinct from
  an internal compute node, and realised *as a node* (not as ports on the `Graph`). A
  **`GroupInputNode`** publishes host-supplied values into the graph; a
  **`GroupOutputNode`** exposes values for the host to read. Blender-group naming — and,
  like Blender, **one node holds many pins**: a graph's whole interface is these two
  nodes, not 2N. Pins are individually typed (`addBoundary<T>` per pin), so a fixed set
  of differently-typed pins needs no dynamic ports. _Avoid_: source/sink node (those are
  internal nodes that happen to have no input/output; a boundary node is specifically the
  *interface*).
  **Invariant: every Graph has exactly one of each.** None would leave the graph with no path
  in or out; several would be confusing and redundant, since one boundary node already grows
  dynamic pins. So `Graph` creates the pair on construction, refuses to remove either, and
  refuses a second — a totality guarantee like acyclicity, not an editor policy. Callers may
  rely on `boundaryInputNode()` / `boundaryOutputNode()` being non-null.
- **Bindable input / output** — the unit a host actually binds: a **named, typed pin**
  on a boundary node (a **`BoundaryInput`** / **`BoundaryOutput`** handle), *not* the
  node. `Graph::boundaryInputs()` / `boundaryOutputs()` flatten every boundary node's
  pins into one list, so a host binds pins without caring how many nodes host them.
- **Host** — whatever drives a graph and binds its boundary: **gui** (flowview —
  input from a file-pick or a live source, output displayed / saved on demand) or
  **cli** (input from args, output written). The graph is the same; only the binding
  differs. The host binds through the seam (enumerate boundaries → `setValue` each
  input → run → read each output `value()`), so it never names a concrete node type.
- **Boundary name** — the stable string that addresses one boundary (`"source"`,
  `"result"`) so a host can bind the right one (a cli arg → an input, an output → a
  file). Distinct from a node's display `name()`.
- **Group node / subgraph** *(being designed)* — a node containing its own graph, exposing
  selected inner ports as its own via the *same* boundary mechanism; the top-level
  graph is the outermost group. One mechanism at every level.
- **Inner graph** — the live `Graph` a group node owns. **Always per-node**: two group nodes
  never share one, because a `Port` holds a persistent value, so sharing would make two
  instances stomp each other's intermediates. A shared *recipe* is a template reference, never
  a shared running graph.
- **Inline group** — a group whose recipe is stored **inside the parent document**. Editable in
  place; it is part of the parent, so its edits mark the parent dirty and ride the parent's undo
  history.
- **Linked group** — a group whose recipe lives in its own document (its **template**), referenced
  by path. **Read-only in place**: it is exactly what the template says, so a template edit
  propagates to every linked group that loads it, with no per-instance diff. Per-instance variation
  is expressed by **exposing the varying value as a boundary pin**, not by overriding inner values.
  _Avoid_: "instance group" — "instance" stays the ordinary English word.
- **Template** — the standalone document a linked group is built from. An ordinary graph document;
  nothing marks it as a template but the fact that something links it.
- **Interface cache** — the linked group's pin names + types, recorded in the **parent** document
  beside the template path. Subordinate to the template (which always wins when present); it exists
  so a missing template degrades to a repairable placeholder with its wiring intact, and so a
  changed interface can be *diffed* and reported rather than silently dropping edges.
- **Active graph** — the graph the host's panes are currently pointed at, identified by a
  **path** of group nodes descended from the root. A host has one; descending into a group
  retargets the canvas, Inspector, Preview and Issues panes together. **Host binding is
  root-only** — below the root, a `GroupInputNode`'s values are driven by the parent's edges,
  so the Interface pane there edits the group's *interface* rather than binding it.
- **Rectification** — reconciling a linked group against its template on load: pins added,
  removed, or retyped since the parent was saved are diffed against the **interface cache** and
  reported, rather than quietly dropping the parent's edges.
- **Override** *(deferred — prefab-style)* — per-instance divergence from a template's recipe.
  Deliberately not built: parameterising via boundary pins covers the cases, and overrides need a
  template-stable address for an inner node plus conflict rules.

## Port arity — one value, a collection, or many pins

Three distinct shapes; keep them apart (conflating the first two is a design trap).

- **Vector-valued port** — *one* pin whose value is a **collection**
  (`Port<std::vector<image::Image>>`): one connection, aggregate payload. Needs **no
  engine support** — a `PortValue` holds any payload, so a port already carries a
  `std::vector<T>` (and shares it, rather than copying it per edge). Use when the *data*
  is a collection (a `CombineImages` op).
- **Dynamic ports** *(a.k.a. variadic pins)* — a node with a **runtime-variable number
  of single-value pins**: N connections, N pins, added/removed/reordered at runtime.
  This is the engine feature (M4 vertical b). Use when you gather N *separate* upstream
  sources (a `GroupInputNode`'s N graph inputs; a `Merge` node). _Avoid_: "N inputs" as
  a phrase — say which of these you mean.
- **Multi-connectable port** *(fan-in; deferred)* — a **third** shape: *one* input pin
  that accepts **many** edges and aggregates them (N connections into one pin). Distinct
  from a vector-valued port (one edge, collection payload) and from dynamic ports (N
  pins). Deferred — it breaks the **single-source invariant** (`connect` reports
  `InputInUse`; `populateInputs` copies one upstream), *and* an unordered set of edges →
  an ordered `T[N]` is ambiguous / reshuffles on reconnect. **Not needed:** "N inputs →
  `T[N]`" is instead a **node reading its dynamic pins** as a collection (a `Merge`), which
  keeps stable pin order + single-source edges and reuses the vertical-b engine.
- **Output fan-out** — the single-source rule guards **inputs only**; an **output already
  feeds many inputs** (each copies the persistent value). No engine work — the one gap was
  the canvas (the link-detach flag must be scoped to input pins, else an output drag moves
  its link instead of starting a new one).
- **Merge / Split** — the bridge between arity shapes: **Merge** gathers many single
  values → a vector-valued port; **Split** takes a vector-valued port → many. (Merge as
  N *dynamic pins* is a strawman for the fan-in port above; deferred with it.)

- **Port-type registry** *(M4 b)* — the addable **port types** for dynamic ports, keyed by
  a display name (lain's Factory idiom, as for the node palette / codecs). `registerPortType<T>("Image")`
  captures `T` in a creator closure `(DynamicPortsNode&, name) → node.addDynamicPort<T>(name)`,
  so **flow core names no payload type** — the app registers `image::Image` / a future
  `Voxel`. A dynamic node's "+" menu is the registry's keys **filtered by the node's
  `acceptsPortType`** (a `Merge` takes only `Image`; a `GroupInputNode` takes any). No
  hardcoded set of pipeline types.

- **PortId** *(M4 b)* — an **opaque, stable per-port handle** (the port-level analogue
  of `NodeId`, scoped within its node), so an edge and a `BoundaryInput` handle survive a
  pin being added/removed/**reordered** — the port's identity is not its position. A
  `PortIndex` is only a positional cursor for *iterating* a node's ports, never a durable
  reference.
- **PortAddress** *(M4 b)* — the conglomerate **`{NodeId, PortId}`**: the durable **in-memory**
  address of one port on one node. The unit edges and handles reference (an **Edge** is two
  PortAddresses, `{from, to}`). Holds **no direction** — an edge implies it by position, a lone
  address derives it from `Port::direction()`. Has `==` + `std::hash` so it keys selection / lookup.
  (The **on-disk** edge form addresses ports by *name*, not PortId — see `flow::serialize`'s
  name-addressed edges; PortAddress is the runtime primitive it resolves to.)

## Value display — two purposes, two seams

Rendering a port's value splits by *purpose*; don't conflate them.

- **Text** *(cli dump, debug, inspector labels)* — **`meta::toString<T>(const T&)`**
  (`lain::meta`, fmt-free): best-effort stringify — `bool`→true/false, a member
  `toString()`, else an ostream operator, else the type name. Unlike
  `lain::string::format` (which needs a *formattable* type), it always produces
  something. A type opts into a nice text form just by exposing `toString()` — there
  is **no central `holds<int>/holds<float>/…` ladder**.

- **PortType** *(the per-type flyweight)* — `flow`'s **`PortType`** (`porttype.h`)
  bundles a declared type's reflective facts — `type_index`, human `typeName`, and a
  `describe(PortValue)` bridge over `meta::toString` — into **one static instance per
  type**. A `Port` holds a single `const PortType*` (not a copy of each fact), captured
  at `addInput/addOutput<T>`. `Port::type()` / `typeName()` / `describe()` forward
  through it. This is **the extension point for per-type facilities**: a new one becomes
  a field on `PortType`, never another functor on every `Port`.

- **GUI view** *(deferred)* — showing a value richly (a texture *thumbnail*, not the
  string `"acm::Texture 64x64"`) is a **separate, larger seam**: a registry of viewers
  keyed by type, living in `lain::gui` or the app. Not built yet. Today each adapter
  keeps its own texture branch (cli reads back pixels, the inspector draws a thumbnail)
  and falls through to `Port::describe()` for everything else.

## Color & display — the non-encoding swapchain

Everything drawn to a swapchain image writes **display-ready sRGB bytes**: the stored
value is already sRGB-encoded and presentation-ready. The swapchain render pass is
**non-encoding** — the driver does *not* apply the linear→sRGB transfer function on store
— so each producer encodes for itself.

- **GUI** emits its native, already-sRGB colors as-is. This is why a **non-encoding**
  swapchain is required: an auto-encoding (sRGB-format) framebuffer would encode ImGui's
  sRGB colors a *second* time → **GUI washout** (the too-bright/pale symptom that motivated
  this model). See [ADR-0002](docs/adr/0002-non-encoding-swapchain-for-gui-color.md).
- **General rendering** (non-GUI, typically linear-lit — e.g. a future 3D `flow` node)
  must **encode in-shader** to display-ready sRGB before writing. It gives up the hardware
  auto-encode by design, in exchange for GUI and general content sharing one swapchain pass
  (which also sidesteps the MoltenVK mutable-format two-pass flicker bug,
  [MoltenVK#2261](https://github.com/KhronosGroup/MoltenVK/issues/2261)).

**Surface-agnostic GUI correctness** is the invariant that the GUI is pixel-correct
whatever swapchain format acm selects. It is met by **preferring a non-encoding
(UNORM-format) surface** — now archimedes' *default* `SurfacePreferences` (BGRA then RGBA,
`SrgbNonlinear`), so `lain::app` inherits it with no code of its own. The swapchain stores
raw bytes while its colorspace stays `SrgbNonlinear`, so the compositor still reads them as
sRGB. The fallback for a driver that
offers *only* an sRGB surface is a **UNORM alias view** (a UNORM-format view of the sRGB
image via `VK_KHR_swapchain_mutable_format`, rendered through with no encoding) — designed,
not yet built (dormant on MoltenVK, which offers a UNORM surface).

## The `image` library — CPU rasters, typed and tracked

`lain::image` is the CPU-side raster foundation. It names no GPU/UI types, so an **Image**
rides a `flow` port like any payload. Vocabulary, three axes kept apart:

- **PixelFormat** — a pixel's byte layout: **ColorModel** (the channel set + order —
  `Gray`/`GrayAlpha`/`RGB`/`RGBA`) × **ChannelType** (per-channel storage — `U8`/`U16`/`F32`). The
  primary runtime handle. Its **PixelFormatDescriptor** is the per-format fact-bundle
  (channel count, bytes-per-pixel, has-alpha), looked up from the format. Formats a codec meets
  that lain can't represent (indexed, sub-byte, colourkey, non-sRGB/ICC space) are expanded
  losslessly by the reader, never silently — see [WORK.md](WORK.md) M3.
- **Color** — a pixel *value* of a given PixelFormat: N channels of T, a strong type over a
  `math::Vec`. It is **layout-neutral** — *which* channel is which is the PixelFormat's
  business, not the Color's.
- **Image** *(the single owner)* — extent + PixelFormat + a byte buffer, plus the two tracked
  tags below. It is the sole owner of its pixels; **ImageView** is a non-owning, typed,
  strided view *over* an Image's bytes (obtained via `as<Color>()`) — the "iterate over
  pixels, not bytes" surface. Views never own.
- **ColorSpace** *(tracked, enforced)* — the transfer/encoding axis (`Unspecified`/`Linear`/
  `sRGB`), a property of the Image, **separate from PixelFormat** (sRGB and linear share a
  byte layout but not a meaning). **AlphaMode** *(tracked, enforced)* — whether color is
  premultiplied by alpha (`Unspecified`/`Straight`/`Premultiplied`), meaningful only for
  alpha-bearing formats. Both default `Unspecified` (be explicit).
- **No implicit conversion.** Nothing auto-converts space or alpha (a round trip sheds
  accuracy). The one verb **`convert`** is overloaded on its *target* — `convert(img,
  PixelFormat | ColorSpace | AlphaMode)` — and is direction-agnostic (converting to the value
  the Image is already in is a no-op). See [ADR-0003](docs/adr/0003-tracked-enforced-colorspace-alphamode.md).

### Op-class enforcement

An operation's space/alpha requirement follows its **class**, and a violation is loud
(`log::ensure`: assert in debug, log + invalid Image in release) — never a silent wrong result:

- **Value-blending / cross-pixel** (`convolve`, `sharpen`, `resize`, arbitrary `rotate`,
  `RGB→Gray` luminance) — require **`Linear`** and, for alpha formats, **`Premultiplied`**
  (blending in sRGB or with straight alpha is wrong).
- **Nonlinear per-pixel tone** (`gamma`, `contrast`) — require **`Straight`** (a nonlinear
  curve on premultiplied color is wrong).
- **Pixel-rearranging / linear-scale** (`crop`, `rotate90`, `brightness`, `clamp`) — agnostic.

## Loading — bytes in, decoded assets out

The path that turns a file into an in-memory asset, split by concern so no one library
becomes a junk drawer. Transport (getting bytes) is separate from codec (decoding them), and
both sit behind service-shaped seams. See [ADR-0004](docs/adr/0004-static-linking-service-shaped-seams-over-thorax.md).

- **Service-shaped seam** *(the convention)* — a subsystem exposed as a **namespace of free
  functions over a hidden singleton** (the `lain::log` shape), so an internal singleton or a
  future `thorax::service` can back it interchangeably with no caller change. `lain`'s answer to
  anything that would otherwise be a **floating global**. _Avoid_: manager object, service class
  (on the public surface).

- **Buffer** *(the owned byte region)* — an **aligned, fixed-size, single-owner** block of bytes,
  in `lain::memory`. Defined by its **refusals** as much as its contents: no `resize`, no byte
  `operator[]`, alignment guaranteed — the affordances a `std::vector<uint8_t>` wrongly exposes.
  Move-only for now; a non-owning slice (**BufferView**) and a refcounted share (**SharedBuffer**)
  are later derivations. _Avoid_: Block (marv's name), Bytes, blob, `vector<uint8_t>` as a stand-in.

- **Allocation seam** — the `memory::alloc(size, align)` / `dealloc(ptr, size, align)` free
  functions a **Buffer** routes every allocation through. The **sized, aligned** shape keeps it
  pool-ready: the backing is plain aligned allocation today and a recycling pool drops in behind
  it unchanged. The *manager* (mimalloc / marv / custom pool) is a backend chosen by profiling,
  not a type in any signature.

- **IO scheme** — a **byte-transport backend keyed by URI scheme** (`local` now; `remote`/`s3`
  later). `lain::io::read(uri) → Buffer` dispatches to one. **Media-agnostic** — `io` never
  decodes; it only moves bytes. `read` is a **whole-asset** read (the resource in one `Buffer`); a
  streaming **Stream** transport (incremental/seekable, for video and large/network assets) is a
  deferred peer to `read`, not a change to it. _Avoid_: loader (that's a Reader).

- **Reader** / **Writer** — a **Reader** decodes a `Buffer` of one format into a typed asset; a
  **Writer** encodes the asset back to bytes. One per format, holding both directions (they share
  a codec dependency): `JpegReader` + `JpegWriter`. _Avoid_: loader, importer, decoder-only.

- **Reader registry** — the per-media `core::Factory<Reader>` keyed by format (a
  `Factory<ImageReader>` behind `lain::io::image`). A **codec plugin** attaches to it by
  **explicit registration** — a `registerCodec(registry)` call, never self-registering static-init
  (the linker strips an unreferenced static lib). The *enabled* codecs are collected by the build
  into a generated **`registerImageCodecs`** aggregator the app calls once, the way
  `flowview::registerExampleNodes` seeds the node palette.

- **`lain::io` vs `lain::io::image`** *(transport vs codec seam)* — `lain::io` (`libs/io`) is the
  media-agnostic transport (`read → Buffer`, scheme dispatch), **codec-dependency-free**.
  `lain::io::image` (`libs/io/image`) is the **codec-free seam** for one medium: the
  `Reader`/`Writer` interface + the registry + the `load`/`save` facade. Video/audio arrive as
  peer seams (`lain::io::video` / `lain::io::audio`).

- **Codec plugin** — a **swappable per-format implementation** (`lain::io::image::jpeg`), a
  separate satellite target living under a top-level **`plugins/`** root (peer to `libs/`/`apps/`,
  as in `thorax`), *not* buried among the interfaces. It depends on the `lain::io::image` seam +
  its third-party codec (fetched via `cmake/addXXX.cmake`); the dependency never inverts. Static
  today, a `thorax` DSO tomorrow behind the same `registerCodec` seam. A format is enabled/disabled
  by an opt-out CMake `option`, so a consumer pulls only the codecs it asked for. _Avoid_: reader
  lib, backend, importer.

## The `data` library — serialization DOM + reflection

`lain::data` is the format-neutral serialization core: a value tree every transport pivots
through, plus the reflection that maps a C++ type to and from it. **Two hops** — `T ↔ Value`
(reflection) then `Value ↔ bytes-in-format` (codec, in `lain::io::data`) — so one type
declaration serves json today and websocket / yaml / xml later. (Graph serialization is the
first customer; the spine is general but built Graph-driven, json-first — see WORK.md Tier A.)

- **Value** *(the DOM)* — the owned, format-neutral document node: a recursive variant over
  `Null / Bool / Int64 / UInt64 / Double / String / Bytes / Array / Object`, with **Object
  insertion-ordered** (deterministic, diff-friendly output). It is the IR of *serialization*
  the way **Image** is the IR of *pixels* — a codec produces/consumes it, never the concrete
  C++ type. Numbers are the **widened-canonical** set (every narrow int/float rides losslessly
  in `Int64`/`UInt64`/`Double`; the narrowing happens in the typed read, not the DOM). **Bytes
  is a first-class node** (raw bytes), so binary transports carry them raw and **Base64 is a
  per-text-format encoding**, never in the DOM. _Avoid_: DataBlock, Block, blob, json (as the
  DOM type name), document, node (flow::Node's word).

- **The type is the schema** *(principle)* — there is **no separate validation layer**.
  Format-safety *is* a typed `fromValue<T>` succeeding: it fails on a shape/type mismatch,
  `optional<T>` tolerates an absent key, `variant` rejects an unknown arm, a narrow int
  range-checks. Machine-generated files need layout + type checks and nothing heavier.

  **Serialization vs reflection** *(the honest split)* — "serialization" is strictly the
  `Value → bytes` step (`io::data` — json/yaml/binary). `T ↔ Value` is **reflection into the
  DOM**, format-neutral, no bytes: hence the `toValue`/`fromValue` verbs. The word `serialize`
  survives only as the **customization-point** name (`serialize(Archive&, T&)` — cereal's term;
  the Archive *could* stream) and as **`flow::serialize`**, the graph operation-layer (a verb-named
  companion to `flow::edit`, exposing `toValue`/`fromValue` for a Graph). Transport (`bytes ↔
  file/socket`) is `io::read`/`write`. Three verbs, three layers; none overclaims a format.

- **Archive** *(the visitor)* — the direction-agnostic target a type's `serialize` writes
  against; `ar.member("radius", n.radius)` names each field **once** and serves **both** save
  and load (the Archive carries the direction). This is the "simpler than nlohmann" lever (one
  function, one key string, no drift — vs. nlohmann's paired `to_json`/`from_json` that repeat
  every key), and the **seam** a future streaming/binary backend slots behind without touching
  any `serialize`. The typed facade over it is `data::toValue(const T&) → Value` /
  `data::fromValue<T>(const Value&) → optional<T>` — Value-facing verbs, deliberately **not**
  `write`/`read` (those imply bytes; reflection emits a tree, not a format). _Avoid_: serializer,
  stream (as the type name), write/read (for the T↔Value facade).

- **serialize** *(the customization point)* — a free (ADL-found) or member
  `serialize(Archive&, T&)` declaring a type's mapping; **layered** — a compound type composes
  from its members' `serialize`, and supporting a new type is one thin overload. The library
  ships `serialize` for the std containers (`vector`/`array`→Array, string-keyed `map`→Object,
  `optional`→present-or-absent, `pair`/`tuple`) and `memory::Buffer`→Bytes. A **member**
  `serialize` (preferred over the free one, detected by a `has_member_serialize` trait) is the
  **private-member** escape hatch; the encouraged path is a free function over a plain data
  struct kept separate from functional classes. **`LAIN_SERIALIZE(T, fields...)`** is a thin
  macro *wrapper* over the visitor (field-name = JSON key), never the only route. Full
  no-mention reflection waits on C++26 (P2996); the visitor API is forward-compatible with it.

- **Enum-as-name** — an enum serializes as its `meta::enums` name string (underlying-int
  fallback if unnamed) — human-readable, refactor-stable; `fromValue<E>` maps back by name.

- **Tagged variant** — a `variant` serializes with a **stable-string discriminator**
  (`{ "type": <key>, "value": … }`), the *same idiom* as a polymorphic node's `core::Factory`
  key — deliberately **not** `meta::typeName` (display-only, unstable). **Untagged variant**
  (try each arm in declaration order, first clean read wins) is **deferred**; it relies on reads
  being **pure / rollback-safe** (no mutation on a failed read — a property v1 reads hold
  anyway), so it slots in later with no rework.

- **lain::io::data** *(the codec seam)* — bytes ↔ Value for one format, mirroring
  `lain::io::image`: a `DataReader`/`DataWriter` interface, a `core::Factory` registry keyed by
  format (`"json"`), a `load`/`save` facade, and per-format **codec plugins** under
  `plugins/io/data/<fmt>` that own the third-party parser (nlohmann for json, ryml for yaml,
  pugixml for xml) — **nlohmann is the json codec's private dep, never `Value`**. json-first;
  other formats land as-needed.

## Graph serialization — `flow::serialize`

Saving / loading a Graph, built on `lain::data`. A **separate target** (`libs/flow/serialize`,
`lain::flow::serialize`) depending on `flow` + `data` — a verb-named operation-layer companion to
`flow::edit`, kept *out* of `flow` core by the **boundary rule**: *a concern lives in `flow` core
iff it stays payload-agnostic; the moment it must name a concrete payload type or an external format
(as serialization must), it lives outside.* Free functions `toValue(const Graph&, ctx) → Value` /
`fromValue(const Value&, ctx) → LoadResult`.

- **The recipe, not the cooked data** *(the governing principle)* — serialization stores a graph's
  **structure** (node `kind` + params + dynamic pins + edges), **never** computed port values or
  host-bound boundary values. The graph re-runs to reproduce them: a GPU/`acm::Texture` port value
  never serializes, and a `GroupInputNode`'s host-injected values never serialize (the host re-binds
  each run).

- **kind** — a node's `core::Factory<Node>` key (the stable string that reconstructs its type), the
  tag every serialized node carries; distinct from the display `name()`. A **variant** discriminator
  is the *same idiom* — a stable-string-keyed registry. _Avoid_: type, class (for the node's factory
  key, in the file).

- **Node-id remap** — load mints **fresh** NodeIds via `Graph::add` and rewrites edges through a
  `savedId → newId` table (returned, so an adapter re-keys its metadata). Deterministic and
  **canonical** (assigned in file order), so a no-edit round-trip is idempotent. Needs **no new Graph
  API** and *is* the **subgraph-paste** primitive (paste-into-existing must remap to dodge id
  collisions) — load and paste are one mechanism. Chosen over id **preservation** (which would need
  an insert-with-id seam + brittle ctor-order coupling).

- **Name-addressed edges** — the on-disk edge references `{ node-id, port-name }`, resolved to a
  `PortAddress` on load. A port **name** is a stable string key (per the versioning contract): it
  survives a node's port-declaration order being refactored, and lets dynamic pins reconnect by name
  with no PortId persistence. Requires **port-name uniqueness within a direction**, enforced at the
  add seam (static ports → `log::ensure` author-contract; dynamic pins / boundary renames → a checked
  reject). `PortAddress {NodeId, PortId}` stays the **in-memory** edge primitive; name-addressing is
  only the on-disk form.

- **Value-serializer registry** — the app-populated table keyed by a `PortValue`'s `type_index`,
  holding a stable **type-key** + a `toValue`/`fromValue` pair (closures capturing `T`, the
  `core::Factory::registerType<T>` idiom). Round-trips **params** (and any future savable
  `PortValue`); the node's *declared* param type is authoritative on read ("the type is the schema"),
  the stored key a cross-check. Distinct from the **port-type registry**, which recreates dynamic
  *pins* — a dynamic pin serializes its **type**, never its value.

- **Dynamic-pin serialization** — a `DynamicPortsNode` (incl. both boundary nodes) additionally
  serializes its dynamic-side pins as `{ name, portTypeKey }` (**direction implied by
  `dynamicSide()`**), replayed on load via the **port-type registry** (which gains a `type_index →
  key` reverse lookup for save). A plain node serializes no ports — they're implied by its `kind`.
  **Contract:** a `DynamicPortsNode`'s factory form has an **empty dynamic side** (all dynamic pins
  are serialized + replayed), so no double-add.

- **`editor` section** — a separate, **adapter-owned** part of the document keyed by node-id that
  `flow::serialize` **round-trips as an opaque `Value`** and never interprets: node position, color,
  size, collapsed-state, comments. Keeps `flow::serialize` GUI-free (placement is the adapter's job)
  in one file; a headless load ignores it.

- **version** — a document-root **integer** (monotonic, *not* `core::Version` semver), bumped on an
  incompatible encoding change; migration is a version-switch on read, a **too-new** version is a
  fatal load. The deeper compat contract is the **stability of the string keys** (`kind`, value
  type-keys, port names) + tolerant reads (unknown param/pin skipped) — soft forward-compat for free.
  Per-node-type schema versioning is deferred.

- **Canonical file** — a file `flow::serialize` wrote round-trips **byte-identically** (ordered
  `Object`, canonical id remap, container-preserved edge/param order, round-trippable numbers). A
  non-canonical (hand-edited) file **normalizes on first save**, then is stable.

- **LoadResult / LoadIssue** — `fromValue` returns `{ Graph graph; std::vector<LoadIssue> issues; }`
  (`clean()` == no issues), **best-effort**: an unknown `kind` / port-type / param or a rejected edge
  is skipped + recorded, not fatal — and because the graph is rebuilt through `Graph`'s primitives, a
  partial load is still an **invariant-valid** Graph. Each issue is **both** logged (`lain::log`) and
  returned (a GUI shows them and treats `!clean()` as failure; a cli refuses on an `Error`);
  `Severity` is `Warning` / `Error`. The engine reports, the host decides. `flow::loadGraph(uri, ctx)`
  funnels codec + semantic failures into one `LoadResult`.
