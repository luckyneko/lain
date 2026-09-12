# `core::Uri` — an identity, not a path algebra, and not RFC 3986

---
Status: accepted
---

A uri is a bare `std::string` everywhere it matters: `io::read` / `write` / `openStream` /
`createStream`, `media::FrameRef::source`, `FrameSource::uri()`, `graphio::templateKey`, and every
opener. Nothing stops one being pasted into a `std::filesystem::path`, and that is not a
hypothetical — M10 slice 3 found `openSequence` building its `fs::path` from the **whole** uri, so
`s3://bucket/frames` became a relative directory named `s3:` that the opener then asked the working
directory about. The fix at the time was `io::localPath`, which collapsed two hand-written
uri→path conversions into one and made the refusal explicit. This ADR types the thing itself.

**The decision was written down on 2026-09-01 and then rediscovered on 2026-09-11.** WORK.md's
*Queued: `core::Uri`* section carried the reasoning, including both collisions below, and its
"build after slice 5" condition was satisfied on 2026-09-02. Three separate notes in a later reading
pass — *"why is there no URI class? a bunch of random functions on a std::string"*, *"scheme.h feels
like it should be part of a URI class"*, and the `NumberField` complaint — re-derived it from
scratch, because nothing in `libs/io/include/lain/io/uri.h` pointed at the queue. That is the
general lesson, and it is why this is an ADR beside the code rather than a section in a plan file:
**a queued decision with nothing pointing at it from the code it governs stops being a decision and
becomes a rediscovery.**

## Decision

`lain::core::Uri` is a value type naming one resource. It carries an opaque `scheme://rest` split,
answers what it is *called* and where it is *local*, and does nothing else.

### It is NOT an RFC 3986 parser, and not a `fs::path` for schemes

Two of lain's own strings collide with the standard head-on, and both are load-bearing:

- **A sequence pattern is not a valid URI reference.** `shot.<frame:04>.png` uses two characters
  that appear in no production of RFC 3986's grammar (`pchar` = unreserved / pct-encoded /
  sub-delims / `:` / `@`), so a conforming parser rejects it outright. Percent-encoding fixes the
  parse and wrecks the human-readable identity strings a manifest is made of.
- **`C:\footage\clip.mp4` parses as scheme `C`**, on a platform lain ships and CI covers.

The existing `://` test is immune to both by construction, so it is kept. The type's job is to stop
a uri being pasted into a `fs::path`, not to model the web.

*(**Amended 2026-09-12, M13 slice 2.** The first bullet used to read `shot.####.png`, which collides
with the standard the other way round — `#` is the fragment delimiter, so a conforming parser reads
path `shot.` and fragment `###.png`. This ADR then predicted that retiring `####` would **remove**
that collision. It does not: the named form's own delimiters are excluded from the grammar too, so
the collision changed shape rather than disappearing — and it is now deliberate, since `<` and `>`
are illegal in a Windows filename and a real file therefore cannot be mistaken for a pattern. The
decision never depended on either: the `C:\` collision stands alone.)*

### Its home is `lain::core`, not `lain::io`

The most valuable site is `media::FrameRef::source`, and `lain::media` links no `io` by rule — a
medium-neutral library that knew how to open things would end up depending on every medium. `core`
is std-only and already names `std::filesystem` in `paths.h`, so a local-only `path()` accessor
fits.

Canonicalisation touches the filesystem, so it stays in `io` as a free function,
`io::canonicalise(Uri) -> Uri`.

### No path algebra on it

Every `parent_path` / `relative` / `filename` in the tree operates on a genuinely local path, and
none of those verbs means anything for a scheme with no implementation — the
registry-with-one-member rule, applied to methods. `path()` plus `fs::path` covers every current
caller.

**`extension()` is the exception, and it survives that test where the others do not:** "which codec,
or which medium" is a real question for every scheme, including one served remotely. Three seams ask
it today (`io::image` keys its codecs by it, `io::video` claims a set of container extensions,
`io::sequence` dispatches a uri to a medium by it), and a format decided in three places eventually
disagrees with itself over `clip.MP4` — silently, as the wrong opener or none.

### The private `scheme.h` is absorbed and deleted

`parseUri` / `isLocalScheme` become `Uri::scheme()` / `Uri::isLocal()`. That file already carried
the comment *"a full Uri type would live here if scheme handling ever grows"*; it grew.

## What this does NOT buy, stated up front

**Canonical-ness stays a discipline rule.** The version that makes "a key computed two ways"
*unrepresentable* is a distinct `CanonicalUri` that only `io::canonicalise` can mint, which costs a
cross-library friend or a token type. There are five call sites and one historical miss
(`graphio::templateKey`, already collapsed into `canonicalUri`). Reach for it if a second miss
appears.

## Costs

- `media::FrameRef::source` becomes a `Uri`. Cheap: `ManifestFrame` already flattens it to a
  `std::string`, so it costs one `toString()` in `manifestOf` — no serialize arm, no wire change.
- A cross-cutting change touching `read` / `write` / `openStream` / the openers / `FrameRef` /
  `templateKey`. This is why it was deliberately sequenced after M10 slice 5 rather than interleaved
  with the FFmpeg unknowns.

## Amendment (2026-09-12) — what the signatures cost, once they were typed

Slice 1 landed the type; slice 1b landed the signatures. Three things were settled by doing it.

**A logged `Uri` needed a decision, and it was not the one the cost list assumed.** 29 log sites
print a uri, 13 of them in the FFmpeg plugin, and `lain::log` did not carry `lain::string`'s
`toString()` formatter — so typing the signatures meant either `.toString()` at every site or a new
dependency for the plugin. The measurement settled it: **that formatter had no production consumer
at all**, its own unit test being its only exercise, so it was reachable from no log site in the
tree. `lain::log` now links `lain::string` PUBLIC and includes the formatter, the 29 sites changed
by zero lines, and the dependency runs one way only — `lain::string` must never link `lain::log`.

**`.toString()` marks exactly one seam, and that is a feature.** `flow::serialize`'s `TemplateCache`
and `ResolvedTemplate::key` stay `std::string`, because to flow a template key is an **opaque
identity token** and ADR-0013's amendment already says flow must not interpret a `source` path —
`scheme()`, `path()` and `extension()` are precisely the verbs it must not call. The four
`.toString()` calls at that boundary are where a name stops being one. `ManifestFrame::source` stays
a string for the matching reason on the wire side.

**The "not urgent" latent bug was real, and the type is what surfaced it.** This ADR recorded
`graphio::loadGraph` doing `std::filesystem::path(uri).parent_path()` on its document argument as
*"the same shape as the bug slice 3 fixed, without the exposure"*. With the argument typed, that line
does not compile: it now asks `uri.path()` and falls back to no document directory when the answer is
`nullopt`. That is the value of the type stated concretely — not that it refuses bad input at
runtime, but that it makes a caller which cannot serve a remote resource say so at build time.

One practical note for the remaining slices: because `Uri(std::string_view)` is implicit, an
un-migrated *definition* compiles as a separate overload, so a half-done rename fails at **link**,
not at compile. Build to a link.
