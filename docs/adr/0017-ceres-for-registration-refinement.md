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

**As built (M9 slice 2, sub-slice 5).** The refiner's residual is one corner seen by one camera, run
through lain's own `camera::project<T>` with Ceres' automatic differentiation. It is whitened by the
corner's covariance when the detector reports one, else by the noise model's sigma, under the
request's robust loss. Cameras and boards are angle-axis plus translation, and the reference camera is
constant. A global refinement is `SPARSE_SCHUR` over Eigen's sparse Cholesky, eliminating the board
poses first. A pose-only solve (held-out validation) is a small `DENSE_QR`.

- **The scale test is not a trigger.** 100 cameras and 4,000 capture groups, with each board seen by
  eight cameras, make 768,000 corner residuals. The single-threaded refinement converged in 9
  iterations and 16.1 s, and the whole registration, held-out validation included, took 19.7 s at
  663 MB peak (linux-x86_64, four cores, Release). A Debug build is about 65 times slower, so the
  scale test runs in Release only.
- **The default robust loss is Cauchy, not Huber**, decided on a measurement. The outlier a board
  registration meets is a whole view that does not belong: a frame from another instant, with its
  corners tens of standard deviations off. Huber's pull stays linear out there. One such view, at 66
  standard deviations, moved a camera 8.4 mrad under Huber and 1.6 under Cauchy, against 1.8 with no
  stray view at all (1 m rig). Cauchy's non-convexity is safe because initialisation, which a stray
  view cannot steer, starts the refinement close.

## Amended 2026-10-06: latent scene points, and the gauge a targetless problem leaves free

Decided while planning M9 slice 3 (targetless registration with known intrinsics).

**The refiner gains landmarks beside its bodies, as `refiner.h` always said it would.** A landmark
is a 3-DoF point in the reference frame, observed through a list of its own, so a board problem is
untouched. In Ceres it is a second residual, one landmark seen by one camera
(`AutoDiffCostFunction<…, 2, 6, 3>`), through the same `camera::project<T>` and the same whitening
helper as a corner. Landmarks join the bodies in the first elimination group. Modelling a landmark
as a body with one point at its origin was refused: the body's rotation is unobservable, three
wasted degrees of freedom per point held up only by damping.

**Which linear solver runs is a rule of the problem's shape.** Free cameras use `SPARSE_SCHUR`, as
before. Held cameras with at most one latent block (a board's held-out pose) keep `DENSE_QR`, so
board registration's validation does not change. Held cameras with many latent blocks (held-out
tracks triangulated together) use `SPARSE_NORMAL_CHOLESKY`, since a dense factorisation of
thousands of independent points would exhaust memory. *(Amended 2026-10-07, sub-slice 8a:
`registration::validate` refines each held-out track on its own, the track's leave-one-out landmarks
in one problem. A track's prediction then does not depend on which other tracks were held out, and
one start that fails costs only its own track. So a validation problem holds a few latent blocks,
and the same rule picks `SPARSE_NORMAL_CHOLESKY` for it.)* The solution's account names the solver that ran, since the rule is otherwise visible only as
time and Ceres may substitute one.

**A targetless problem's scale is left free.** Holding the reference camera removes six degrees of
freedom and leaves one, the global scale, unconstrained. Ceres leaves the whole gauge free in BAL
problems and converges, because Levenberg-Marquardt's damping keeps the Schur system positive
definite and the gradient has no component along the scale direction. The result is normalised
afterwards (ADR-0016, amended). The slice that adds landmarks measures the scale drift, the
iteration count and any failure of the sparse factorisation; a bad measurement is the trigger to
hold one camera's translation length on a `SphereManifold<3>`.

*Measured (M9 slice 3, sub-slice 4): the trigger did not fire.* Eight cameras and 2,000 landmarks
started 5 mrad and 5% out converged in four or five iterations over thirteen seeds, at a scale
between 0.980 and 1.022, from starts within 0.2% of 1. The scale was the same at 0 px of noise as
at 0.3 px, so the solver's path from the start sets it, not the noise. A ring of 100 cameras and
50,000 landmarks converged in four iterations at 1.004. No factorisation failed, and every final
cost sat where whitened noise puts it. A drift inside what the starts were moved, which costs no
iterations, is what normalising afterwards is for.

**A projection that fails at the starting point aborts the solve**, since a residual that returns
false there is fatal to Ceres. A targetless initialisation can place a landmark behind a camera,
so the method module filters by cheirality before it refines rather than letting a failed solve
stand for it.

## Amended 2026-10-12: the seam moves, and a model may be free

Decided while planning M9 slice 4 (targetless calibration). The seam's move (the first point) and the
parameter-generic projection (the third) are built; the rest is not yet.

**Calibration refines through the same seam, so the seam leaves registration.** Targetless calibration
is a bundle adjustment with the camera model free. ADR-0016 keeps calibration and registration apart, so
calibration cannot use `registration::Refiner` where it lives. The seam moves to
`lain::camera::refinement`, with the noise model and the robust loss, and using-declarations are left at
the old names: the move the board pose seam made in slice 2. It stays one production path: the Ceres
plugin fills the moved seam, and neither method gets a refiner of its own. *(Settled 2026-10-13, while
planning sub-slice 3:)* the header is `lain/camera/refinement/refiner.h`, since every sub-namespace of
`lain::camera` has a directory. Two names lose their stutter there, `refinement::Status` and
`refinement::BodyObservation`, and registration keeps their old names as aliases. *(Built
2026-10-13, sub-slice 3.)*

**A problem's cameras may share a model, and a model may be free.** A moving camera's sampled frames are
the problem's cameras, and they share one model. Each model then states which of its parameters move:
focal length, principal point, distortion coefficients. In Ceres a free model is one parameter block,
its held parameters on a `SubsetManifold`, ordered after the cameras, so the landmarks are still
eliminated first. The refined parameters come back unchecked, and the method passes them
through `CameraModel::create`, as it does an estimator's. Within one solve the projection's domain is the
starting model's. A result whose distortion folds inside the image is refused by `create`, not
reported as a model. Registration never frees a model: joint intrinsic and extrinsic registration
remains outside M9. *(Built 2026-10-14, sub-slice 5. Left empty, the problem's new fields mean
today's problem, so registration changed by no line. The `refine()` facade picks the first refiner
that can estimate every model a problem frees, as board calibration picks its estimator, and answers
a new `Status::Unsupported`, naming the models, when none can. A free model is seen through body
observations as well as landmarks, through the one `observe()` every residual shares, and the
linear solver is Schur when the cameras or any model is free. The free model's residuals live in a
file of their own: in one file with the held ones they slowed the held path by 9 to 11%, through
GCC's per-file inlining limit, measured on targetless registration's scale case.)* *(Amended
2026-10-15, ahead of sub-slice 9c: each free model has an ordering group of its own after the cameras',
where it had shared theirs. A group is a set ordered by address, and the AMD ordering of the reduced
system breaks ties by that order. The cameras sit in one array, whose addresses keep their order, but a
model block is an allocation of its own, so the solution's last bits followed wherever the heap put it:
a targetless calibration's resamples came out different on a pool than serially.)*

**Ceres differentiates the model's parameters through lain's projection**, never through a copy. The
distortion formulas and intrinsics become scalar-generic in their parameters as well as their point,
and each model's automatic derivatives are tested against finite differences, its iterative inverse
included. The refiner declares the models it can estimate. A model that fails that test is not
declared, so a request for it is refused rather than refined on wrong derivatives. *(Built 2026-10-13,
sub-slice 4: the models still hold doubles, and their formulas became static templates over a
coefficient array, generic in the coefficients' scalar and the point's. All six models' derivatives
agree with finite differences, the worst at 4.0e-8 of the derivative's scale, through the inverse
Brown-Conrady model's Newton iteration.)* *(Corrected 2026-10-14, while planning sub-slice 5: that
agreement was measured at nonzero test coefficients only. At the neutral model, where targetless
calibration starts, the inverse model's Newton solve stops before taking a step, and every
coefficient's derivative comes back 0. Fixed the same day, in its own commit ahead of sub-slice 5:
the solve takes a step before it may stop, and the derivatives agree at the neutral model too, the
inverse model's worst at 8.0e-9.)* *(Built 2026-10-14, sub-slice 5: the Ceres refiner declares all six models, through an
exhaustive switch, so a new model is a compile error there until it is tested and listed.)*
