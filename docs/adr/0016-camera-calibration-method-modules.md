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

Registration follows the same optional-method structure as calibration:
`lain::camera::registration` owns the shared request, report, transforms, diagnostics, and fitness
contracts, while `lain::camera::registration::{board,targetless}` own the optional implementations.
Both return the same registration report; method selection and ordered, short-circuit fallback remain
graph orchestration concerns.

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
kinds; `availableCameraNodeKeys` still answers which can run, and flowview's menu offers only those,
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
