# What a codec may claim about colour — read what the file states, record it or refuse

---
Status: accepted
---

[ADR-0003](0003-tracked-enforced-colorspace-alphamode.md) made `ColorSpace` and `AlphaMode`
*tracked tags* that operations *enforce*, and named the workflow that buys: "I/O reads a file in
its native space → the user explicitly converts to linear for the work → converts back for
output. The tag is the contract that keeps that pipeline honest."

**That contract had no owner at the codec boundary, and the codecs did not keep it.** An audit of
every reader and writer found four different policies for one question and a documentation claim
matching none of them:

| Codec | ColorSpace it produced | |
|---|---|---|
| PNG | sRGB chunk / iCCP / a gAMA window, else Unspecified | genuinely read |
| JPEG | **always sRGB** | hardcoded — stb_image discards every APP marker, so nothing was consulted |
| TIFF | **always Unspecified** | hardcoded — ICC and TransferFunction were never read |
| video (FFmpeg) | all three stream axes; explicit tags refused or believed; untagged → BT709, logged | genuinely read |

And **no still-image writer recorded a colour tag at all.** PNG wrote no `sRGB`/`gAMA`/`iCCP`;
JPEG emitted stb's bare JFIF header; TIFF wrote `PHOTOMETRIC` (layout) and `EXTRASAMPLES` (alpha)
only. No image `canEncode` consulted `colorSpace()`, and the `io::image` facade had no colour hook
at all.

The costs were concrete, not theoretical. lain's own round trip lost the tag, so a saved sRGB PNG
reloaded as `Unspecified` — which is why the natural stills→video path needed a hand-placed
`ConvertNode` in it. A `Linear` image saved as JPEG was accepted silently and read back tagged
`sRGB` over pixels a whole transfer curve away, with nothing logged. A `Premultiplied` PNG came
back labelled `Straight` over already-scaled colour, which is the data corruption
`ImageWriter::canEncode` exists to prevent. TIFF wrote "unassociated alpha" into a durable file for
an image whose alpha mode lain had never been told. And because the tag came from the *format*
rather than the file, the same pictures behaved differently depending only on how they were stored:
as JPEGs they rendered to video stamping an `sRGB` tag into the container on no evidence; as TIFFs,
or as PNGs lain itself wrote, they were refused until someone declared a space by hand.

## Decision

**A reader states only what the file states. A writer records the tag when the format can state it,
and refuses when it cannot.** Never invent, never silently drop.

- **Untagged reads as `Unspecified`.** A format-wide convention is not a substitute for a tag.
- **Metadata lain cannot represent also reads as `Unspecified`** — an ICC profile, an unrecognised
  curve. "A space we can't hold" is not "no space": the caller has to decide, so the reader says so
  rather than picking the nearest name.
- **A writer that cannot state a space refuses it**, so the loss is loud at `canEncode` instead of
  arriving later as a relabel. `Unspecified` is *accepted* by a still writer: "no claim" is
  something a still file can honestly express, and it round-trips exactly.
- **Each codec's colour decision is one file holding both directions**, on the
  `plugins/io/video/ffmpeg/src/colorpolicy.{h,cpp}` precedent, whose own header says why: *"BOTH
  DIRECTIONS LIVE IN ONE FILE ON PURPOSE… Splitting them across two files is how the two halves
  drift."* That drift is exactly the defect being fixed — a reader that read three chunks beside a
  writer that wrote none.
- **Each such file switches exhaustively on `ColorSpace`**, so a fifth enumerator fails the build
  (`-Werror,-Wswitch`) rather than silently changing nothing. Before this, no image codec had an
  exhaustive switch, and `BT709`'s arrival in ADR-0018 reached none of them.

What each format can state, which is what "refuse what won't fit" resolves to:

| | `Unspecified` | `sRGB` | `Linear` | `BT709` |
|---|---|---|---|---|
| **PNG** | no chunk | `sRGB` chunk (+ gAMA/cHRM) | `gAMA` 1.0 | **refused** |
| **JPEG** | no marker (reads back sRGB) | the JFIF header it already emits | **refused** | **refused** |
| **TIFF** | no tag | `TransferFunction` | `TransferFunction` | `TransferFunction` |
| **video** | refused at the seam | `IEC61966_2_1` | refused at the seam | `BT709` |

**PNG refuses `BT709`** because its only transfer handle is `gAMA`, BT709's exponent is 0.45, and
lain's own PNG reader answers `sRGB` for that window — writing it would produce a file lain itself
relabels. **TIFF is where `BT709` goes instead**: `TransferFunction` is a sampled curve that states
a transfer exactly, so it distinguishes two spaces that are byte-identical on disk. **JPEG refuses
`Linear` and `BT709`** because stb emits a fixed JFIF header and offers no marker to write a space
into. A caller who needs a space a format cannot hold converts, or writes TIFF.

**Video keeps its documented default** — untagged → `BT709` with a log line (ADR-0018) — and the
media differ for a reason rather than by accident. A still file may hold arbitrary data: a mask, a
heightmap, a normal map, a depth pass. "Untagged" there genuinely means unknown, and guessing would
be wrong about a whole class of legitimate images. A delivery-coded video stream is always a
picture encoded against *some* transfer; there is no such thing in practice as a linear-light h.264
stream, and refusing ordinary untagged footage would be loud and wrong.

### The two conventional arms, named as such

Everything above is a fact read from a file except two arms, and they are called out here so they
are not mistaken for readings:

- **PNG's `gAMA` window** (0.44–0.47 → sRGB, ≈1.0 → Linear). `gAMA` states an exponent, not a
  standard, and sRGB's curve is not a pure 1/2.2 power law. Kept because a gAMA-only PNG is common
  and discarding its one colour statement would be worse. Exact for lain's own files, which is what
  the `Linear` arm reads back.
- **JPEG's JFIF arm.** JFIF fixes BT.601 primaries and states no transfer at all, so "JFIF implies
  sRGB" is universal practice rather than specification. Kept because a JFIF-only JPEG is the
  overwhelmingly common case, and it is **logged** rather than applied silently. It is also the
  *lowest* precedence: an explicit Exif `ColorSpace`, or a profile lain cannot hold, overrides it —
  so the old behaviour's actual lie (an Adobe RGB JPEG reported as sRGB) is gone.

JPEG's precedence puts **Exif above the ICC profile**, which is the Exif specification's own
arrangement rather than a preference: `ColorSpace` is the field defined to answer this question, 1
means sRGB, and `Uncalibrated` is what a file says when the answer lives in the profile instead.
Ordering the profile first would discard an explicit sRGB statement in the very common case of a
camera JPEG carrying both — reading *less* than the file states, which is its own wrong answer.

### Where a format supplies a fact lain did not have

Two round trips change a tag, and both are the *format* speaking rather than lain inventing:

- **PNG, `AlphaMode::Unspecified` → `Straight`.** PNG's alpha is unassociated by specification, so
  an RGBA PNG's alpha genuinely is straight. `Premultiplied` is refused rather than written.
- **JPEG, `ColorSpace::Unspecified` → `sRGB`.** A JPEG always carries a JFIF header, so the
  convention above applies to lain's own file exactly as it does to a foreign one.

## Considered options

- **Make JPEG return `Unspecified` always** (delete the invented sRGB without parsing markers).
  Simplest, and it makes the "never guess" rule literally true — but it makes essentially every
  JPEG unusable without a hand-placed declaration, discarding explicit Exif tags the file *did*
  state. Rejected: reading less than the file says is not more honest than reading it.
- **Adopt video's rule for stills** (untagged → sRGB with a log line, uniformly). Attractively
  uniform, but wrong for the class of still images that are not pictures at all. Rejected; the
  media genuinely differ, and the ADR states why instead of flattening it.
- **Writers stay tag-free, and `canEncode` refuses anything but `Unspecified`/`sRGB`** — mirroring
  `io::video::canEncode`, making the loss loud without changing any output bytes. Rejected: it
  leaves lain unable to round-trip its own images, so the `ConvertNode` in every stills→video graph
  stays required forever.
- **TIFF states colour with an embedded ICC profile** rather than `TransferFunction`. What most
  tools do, and better interoperability — but it means synthesising and shipping profile blobs for
  three spaces, and matching them on read. `TransferFunction` needs no blob, is generated from
  `image::toLinear` (so the curve written and the curve `image::convert` applies are one function),
  and compares exactly. Cost, accepted and stated in `tiffcolor.h`: the table is fixed by the TIFF
  specification at 2^BitsPerSample entries per colour channel, so 1.5 KB on an 8-bit RGB image and
  384 KB on a 16-bit one — under 1% of any 16-bit image anyone actually writes, disproportionate
  only for a tiny one.
- **Fuzzy-matching a foreign TIFF curve** against the ones lain knows. Rejected: it would be a
  second gAMA-style heuristic, in the one place that could be exact. A near-miss reads as
  `Unspecified` and the caller declares, which is the contract images already have.

## Consequences

- **lain's own round trip keeps the tag**, so the `ConvertNode` that the stills→video path needed
  is no longer structural. The `assume` param remains what a caller uses for genuinely untagged
  input.
- **Existing files read differently, and this is the point.** A JPEG carrying an ICC profile or an
  explicit Exif `Uncalibrated` now reads `Unspecified` where it used to read `sRGB` — including a
  fixture in this repo, generated by `sips`, which carries a grey gamma-2.2 profile and was being
  reported as sRGB. A TIFF written by lain now carries a curve and reads back tagged.
- **Existing output bytes change.** PNG and TIFF files lain writes gain colour tags; a TIFF's
  ExtraSamples value changes for an image with no stated alpha mode.
- **Some saves that used to succeed now fail** — `Linear` or `BT709` to JPEG, `BT709` to PNG,
  `Premultiplied` to PNG. Each is a case that previously produced a mislabelled file, and each
  names the conversion in its refusal, which `Image::toString` now carries the tags for.
- **The matrix is a test, not a table in a document.**
  `plugins/io/image/test/test_colorroundtrip.cpp` asserts the property across every enabled codec —
  a tag survives or is refused — and one case asserts that *some* codec can carry every
  `ColorSpace` lain has, so adding a standard that nothing can save fails there rather than being
  discovered later.
- **The same denylist-versus-allowlist defect existed one level up, in the video reader, and was
  fixed straight after** (see ADR-0018's 2026-09-03 amendment). Its refusal lists named the
  standards to reject and let everything else fall through to BT709 — including the LOG transfers,
  `AVCOL_TRC_LINEAR`, and DCI-P3/Display P3/XYZ primaries. They are now allowlists, so an
  unrecognised tag is refused rather than claimed, which is the same lesson this ADR's per-format
  capability tables encode: **default to refusing, not to claiming.**
- **One finding did NOT survive being measured, and the correction is worth recording.** The
  untagged YUV decode matrix looked like a bug — lain tags such footage BT709 while swscale's
  default decodes it with BT.601 coefficients — and the obvious fix was to drive the matrix from
  the resolved policy. Measurement refuted it: lain's own untagged fixture decodes back to its
  source colour exactly under BT.601 and 10 counts out under BT.709, at 720p as well as at 64×48,
  because an encoder that writes no tag is one that used BT.601. **The transfer and the matrix are
  separate questions with different right answers**: the BT.601 and BT.709 OETFs are the same curve
  to within rounding, so ADR-0018's transfer guess is free, while the coefficient sets are 17%
  apart on a saturated green. The behaviour therefore stayed and became a stated, tested, logged
  decision (`decodeMatrixFor`) instead of a library default nobody had chosen — which was the real
  defect all along.
