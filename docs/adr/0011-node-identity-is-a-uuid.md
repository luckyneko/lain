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
- **Identity order is not node order.** `Graph` keeps its keyed owner
  (`std::map<NodeId, std::unique_ptr<Node>>`) for lookup and a separate insertion-order
  `std::vector<NodeId>`. Adding a node appends its id; removing one erases it from both containers;
  `nodeIds()` exposes the vector order. The topological execution order remains a third, independent
  order derived from edges — and it must now *seed* from that vector rather than from map iteration,
  which today is ascending-integer and after this is arbitrary. Topo order is not cosmetic: it drives
  serial execution and the canvas's default column layout.
- **The JSON `nodes` array carries insertion order.** Serialization walks `nodeIds()`, and loading
  appends nodes in array order while restoring their ids. No separate `order` field is needed. A v4
  id pasted among v7 ids therefore has no effect on document order.
- **A NodeId is immutable after graph admission.** The v2 loader stages node headers first, takes the
  first valid input/output boundary ids, and constructs `Graph{BoundaryIds}` so its invariant pair is
  born with the saved UUIDs in the canonical first two positions. It then replays those boundary
  definitions and adopts remaining nodes in array order through `add(node, requestedId)`. Ordinary
  `add(node)` mints. There is no post-insertion re-key operation; a second boundary is reported and
  skipped rather than merged into the canonical node. A document missing one or both boundary nodes
  is not an error — the loader mints the absent id and records an issue, since the pair is a graph
  invariant and a hand-written or truncated file must still open.
- **UUID ids are schema version 2.** The public loader first routes a whole document by its root
  version. A private v1 migrator rewrites the `data::Value` DOM from numeric ids to UUID strings —
  nodes, edge endpoints and editor keys together, recursively through inline bodies — and then hands
  that v2 DOM to the sole graph decoder. `toValue` writes only v2; there is no v1 writer or parallel
  v1 graph loader. Linked templates are documents in their own right and must pass through the same
  version router rather than calling the body decoder directly.

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

Version 7 gives generated ids useful time locality and a rough creation-time shape in logs and diffs,
while retaining UUID's coordination-free identity. It does **not** define graph order: multiple ids
can be minted in the same millisecond, monotonic generation within that millisecond is optional, and
the parser deliberately accepts other UUID versions. Deriving insertion order from UUID comparison
would therefore make a representational choice into a fragile behavioural contract. `Graph` tracks
insertion order explicitly instead.

## Consequences

- **imnodes gets a document-lifetime `CanvasIds` adapter.** Its `int` ids are unsuitable as domain
  identity, and the naive replacement — a dense table rebuilt each frame — is worse than the cast it
  replaces: deleting a node shifts every later index, so within that frame an int names a *different*
  node than imnodes has state for. `AppContext` owns one bidirectional, **monotonically allocated**
  mapping instead, so an int names one object for as long as the document lives and a deleted node's
  int is never recycled onto another. It maps `NodeId`, `PortAddress`, and edges — an edge needs no
  new identity, since `connect` is single-source, so an edge *is* its destination `PortAddress`. The
  adapter removes the node casts, `pinId` arithmetic / `decodePin`, and edge-index link ids together.
  Canvas integers never serialize; the editor section remains keyed by UUID.
- **`CanvasIds` fixes identity, not the navigation reset.** The mapping outlives edits, navigation and
  UUID-preserving undo/redo, and resets only when document identity does — but re-seeding node
  positions from the `EditorTree` and clearing the canvas selection on navigation both remain
  **mandatory**, for reasons that live below the id layer and that unique ids therefore cannot fix.
  imnodes destroys a node's `ImNodeData` the first frame it is not submitted (`ObjectPoolUpdate`), so
  descending into a group discards the parent level's positions regardless of what its int was; and
  selection is stored as *pool indices* which that same pass frees without pruning, so a stale index
  can later be handed to an unrelated node. Recorded because "ids no longer collide across levels" is
  exactly the reasoning that would delete these two calls.
- **The canonical `1..N` renumbering on save goes away.** Files carry real ids, so a node keeps its
  identity across saves rather than being renumbered — better for diffs, at the cost of bigger files.
- **`[3] Tint` becomes `[…0f2a8e55] Tint`** in the cli dump and Inspector header; a truncated id keeps
  it scannable. The **tail**, not the prefix this originally said: a v7 id leads with a millisecond
  timestamp, so every node of one graph shares its first characters and a prefix renders them all
  alike. The last 32 bits are pure randomness (`core::Uuid::shortString`).
- **Undo's positional identity retires.** `pathOrdinals` and `pendingReselect` exist only because a
  load mints fresh ids. A restore now preserves them, so the active path and canvas selection travel
  as ids.
- **Cross-document references become possible** — a linked group naming a node inside its template,
  for instance. Not needed today; previously it was structurally impossible.
- **Hand-editing needs no special tooling.** `uuidgen` is enough. A v4 among v7s is harmless because
  the containing JSON array, not UUID comparison, determines node order.
- **Version 1 support is removable.** Its numeric-id knowledge lives in one private translation unit
  plus fixed migration fixtures. Deprecation reports a warning asking the user to save as v2; dropping
  support later deletes that translation unit, its dispatch case and fixtures without touching the v2
  writer or decoder.
- **Loading gains a requested-identity primitive without weakening identity.** `Graph(BoundaryIds)`
  establishes the invariant pair and `add(node, requestedId)` establishes any other restored identity;
  neither changes an admitted node's id. A duplicate requested id follows the deterministic recovery
  rule above rather than overwriting the existing owner.
