# OpenCV arrives as a pinned, minimal, verified prebuilt

---
Status: accepted
Fills in how [ADR-0016](0016-camera-calibration-method-modules.md)'s optional OpenCV backend is
obtained. Amends [ADR-0004](0004-static-linking-service-shaped-seams-over-thorax.md) with a second
shared-linked dependency, for a different reason than the first.
---

Milestone 9 uses OpenCV for ChArUco detection and calibration estimation. Building OpenCV from
source inside lain is the cost this ADR avoids. It is a large CMake project with a long tail of
optional dependencies, several of which it downloads while configuring. M10 faced the same choice
for FFmpeg and answered it with a sibling repository publishing pinned, verified archives
([ADR-0019](0019-ffmpeg-lgpl-for-video-codec-support.md)). OpenCV gets the same treatment:
[`luckyneko/opencv-prebuilt`](https://github.com/luckyneko/opencv-prebuilt).

## Decision

**OpenCV is fetched from a pinned, hash-checked `opencv-prebuilt` archive. The line is 4.14, the
module set is fixed by a named profile, the libraries are shared, and every archive is verified
against its name before it is published.**

- **Profile `calib`:** core, imgproc, flann, features2d, calib3d, objdetect.
  - ChArUco (`aruco::CharucoDetector`) has lived in objdetect since 4.7, so contrib is not needed.
  - calib3d requires flann and features2d. That also covers the feature front end and geometric
    verification the later targetless slices need. *(Since M9 slice 3 sub-slice 7 the plugin links
    features2d and flann itself, for SIFT and Approximate matching. The module set is unchanged.)*
  - A published profile's module set never changes. Another module means a new profile name, so a
    pinned archive name cannot come to mean two products.
- **The 4.14 line, not 5.0.** 5.0 (June 2026) splits calib3d into geometry/calib/stereo and makes
  calib depend on objdetect and stereo, which is eight modules instead of six, in a `.0` release.
  4.x is still maintained alongside it, and ADR-0016's vocabulary was written against calib3d.
  Moving to 5.x later is a new profile plus a change contained in one plugin, because no OpenCV
  type crosses a lain-owned interface (ADR-0016).
- **Shared libraries.** This is the ADR-0004 amendment. The reason is **symbol isolation**, not
  licensing:
  - OpenCV compiles its own zlib (1.3.2) into `opencv_core`. lain statically links zlib 1.3.1
    (`cmake/addZlib.cmake`) into every binary that has the png or tiff codec, which includes
    flowview.
  - A static OpenCV puts two zlibs into one link. The best case is a duplicate-symbol failure; the
    worst is one silently answering the other's calls.
  - Shared, with OpenCV's `-fvisibility=hidden`, the bundled zlib is invisible, and the prebuilt's
    check proves no zlib symbol is exported.
  - Apache-2.0 would have allowed static linking. Nothing of lain's crosses this boundary, so
    ADR-0004's actual concern (lain's global state fragmenting across DSOs) is untouched.
- **Windows ships Release and Debug** (MSVC v143, `/MD` and `/MDd`, one archive). OpenCV's API passes
  `std::vector` and `std::string`, and an MSVC Debug consumer changes their layout
  (`_ITERATOR_DEBUG_LEVEL`), so it cannot use Release libraries. Windows is built with MSVC because a
  MinGW C++ library cannot be consumed by MSVC at all. FFmpeg could be built with MinGW only because
  its API is C.
- **Nothing optional is inside.** The prebuilt switches off every optional component explicitly:
  - no image, video or GUI I/O (lain has its own);
  - no Intel IPP (not an approved licence, and a binary blob downloaded at configure time);
  - no Eigen (MPL-2.0, whose exception ADR-0015 scopes to Ceres);
  - no LAPACK, OpenCL, TBB, OpenMP or CUDA (results would vary by machine);
  - no ARM acceleration libraries, so every target ships the same inventory. KleidiCV and ARMPL are
    also configure-time downloads.

  What remains is OpenCV (Apache-2.0) and the code upstream compiles into these modules, each
  permissive and each reviewed: zlib (Zlib), Berkeley SoftFloat (BSD-3-Clause), DLPack
  (Apache-2.0), KAZE/AKAZE (BSD-3-Clause), and MSER's chi-square table (BSD-3-Clause). None needs a
  new ADR-0015 exception.

## Verification lives in the prebuilt, and is re-checked here

The prebuilt's `check.sh` checks the built libraries before packaging:
- the exact module set, from the files and from the library's own report;
- that configure downloaded nothing;
- that the installed third-party licence texts are exactly the reviewed list, so a version bump
  that vendors something new fails until someone reads it;
- no exported zlib symbol;
- no dependency outside the OS, the platform C++ runtime or the archive;
- relocatability: the tree is **moved**, and a consumer builds against it through
  `find_package(OpenCV CONFIG)` and runs with no environment help;
- a synthetic ChArUco detect + calibrate that must recover a known camera.

Three OpenCV defaults would each have produced a broken artifact without failing its build:
- the **absolute** install prefix baked into every RUNPATH;
- the **system** zlib on Linux;
- the **static** CRT under MSVC.

The build overrides all three, and the check proves the overrides took effect.

lain re-checks the archive at configure time, as `addFFmpeg.cmake` does (`cmake/addOpenCV.cmake`,
built by M9 slice 0). It parses the archive's
`MANIFEST.txt` for the version, profile, modules, linkage, target and third-party line, and refuses a mismatch
without executing anything, so the gate still works when cross-compiling. A runtime test asks the
linked library for `cv::getBuildInformation()` as a second opinion. `LAIN_OPENCV_ROOT` may point at
a local install; that path has no manifest, so configure warns and the runtime test is the only
gate.

## Provenance

OpenCV does not sign its release tags or source tarballs, so there is no equivalent of
ffmpeg-prebuilt's GPG check. The prebuilt pins the tag's **commit** (`4.14.0` →
`0654a42e19215ef25b1d367d822f3c630447e7c7`) and refuses a checkout that resolves anywhere else. The
tree id travels in the manifest. GitHub's generated tarballs are not used, because their bytes have
changed under a stable tag before.

## Why not the alternatives

- **Official releases:** binaries for Windows, iOS and Android only. The Windows one bundles videoio,
  highgui and IPP.
- **opencv-mobile:** drops calib3d, objdetect and flann, and is built without RTTI or exceptions.
- **conda-forge `libopencv`:** the full contrib build (5.0), inside a conda environment, pulling in
  Qt, HDF5, OpenVINO, protobuf, FFmpeg, BLAS and Eigen.
- **ConanCenter:** binaries for default options only; a minimal configuration falls back to a source
  build. **vcpkg** always builds from source.
- **Homebrew and distribution packages:** whatever version the system has, with large dependency
  trees (Homebrew's OpenCV depends on its GPL-configured FFmpeg).
- **Building inside lain:** exactly the cost this decision exists to avoid, paid on every clean
  configure and on every CI leg.

## Consequences

- **M9 gains a slice 0** (WORK.md): `cmake/addOpenCV.cmake`, an opt-in `LAIN_CAMERA_OPENCV`
  (default OFF, so an all-off configure fetches nothing), the manifest gate, notices, and a probe
  test. Nothing depends on it yet. It is blocked on the first published release, because the
  per-target hashes come from that release's `SHA256SUMS`. *(**Amended 2026-10-02:** `LAIN_CAMERA_OPENCV` defaults ON for a
  top-level build and OFF when lain is bundled, so a consumer's configure never downloads a binary it
  did not ask for. The option is what is wanted; `addOpenCV.cmake` says whether this platform has an
  archive, and an unavailable plugin is simply not built, with the reason in the configure summary,
  while a broken download or manifest still fails. `-DLAIN_CAMERA_OPENCV=OFF` is the all-off
  configure, and it fetches nothing. The same day, camera node kinds began registering whatever the
  backends ([ADR-0016](0016-camera-calibration-method-modules.md), amended), so a camera document
  loads in a build without the plugin. FFmpeg follows the same shape (ADR-0019, amended).)*
- **OpenCV brings its own thread pool.** Its built-in parallel framework is left in the prebuilt,
  and ADR-0024 says a process has one pool. The camera plugin has two options: cap OpenCV with
  `cv::setNumThreads`, or install a `cv::parallel` backend that runs OpenCV's `parallel_for_` on
  `multi`. Choosing is M9 slice 1's job; either works with these binaries. *(**Decided
  2026-09-30:** the first. The plugin calls `cv::setNumThreads(0)` when it registers, and lain
  parallelises across frames on its own pool. The reasoning is in
  [ADR-0016](0016-camera-calibration-method-modules.md).)*
- **Obligations are notices, not source.** Apache-2.0 and the vendored permissive licences ask a
  redistributor to pass on the licence texts. They travel in the archive; `addOpenCV.cmake` stages
  them and registers an `--licenses` notice, as FFmpeg's does.
- **A new module or OpenCV line is a new prebuilt release** plus a hash bump in `addOpenCV.cmake`,
  never a source build in lain.
