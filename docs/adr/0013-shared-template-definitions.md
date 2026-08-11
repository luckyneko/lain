# Linked groups share one template definition, replaced rather than mutated

---
Status: accepted
Completes the sharing capability ADR-0012 unlocked; see ADR-0010's revisit note
---

[ADR-0012](0012-definition-and-evaluation.md) moved runtime state off the definition, which removed
the reason [ADR-0010](0010-inline-vs-linked-groups-no-prefab-overrides.md) gave for every linked group
owning its own copy of its template — *"a `Port` holds a persistent value, so two instances sharing
one inner graph would stomp each other's intermediates"*. M6 proved the capability by running one
`const Graph` through N independent `Evaluation`s, including concurrently, but left `LinkedGroupNode`
copying. This decides the ownership model that makes the capability real.

The copies are not merely wasteful. Because each instance loads the template separately, each gets its
own node ids — which is why the loader carries an `IdPolicy::Mint` arm at all, since ADR-0011's
"a duplicate id is a certainty whenever one file is loaded twice" would otherwise bite. Sharing
removes the duplication rather than compensating for it.

## Decision

**One definition per template, owned by a host-held cache; instances hold `shared_ptr<const Graph>`;
a template edit REPLACES the cached definition rather than mutating it.**

- **The cache is `flow::serialize`'s type and the host's property.** `TemplateCache` maps a canonical
  source path to `{shared_ptr<const Graph>, EditorTree}`; the host owns one and passes it into
  `fromValue`. Loading stays in `flow::serialize`, so the recursive template load and the self-link
  cycle guard remain where they already work; ownership and invalidation stay with the host, which is
  the only side that knows about files and about document lifetime. Resolution order is **cache, then
  cycle guard, then build, then cache** — a partially built template is never cached, so a genuine
  link cycle is still refused rather than half-stored. (*Amended during slice 2:* the injected
  resolver runs one step earlier than this reads, because the canonical key IS the resolver's answer —
  `flow` must not interpret a `source` string itself. So a template file is still read once per
  instance; what the cache shares is the BUILD, which is what the ordering above protects.)
- **The layout travels with the definition.** A cache entry holds the template's `EditorTree` beside
  its `Graph`, because loading a template produces both and an instance needs the arrangement its
  author made. (Discarding it is how a linked group's nodes once landed in default columns.)
- **`Node::innerGraph()` returns `const Graph*`.** The engine already used it that way. Mutable access
  to a contained graph becomes a property of the *inline* kind, which is the only kind that has one to
  itself — so "a linked group is read-only in place" stops being a `bool` each pane must remember to
  check and becomes something the type system refuses.
- **The group hierarchy splits to say that.** Abstract `GroupNode` holds the port mirroring
  (`portMap`, `exposePort`, `mapPort`, `innerPin`) and a pure-virtual `innerGraph()`;
  **`InlineGroupNode`** owns a `Graph` and exposes a mutable `inner()`; **`LinkedGroupNode`** holds a
  `shared_ptr<const Graph>`. This also brings the code onto ADR-0010's own vocabulary, which has said
  *inline* and *linked* from the start.
- **An unresolved link is still a real graph, privately owned.** A link whose template cannot be
  resolved builds placeholder pins from its interface cache, as now — into a `Graph` it owns alone,
  behind the same `shared_ptr`. So `innerGraph()` is uniform and a placeholder stays repairable and
  lossless to re-save.
- **A template's definition is never edited in place; it is replaced.** Reload is
  **snapshot → invalidate → restore**: snapshot the open document, drop cache entries, rebuild through
  the ordinary loader. Every instance is reconstructed, so port re-sync, interface rectification and
  child-`Evaluation` replacement all happen by the rules that already exist, in one code path.
- **Invalidation has two triggers.** Saving any document drops the cache entry for its path — *required*,
  not a convenience: the `Edit Template… → Save → Return` round trip works today because returning
  re-reads the file, and a stale cache would break it. And an explicit **Reload linked groups** gesture
  drops the whole cache, for edits made outside the app. There is no file watching.
- **Reload is a refresh, not an edit.** It pushes no undo entry — undo cannot restore the previous
  template, since every restore re-resolves against the current cache — and marks the document dirty
  only when the rebuilt document actually differs, which is the comparison `UndoStack::push` already
  performs.
- **A definition containing an unresolved link is not cached.** A structural check after building, not
  a reading of issue severities. So a template that links a missing file is re-read on next use and
  heals the moment that file appears, instead of freezing a repairable failure behind a gesture.
- **The cache resets when DOCUMENT IDENTITY changes**, exactly as `CanvasIds` does, and on the same
  discriminator. It survives edits and undo/redo — which is what keeps a template's inner node ids,
  and therefore preview keys and canvas ints, stable across an undo.

## Why

**Replacement rather than mutation is what keeps `const Graph&` honest.** ADR-0012 made a definition
concurrently readable, and a shared definition is read by N instances. If a template edit mutated the
shared `Graph`, that promise would hold only until someone edited a template while another instance's
evaluation was running. Swapping a whole definition, on the main thread, at a document rebuild, means
there is no window in which a definition is both being read and being written — the hazard is removed
rather than guarded.

**One loader, not two.** Patching instances in place — swap the pointer, re-sync the ports, rectify
the interface cache — is a second implementation of what the loader already does. That shape is
precisely how a nested `Edit Template…` resolved against the wrong graph, and how a freshly added
linked group lost its layout: *two paths do the same job, one got updated*. Reusing the
snapshot/restore path costs a rebuild of a document that is small by construction and is already the
mechanism undo runs on every keystroke-sized edit.

**The host owns the cache because only the host knows when it is wrong.** A cache keyed by file path
is invalidated by events in the file system and in the user's workflow — a save, a gesture. `flow`
does no file I/O, deliberately. Putting the *type* in `flow::serialize` and the *instance* in the host
keeps the recursive load where it is proven while giving invalidation to the layer that can observe
the reasons for it.

**Sharing deletes a special case.** With one definition per template there is one set of node ids, so
the loader's `IdPolicy::Mint` arm — added because loading one file twice made a duplicate certain —
has nothing left to prevent, and goes. A design that removes a mechanism is a better sign than one
that adds a discriminator, which is the same lesson as ADR-0011.

**Read-only in place is strengthened, not weakened.** It might seem that sharing makes in-place
template editing natural, since edits would reach every instance immediately. That is the argument
*against* it: ADR-0010 made template editing an explicit act precisely because it changes every
instance, and sharing raises the stakes. The const-only accessor now enforces what was policy.

## Consequences

- **`IdPolicy::Mint` and its branch in the loader disappear**, along with the note explaining why a
  template instance must not preserve identity.
- **Two instances of one template have identical inner node ids** — by construction, not by accident.
  This is safe only because `PinKey` carries its level (M6 step 5) and each instance has its own child
  `Evaluation` keyed by the *group node's* id in the parent. Without step 5 this decision would have
  reintroduced the preview-crossing bug it superseded.
- **Resolving one linked group is one routine, shared by the loader and the host.** `resolveLinkedGroup`
  takes a node whose `source` is already set and does what a document load does for each link it reads
  — same resolver, cycle guard, cache participation, rectification and layout. `Add ▸ Linked Group…`
  used to load the template as a standalone document instead, which was already how a freshly added
  group lost the template's layout, and would now have given it a private copy of a shared definition.
- **flowview's `resolvePath` splits in two**: a const resolution for reading (navigation, panes,
  previews) and a mutable one for editing that stops at a linked group. Pane signatures take a
  `const Graph&` plus, where they edit, a nullable mutable one.
- **`edit::syncGroupPorts` takes a `const Graph&` for the interior.** It already only read it.
- **No document schema change.** A linked group still stores `source` + `interface`; nothing about
  sharing is visible on disk, so there is no version bump.
- **A template open as a document coexists with a cache entry for the same file.** They are separate
  `Graph`s with the same node ids, which is harmless — ids are unique within a graph, never across
  them — and the save-invalidates-its-path rule keeps the cache from serving a stale one afterwards.
- **Deliberately still out:** file watching, prefab overrides, and per-instance divergence of any kind.
  A template is one definition for every instance; parameterising remains what boundary pins are for.
