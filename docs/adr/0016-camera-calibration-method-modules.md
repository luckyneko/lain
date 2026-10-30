# Structure camera calibration and registration around shared evidence

`lain::camera` owns camera geometry and applicability, `lain::camera::board` owns reusable board
patterns, physical board descriptions, rendering, and fingerprints, and `lain::camera::calibration`
owns the shared request, report, fitness, and validation contracts. Board and targetless calibration
are optional method modules under `lain::camera::calibration::{board,targetless}` so computer-vision
and optimization dependencies stay out of the core modules and consumers pay only for enabled
methods.

`lain::camera::capture` owns timestamped camera frames, clock-domain identity, capture groups, and
grouping reports. `lain::camera::registration` consumes explicit capture groups and owns spatial
registration; it does not own timestamp-association policy.
*(Amended 2026-10-20, from the M9 review: M9 builds capture groups and by-position grouping only.
Timestamp grouping, clock-domain identity and the capture manifest that would assign durable camera
identities come after M9, decided with the repo owner. Until then a graph names each camera by where its
footage lies (amended 2026-10-21, below).)*

Registration follows the same optional-method structure as calibration:
`lain::camera::registration` owns the shared request, report, transforms, diagnostics, and fitness
contracts, while `lain::camera::registration::{board,targetless}` own the optional implementations.
Both return the same registration report; method selection and ordered, short-circuit fallback remain
graph orchestration concerns.
*(Amended 2026-10-20: a registration's result leaves the process as a **registered rig**, taken the
way a calibration's model is. `Report::rig()` is every camera's `referenceFromCamera`, the reference
and the scale status, set only when the registration succeeded, whatever its verdict. The
`registeredRig` node passes it on and clears its output when there is none. The **rig document** is
`lain::camera::serialize`'s, versioned and read strictly, written by flowview's `run` for a rig output
and bound back by `--rig`. The report itself stays a one-line summary. Until then the transforms
reached no file at all, since `run` writes a report as that line. Decided with the repo owner; see
WORK.md's *After slice 4: the registered rig* under Milestone 9.)*

## Consequences

Both method modules return the same `lain::camera::calibration::CalibrationReport`. Board objects are
not owned by calibration and may later be consumed by a sibling `lain::camera::registration`
capability. Method selection and ordered, short-circuit fallback are graph orchestration concerns;
neither calibration method depends on the other or contains fallback policy.

Calibration and registration remain separate operations. Registration consumes immutable camera
models and may use board or targetless evidence, but no supported registration path jointly adjusts
intrinsics and extrinsics.

Registration APIs impose no small fixed limit on cameras, capture groups, image resolution, or
observations. Implementations preserve sparse optimization structure and release decoded image data
after extracting the observations required by registration. Resource exhaustion may still prevent a
particular machine from completing a large dataset; this is reported as failure rather than encoded
as an artificial public limit.

OpenCV is permitted as the initial optional ChArUco backend, but only modules that directly perform
OpenCV-backed work may include or link it. Shared board, calibration, registration, request, report,
and observation contracts use lain-owned types, allowing OpenCV-backed implementations to be
replaced incrementally without changing those contracts. *(How OpenCV is obtained — the 4.14 line, a
fixed `calib` module profile, shared libraries from a pinned and verified prebuilt — is
[ADR-0026](0026-opencv-from-a-pinned-minimal-prebuilt.md).)*

`lain::camera::feature` owns backend-neutral scene-feature observations, feature tracks, track sets,
and extraction reports shared by targetless methods. Optional producer modules perform feature
detection and description, matching, track construction, geometric verification, and the
initialization evidence needed by targetless solvers. Targetless calibration and registration
consume this shared evidence rather than owning separate feature front ends. A high-level operation
may accept a frame sequence and delegate to the configured producer, but every path reaches the same
production extraction and track contracts.
*(Amended 2026-10-06: track construction and the acceptance half of geometric verification are
`lain::camera::feature`'s, not a producer's. A producer proposes candidates and lain measures them;
see "Targetless registration with known intrinsics, as planned" below.)*

`lain::camera::board` owns the backend-neutral board-observation contract, while optional detection
modules produce those observations from images and board specifications. Calibration and
registration consume the same observations, avoiding duplicate detection and ensuring tests and
debug visualizations inspect the evidence actually supplied to optimization. Detection remains an
operation over a board value; processing state is not stored on the board object.

A board observation retains source-frame identity and compact image-space evidence, not decoded
image ownership. Debug visualization resolves the source image through the capture interface when it
needs pixels, so retaining observations does not force 8K frame buffers to remain resident.

A feature track similarly retains compact source-frame identities and image-space observations, not
decoded image ownership. Calibration, registration, validation, tests, and debug visualization
consume the same accepted tracks. Ceres begins after tracks, geometric verification, and initial
estimates exist; nonlinear refinement does not replace the shared feature front end.

Board detection always returns a lain-owned board-detection report rather than an optional
observation or boolean. Successful and partial reports may contain a board observation; every report
retains structured status, rejection reasons, summary statistics, timing, and backend provenance so
failed frames remain useful to view selection, tests, and diagnosis.

The detection request selects summary or detailed diagnostics. Summary is the production default;
detailed diagnostics may retain rejected marker and corner candidates plus refinement evidence for
debug overlays. Neither level retains source pixels, and choosing a detail level does not change the
accepted observation supplied to calibration or registration.
*(Amended 2026-10-20, from the M9 review: the debug overlays are **not built**. The OpenCV detector
fills `DetectionEvidence` for `DetailLevel::Detailed`, but `detectBoard` has no setting that asks for
it and nothing in flowview draws a detection. Deferred, decided with the repo owner. **Trigger:** the
first real capture whose detections need inspecting.)*

Detection may operate on a configurable downsampled image, but board observations always use the
original source-image pixel coordinate system. The request exposes the scale policy, and the report's
reproducibility evidence records the requested policy, actual preprocessing transform, and whether
subpixel refinement used native-resolution pixels. Native-resolution detection remains available for
accuracy and regression comparisons.

Native-resolution subpixel refinement is enabled by default after downsampled detection and operates
only around accepted feature locations. Detection scale and refinement resolution are independently
configurable, allowing tests to compare native detection, coarse detection with native refinement,
and fully downsampled processing without changing output coordinates.

Detected features may carry pixel covariance only when a backend provides a defensible uncertainty
estimate. Refinement response, iteration count, and displacement remain raw diagnostics and are not
converted into statistical error. When uncertainty is unknown, optimization uses an explicitly
configured observation-noise model rather than fabricated per-feature confidence.

Camera models identify distortion direction as part of the model variant. Forward and inverse
Brown-Conrady coefficients are not interchangeable merely because both use five values. Projection
and unprojection follow the declared model, and each physical imager and stream geometry receives its
own camera model rather than inheriting one device-wide calibration.

The first real-camera calibration fixture is a **generic** real-camera fixture, and its first capture
happens to come from an Intel RealSense D455 RGB stream. *(Amended 2026-09-30: this read "uses the
Intel RealSense D455 RGB stream", and the repo owner corrected the emphasis before any of it was
built. The goal is to be as generic as possible, so the D455 is a capture, not a type: nothing in
the code names a device. The fixture's provenance is a generic record (device make, model and
identity when known, stream profile, SDK and firmware key-values, an optional imported
manufacturer model), and a second camera is a second capture, not a second schema.)* Camera-model
support is capability-specific: the shared camera module may represent, project, unproject, validate,
or import a model even when a particular optional calibration backend cannot estimate that model.
Calibration requests reject unsupported estimation combinations through a failed calibration report.
Each backend declares its estimatable model variants, and successful reports retain the exact model
variant and coefficient convention used; backends do not silently substitute a similar model.

The shared camera model uses a closed set of typed distortion variants rather than a runtime plugin
ABI or an untyped coefficient array. Each variant defines its exact coefficient meanings,
distortion direction, projection and unprojection behavior, serialization identity, and supported
operations. New variants are added deliberately when a concrete camera family requires them.

*Amended 2026-10-05:* "defines" is literal. Each variant's struct in `distortion.h` declares its
display name, its coefficient names in order with their values, its projection (camera-frame
direction to distorted normalised coordinates), its unprojection (back to a unit ray), and its
domain. A camera model's projection is just its distortion's projection followed by its intrinsics,
and names no model. A variant that lacks any of these fails to compile where the variant is visited.
Until this amendment, the same knowledge lived in `if constexpr` chains in the projection kernels and
in visitors in `cameramodel.cpp`. Several of those ended in a catch-all `else`, so a seventh variant
would have compiled and been described wrongly. The serialization identity stays with the serialize
target (`libs/camera/serialize`), which owns the document format.

The initial variants are no distortion, Brown-Conrady 5, Inverse Brown-Conrady 5, Modified
Brown-Conrady 5, Rational Brown-Conrady 8, and Kannala-Brandt 4. Thin-prism and tilted-sensor models
remain deferred until a concrete camera requires them. Every built-in variant has formula-level
golden tests for coefficient ordering, projection, unprojection, and round-trip behavior across its
declared valid domain.

Projection and unprojection return a lain-owned value plus structured status. Ordinary invalid input,
behind-camera geometry, model-domain violations, and iterative non-convergence
do not produce NaN sentinels or throw exceptions. Bulk operations retain per-element outcomes and
summarize failure counts for reports and validation.
*(Amended 2026-10-20, from the M9 review: **no bulk operation is built.** Calibration, registration
and validation project one point at a time, and each counts its own failures where it reports them.
Deferred, decided with the repo owner. **Trigger:** the first caller that projects many points and
must report per-element failures, such as a reprojection-error view.)*

Projection success is independent of image containment. A mathematically valid off-image pixel is
returned successfully, while a separate containment query tests the camera model's image rectangle.
Optimization may therefore evaluate intermediate off-image residuals without misclassifying them as
camera-model failures.

Public camera models are immutable validated values constructed through a factory that returns
structured diagnostics on failure. Construction rejects invalid image geometry, non-positive focal
lengths, non-finite parameters, and model-specific invariant violations. Calibration solvers may use
unchecked internal candidates, but only a validated final value can enter a successful report or
registration request.

Successful calibration may leave parameter uncertainty unknown. Reports always include structured
held-out-view and deterministic-resampling sections, but a failed attempt may mark either unavailable
with a reason when it could not produce the required evidence. Full covariance or parameter standard
deviations are optional diagnostics because their cost and meaning vary by backend. A
reconstruction-ready verdict requires demonstrated stability, not a complete covariance matrix;
missing uncertainty is never encoded as zero.

Calibration requests handle imported manufacturer or external intrinsics through an explicit policy:
ignore them for independent estimation, use them as an initial estimate while allowing refinement, or
hold them immutable and validate them against the supplied dataset. Reports retain the selected
policy and imported-model provenance. A backend never silently replaces an estimate with imported
parameters.

An imported model seeds all parameters only when its exact model variant is compatible with the
requested estimate. Different distortion variants may seed compatible pinhole intrinsics, but their
distortion coefficients begin neutral unless a separately tested explicit conversion exists. Reports
record every reused or converted field; coefficient arrays are never copied between forward and
inverse Brown-Conrady variants based only on matching shape.

An imported manufacturer model (the D455's RealSense factory model, for the first capture) is a
comparison diagnostic, not the ground-truth oracle for the fixture. Fixture pass/fail criteria use independent held-out board reprojection, geometric coverage,
parameter and resampling stability, repeat captures, and reconstruction of known board geometry.
Factory-versus-estimated parameter differences remain visible but cannot alone accept or reject a
calibration.
*(Amended 2026-09-30, when the fixture harness was built. **Repeat captures** are two or more capture
sessions with the camera unplugged and re-mounted between them, and the **repeat check** over them
has two halves, because the first alone was measured to be blind to what the second sees.
**Prediction:** each Ready session's model, held fixed, must predict every other session's views to
the Ready held-out angle; it sees a lens that is not the lens. It is weak on intrinsics, since a board
pose recovered with the model fixed absorbs most of a focal or principal-point error: on synthetic
footage a second camera 3% longer and 12 px off-centre predicted the first's views at 0.27 mrad
against 0.6 for Ready. **Agreement:** every two Ready sessions' intrinsics must differ by no more than
three standard deviations of their combined resampled spread. The Ready stability bounds themselves
were tried and refused, because they bound one capture's resamples and two captures also differ by
their poses: two sessions of one camera came out 0.544% apart in focal length against a 0.5% bound.
A fixture therefore needs two Ready sessions. A manufacturer model is held on its own session's views
and reported with its difference from the estimate, and never gates. **Reconstruction of known board
geometry is not built yet**: rebuilding a board without its geometry needs relative-pose recovery
through the backend, designed on its own.)*

Real-camera testing uses two fixture tiers. A compact curated ChArUco image set, first captured on
the D455's RGB stream, is committed for deterministic integration tests. Everything before it lands
on synthetic evidence; the fixture is the last piece of the first slice, because it waits on a real
capture. A larger external capture is addressed by a versioned manifest
and content hashes for accuracy, performance, downsampling, and resampling experiments. Ordinary CI
runs the compact tier; extended validation reports that the larger tier was unavailable rather than
silently substituting different data.
*(Amended 2026-09-30. **Storage:** a fixture is a folder under `plugins/camera/test/fixtures/`
holding `fixture.json` (board, distortion model, and each session's frames folder and capture record)
and one folder of committed PNG stills per session, about 14 each so every session can be Ready on
its own. The printed board is lain's own rendering (`apps/flowview/examples/render-board.json`),
measured after printing. **Extended-tier addressing:** a committed `extended.json` pins path, SHA-256
and size for every file of a full capture, which is itself a fixture folder, found as
`$LAIN_CAMERA_FIXTURE_DATA/<name>`. It is Verified and run, Unavailable with the reason, or a Mismatch
naming each missing, resized, rehashed or unpinned file, which is a failure rather than a run on
different data. One tool writes and verifies the manifest through the same hash.)*

Reconstruction-fitness verdicts use named, versioned threshold profiles with explicit request
overrides. Reports retain raw pixel RMS, but cross-resolution acceptance emphasizes normalized or
angular ray error together with held-out performance, geometric coverage, parameter stability, and
failure rates. Every report records the fully resolved thresholds, avoiding an implicit universal
pixel-RMS cutoff across resolutions and lens models.

Delivery is staged through complete production paths: shared camera geometry and ChArUco board
calibration first, fixed-camera board registration second, targetless registration with immutable
known intrinsics third, and targetless calibration last. Fallback orchestration is exposed only after
both calibration methods exist; no public targetless stub stands in for an unimplemented method.

Flow integration is a sibling adapter over these production operations rather than their owner.
Calibration and registration nodes are stateless definitions whose `compute` reads one finite batch
request from an `Evaluation` and publishes the corresponding report; they do not accumulate frames or
solver state on the node. Reports and sequence handles travel as shared immutable `PortValue`
payloads. The current vector-backed `MapNode` may help compact in-memory workloads, but production
video calibration does not materialize a `std::vector<image::Image>`; it retains the finite, lazy
`FrameSequence` seam whose loader design is deferred to its own session. *(**That session happened:**
Milestone 10, decided in [ADR-0018](0018-frame-sequences-and-host-driven-rendering.md) and built
2026-08-31 → 09-04. A sequence is a list of frame references over sources, lazily decoded; the host
owns the frame loop, so a render is a fold rather than a map.)*

**Node kinds register only when they can run.** *(Added 2026-09-30, decided with the repo owner
while planning the first slice.)* A camera node kind is registered with a host's node factory only
when a backend providing the capability it needs is registered: rendering for a board renderer,
detection for a detector, detection plus estimation for calibration. With no backend (the default
`LAIN_CAMERA_OPENCV=OFF`), no camera node kind exists. The value types those nodes carry are still
registered as port types, since they are vocabulary: a boundary pin of one still loads.

This is deliberately the opposite of video's rule (ADR-0019's amendment), and the difference is in
what is missing. Video's reader seam always exists and only a *format* may be unsupported, the way
an image loader exists even when JPEG is not compiled in, so `openSequence` is always a node and
reports a missing codec when run. A calibration node with no backend has no seam behind it at all,
and a node that cannot function should not be offered.

The cost is recorded rather than avoided. A document saved by a build with camera nodes, opened in
one without, loses those nodes (and their edges) on load. The loader reports that as an Error, and
flowview's headless `run` and `list` refuse any document that did not load as saved rather than
running what is left, which would be a different graph reported as success.

*(**Amended 2026-10-02 — reversed: every camera node kind registers, whatever the backends; what is
OFFERED follows them.** The cost above was taken on the assumption that a backend-less build would
be rare, and it was the default configuration, so every fresh clone paid it. The argument that a
camera node "has no seam behind it" was also wrong: `board::render`, `board::detect` and
`calibration::board::calibrate` are `lain::camera`'s, built unconditionally, and each already
reports a missing backend ("this build has no board renderer", a Failed report with
`Rejection::NoBackend`, `Failure::NoDetector`). So ADR-0019's rule applies here unchanged: whether a
build can RUN a node is a capability, whether a document can NAME it is vocabulary, and a missing
capability must not cost a document its vocabulary. `registerCameraNodes` now registers all five
kinds *(seven by 2026-10-09: both registration nodes joined, under the same rule; eleven by
2026-10-20: targetless calibration and the three fallback nodes joined)*; `availableCameraNodeKeys` still answers which can run, and flowview's menu offers only those,
so "a node that cannot function should not be offered" survives as a rule about the menu rather than
about loading. A camera document opened without a backend loads whole, saves back unchanged, and its
nodes say what is missing when run. The same day the OpenCV plugin began defaulting ON for a
top-level build ([ADR-0026](0026-opencv-from-a-pinned-minimal-prebuilt.md)).)*

*(Corrected later on 2026-10-02: "says what is missing" was true of a REPORT, and of a log line, and
only the first reaches a gui. `detectBoard` and `calibrateCamera` put the reason in a report on an
output, which the Inspector shows; `renderBoard` had nothing but the log, so in gui-mode a board render
with no renderer was an empty node with no reason anywhere on screen. It now fails with `render()`'s
reason through `NodeEvaluation::fail` (ADR-0025, amended), which the canvas marks Failed and Issues
lists — and so does `boardSpecification` given parameters that are not a board. `board::render`
returns that reason (`RenderResult`) instead of logging it. Also worth knowing from the same report:
an EXISTING build directory keeps `LAIN_CAMERA_OPENCV=OFF` after the default changed, since `option()`
never overrides a cached value — reconfigure with `-DLAIN_CAMERA_OPENCV=ON`.)*

**OpenCV runs serial; lain supplies the parallelism.** *(Added 2026-09-30.)* The OpenCV plugin sets
`cv::setNumThreads(0)` once, when it registers, so OpenCV's built-in pool never runs beside the
process pool ADR-0024 makes the only one. Parallelism comes from lain's side of the seam: detection
over a frame sequence runs one frame per task on that pool, each task decoding its frame and
releasing it. The deterministic-debug execution policy runs the same work serially. Routing OpenCV's
`parallel_for_` onto `multi` was the other option (ADR-0026) and was not taken: per-frame
parallelism already fills the machine, and a nested parallel region inside each frame would only
contend with it.

**Board rendering is filled by the backend.** *(Added 2026-09-30.)* `lain::camera::board` owns the
pattern, its fingerprint and the rendering value (raster, description, fingerprint). The OpenCV
plugin fills the raster, so a rendered board and a detected board are the same backend's idea of the
pattern, and lain carries no second implementation of the ArUco dictionaries.

**Fixed-camera board registration, as planned.** *(Added 2026-10-05, decided with the repo owner
while planning M9 slice 2.)*

- **A board's pose in one view is `camera::board`'s question, not calibration's.** Calibration
  validates by recovering each held-out view's board pose with the model held fixed, and that solve
  lived on `calibration::Estimator` (`boardPose`). Registration needs exactly the same solve, so
  leaving it there would make registration depend on calibration, two siblings this ADR keeps
  apart. It becomes a `board::PoseSolver` seam with its own registry and a `board::pose()` facade,
  filled by the OpenCV plugin, and both method modules call it. The answer carries the planar
  target's **second solution** as well. A board seen far away or nearly square-on has two poses
  that fit about equally well, and choosing between them takes evidence from another camera, which
  one view does not have.
- **Capture groups are explicit.** Registration takes `capture::CaptureGroup` values, plus a
  by-position helper for frame-locked footage, in which frame k of every camera is group k. The
  automatic timestamp grouping CONTEXT.md describes (symmetric discovery, a tolerance, reported
  ambiguity) is **deferred**. Its trigger is the first rig footage that is not frame-locked.
- **A graph hands registration its cameras as collections.** `registerCameras` takes
  `footage : vector<FrameSequence>` and `models : vector<CameraModel>`, paired by position. One
  wire carries a hundred cameras, and the input composes with `listDir → map(openSequence)` and
  `map(calibrateCamera)` (M8). Until a capture manifest assigns camera identities, a camera's
  identity in a graph is its footage's canonical source uri.
  *(Amended 2026-10-21, decided with the repo owner in the M9 review: a camera is named by where its
  footage lies within the folder the rig lies in, `cam0` for a folder of stills and `cam0.mp4` for a
  video (`capture::identitiesByLocation`, `capture/location.h`), and a by-position group's identity,
  and a moving camera's track identities, spell their frames' sources relative to that folder. Before,
  a camera was named by its absolute source, a track's or group's identity digested it, and every k-th
  one in identity order was held out, so the same rig in another folder held out other evidence (0.795
  against 0.966 mrad). Board registration's group order and targetless registration's track order
  followed the absolute paths; board calibration and targetless calibration already ordered their
  evidence by capture order, so only their reported identities change. The flowview verticals run
  each method from a second folder and require the same bytes.)*
  *(Amended 2026-10-09, M9 slice 3, sub-slice 10: `registerCamerasTargetless` takes the same two
  collections and no board. A node per method, sharing the pairing, the naming, the grouping and
  the registration request's settings through one base, `RigRegistrationNode`, so the two methods
  cannot mean different things by one setting.)*
- **An "ambiguous" rig is three cases, each tested.**
  - A **weak bridge**: a camera reaches the rest only through too little shared evidence. The rig
    is connected and is never Ready, and the report names the bridge.
  - A **planar pose flip**: one view's best single-view pose is the wrong one of the two.
    Initialisation chooses by cross-camera agreement, not by trusting the view.
  - A **conflicting capture group**: a group whose frames were not simultaneous, or whose board
    moved, contradicts the rest. Initialisation must not be steered by it, the robust loss must
    bound its influence, and the outlier diagnostics name it.
- **The global refinement is a seam.** `registration::Refiner` is lain-owned and holds no Ceres
  type: cameras, latent rigid bodies (board poses) with their known points, and observations in.
  The Ceres plugin fills it. It is shaped so that targetless registration adds latent scene points
  beside the rigid bodies, not a second path. *(Moved 2026-10-13 to `lain::camera::refinement`, as
  `refinement::Refiner`, so calibration can refine through it too; registration still names it. See
  the slice 4 amendment below.)*

**Targetless registration with known intrinsics, as planned.** *(Added 2026-10-06, decided with the
repo owner while planning M9 slice 3.)*

- **Producers propose; lain decides.** This amends the paragraph above that gave "track
  construction" and "geometric verification" to the producer modules. A producer backend returns
  candidates: features and their descriptors, each feature's nearest neighbours in another image,
  and candidate relative or absolute poses. `lain::camera::feature` applies the ratio and mutual
  tests, builds the tracks, measures every candidate pose by its angular residual and cheirality
  through lain's own unprojection and triangulation, and recomputes the inliers itself; a backend's
  inlier mask is never read. It is the rule the board pose seam already follows ("the facade
  measures, the backend only proposes"), and for the same reason: a choice made by one measure is
  the same choice whichever backend proposed, and a test of the choice needs no backend at all.
  *Built in M9 slice 3, sub-slice 5:*
  - **The geometry seam carries no mask at all**: it returns poses and nothing else, so there is
    nothing to read.
  - **The choice never depends on the order** the candidates came in. It goes by inlier count,
    then summed squared residual, then a canonical order of the poses.
  - **A pair whose parallax is below the inlier angle is a point at infinity.** It is an inlier
    when its rays agree under the candidate's rotation, so distant static background survives
    verification while near points still decide the direction of the baseline.
  *Built in M9 slice 3, sub-slice 7 (the OpenCV producer, `plugins/camera/opencv`):*
  - **SIFT, a matcher with both searches, and a geometry solver**, registered as "opencv". Each
    proposes; none applies a test lain's facades apply.
  - **OpenCV is handed rays as pixels of a virtual pinhole of focal length 1000**, and the inlier
    angle in its pixels. On normalised coordinates with an identity camera, USAC's MAGSAC++
    proposed essential matrices 0.6 to 15 mrad out across 20 seeds; at pixel scale, 0.25 to
    0.47 mrad on every seed.
  - **A ray more than 80° off the axis is left out of what OpenCV proposes from**, and only that.
    Lain's measurement still sees every pair.
  - **Every proposal is serial and seeded.** USAC runs with `isParallel = false`, OpenCV's own pool
    at one thread, and the grid neighbourhood rather than FLANN's, so the same input proposes the
    same candidates on any worker.
  - **An Approximate search builds its FLANN index under a mutex**, since the build draws from the
    process-global `std::rand` and lain matches pairs in parallel. Searches stay parallel.
- **A feature track is a static landmark across the whole capture.** Every camera samples the same
  few capture groups. A feature that stays at the same pixel across a fixed camera's sampled frames
  is a **static feature**; a transient one is moving content and is dropped before matching. Each
  camera pair is matched once, on the static features, and verified, and tracks join across cameras.
  So targetless registration's capture groups choose which frames are examined and nothing more.
  Matching inside every group was the alternative, and it was refused on two counts: its cost is
  camera pairs × groups rather than pairs, and a static point reappears in every group, so a
  held-out group would never be independent of the groups that were fitted.
  *Built in M9 slice 3, sub-slice 6 (`feature::extractTracks`):*
  - **"The same pixel" is decided through the matcher seam.** A camera's frames are paired
    through `match()`, so the mutual and ratio tests apply. A pair then counts only within 1.5
    processed pixels and a 1.5× size ratio. Lain cannot read a descriptor, so an absolute
    descriptor bound would have to be set per descriptor kind.
  - **A static feature sits at its median pixel.** It is found in at least half of the camera's
    frames.
  - **The sampled groups are spread evenly across the capture.** Groups are ordered by their
    members' median timestamp, then median ordinal, then identity, so the choice does not depend
    on input order.
  - **One feature per 1/64-source-pixel cell**, per frame and again among the static features. It
    collapses SIFT's duplicate orientations and makes a track's identity, a digest of its cameras
    and pixels on that grid, unique by construction. *(Amended 2026-10-07: the cell is still the
    feature, but its orientations are no longer collapsed before matching. Every orientation is its
    own descriptor row, every row is matched, and a match between rows is a match between their
    cells. Collapsing first kept the smallest angle, which depends on a view's roll: on real SIFT
    two views kept agreeing orientations in 83 of 128 shared cells, and a fifth of the correct
    matches were lost. A cell matched to two of the other camera's is dropped from that pair. The
    cost is matching every orientation: 1.49 rows a feature on the rendered texture, which nearly
    triples an exact match at the same feature cap.)*
  - **An observation's covariance is the requested localisation**, in the pixels the backend
    searched, scaled back to source pixels. A camera searched at a coarser scale is weighted
    accordingly. *(Amended 2026-10-07: sub-slice 7 measured SIFT's localisation as proportional to
    the feature's size, 3.4% of it natively and within the same bound at half scale, so the
    covariance is now `localisationPerSize` times the static feature's size in source pixels. A
    fixed 0.7 processed px was three times too loose for the commonest features. A coarser search
    still weighs less, because it finds features larger.)*
- **A track's observations belong to views.** A `View` is the set of frames one set of feature
  positions was taken from: one per camera in fixed-camera registration, one per frame in the
  targetless calibration of a moving camera (slice 4). A track holding two different features of one
  view is a conflict and is rejected whole.
- **Placeability, not connectivity, decides whether a targetless rig can register.** Two overlaps
  A–B and B–C that share no track seen by all three cameras each keep a scale of their own, so the
  global refinement is singular and no absolute pose can place C. A board never meets this, because
  it is metric. A camera is therefore **placeable** only when it shares triangulated landmarks with
  cameras already placed, and a rig with an unplaceable camera fails as disconnected, naming those
  cameras.
  *Built in M9 slice 3, sub-slice 8b (2026-10-07):*
  - **Placeability is a closure from a seed pair, with the geometry facades' own minima.**
    - A pair needs 5 shared tracks to seed.
    - A camera needs 4 tracks that already have two placed cameras.
    - A rig fails as disconnected when no pair's closure reaches every camera, naming the cameras
      the best-supported pair cannot reach.
  - **The seed is the covering pair with the most relative-pose inliers whose median triangulation
    angle clears 2°**, of the 16 sharing the most tracks.
    - When none clears it, the widest seeds the rig and the report says the floor was not cleared.
    - The floor ranks pairs. It never refuses a rig, so fitness, not initialisation, judges a
      narrow rig.
  - **The hold-out guard is anchored on the first covering seed**, the one the rig is most likely to
    be placed from. A track is held out only while every camera it helped place keeps the support
    it was placed with. Anchoring on the best-supported pair instead fails whenever that pair
    places nobody else, as a pair seen only by each other does.
  - **An observation behind its camera at the start is left out of the refinement**, then every
    camera is checked again for the 4 landmarks it needs. A landmark behind a camera fails a whole
    Ceres solve (ADR-0017), and a camera left with no observation would be returned unchanged.
  - **Registering footage is extracting its tracks, then registering them, exactly.** An extraction
    that fails for a reason other than its dataset is `ExtractionFailed`.
- **Board and targetless registration are compared on held-out feature tracks.** A public
  `registration::validate` takes registered cameras and track ids that neither registration fitted,
  and predicts each held-out observation by triangulating its landmark from the other members with
  every camera held. Being scale-invariant, it compares a metric board registration with a
  scale-ambiguous targetless one without fitting anything on the validation data.
  *Built in M9 slice 3, sub-slice 8a (2026-10-07):*
  - **One fixed procedure, whatever the report asked for its own refinement.** It uses the tracks'
    own covariances (the default noise model where a track has none) and no robust loss, so two
    reports are compared under one prediction.
  - **One refinement per held-out track, every camera held.** A track's prediction then does not
    depend on which others were held out, and a start that cannot be refined leaves only its own
    members unpredicted.
  - **A two-member track's residual counts twice.** It is measured against the epipolar plane, which
    gives it one degree of freedom where a transfer has two. Counting it twice keeps the RMS
    independent of the mix of track lengths, under isotropic noise.
  - **It answers `Unavailable` rather than zeros** when there is nothing to predict.

  *Exercised end to end in M9 slice 3, sub-slice 9 (2026-10-08):*
  - **One rig, both methods, the same footage.** Four cameras watch a textured scene while a printed
    board moves in front of it. Board registration places them from the board; targetless
    registration places them from the scene and drops the board as moving content. Both are
    validated on the targetless result's held-out track ids, which neither method fitted.
  - **The two results are compared after a scale-only fit, not a similarity fit.** Both methods pin
    the same requested reference camera to the identity, so the scale of the centres is the only
    freedom left between them. A similarity fit would also absorb the reference camera's own error.
  - **What held-out transfer can and cannot see, measured:**
    - **It sees one camera turned 5 mrad.** A tilt shows in every track. A pan stays inside the
      near-horizontal epipolar planes of a horizontal rig, so it shows only in tracks of three or
      more members.
    - **It cannot see the rig's weak mode.** The board's result is twice as close to the truth as
      the targetless one, yet they score 0.70 and 0.72 mrad. That mode is what the bootstrap's
      rotation spread measures.
    - **Its RMS has no robust loss.** One wrong track among the held-out ones moves it more than every
      right track together. WORK.md's sub-slice 9 notes put this to the repo owner, with the
      alternatives.

  *Judged from medians, and per camera (2026-10-21, decided with the repo owner):*
  - **Fitness judges a median-based held-out residual**, `HeldOutEvidence::robustAngle`: for each kind
    of member (a track member against its predicted point, against an epipolar plane, or a board
    view), the median of its members' mean squared residual per point, scaled by that kind's
    mean-to-median ratio under normal errors, the kinds weighted by their points. Under normal errors it
    reads as the RMS (0.52 against 0.55 mrad on noise drawn at the stated covariance), and a few wrong
    members cannot decide it. The RMS stays in the report for diagnosis. Both methods' held-out evidence
    is gathered by one private routine.
  - **No held-out member is classed an outlier.** Leaving out what an outlier rule rejects was the
    owner's first choice, and was measured and refused. The fit's rule (whitened residual over the loss
    scale) flagged 6% to 14% of correct registrations' held-out members, since a held-out residual also
    carries its prediction's error. Self-normalising rules, pooled or per camera, flagged about a fifth
    on SIFT features, whose held-out residuals are heavy-tailed because a feature's precision follows its
    size. On those features the median-based residual reads 0.23 to 0.31 mrad where the RMS reads 0.64
    to 0.91, while a camera turned 5 mrad reads 2.9 to 9.0, so it still separates right from wrong.
  - **Each camera is judged on its own** against the same held-out bound, once it has ten held-out
    points (`FitnessThresholds::minimumHeldOutPerCamera`), so one bad camera is seen however many good
    ones the rig has.
  - **The resampling spread is median-based too**: each camera's median distance from the result,
    scaled so that normally scattered resamples read as their RMS, as calibration's stability became on
    2026-10-19.
  - **The profiles are amended in place**, being provisional until real footage, and
    `registration-targetless/1` holds position to 0.2% of the median depth, as `registration/1` does.

  *A verdict takes the camera models as given (2026-10-29, decided with the repo owner):*
  - **Registration with known intrinsics judges the rig against its models, never the models.** M15's
    example rig plan registered with its uncalibrated nominal models is Ready, with the same held-out
    residual as with the true ones (0.0549 mrad for both), while 14 times further from the truth in
    rotation (9.86 against 0.68 mrad) and 12 times in position (4.82 against 0.40 mm); scale does not
    show it either (0.9994 against 0.9995). A board pose solved through a model a focal length or a
    principal point out absorbs the error, as M9 slice 1's sub-slice 8b found for calibration's
    cross-validation. So Ready means a rig consistent with the models it was given.
  - **What sees a wrong model is a comparison against something independent**: M15's `compareRigs`
    against a synthetic capture's truth, which is how this was found, or calibration's own evidence
    for each model (for a real camera, its sessions' agreement).
  - **Refused for now: capping a verdict at Exploratory unless every model was calibrated**, as a held
    model caps a calibration. A camera model records no origin (`Provenance` names backends, not
    models), so it would add a field to the camera-model document that a hand-edited file could set
    to anything. Trigger: the first real rig mixing calibrated and uncalibrated cameras.
- **A scale-ambiguous result is normalised** so the median depth of the landmarks seen from the
  reference is 1, and its report says so.
- **SIFT** fills the OpenCV extractor. **Matching is a request choice between Exact and
  Approximate**, Exact by default: the deterministic-debug policy runs "the same work, serially",
  so the default must not depend on a process-global random seed, which OpenCV's bundled FLANN
  does. A matcher declares which searches it supports, as an estimator declares the models it can
  estimate, and a deterministic-debug request refuses Approximate rather than substituting Exact.
- **A graph reaches it through one node, extraction included** *(built 2026-10-09, M9 slice 3,
  sub-slice 10)*. `registerCamerasTargetless` runs the footage overload, as `registerCameras` runs
  board registration's, and is offered where a build can extract, match, pose from rays and refine,
  with no board backend. It exposes the extraction settings that describe the rig and its footage
  (the search scale, the groups sampled, the feature cap, the search and its ratio, the inlier angle
  in milliradians, the inliers a pair needs) and keeps the rest at the library's defaults, since they
  describe the extractor. It has no `pixelSigma`: every observation extracted from footage carries
  its own covariance. The provisional values sub-slice 9 measured were kept by the repo owner before
  the node exposed them.

**Targetless calibration, as planned.** *(Added 2026-10-12, decided with the repo owner while
planning M9 slice 4. Nothing below is built yet; WORK.md's slice 4 plan holds the build order.)*

- **One moving camera, a standing scene.** Targetless calibration takes one camera's footage of a
  predominantly static scene, the camera translating as well as turning. Every sampled frame is its own
  view, so a track is a scene point seen across frames. Nothing stays at one pixel, so moving content
  cannot be dropped before matching as it is in fixed-camera extraction: verification rejects most of
  it, and the robust loss and the outlier report the rest. Footage that only turns has no parallax to
  reconstruct from, and fails initialisation with that reason. *(Built 2026-10-15, M9 slice 4,
  sub-slice 9b: refused when most of the seed's matches are points at infinity. A median over the
  matches that triangulate cannot say it, since none triangulates below the minimum parallax: moving
  content across a pan has real parallax and decided it. Two findings for the repo owner: a pan whose
  focal length falls between two sweep candidates was explained, over stand-ins, by a pan of another
  angle and an arbitrary baseline, a spurious orbit only the verdict rejected; and footage whose every
  frame looks at one point does not say its focal length, since every pair's optical axes meet, so the
  sweep cannot choose among the candidates that explain it. WORK.md's sub-slice 9b note holds the
  numbers.)* *(Amended 2026-10-16, decided with the repo owner while planning sub-slice 10: a rendered
  pan through OpenCV is posed as an orbit and judged Ready unless its start is exact, so a seed pair
  most of whose matches a homography explains is not a seed. A homography explains a pan, and a single
  plane, whatever the focal length; a rotation fit under the start model was the alternative.)*
  *(Built 2026-10-18, sub-slice 10d: tested once on pixels before any start, over every pair of frames
  sharing the five tracks a seed needs, at 80% of their shared tracks within 4 px both ways; when every
  pair is refused, the calibration fails before any start. A geometry backend proposes homographies
  and lain measures them, as for every other two-view model. Found: distortion bends a homography, so a
  pan through a lens moving its corners by about 50 px escapes the test and is posed as an orbit. The
  test needs no model, and a model is what would undistort.)*
- **Two extractions, one feature module.** `lain::camera::feature` gains a sequence extraction beside
  the fixed-rig one, sharing its per-image and per-pair work and its union. A sequence pair is verified
  by a **fundamental matrix**, since no model exists yet to turn pixels into rays. The geometry backend
  proposes fundamental-matrix candidates, and lain measures each by its Sampson distance and chooses,
  by the same rule as its pose facades *(the facade and OpenCV's producer built 2026-10-15, M9 slice
  4, sub-slice 7a; the shared module in 7b; the sequence extraction, `extractSequenceTracks`, in
  7c)*. A fundamental matrix ignores lens distortion. A lens whose
  distortion costs too many peripheral matches is the trigger to verify again under the swept model.
  *(Measured 2026-10-15, M9 slice 4, sub-slice 8, at 1920 x 1080 and the default 4 px, through k1
  alone. Adjacent frames keep every correct match through every lens up to a 317 px corner shift,
  since between them a lens's warp barely changes. The path's ends, 0.7 rad apart, keep all of their
  outer-third matches at a 72 px shift, 95% at 193 px and 84% at 317 px. Not yet the trigger.)*
- **The starting focal length comes from a focal sweep** when no imported model seeds it. Lain tries a
  fixed, log-spaced set of focal lengths on the most strongly verified pairs, through its own
  relative-pose facade, and keeps the candidate whose poses explain the most matches. Every candidate
  is scored at one pixel threshold turned into its own angle, because a fixed angle would favour the
  longest focal length. An imported model under the Initial policy skips the sweep, and HoldAndValidate
  validates an imported model against ordinary footage. Requiring a prior, and COLMAP's fixed guess of
  1.2 times the longest side, were the alternatives. *(Amended 2026-10-16, decided with the repo owner
  while planning sub-slice 10: the two-view score is replaced by three starts. On rendered footage
  through OpenCV every candidate explained the same matches to within a few, since two frames of a
  path aimed at one object barely constrain a focal length; the stand-ins had hidden it by proposing
  only true poses. The bundle adjustment converges from about 0.7 to 3 times the true focal length, and
  a start below that ends in a wrong minimum, so three starts at geometric thirds of the sweep's range
  cover it. Each is reconstructed and adjusted, and the lowest final cost per residual wins, since a
  wrong minimum costs three to eight times a right one. Scoring the widest pairs and COLMAP's fixed
  prior were the alternatives. Built in sub-slice 10c; WORK.md's sub-slice 10 plan holds the
  numbers.)* *(Built 2026-10-16 with one addition, found by its own tests: the starts are ranked
  first by the frames their adjustment kept, as seeds are by the frames their placement reaches, and
  only then by cost. A cost per residual compares fits of the same footage only: over a stand-in scene
  with eight wrong matches, a start twice the true focal length reconstructed three frames of nineteen
  at 0.45 per residual, against the right start's 0.87 over seventeen, and cost alone chose it.)*
  *(Amended 2026-10-19, with the second adjustment below: a start is ranked by its FIRST adjustment.
  The second leaves out whatever its own start's result does not explain, so from a wrong minimum it
  keeps what agrees with that minimum and fits it about as closely: repeated three times on rendered
  board footage, the long start kept 56% of the observations at 0.32 per residual against the right
  start's 0.34, and would have won.)*
- **The principal point is held by default**, at the image centre or at an imported model's, because
  self-calibration conditions it poorly. A setting of the method frees it. *(Reversed 2026-10-16,
  decided with the repo owner while planning sub-slice 10: it is FREED by default. On rendered footage
  a held principal point blinds Ready: the held-out frames' poses absorb its offset, so a model whose
  ray mapping was 10 to 18 px out was Ready, while freed it came back within 1.8 px with a resampled
  variation inside Ready's bound. Footage that conditions it poorly shows that variation and is
  Exploratory, which is what the stability criterion is for. A setting still holds it. Built in
  sub-slice 10b.)* *(Measured 2026-10-18, sub-slice 10e: not always. Freed, the principal point
  still came back 2 to 4 px off on some footage while the verdict was Ready: at f = 1000 on 1280 x
  720 up to 3.9 px off against a resampled variation of 1.7 to 2.0 mrad, and with a printed board in
  close views 2 to 4 px off on three seeds of six against 0.4 to 0.75 mrad. Each resample starts from
  the result, so the bootstrap measures the spread around a minimum and not whether it is the right
  one, and the held-out frames' poses still absorb the offset. Recorded for the repo owner in
  WORK.md's sub-slice 10e note, with the options.)* *(Amended 2026-10-19: the board's case is
  answered in the evidence, by a **second bundle adjustment**. Resampling frames and comparing the
  three starts were measured and refused first, since neither saw the wrong minimum; what did differ
  was the winner's cost per residual, 1.97 to 2.07 against a right seed's 1.68. So the adjustment runs
  again from its result without the observations whose whitened residual exceeds the loss scale, the
  definition of an outlier every method shares, started through the same routine as a bootstrap
  resample. On the six board seeds the model came back 0.21 to 0.55 px out on average where it had been
  0.30 to 4.43, and on the plain scene through two lenses it improved on every seed; through the
  strongly distorted 1080p lens it moved the other way, 0.90 to 1.25 px, still Ready. It does not
  answer the HD lens, which sees no board: 1.45 to 3.56 px over three seeds, Ready, against 1.5 to 4.7
  before. One pass rather than iterating to convergence: further rounds moved the right minimum by
  about a tenth of a pixel either way, and each round lets a wrong start shed more of what disagrees
  with it. WORK.md's slice 4, *After sub-slice 10e*, holds the numbers.)* *(Measured and refused
  2026-10-19 for the HD lens: a covariance from the refiner. The model's marginal covariance from the
  final adjustment agrees with the landmark bootstrap, 0.95 mrad on the HD lens against errors up to 3
  mrad, and understates the error the same way through the lenses and the board footage (errors of 2 to
  5 of its standard deviations). Both inherit the features' stated noise, which understates SIFT's
  error 1.5 to 3.5 times on rendered footage, most for the smallest features. The observations carry no
  systematic bias, so the HD lens's error is imprecision the stated noise hides; what would let a
  precision measure see it is an honest feature covariance.)* A **held parameter** is not
  estimated, so it has no resampled variation. The report says it was held, and the fitness criterion
  on it does not apply; its variation is never recorded as zero. *(Scoped 2026-10-13, while planning
  sub-slice 2:)* a held parameter is part of an estimate. A model held whole under HoldAndValidate
  estimated nothing, so its resampling evidence stays unavailable and it is at most Exploratory; read
  without that limit, the rule would exempt every criterion and let a held model reach Ready. So the
  report says "held" inside its resampling evidence, never in place of it.
- **The refiner estimates the model, through the one projection implementation.** The refiner seam
  moves from `lain::camera::registration` to `lain::camera::refinement`, so calibration does not depend
  on its sibling: the precedent is the board pose seam, which moved to `lain::camera::board` for the
  same reason. The seam then gains cameras that share a model, and models with free parameters
  ([ADR-0017](0017-ceres-for-registration-refinement.md), amended). Registration never frees one, so
  the exclusion of joint intrinsic and extrinsic registration stands. The projection kernels become
  scalar-generic in their intrinsics and coefficients as well as their point *(built 2026-10-13: the
  models hold doubles, and their formulas are static templates over a coefficient array)*. A backend
  declares the models it can estimate, and a model whose automatic derivatives do not match finite
  differences is not declared.
- **Validation is on held-out frames.** A held-out frame is the targetless analogue of a held-out board
  view. It is kept out of the reconstruction, posed afterwards against the landmarks with the model held,
  and measured as angles through lain's projection *(built 2026-10-15, sub-slice 9b: over the
  observations within its pose's inlier angle, the rest counted and reported, since about 1.7% of
  extracted tracks are wrong along an epipolar line and one of them in a held-out frame would decide the
  angle alone)*. So the calibration fitness criteria keep their
  meaning in both methods (views, coverage, held-out angle, focal and principal-point variation), and
  the targetless profile, `reconstruction-targetless/1`, differs only in its numbers: a scene feature
  is located less precisely than a board corner. *(Amended 2026-10-19: it differs in one criterion as
  well. A scene feature is located to a fraction of a pixel whatever the lens, so a fixed held-out angle
  held a wide lens to a tighter bound than a long one: measured in sub-slice 10e, the residual was 0.6 to
  0.9 px from f = 250 to 1500, and an accurate model through f = 250 failed Ready on its angle alone.
  The targetless profile judges the held-out residual whitened by each observation's stated covariance
  instead, RMS per axis, at 4 for Ready and 8 for Exploratory, and leaves the angle unjudged; board
  calibration keeps the angle, since its corners rarely state a covariance. The right models measured
  1.2 to 2.9, since small features state less than their true error. Neither statistic sees a wrong
  model whose error the reconstruction absorbs, a principal point held 10 px off or a distortion model
  left out, which validated as well as the truth.)* An empty profile in a request is the method's own.
  The bootstrap resamples tracks. The reconstruction's poses and landmarks are diagnostics with an
  arbitrary scale, never part of the result.
- **The fallback is a graph of gates and small nodes**, the shape the 2026-08-16 review asked for. Each
  method stays one node. A method node's footage passes through a Gate. A `calibrationMethod` node says
  which methods the graph's `method` input includes, and a `nextAttempt` node switches the next
  method's gate from the earlier attempts' reports. A method that is not needed is therefore never run,
  by suppression (ADR-0007). A `calibrationAttempts` node combines the attempts made, in priority order,
  and names the accepted one. Editing one method's setting re-runs only that method's node. One
  fallback node, which would carry every setting of both methods, and an attempt chain threaded
  through the method nodes, were the alternatives. The method modules still contain no fallback
  policy. *(Amended 2026-10-19, sub-slice 11's plan: `nextAttempt` and `calibrationAttempts` take the
  reports on pins added as Merge's branches are, every one Optional, the order added the priority
  order, so neither node knows which method a pin carries. The attempts value derives the accepted one
  rather than storing its position. With no attempt made, as when the footage is missing upstream,
  `calibrationAttempts` produces nothing, so the absence propagates rather than reading as a failed
  fallback. Built 2026-10-20, sub-slice 11c: the two nodes share an `AttemptReportsNode` base, which
  reads the live reports in pin order once for both. Shipped 2026-10-20, sub-slice 12, as
  `apps/flowview/examples/calibrate-fallback.json`, run end to end on rendered footage. Its `method`
  is a boundary input the caller must bind (`--method board-then-targetless`): an unbound boundary
  input is empty, so `calibrationMethod`'s own default never applies through one, and the graph input
  defaults that would let it were deferred as a feature of their own.)*
- **Acceptance is stated once**: an attempt is accepted when it succeeded and is Ready under its own
  method's profile, decided by one shared function that the gating node and the combining node both
  call. A looser acceptance is a looser profile, not a second setting on the fallback.
- **No standalone extraction node yet.** Its trigger was "a second consumer", which assumed calibration
  would consume the fixed-rig extraction. Calibration consumes a different extraction, so each has one
  consumer. Restated: a second consumer of the same extraction, or a measured need to re-tune a solver
  setting on real footage without extracting again.
