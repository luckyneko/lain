# FFmpeg (LGPL configuration) for video codec support

Video decoding and encoding are provided by one optional FFmpeg-backed codec plugin behind the
lain-owned `lain::io::video` seam. FFmpeg is **fetched from a pinned, tier-verified prebuilt
archive** (amended 2026-08-31; originally *found, not fetched* — see below), must be an **LGPL
configuration**, and no FFmpeg type crosses a lain-owned interface. Its LGPL-2.1 licence is the
second approved, scoped exception to the permissive-by-default dependency policy of
[ADR-0015](0015-permissive-by-default-production-dependencies.md).

Delivery-codec encoding uses **platform hardware encoders through FFmpeg's own wrappers**
(`h264_videotoolbox`, `hevc_videotoolbox`, the Media Foundation and NVENC wrappers), which are
LGPL-safe because the encoding happens in the operating system. Archival encoding uses FFmpeg's
native ProRes, FFV1 and MJPEG encoders. **libx264 and libx265 are never enabled**: they are GPL, and
linking a build configured with them relicenses the whole distribution regardless of which encoder is
called.

**Amended 2026-08-31 — fetched, not found.**
[`luckyneko/ffmpeg-prebuilt`](https://github.com/luckyneko/ffmpeg-prebuilt) now publishes
tier-verified LGPL shared archives for `macos-arm64`, `linux-x86_64`, `linux-arm64` and
`windows-x86_64`, each carrying upstream's licence texts, an `INVENTORY.txt` of every codec, and a
`MANIFEST.txt` stating tier, full configure string and corresponding-source url. A release **archive**
fits the `cmake/addXXX.cmake` FetchContent idiom perfectly — the objection below was to building
*autotools*, not to fetching — so `addFFmpeg.cmake` fetches a pinned, hash-checked archive by
default, and `LAIN_FFMPEG_ROOT` points at a system install for anyone who wants one. That path is
subject to the same tier gate; nothing is trusted for being local. The macOS worry recorded at the
end of this ADR is thereby retired.

The **delivery encoders actually available** are narrower than the sentence above implies, and were
established by reading the shipped inventories rather than assumed: every one of these backends is
`[autodetect]` in FFmpeg's configure, so a hermetic `--disable-autodetect` build ships only what its
configure line names. As of `ffmpeg-8.1.2-lgpl` that is `h264_videotoolbox` / `hevc_videotoolbox` on
macOS, `h264_mf` / `hevc_mf` / `h264_nvenc` / `hevc_nvenc` on Windows, and `h264_vaapi` /
`h264_v4l2m2m` / `h264_nvenc` / `hevc_nvenc` on Linux. NVENC compiles against NVIDIA's MIT-licensed
headers and `dlopen()`s the driver at run time, so it links nothing restrictive and needs no
`--enable-nonfree` — that requirement belongs to libnpp and the CUDA SDK filters. **Decoding is
uniform** across every target, as is archival encoding, so only the delivery-write path is
platform-conditional, and the writer selects by availability rather than by a hardcoded name.

## Why not the alternatives

**Platform-native backends** (AVFoundation/VideoToolbox, Media Foundation) need no exception and
fetch nothing, but they fail on three counts. Linux has no equivalent framework, so the option
defers FFmpeg rather than avoiding it — and the usual substitute, GStreamer, is LGPL too and
typically wraps FFmpeg anyway. Windows codec availability is not guaranteed at runtime, with HEVC
and AV1 depending on Store-installed extensions. Decisively, **frame-accurate seeking differs
between backends**, and [ADR-0018](0018-frame-sequences-and-host-driven-rendering.md) requires that
"frame 412" mean frame 412 on every platform and every run; three backends would mean three
seek-accuracy behaviours, producing discrepancies that surface only inside a calibration report.

**A narrow permissive stack** (libvpx, SVT-AV1, libwebm — all BSD) needs no exception and cannot
read the H.264 and HEVC MP4s that every real camera produces, including the D455 fixture Milestone 9
specifies.

## The configuration is enforced, not documented

The licence lives in the build configuration, not the API called. Linking an FFmpeg built with
`--enable-gpl` produces a GPL combined work even if no GPL codec is invoked — which is the trap in
the most convenient sources, since Homebrew and most Linux distribution packages ship GPL-configured
builds. Prebuilt LGPL artifacts (for example BtbN's separated `-lgpl-` builds) and feature-flagged
source builds (vcpkg, Conan) are the supported routes.

FFmpeg reports its configure string at runtime through `avutil_configuration()`. The build therefore
**fails configure when `--enable-gpl` or `--enable-nonfree` appears in the linked library's
configuration**, and records the exact string in the dependency inventory. This is the same species
of guard as `EIGEN_MPL2_ONLY`: a policy the build enforces rather than one a future contributor must
remember.

**Amended 2026-08-31 — the gate reads the manifest; the runtime check stays as a test.** A prebuilt
archive states its tier, full configure string and corresponding source **as text**, so the
configure-time gate parses `MANIFEST.txt` and executes nothing. That matters beyond convenience: a
`try_run` against `avutil_configuration()` cannot run when cross-compiling, which is precisely when a
consumer is least able to inspect what it linked. The compiled probe does not disappear — it becomes
a permanent `[video]` **runtime** test asserting the linked library agrees, which is belt and braces
for the fetched path and the only cover for a `LAIN_FFMPEG_ROOT` install. Two independent things must
now be wrong at once for a GPL-configured FFmpeg to reach a build, which is the same rule the
publishing repository applies to its own artifacts.

## Consequences

The plugin is opt-in, and an all-disabled configure performs no dependency resolution — the rule
already agreed for the OpenCV and Ceres camera plugins. When video is disabled the video node kinds
are not registered, and a saved graph referencing one produces an unknown-`kind` `LoadIssue` through
the existing best-effort load rather than failing opaquely.

**Amended 2026-08-31 — there are no video node kinds; there is one `openSequence` node over an
opener registry.** Disabling video should remove a *capability*, not a graph's vocabulary. An
unknown `kind` drops the node **and every edge attached to it**, so a document authored with video
would come back structurally damaged on a build without the plugin, and re-saving it would make that
permanent. With one medium-neutral node the document loads intact and reports *"no opener for
`.mp4`"* when run — which is both more informative and exactly how `io::image::load` already behaves
for a format whose codec was not compiled in. The registry lives in a small `libs/io/media` facade
(`lain::media` depends on no `io`, so the dispatcher cannot live there); each medium seam registers
into it, and the app calls `registerSequenceOpeners()` beside `registerImageCodecs()`.

**Built 2026-09-02 (M10 slice 5a), with three corrections to that sentence.** The facade is
`lain::io::sequence`, not `io::media` — inside a namespace called `media` the name shadows
`lain::media` at every mention (settled at slice 2). The wiring runs the other way round: the
media seams do not know the dispatcher exists, and `io::sequence`'s own `openers.cpp` — the one
translation unit in that library naming a medium — registers them, which keeps the dependency
acyclic and leaves the registry itself medium-free for anything registering from outside this
tree. And dispatch is **by extension with one default**: video claims container extensions by
name, while the image medium is the default because it is addressed *structurally* (a folder has
no extension, and `shot.####.png`'s extension names the still format rather than the sequence's).

The consequence above only holds because **the seam `lain::io::video` is built unconditionally
while its codec plugin is not.** The container-extension claim therefore lives in the seam, so
`.mp4` still means *video* in a build with no video codec and is refused with that reason. Had
the claim lived in the plugin it would have vanished with it, and an `.mp4` would have fallen
through to the still opener to be told it is not a directory — the vocabulary loss this amendment
exists to prevent, arriving by another door. Note also that the video **reader** registry is keyed
by *backend* name rather than by format, unlike `io::image`'s: a demuxer identifies containers by
content, so an extension key there would be a second, worse answer to a question it already
answers.

**Backend, not codec — and container and codec stay one interface.** A video file has two aspects,
a **container** (mp4, mov, mkv) and a **codec** (h264, prores, vp9), and `"ffmpeg"` is neither: it
is one implementation covering both, as a platform-native alternative would be. Decomposing the
reader into `Demuxer -> Packet` and `Decoder(Packet) -> Image` was considered and rejected, and not
for the weak reason that one library happens to do both. A demuxer's output is **not**
codec-neutral — the same H.264 stream leaves an MP4 as length-prefixed AVCC with its parameter sets
in the container's `extradata` and a TS as in-band Annex-B, which is why FFmpeg needs a bitstream
filter between them — so that seam is wrong until lain models bitstream filters too, and its
wrongness would be found by a file rather than by a compiler. "Decode frame 412" is also one
algorithm across both halves: seek to the preceding keyframe (demuxer), flush, decode forward
matching by pts (decoder). The distinction is real and appears as **reported facts** (the reader
names the container and codec it found) and, for the writer, as **options** — the codec chosen by
availability while the container follows the extension. Revisit the split when lain owns a demuxer
of its own and wants a standard codec to decode its packets, or when a second backend covers a
disjoint set; `Packet` is the type that would have to exist first. The dependency keeps being
called "the video codec plugin" throughout this ADR, the README and `LAIN_IO_VIDEO_FFMPEG`, which
is what it is; *backend* is what the registry chooses between.

Distribution preserves FFmpeg's notices, identifies how recipients obtain the corresponding source,
permits relinking (satisfied by dynamic linking), and publishes any modifications to FFmpeg itself
under LGPL. This does not change lain's MIT licence or the licence of lain-owned files. The root
`README.md` records FFmpeg as an approved planned dependency; its exact version, configure string
and enabled components replace that planned status when the integration lands.

FFmpeg is autotools and does not fit the `cmake/addXXX.cmake` FetchContent idiom, so it is located
rather than built by the repository. macOS is the platform least served by ready LGPL artifacts and
should be confirmed early, since it is the primary development target.

**Superseded 2026-08-31 by the amendment above.** The premise was right and the conclusion no longer
follows: nothing needs to *build* autotools to *fetch* an archive. The macOS gap this paragraph
identified was real — it is why `ffmpeg-prebuilt` exists — and it is now closed for `arm64`. There
is deliberately no `macos-x86_64` artifact: GitHub has retired the `macos-13` image and drops macOS
Intel entirely in August 2027, so `addFFmpeg.cmake` fails there with a clear message rather than
requesting a url that does not exist.

Because the dependency is optional and lands after the medium-neutral sequence work, the first
`FrameSequence` backend is an **image sequence** built on the existing image codecs. That vertical
exercises laziness, identity, clipping, homogeneity, the render sweep and bounded memory with no new
dependency and no licensing question, so an FFmpeg integration that proves troublesome does not
block a working feature.
