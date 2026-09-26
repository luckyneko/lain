# Tracked, enforced ColorSpace + AlphaMode — no implicit conversion

---
Status: accepted
---

`lain::image` treats an image's **ColorSpace** (linear vs sRGB) and **AlphaMode** (straight
vs premultiplied) as *tracked tags on the Image* and *enforced preconditions of operations*,
never as things an op silently fixes up. A value-blending op (convolve, resize, arbitrary
rotate, `RGB→Gray`) requires `Linear` + `Premultiplied`; a nonlinear tone op (gamma,
contrast) requires `Straight`. On a mismatch the op is loud — `log::ensure` asserts in debug
and logs + returns an invalid Image in release — rather than producing a subtly-wrong result.
Conversions are explicit and target-based (`convert(img, ColorSpace|AlphaMode)`), and the tag
defaults to `Unspecified` so an undeclared image trips the guard instead of being guessed.

The workflow this buys: I/O reads a file in its native space → the user explicitly converts to
linear (and premultiplied) for the work → converts back to sRGB for display or output. The tag
is the contract that keeps that pipeline honest.

## Decision

- **Two tags on `Image`, defaulting `Unspecified`.** `ColorSpace { Unspecified, Linear, sRGB,
  BT709 }` and `AlphaMode { Unspecified, Straight, Premultiplied }`. `Unspecified` is the
  default so "be explicit" is the path of least resistance — a space/alpha-sensitive op on an
  undeclared image asserts, which is the reminder to declare/convert first.
- **ColorSpace is separate from PixelFormat.** sRGB and linear share a byte layout, so the tag
  is its own axis, not baked into the format enum (which would combinatorially explode).
- **Enforcement is by op *class*, and loud.** The op owns its condition; `log::ensure` owns the
  uniform response (assert + log + invalid Image). No silent auto-conversion inside ops.
- **`convert` is one target-based verb**, overloaded on `PixelFormat` / `ColorSpace` /
  `AlphaMode`, direction-agnostic (to-the-current-value is a no-op). The only explicit path
  between spaces/alpha modes.
- **The tag holds only named standards.** A custom gamma is an effect op that leaves the tag
  `Unspecified`; YCbCr matrix/range and wide-gamut primaries are future *conversion arguments*,
  never crammed into the transfer enum.

## Considered options

- **Auto-linearize inside ops** (each op decodes → operates → re-encodes). Correct-by-default,
  but every op pays a decode/encode round trip, and repeated round trips shed accuracy —
  exactly the silent quality loss we want to avoid. Rejected: the user explicitly converts once
  and works in that space.
- **No tracking — raw values, correctness entirely on the caller.** Simplest, and it matches
  the swapchain's "producers encode for themselves" model — but a blur of sRGB bytes or of
  straight-alpha edges is a *silently* wrong result with nothing to catch it. Rejected: the
  whole point is to make those mistakes loud.
- **A domain `enforce` module** (`requireColorSpace`/`requireAlpha`). Built first, then dropped:
  it bundled a fixed condition with the response and didn't fit compound preconditions. Replaced
  by a general `log::ensure(cond, fmt, …)` — the op composes its own condition (from small
  predicates) and `ensure` is the reusable assert-and-log.

## Consequences

- **Every value-blending / nonlinear op carries a precondition.** Callers thread the image
  through `convert(Linear)` / `convert(Premultiplied)` before such ops (see `flow-example`'s
  `BlurNode`, which does exactly this round trip). This is deliberate friction — the tag is a
  contract, not decoration.
- **The bail path is release-only observable.** In debug the guard asserts (aborts), so the
  "returns invalid Image" behaviour is only reachable under `NDEBUG`; its tests are guarded
  accordingly.
- **What a CODEC may claim is a separate decision, and it needed one.** This ADR's workflow opens
  with "I/O reads a file in its native space", which assumes a reader that reads and a writer that
  records — and for years neither was true of every codec: JPEG asserted sRGB without looking, TIFF
  asserted Unspecified without looking, and no still-image writer recorded a colour tag at all, so
  lain's own round trip lost the tag this ADR calls a contract. Settled in
  **[ADR-0020](0020-codec-colour-tag-policy.md)**: a reader states only what the file states; a
  writer records the tag when the format can state it and refuses when it cannot.
- **Adding a color space is additive.** New named standards (PQ, DisplayP3, …) extend the enum;
  `convert(img, ColorSpace)` already expresses every pairing without a `toX`/`toY` explosion.
  **Amended 2026-09-01, cashing that claim:** the first such addition — `BT709`, for M10's video
  decode ([ADR-0018](0018-frame-sequences-and-host-driven-rendering.md)) — landed as one
  enumerator plus one curve pair, and *removed* code rather than adding any. What makes it
  additive is that `convert` **composes through Linear** instead of dispatching on the ordered
  pair: three spaces would have meant six pairwise arms, and the "this pairing is not supported"
  bail arm is now unreachable by construction.
  **Amended 2026-09-26, closing the `toX`/`toY` hole this bullet opened.** The composition was
  cashed as a **public pair**, `image::toLinear` / `fromLinear` — so the sentence above promising no
  `toX`/`toY` explosion was discharged by shipping one, and a caller spelled
  `fromLinear(dst, toLinear(src, c))` itself. The pair is now **one value**:
  **`image::TransferCurve {toLinear, fromLinear}`**, answered by `image::transferCurve(ColorSpace)`
  in its own **`transfercurve.h`** + `details/transfercurve.inl`.
  What that buys is not a shorter spelling but an **invariant**: the pair is what a colour space IS
  on this axis, so it is a type you get whole or not at all. Each space's pair is built once as a
  `constexpr` constant beside the two curves it pairs, and the switch only maps a space to one of
  them — so the switch cannot get a direction wrong at all, and the single line where a pair *could*
  be reversed sits directly beneath the bodies it names. The curves are named space-first
  (`srgbToLinear` / `srgbFromLinear`), which makes each pair sort adjacent and makes the suffixes
  mirror `TransferCurve`'s members, so a reversed, doubled or cross-standard pair reads as wrong
  rather than needing a test to catch it. The identity pair lives ON the type,
  `TransferCurve::identity()`, which keeps a maximally generic name out of the library-wide
  `image::detail` scope — a nested `detail::transfer` namespace was built for that and removed, since
  a FILE DOES NOT SCOPE NAMES and removing the one name that needed protecting does.
  It has its own header rather than a place beside the enum, the way `formatDescriptor` sits with
  `PixelFormat`: that descriptor is `constexpr` arithmetic needing nothing, while these curves need
  `math::pow`, and `colorspace.h` has zero includes and reaches every consumer of the tags. The gain
  is API surface, not compile cost — `tiffcolor.cpp` no longer sees `luminance` / `saturate` /
  `convert<Dst>` / `mapColorChannels`, but still pulls 311 transitive headers against 313, because
  both reach GLM for `pow`. Measured, after an earlier draft of this amendment asserted a large
  reduction that was not real.
  It is a behaviour table, not a descriptor: nothing about sRGB's 0.04045 is *derivable* from the
  enumerator, which is why the model is flow's `PortType` flyweight rather than
  `PixelFormatDescriptor` — whose own comment defines a descriptor here as *"pure derivable data
  with no identity"*. Naming it for the space rather than the curve was considered and refused for
  the same reason it is not `ColorSpaceDescriptor`: a space-named sidecar is where a reader would
  put the primaries and matrix this ADR keeps OUT of the transfer enum.
  **A scalar `convert(float, ColorSpace, ColorSpace)` was built and then dropped.** It existed
  because a bare channel value carries no tag, so both ends had to be stated; resolving the pair
  once dissolves that premise rather than working around it — after `transferCurve(space)` the
  float genuinely needs no second argument. Its `from == to` short-circuit moved to
  `convert(img, ColorSpace)`, which is now the only place relying on it, with the measurement that
  makes it load-bearing: over a 1001-point sweep **314 sRGB and 412 BT709 values** do not survive a
  decode/encode round trip, so equality has to return the image untouched rather than let the
  curves cancel.
  The one `switch` behind `transferCurve` is what flags the next standard with `-Wswitch` rather
  than a wrong picture. Correcting this bullet's own stale claim while here: it is no longer *the
  tree's* only exhaustive switch on the enum — ADR-0020's codec work gave `pngcolor.cpp` two and
  `jpegcolor.cpp` / `tiffcolor.cpp` one each, for the same reason — so what it is is
  `lain::image`'s, which is the layer this ADR governs.
