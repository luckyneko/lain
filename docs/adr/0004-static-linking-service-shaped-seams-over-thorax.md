# Static linking with service-shaped seams — thorax deferred

---
Status: accepted
---

`lain` links everything **statically** for now and **defers adopting `thorax`** (its
sibling DSO/plugin framework, `extern/thorax`). New subsystems — `io`, `memory`, and the
per-media format registries — are built as ordinary static libraries behind
**service-shaped seams**: a namespace of free functions (the `lain::log` shape) fronting a
hidden singleton or `lain::core::Factory<Base>`, so a `thorax::service` can back any of them
later without touching a single caller. The plugin machinery is not justified while `lain`
is a build-from-source, single-author, undistributed prototyping monorepo — and its modularity
benefit is already available statically through the `Factory` seam.

The question that gates the whole thing: the *only* capability static linking genuinely
lacks is loading code whose heavy dependency (OpenCV, the AWS SDK) may be **absent at
runtime in an environment not controlled at build time**. On a build-from-source project the
build environment *is* the runtime environment, so an `if(OpenCV_FOUND)` build-time guard
compiles (or skips) the feature and links only what it built against — the "missing at
runtime" scenario is a *distribution* posture `lain` is not in yet. Everything else `thorax`
offers (a shared service registry, one process-wide logger/executor/factory across DSOs) is
machinery to *repair* the global-state fragmentation that DSOs themselves introduce — a cost
of going multi-DSO, not a pre-existing pain that adopting `thorax` relieves.

## Decision

- **Static linking; one binary.** No DSO boundary, so `spdlog`'s logger, Taskflow's executor,
  and every `Factory` are single instances with no fragmentation to reconcile.

  **Amended 2026-08-31 (M10).** FFmpeg is the first and so far only exception, and the reason is
  **licensing, not convenience**: LGPL-2.1's relinking requirement is satisfied by dynamic linking
  on its own, while a static build additionally owes consumers relinkable object files — which is
  why [ADR-0019](0019-ffmpeg-lgpl-for-video-codec-support.md)'s prebuilt artifacts are published
  shared-only. This ADR's actual concern is untouched: the fragmentation it guards against is
  lain's own global state crossing a DSO boundary, and no lain type, logger, executor or `Factory`
  crosses this one — no FFmpeg type crosses a lain-owned interface either, so the boundary carries
  nothing but C. The costs are ordinary and local to `addFFmpeg.cmake`: a build-tree rpath so tests
  can load the libraries, `@executable_path/../lib` / `$ORIGIN/../lib` for anything staged, and an
  explicit DLL copy on Windows, which has no rpath. Everything else still links statically, and
  "one binary plus the libraries one dependency is legally obliged to ship separately" is not a
  step toward `thorax`.
- **Service-shaped seams are the convention.** Anything that would otherwise be a floating
  global is exposed as free functions over a hidden singleton (as `lain::log` already is):
  `memory::alloc`/`dealloc` fronting the allocator, `io::read` fronting scheme dispatch, and a
  per-media `Factory<Reader>` fronted by `load`. Each is a seam a `thorax::service` can occupy
  later.
- **`Factory<Base>` is the modularity mechanism, statically.** Formats and IO schemes register
  themselves into a `Factory`; a missing codec's registration is simply *not compiled* (a CMake
  dependency guard), which is the "don't load a feature whose dep is absent" behaviour without
  any runtime loader.
- **A direct `dlopen` escape hatch stays open.** The one or two things that genuinely need
  runtime loading in the near term (camera drivers) can `dlopen` a single backend behind a
  `Factory` registration — without pulling in the whole `thorax` framework.

## Considered options

- **Adopt `thorax` now.** Rejected. Its value (runtime-optional heavy deps) answers a
  distribution need `lain` doesn't have, while its cost lands immediately: the ABI boundary
  **forbids STL types in virtual signatures**, so an `IImageReader` service could not return a
  `lain::image::Image` by value (it owns a `std::vector`/`Buffer`) — every payload would marshal
  across a C-ish boundary, fighting `lain`'s typed-value identity. Plus hidden-visibility
  discipline, manifests, and DSO lifetime management, for a modularity the `Factory` seam
  already provides.
- **Hard-link every heavy dependency into the core.** Rejected as the opposite failure — a
  codec every consumer pays for whether or not they decode that format, and a binary that won't
  launch if an optional library is missing. The per-media *separate target* (`lain::io::image`
  built apart from `lain::io`) plus build-time guards gives selective linkage without a plugin
  system.

## Consequences

- **`thorax` remains a clean future upgrade, not a rewrite.** Because every seam is
  service-shaped, `thorax` slots in *behind* it later — the `Factory` is the seam, a plugin is
  one backend for populating it (exactly as `marv`'s own README anticipates being "fronted by a
  `thorax` plugin"). No caller changes when that day comes.
- **Global correctness is scoped to the single binary** — which is the point. Splitting into
  DSOs would fragment `spdlog`/Taskflow/`Factory` state; staying single-binary sidesteps the
  problem rather than importing `thorax` to patch it.
- **The convention is now load-bearing.** New foundational subsystems are expected to adopt the
  service-shaped seam (free functions over a hidden singleton) so this decision stays reversible
  toward `thorax` for all of them at once.
