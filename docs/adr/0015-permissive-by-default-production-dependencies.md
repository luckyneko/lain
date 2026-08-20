# Permissive-by-default production dependencies

Production dependencies default to permissive licenses compatible with lain's MIT distribution,
including MIT, BSD, Apache 2.0, Boost, zlib, and similarly scoped licenses. Copyleft or mixed-license
dependencies require an explicit documented exception; dependency reviews include transitive
libraries and enabled optional features so a permissive top-level library cannot silently introduce
an unapproved solver or runtime.

Eigen is the first approved exception. Its MPL-2.0 file-level copyleft is accepted only for the
optional Ceres-backed camera registration implementation. This does not change lain's MIT license or
the license of lain-owned files. Builds define `EIGEN_MPL2_ONLY`; distribution preserves required
notices, identifies how recipients can obtain the corresponding Eigen source, and makes any
modifications to MPL-covered Eigen files available under MPL-2.0.

FFmpeg is the second approved exception, recorded in
[ADR-0019](0019-ffmpeg-lgpl-for-video-codec-support.md). Its LGPL-2.1 licence is accepted only for
the optional video codec plugin, only in an LGPL configuration, and the build **fails** when the
linked library reports `--enable-gpl` or `--enable-nonfree` in its configure string. The enforcement
matters more than the permission: a GPL-configured FFmpeg relicenses the combined work whether or not
a GPL codec is ever called, which is exactly the silent introduction this policy exists to prevent.

## Consequences

Third-party dependencies remain optional or private behind lain-owned interfaces where practical.
The repository `README.md` is the reviewed inventory of current and approved planned third-party
licenses. It records exceptions visibly so a dependency can be audited or replaced later. Exact
versions, enabled modules, transitive dependencies, and licenses must be recorded when an
implementation lands, and optional features with unapproved licensing are disabled even when
upstream enables them by default.
