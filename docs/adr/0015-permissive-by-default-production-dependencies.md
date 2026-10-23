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

## Every built dependency's notice travels with the binary

**Amended 2026-10-23.** Every dependency compiled or linked into a `lain::app` binary prints its
notice through `--licenses`, and so do lain and its submodules (archimedes, multi). The README row
is not enough for a distributed binary: it carries no copyright lines and does not travel with the
binary, while MIT, BSD-3-Clause, libtiff's licence and Apache-2.0 each ask that the notice or the
licence accompany copies. Taken by the repo owner as recommended, over printing only the notices a
licence demands and over recording that lain distributes source only.

- **Every built dependency, not only those that oblige.** fmt waives the notice for compiled
  object code; zlib's, libpng's and GLFW's terms bind only source distributions; stb can be taken
  as public domain; portable-file-dialogs is WTFPL. Their notices print anyway. One rule keeps a
  per-licence legal reading out of the build, and the test checks one property.
- **The text is the dependency's own.** A header line names the dependency, its version and its
  licence; the licence file follows verbatim (indented), read from the fetched source at configure
  time by `lain_register_notice` (`cmake/lainNotices.cmake`), never transcribed. A licence file
  missing from a fetched source fails configure. Only the Apache-2.0-only dependencies (the Vulkan
  loader, MoltenVK on macOS) stage their text into `third-party/<id>/` and print a pointer instead,
  as OpenCV's notice already did. FFmpeg's, OpenCV's and Eigen's notices stay as they were: they
  carry corresponding-source and manifest facts beyond a licence.
- **A dependency satisfied by a system package** was not fetched, so there is no file to read:
  its notice names the package, the version found and where it is, whose own licence files came
  with it.
- **Build-level, not per-binary**, as FFmpeg's notice already was (ADR-0019): `lain::app` cannot
  know which plugins a consumer links, so it prints every dependency the build contains.
- **Tested both ways.** `libs/app/test/test_notices.cpp` holds the notices to the configuration:
  each built dependency named exactly once, none that was not built, no notice the table does not
  expect, and lain's own LICENSE present line for line.

Not covered, and each recorded in WORK.md's Outstanding index: a staged text reaches a recipient
only if `third-party/` ships with the binary, and lain has no packaging step; MoltenVK's release
archive carries only MoltenVK's own licence, though its dylib compiles in SPIRV-Cross.

## Consequences

Third-party dependencies remain optional or private behind lain-owned interfaces where practical.
The repository `README.md` is the reviewed inventory of current and approved planned third-party
licenses. It records exceptions visibly so a dependency can be audited or replaced later. Exact
versions, enabled modules, transitive dependencies, and licenses must be recorded when an
implementation lands, and optional features with unapproved licensing are disabled even when
upstream enables them by default.
