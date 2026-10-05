# Use Ceres for registration refinement

Board and targetless fixed-camera registration use Ceres as an optional private nonlinear
optimization dependency. Public camera APIs expose lain-owned observations, transforms, requests,
reports, and diagnostics; they do not expose Ceres or Eigen types.

The initial supported configuration disables SuiteSparse and uses Ceres' Eigen sparse or iterative
Schur solvers. This keeps mixed GPL/commercial SuiteSparse components out of the dependency graph
while retaining the block-sparse optimization needed by the large-rig design target. Any future
SuiteSparse configuration requires a separate license decision.

Eigen's MPL-2.0 license is an approved, scoped exception to the permissive-by-default dependency
policy. Camera builds define `EIGEN_MPL2_ONLY`, keep Eigen types private, and follow the source and
notice obligations recorded in ADR-0015 and the repository third-party license inventory.

## Consequences

Ceres, Eigen, and Abseil are paid for only when a registration implementation that needs global
refinement is enabled. Ceres may be linked statically or dynamically according to the host build, but
remains an implementation detail either way.

The root `README.md` records Ceres, Eigen, and Abseil as approved planned dependencies before their
build integration lands. Their exact versions and enabled configurations replace that planned status
when the implementation is added; removing or replacing one updates the inventory in the same change.

Registration has one production global-refinement path rather than separate dense Eigen and Ceres
implementations. It preserves sparse residual structure, does not encode small fixed camera or
capture-group limits, and releases decoded image data after producing the compact observations used
by optimization. Resource exhaustion is reported through the registration report rather than
represented as an artificial API limit.

Global refinement whitens observations using measured pixel covariance when available. Otherwise it
uses a configured uniform pixel-noise assumption and a configured robust loss; detector response does
not silently modify residual weight. Reports retain the assumed noise, loss family and scale, and
outlier diagnostics so weighting decisions are reproducible.

## Amended 2026-10-05: which Ceres, and whose threads

Decided with the repo owner while planning M9 slice 2.

**Ceres 2.2.0, built from source, with Eigen 3.4.0 and Ceres' bundled miniglog.** The inventory
above listed Abseil as a Ceres dependency, but only unreleased Ceres master needs it
(`find_package(absl 20240116)`). 2.2.0, the latest release, logs through glog or through miniglog,
a small glog substitute compiled into Ceres itself under Ceres' own BSD licence. With miniglog
there is no glog, no gflags and no Abseil, so **Abseil leaves the inventory**. Fetched as source
through `FetchContent` rather than a published binary, unlike OpenCV
([ADR-0026](0026-opencv-from-a-pinned-minimal-prebuilt.md)). Ceres' public headers include Eigen's,
so a prebuilt would also pin lain's Eigen and need a Debug and a Release set for MSVC. A static
source build has no ABI to keep in step, at the price of build minutes that are measured when it
lands.

Everything optional is forced off: SuiteSparse (as above), LAPACK, CUDA, Accelerate, gflags, and
Ceres' tests, examples, benchmarks and documentation. Eigen's sparse Cholesky stays on, since it is
the sparse solver this ADR names. **`EIGEN_MPL2_ONLY` is defined for Ceres' own sources too**, not
only for lain's, so building Ceres proves that no LGPL-licensed Eigen code is reached.

**Ceres runs serial (`num_threads = 1`); lain supplies the parallelism.** This is the rule
ADR-0016 set for OpenCV, and the reason is the same: ADR-0024 makes the process pool the only pool,
and Ceres 2.2 owns a thread pool of its own that cannot be pointed at it. Registration parallelises
on lain's side instead. Detection runs one frame per task, as calibration's does, and the
validation and resampling solves run one per task. A single global refinement is therefore
single-threaded. The 100-camera, 4,000-group scale test records how long that takes, and that
measurement is the trigger to revisit.

The build switch is `LAIN_CAMERA_CERES`. It defaults to `${LAIN_NOT_SUBPROJECT}`, as a fetched
prebuilt's does, although Ceres is source-built: it is heavy enough that a bundled lain should not
compile it unasked.
