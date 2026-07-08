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

## Payload-agnostic

`flow` names no GPU/UI types. A **PortValue** is a type-erased slot (`std::any`)
carrying any copyable payload — a CPU value or a GPU handle (`acm::Texture`). A
consumer that cares compares `type()` against `typeid(...)`. See `CLAUDE.md`.

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
  `Gray`/`RGB`/`RGBA`) × **ChannelType** (per-channel storage — `U8`/`U16`/`F32`). The
  primary runtime handle. Its **PixelFormatDescriptor** is the per-format fact-bundle
  (channel count, bytes-per-pixel, has-alpha), looked up from the format.
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
  decodes; it only moves bytes.

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
