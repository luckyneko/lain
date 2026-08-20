# FFmpeg (LGPL configuration) for video codec support

Video decoding and encoding are provided by one optional FFmpeg-backed codec plugin behind the
lain-owned `lain::io::video` seam. FFmpeg is **found, not fetched**, must be an **LGPL
configuration**, and no FFmpeg type crosses a lain-owned interface. Its LGPL-2.1 licence is the
second approved, scoped exception to the permissive-by-default dependency policy of
[ADR-0015](0015-permissive-by-default-production-dependencies.md).

Delivery-codec encoding uses **platform hardware encoders through FFmpeg's own wrappers**
(`h264_videotoolbox`, `hevc_videotoolbox`, the Media Foundation and NVENC wrappers), which are
LGPL-safe because the encoding happens in the operating system. Archival encoding uses FFmpeg's
native ProRes, FFV1 and MJPEG encoders. **libx264 and libx265 are never enabled**: they are GPL, and
linking a build configured with them relicenses the whole distribution regardless of which encoder is
called.

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

## Consequences

The plugin is opt-in, and an all-disabled configure performs no dependency resolution — the rule
already agreed for the OpenCV and Ceres camera plugins. When video is disabled the video node kinds
are not registered, and a saved graph referencing one produces an unknown-`kind` `LoadIssue` through
the existing best-effort load rather than failing opaquely.

Distribution preserves FFmpeg's notices, identifies how recipients obtain the corresponding source,
permits relinking (satisfied by dynamic linking), and publishes any modifications to FFmpeg itself
under LGPL. This does not change lain's MIT licence or the licence of lain-owned files. The root
`README.md` records FFmpeg as an approved planned dependency; its exact version, configure string
and enabled components replace that planned status when the integration lands.

FFmpeg is autotools and does not fit the `cmake/addXXX.cmake` FetchContent idiom, so it is located
rather than built by the repository. macOS is the platform least served by ready LGPL artifacts and
should be confirmed early, since it is the primary development target.

Because the dependency is optional and lands after the medium-neutral sequence work, the first
`FrameSequence` backend is an **image sequence** built on the existing image codecs. That vertical
exercises laziness, identity, clipping, homogeneity, the render sweep and bounded memory with no new
dependency and no licensing question, so an FFmpeg integration that proves troublesome does not
block a working feature.
