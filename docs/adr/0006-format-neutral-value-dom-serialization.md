# Serialization pivots through a format-neutral `Value` DOM, not a direct/streaming serializer

---
Status: accepted
---

`lain` needs to serialize graphs — and, generally, arbitrary C++ types — for file save/load and
for websocket transport, ideally **simpler to declare than `nlohmann::json`**. Two shapes were on
the table: **(a)** a direct / streaming serializer that maps a C++ type straight to a target
format's bytes (cereal's and nlohmann's usual mode), or **(b)** a **two-hop** design — a C++ type
maps to a **format-neutral in-memory document** (`lain::data::Value`) via a single
`serialize(Archive&, T&)` visitor, and a per-format **codec** (`lain::io::data`, mirroring
`io::image`) then encodes that document to bytes.

## Decision

**(b).** One `serialize` visitor per type produces / consumes a `data::Value` tree; codecs (json
first) turn `Value` ↔ bytes. `T ↔ Value` is **reflection** (format-neutral, verbs `toValue` /
`fromValue`); `Value ↔ bytes` is **serialization** (`io::data`, verbs `save` / `load`). Graph
serialization is a **separate `flow::serialize` layer** over this, never in `flow` core.

## Why

- **One declaration, every transport.** A type's `serialize` is written once; json today and
  websocket / yaml / xml later vary only the *codec*, not the type. A streaming serializer couples
  each type to a format family.
- **Simpler than nlohmann.** The `Archive` visitor names each field **once** and serves both
  directions, versus nlohmann's paired `to_json` / `from_json` with every key string duplicated and
  drift-prone.
- **Inspectable + diff-clean + idempotent.** A `Value` is a live tree (debuggable, transformable),
  an **ordered** DOM with distinct `Int64` / `UInt64` / `Double` gives byte-stable, VCS-friendly
  output, and a file we wrote round-trips byte-identically. The live tree is also the schema-migration
  boundary: an old DOM can be transformed to the current shape before functional objects exist.
- **`flow` stays payload-agnostic.** The `Archive` is a **seam**: a future streaming / binary
  backend drops in behind the *same* `serialize` functions if profiling ever demands it.

## Considered options

- **`nlohmann::json` directly as the DOM.** Rejected: it leaks a third-party type as lain's public
  document, and its single-number model **corrupts `uint64` NodeIds** and churns int↔float on
  round-trip. nlohmann instead becomes the **json codec's private dependency** — the same
  "codec owns its parser" pattern as libjpeg under `io::image`.
- **cereal-style streaming (no intermediate).** Rejected: no inspectable intermediate, no uniform
  multi-transport story, and its positional keys make human-diffable graph files worse.

## Consequences

- **Every serialize materializes an intermediate `Value` tree** — an allocation cost a pure
  streaming serializer avoids. Accepted deliberately: uniformity, inspectability, and idempotence
  over raw throughput, with the `Archive` seam preserving a streaming escape hatch.
- **The DOM is a lowest-common-denominator.** Format-specific richness beyond its type set (yaml
  anchors, xml attributes-vs-elements) is out of scope by design — the point is one neutral model,
  not every format's full expressiveness.
- **A new `libs/data` + `libs/io/data` + `plugins/io/data/json`** land together, on the exact
  `image` / `io::image` / `plugins/io/image` template — so the shape is already familiar in the tree.
- **A text codec can't preserve every DOM distinction; that's the codec's business, not the DOM's.**
  Concretely, **JSON has no signed/unsigned distinction**, so the json codec normalises a **positive
  `Int` to `UInt`** on read (nlohmann classifies a non-negative integer as unsigned). This is
  *harmless*: the typed read (`fromValue<int>`) cross-accepts either arm, and the emitted **text is
  stable/idempotent** — but DOM-exact `back == value` does not hold for a positive `Int` through
  JSON (it does for a negative `Int`, a `UInt`, and every other arm). Likewise a **Bytes** node rides
  JSON as a Base64 **string** and reads back as a `String` (JSON has no byte type). A future binary
  codec (CBOR/MessagePack) preserves both distinctions exactly — which is the point of keeping the
  DOM richer than any one format.
