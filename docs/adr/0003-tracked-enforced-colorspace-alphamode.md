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

- **Two tags on `Image`, defaulting `Unspecified`.** `ColorSpace { Unspecified, Linear, sRGB }`
  and `AlphaMode { Unspecified, Straight, Premultiplied }`. `Unspecified` is the default so
  "be explicit" is the path of least resistance — a space/alpha-sensitive op on an undeclared
  image asserts, which is the reminder to declare/convert first.
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
- **Adding a color space is additive.** New named standards (Rec709, PQ, DisplayP3, …) extend
  the enum; `convert(img, ColorSpace)` already expresses every pairing without a `toX`/`toY`
  explosion.
