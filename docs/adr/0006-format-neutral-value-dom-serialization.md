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
  output, and a file we wrote round-trips byte-identically.
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
