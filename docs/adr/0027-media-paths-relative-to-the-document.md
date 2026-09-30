# Media paths are relative to the document on disk and absolute in memory

---
Status: accepted
Extends to media the rule [ADR-0010](0010-inline-vs-linked-groups-no-prefab-overrides.md) set for a
linked group's `source`: a document names the files it needs relative to itself.
---

A node that reads a file (`loadimage`, `listDir`, `openSequence`) holds it as a
`std::filesystem::path` param, and nothing interpreted it. A relative path was read against the
process's working directory, which depends on where flowview was launched from. The file dialog
returns absolute paths, so a document built in the gui named its footage by `/Users/<name>/...`,
and it broke when the project moved. The example documents, which ship their own `data/` folder
and must open from any clone, forced the question.

## Decision

**A path param is absolute in memory. On disk, one inside the document's folder tree is written
relative to that folder, and one outside stays absolute.**

- `../` is never written. A document moved on its own still finds footage kept elsewhere, and a
  project folder moved whole keeps working.
- A load reads each relative path against the folder of the file that stored it. For a linked
  template, that is the template's folder.
- A relative in-memory path (one the user typed) means the working directory, so it is made
  absolute before the inside/outside choice.
- Empty paths and uris with a scheme are left as written.
- "Inside" is lexical: both sides are made absolute and normal, never canonicalised.

**The format owns the rule; a host applies it at the file boundary.** `flow::serialize` defines
`kPathTypeKey` (a param stored under that type key is a file reference) and two transforms over a
document Value, `relativizePaths` and `resolvePaths`. A host calls them only where a document meets
a FILE: after `toValue` when saving, before `fromValue` when loading, and on each template document
it resolves. Undo snapshots never touch a file, so they are never transformed, and the serializer
needs no idea where a document lives.

Threading a location through `toValue` / `fromValue` was built and rejected. A snapshot passes
through those same functions, which made a second "this is a file / this is a snapshot" flag
necessary on every call.

## Why it differs from a linked `source`

A `source` is always relative, `../` included. A template travels with its project, while footage
often lives outside it, where a `../` chain breaks as soon as the document moves alone.

## Deliberately unsettled

- **Linked `source` adopting this rule.** Trigger: a template link breaking because it was written
  as a `../` chain.
- **A nested link resolves against the ROOT document's folder, not its template's**, because the
  host resolver captures the root's folder. This contradicts ADR-0010, but it is harmless while
  templates share a folder with their parent, as the examples do. Trigger: a template linking
  another kept in a different folder.
