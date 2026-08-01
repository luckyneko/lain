# Node identity is a UUID, minted at creation

---
Status: accepted
---

`NodeId` counted from 1 within its own `Graph`, and `PortId` from 1 within its own `Node`. That was
unambiguous while a host showed one graph. Group nodes made every group own a `Graph`, and four bugs
followed in a week — a boundary pin that never appeared, node positions inherited across levels, an
"edit template" that resolved to a *different* node with the same number, and a preview pane showing
another level's image. Each looked unrelated until the shared cause was named: `{node, port}` names a
pin in **every** graph at once.

The first attempt was a discriminator — a process-unique `GraphId` in the key. It was reverted before
landing: it patched the symptom by adding a scoping field rather than fixing identity, and it put a
runtime-only, unserialized value into core's identity vocabulary to serve a UI cache.

## Decision

**A `NodeId` wraps a `core::Uuid` (RFC 9562 **version 7**), minted at creation.** `PortId` stays a
per-node counter — `{NodeId, PortId}` is globally unique the moment `NodeId` is, so only one of the
two needs to change.

- **Serialized as the canonical lowercase string.** Not a JSON number: 64+ bit integers lose
  precision in any JavaScript-based reader, and a 36-character id is easier to read in a diff than a
  19-digit decimal.
- **Parsing is liberal, generation is strict.** We emit v7; we accept *any* well-formed UUID —
  32 hex digits, case-insensitive, hyphens tolerated — with no policing of version or variant bits.
  The id is opaque to us; we need uniqueness, not a version. This is what makes hand-editing work:
  `uuidgen` emits **uppercase v4**, and a pasted one must simply work. The nil UUID parses to the
  null value, so `00000000-…` in a file is treated as missing — re-minted, and reported.
- **Load preserves identity; paste mints it.** Today these are the same operation (`fromValue` remaps
  ids either way). They separate: opening a document restores the nodes *as themselves*; copying —
  including instantiating a template as a copy rather than sharing it — mints new ids. A duplicate id
  is otherwise a certainty, not a probability, whenever one file is loaded twice.
- **Duplicate ids within a graph are detected for free.** `Graph::add` already inserts into a
  `std::map`; an insert that finds the key present *is* the detection. Re-mint and record an issue.
  There is deliberately **no cross-graph registry** — that would reintroduce the process-wide state
  this ADR exists to avoid, to guard an event of probability ~10⁻⁸.

`core::Uuid` lives in `lain::core`: foundational, std-only, and reusable. Generation, parse, format,
comparison and hashing — nothing else of the standard (no v1/v3/v4/v5/v8, no namespace derivation, no
binary forms, no `urn:` prefixes).

## Why a UUID rather than a 64-bit random id

64 bits was the initial recommendation, on storage and readability. It did not survive scrutiny.

**There is no 64-bit standard for what we need.** The 64-bit id space is dominated by Snowflake-style
schemes — timestamp + *machine id* + sequence — which fit in 64 bits precisely because they are
**coordinated**. That is the one thing unavailable here: two people editing documents on two laptops
have no coordinator. Every coordination-free standard is wider (UUID 128, ULID 128, xid 96, KSUID 160).

And the savings do not survive the scale: 10,000 nodes is 80 KB of extra id, on lookups that are not
hot. What a standard buys instead is no bespoke collision arithmetic to defend, no "will we need
wider later" hedge, and no explanation burden. The naming difficulty was the tell — a non-standard
scheme needs a name that says what it *isn't* (`Uid`, `Id64`, `Guid`), and none of them read well.

## Why version 7 rather than 4

`Graph::nodeIds()` is documented as *"ascending id order… i.e. insertion order"*, and the serializer
walks it to write the file. Under **v4** that property dies: ascending-id becomes an arbitrary (stable,
but meaningless) permutation, so nodes appear in random order in every document. Under **v7** the
leading 48 bits are a millisecond timestamp, so ascending-id remains creation order and the property
survives untouched — along with the file ordering, the `EditorTree` keying, and everyone's intuition
about what a diff should look like.

## Consequences

- **imnodes needs a dense per-frame index table.** It identifies nodes by `int`, and the canvas only
  gets away with `static_cast<int>(id.value())` today because ids are small. This is a cost of the
  change — though note `NodeId` was *already* 64-bit, so any scheme that stops ids being small forces
  it; it is not specific to UUIDs.
- **The canonical `1..N` renumbering on save goes away.** Files carry real ids, so a node keeps its
  identity across saves rather than being renumbered — better for diffs, at the cost of bigger files.
- **`[3] Tint` becomes `[019fbafb…] Tint`** in the cli dump and Inspector header; a truncated prefix
  keeps it scannable.
- **Undo's positional identity retires.** `pathOrdinals` and `pendingReselect` exist only because a
  load mints fresh ids. A restore now preserves them, so the active path and canvas selection travel
  as ids.
- **Cross-document references become possible** — a linked group naming a node inside its template,
  for instance. Not needed today; previously it was structurally impossible.
- **Hand-editing needs no special tooling.** `uuidgen` is enough. Users who want the ordering property
  can mint v7 from a short Python snippet (`uuid.uuid7()` exists only in Python 3.14+), and mixing the
  two is harmless — a v4 among v7s simply sorts arbitrarily rather than by creation time.
