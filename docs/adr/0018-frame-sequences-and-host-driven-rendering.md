# Frame sequences and host-driven rendering

A **frame sequence** is a finite ordered list of **frame references** into one or more **frame
sources**, not a handle to a file and not a chain of wrapping decoders. A graph consumes a sequence
and produces **one `image::Image` per frame**; the **host** owns the frame loop, binding a frame
position, running the graph, and handing each result to an encoder. Sequence-valued outputs are
therefore limited to selection — clip, reorder, concatenate, subset — and never carry processed
pixels.

This is the mechanism `lain::media` provides, `lain::io::video` and `lain::io::image` produce, and
Milestone 9 consumes. It supersedes nothing; it fills the gap `WORK.md` has held open since M4 as
"the video boundary loader", and discharges ADR-0014's *"the `Collection` payload"* question by
answering it outside the map.

## A sequence is a list, not a decoder chain

Every sequence operation is a list operation: clip slices, concatenate appends, reverse reverses,
stride strides, and "the thirty views calibration selected" is an arbitrary subset. Exactly one
class decodes. The rejected alternative — an abstract `FrameSource` with `ClippedSource`,
`ConcatSource` and `ReversedSource` wrappers — makes each verb a new class and makes
clip-of-concat-of-clip a chain of virtuals whose flattening nobody owns.

The list form is also what `CONTEXT.md` already described. A **camera-sequence definition** is *"the
ordered media segments that form one logical camera timeline"* and a **media segment** is *"one video
range, image-sequence pattern, or other bounded media source"*. That is verbatim a list over several
sources, and it means one sequence can span several video files, or a video file and a folder of
stills, with no new concept.

**Position and frame identity are distinct, and both are carried.** A clip re-bases position while
preserving identity: frame 0 of `clip(seq, 100, 50)` is position 0 and still ordinal 100 of its
source. Anything positional — `at(i)`, a map element, the timeline — uses position; anything that
*reports* a frame uses identity. They are equal only for an unclipped source, and diverge the moment
anyone clips.

## The decoded unit is an Image, and provenance travels beside it

`FrameAt` has two outputs: `image : image::Image` and `frame : FrameRef`. There is no `Frame`
aggregate.

An aggregate forks the node type system — every existing node takes `Image`, so `Frame` would need an
unwrap at every junction or a parallel set of `Frame`-typed nodes — and then the next caller wants
`Image + depth` and the one after wants `Image + mask`. ADR-0016 already refuses this shape for
camera evidence: a board observation *"identifies but does not contain its source image"*.

The specific pairing that motivates an aggregate, `Image + intrinsics`, is **not a frame property at
all**. `CONTEXT.md`'s camera-model stability requires that *"one camera model applies throughout a
calibration frame sequence"* and fails a request on evidence of change. An aggregate would encode a
per-frame variation the domain model rejects, and duplicate one immutable value across every frame.
Everything of that kind belongs to the **sequence**: its frame spec, its rate, its camera model, and
in Milestone 9 its capture dataset.

A metadata bag on `image::Image` is likewise refused. ADR-0003 admits exactly two extra axes,
ColorSpace and AlphaMode, because both are *enforced* against operation classes; a free-form bag is
unenforceable and its propagation through a merge has no honest answer.

`FrameRef` is `{canonical source URI, ordinal, timestamp}` — a **name you can look up**, never a
back-pointer. A reference holding `shared_ptr` to its sequence would keep a decoder, its handle and
its cache alive for as long as any evaluation retains any single decoded frame, and `flow` retains
values per node, per element, across runs. The source identity is the canonical URI rather than a
minted UUID because a UUID changes every run, which would make a saved manifest meaningless and break
the reproducibility record's *"canonical input identities"*. It is the same rule
`graphio::templateKey` already applies: one file has one key however it is spelled.

The accepted cost: provenance does not survive processing automatically. `blur(image)` yields an
`Image` with nothing attached, and a consumer that needs the frame wires the `frame` pin around it.
Inside a map it survives positionally for free, which is the reconstruction workload.

## A processing graph cannot output a frame sequence

This is structural rather than a policy. A frame reference names a source and an ordinal within it;
a frame a graph **computed** has no source, so there is nothing to reference. Sequence-valued outputs
are consequently legal for selection and illegal for processed pixels, with no rule to police.

Making `result : FrameSequence` carry processed frames would require a graph-backed source whose
`decode(i)` re-enters the scheduler — taking a run lease already held, inverting push into pull at
the payload level, and making `compute()` transitively call `run()`. That is the deadlock shape
`CLAUDE.md`'s hard contracts name: *"fire-and-join only… a node body never blocks on a nested graph
run"*.

The useful case survives intact: a node group that **selects** frames by a metric outputs a real
sequence. `CONTEXT.md` already calls that **calibration view selection**, and Milestone 9 consumes
its output.

## The host owns the frame loop, not a map

A render is a **fold, not a map**: it does not produce a collection, it consumes frames one at a time
and produces a file. Expressing it as a map materialises the collection nobody wanted. ADR-0014 fixed
a map's output as a gathered `std::vector<T>`, deliberately, because a hole cannot be represented — so
mapping a 500-frame range at 4K costs roughly 16 GB of gathered output plus around 50 GB of retained
per-element interior state, and 8K multiplies both by four. ADR-0014 records the retention as
mandatory (*"child state is retained per element… and these are images"*, required by the element
stepper) and per-element incrementality as unavailable (*"one new frame on one stream would recompute
all N"*). Milestone 9's prerequisite note reached the same conclusion independently.

So the graph is a per-frame function and something outside drives the position. A **map remains
correct for small-N analysis** — calibration's selected views producing board observations, feature
extraction producing tracks — where N is tens and the per-element output is megabytes. The map is
scoped, not rejected.

The position reaches the graph as **an ordinary boundary input**, using M4's binding, ADR-0012's
recompute request and incremental evaluation exactly as they already are. The rejected alternative,
an ambient frame cursor on the `Evaluation`, is a second dataflow channel that bypasses every
invariant: it breaks *"a node reads only its evaluated inputs"*, it requires inventing a per-node
"time-varying" flag that can disagree with what the node actually reads — the failure mode M7 removed
by deleting `editableAt` and M8 removed by making split-or-broadcast the declared type — and it has no
answer inside a map, where `EvalPath` already carries an element index.

**`FramePosition` is a distinct port type**, not an `int`. It is what lets the CLI find the loop
counter without a naming convention, what lets `list` report a graph as renderable, and what will let
a boundary editor render a timeline by *type* — ADR-0005's own rule, and its own test for invention:
*"prefer an existing type, inventing one only where none fits"*. A plain `int` cannot say "this is a
position in a frame sequence", so nothing downstream can act on it, and a magic pin name called
`frame` breaks silently when a linked group renames its interface. It is spelled `FramePosition`
rather than `FrameIdx` because nothing in this codebase abbreviates (`PortId`, `NodeId`,
`PortAddress`, `BoundaryPin`) and because *index* is a spent word here — `CONTEXT.md`'s *Position*
entry records that the retired `PortIndex` alias *"existed only to be stored, which is the mistake
the declaration-id contract exists to prevent"*.

**The loop retains one `Evaluation` across the whole range.** The node that opened the sequence
(`openSequence` — one medium-neutral node over an opener registry, settled 2026-08-31; see
[ADR-0019](0019-ffmpeg-lgpl-for-video-codec-support.md)) is upstream of nothing that
changed between iterations, so it does not recompute and its sequence value persists — open handle,
ring cache and decoder position intact, which makes sequential access the decoder's best case. A
fresh Evaluation per frame would reopen and cold-seek every iteration.

## A sequence is homogeneous, indexed at open, and lazily decoded

**Homogeneous.** A sequence exposes one **frame spec** — extent, `PixelFormat`, `ColorSpace`,
`AlphaMode`, nominal rate — and composing a source whose frames do not match is refused at the point
of composition. Under any weaker rule the only way a consumer could cope is by converting, and a
conversion chosen downstream on a frame it did not know would differ is precisely the silent lossy
conversion this design exists to prevent. Camera-model stability already requires the guarantee, so
Milestone 9 inherits rather than re-checks it. Variable-resolution streams and genuinely mixed
material are refused and reported.

**Indexed at open.** The container's own index is used when present; otherwise the source is
demux-scanned without decoding, recording per frame its offset, presentation timestamp and keyframe
flag.

> **Amended 2026-09-02, on building it (M10 slice 5b).** The first clause does not hold and the
> second is what actually happens, always. A container's index holds **keyframe entries only**, so
> it can say where to start decoding but never what frame 412 is — the question this whole section
> rests on. So the table is *always* built by the decode-free scan, and the index is used for seek
> points alone. Two further facts the implementation settled: the table must be sorted into
> **display order**, because with B-frames the packets do not arrive in it and a table left in
> arrival order hands back a different image than the one asked for (a wrong answer, not an error);
> and the cost is one sequential read of the container's packet headers at open, which is the price
> of the exactness promised here. That is what makes the promises true: the sequence is genuinely finite, "frame 412" means frame
412 on every platform and every run, the ring cache can decode a GOP forward instead of re-seeking,
variable frame rate is handled because frames are ordinal and only timestamps are irregular, and the
identities are deterministic. The cost is one decode-free scan for indexless containers; for MP4 and
MOV it is a table read.

**Lazily decoded, with a fixed ring cache, blocking.** A sequence is logically immutable and
physically memoising, which satisfies `flow`'s actual requirement — *"a recompute never disturbs a
payload another slot is still reading"* — through internal synchronisation rather than inertness.
`CONTEXT.md` pre-committed to this wording: *"concurrent decode requests are safe even when an
implementation serializes access internally"*, which promised callers nothing about parallelism. One
decoder behind a mutex is the first implementation; a decoder pool is a later swap behind the same
handle. `at()` blocks and yields an invalid `Image` on failure, the convention `LoadImageNode`
already uses — `compute()` is synchronous everywhere in this engine, and an async payload would be
the first thing to break that.

## Writing is stateful; the policy belongs to the host

`ImageWriter` is one-shot; a video encoder is open-push-finalise and its file is invalid until
finalised. The seam is therefore a **writer handle** — `openWriter(uri, spec, options)`,
`write(image)`, `finish()` — with a one-shot `save(uri, sequence)` facade layered on it for the
transcode case. `finish()` is explicit and returns a status, because a trailer write can fail and a
failure in a destructor has nowhere to go.

The output spec comes from the first frame; later frames must match, refused rather than rescaled.
Frame rate defaults to the bound sequence's rate and is required explicitly when there is no sequence
input.

**Amended 2026-09-02 (M10 slice 6a), two things this decision left implicit.** First, *which* specs
a writer accepts at all. The seam refuses **alpha** (no codec in the LGPL set carries it, and every
one reaches a subsampled plane with nowhere to put it), **`Linear`** and **`Unspecified`** — the
`ImageWriter` rule, *refuse rather than silently degrade*, applied to a `FrameSpec` instead of an
`Image`. `Linear` is the arm worth stating: `AVCOL_TRC_LINEAR` exists and the file would encode
fine, but the read policy above does *not* refuse it, so the result comes back tagged BT709 with
every value off by the ~2.2 gamma — invisibly. Tolerating it would make lain's own round trip
relabel footage, which is the failure this ADR refuses on the way in, arriving by the other door.
And untagged material is treated as BT709 **on the way in** while being refused **on the way out**:
reading is a guess about someone else's file, writing is authorship, and a guess baked into a file
outlives the guess.

Second, what a caller may ASK a writer for. Selection is by availability, so the request is a
**codec family** — `auto / h264 / hevc / prores / ffv1 / mjpeg` — never an encoder's name.
`h264_videotoolbox` is a fact about one platform and one build, so a command line or a saved
document naming it stops working on the next machine, which is precisely what "by availability,
never by a hardcoded name" exists to prevent. The backend probes and *reports* the encoder it found.
`auto` means delivery-or-refuse and never slides into an archival family: a delivery render that
quietly became MJPEG is a wrong answer that looks like success — the same argument this ADR already
makes about a silently shortened video.

**A suppressed frame is the host's policy, not the writer's.** The default stops the render,
finalises what exists, reports the ordinal and exits non-zero: a truncated video is visibly
truncated, whereas a silently shortened one has destroyed the correspondence between input and output
frame and nothing downstream can tell. An explicit flag opts into skipping. Note the asymmetry — a
numbered image sequence *can* represent a hole, as a visible gap in the numbering, while a video
cannot and must close up.

## Consequences

`lain::media` owns the sequence, references, spec and list operations and depends on no `io` at all;
each medium's opener lives in that medium's own io seam (`io::image::openSequence`,
`io::video::open`). A `media` that knew how to open things would depend on every medium, which is the
junk drawer `CONTEXT.md`'s loading split exists to prevent.

A sequence-valued output writes a **manifest** by default rather than pixels, because a selection's
honest artifact is "these frames of that source". The manifest is `data::toValue` over the sequence
through the existing reflection and codec spine, not a bespoke writer, so it is json today and yaml
later for free.

A sequence holds an open handle per **source** for as long as any evaluation retains the value, and a
multi-segment timeline holds one per segment. The `Stream` transport is therefore specified so a
future LRU handle pool is a pure backend swap: it never hands out an OS handle, owns its URI and
logical position so it can reopen and re-seek, lets every operation fail rather than only open, and
keeps acquisition separate from construction.

`ColorSpace` gains `BT709`. Video decodes against a transfer function that is not sRGB, and the
existing values force a choice between a silent lie and making video frames unprocessable, since
op-class enforcement requires `Linear` for any cross-pixel operation. BT.601, BT.2020 and HDR
transfers are **reported and refused** rather than relabelled: they need different primaries or a
real HDR story, and mislabelled footage is a wrong answer in a calibration report.

**Settled 2026-08-31, two open details.** *Which* BT.709 curve, first: the name covers both the
**Rec.709 OETF** (the camera-side encoding in the spec — a linear slope near black, then a 0.45
power) and the **BT.1886 EOTF** (the display-side pure 2.4 gamma standardised later, because Rec.709
never specified the display end). They differ by the ~1.2 system gamma, so the choice is visible:
pick wrong and video sits noticeably lighter or darker than sRGB stills in the same graph.
`convert(img, Linear)` applies the **inverse Rec.709 OETF** — it is the curve the encoder actually
applied, it is what "Rec709 to linear" means in Nuke and OCIO's basic configs, and it inverts
exactly, so decode → Linear → BT709 → encode round-trips. lain applies **no OOTF** and does no
display rendering; BT.1886 viewing sits on the same shelf as HDR.

Second, and found by probing real files rather than reasoning: encoded footage very often carries
**no colour tags at all** (`color_space=unknown`, `color_transfer=unknown`). FFmpeg's convention is
to guess BT.601 for SD frame sizes, which under the refusal rule above would reject perfectly
ordinary material for a tag it never carried. So the refusal applies to **explicitly tagged**
BT.601 / BT.2020 / PQ / HLG only; unspecified is treated as BT709 with a log line. That is a guess
either way, and this one is right for everything modern while the alternative is loud and wrong.

**Amended 2026-09-03.** That default is the VIDEO arm of a general codec rule, not the rule itself:
[ADR-0020](0020-codec-colour-tag-policy.md) settles what any codec may claim, and there a reader
states only what the file states, so an untagged STILL is `Unspecified`. The media differ for a
reason. A still file may hold a mask, a heightmap or a depth pass, where untagged genuinely means
unknown; a delivery-coded video stream is always a picture encoded against some transfer, which is
what makes the guess here right and the same guess wrong for stills. ADR-0020 also records the
three loose ends this decision left in the video reader — the BT.601 decode coefficients used for
untagged footage, the P3/XYZ primaries that are accepted rather than refused, and the log line that
misses a file tagged only by its matrix.

Per-element incrementality, keyed elements, a linked map over N streams, realtime playback of
processed output, and the capture manifest and capture dataset remain outside this decision.
Playback in the viewer is best-effort: advancing a frame rebinds and re-runs, so a heavy graph
simply evaluates as fast as it can.
