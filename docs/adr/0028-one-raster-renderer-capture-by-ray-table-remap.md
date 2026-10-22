# One raster renderer; a capture render is a remap through lain's own camera model

---
Status: accepted (designed 2026-10-21, Milestone 15; nothing built yet)
---

Milestone 15's rig design app needs two kinds of image. The **viewport** is the designer's own
view of a rig plan: interactive, a pinhole, a preview. A **capture render** is what one placed
camera records, and it becomes a synthetic capture's pixels, scored against that capture's ground
truth. A capture render must therefore project through the realised lain camera model exactly as
calibration and registration do, all six distortion models included. GPU rasterisation cannot do
that directly, because a triangle's edges are straight and distortion bends them.

## Decision

**One GPU raster renderer, `lain::render` over archimedes, serves both views. A capture render
rasterises the scene into an intermediate image and then remaps it through a ray table that lain's
own `unproject` builds on the CPU. The camera model is never ported to a shader.**

- **The viewport** rasterises through the user's pinhole, with no distortion, ever. It is a
  preview, and nothing it draws is written into a capture. Distortion belongs only to a camera's
  view; that was the repo owner's observation, and it is what makes one renderer enough.
- **A capture render** has three steps:
  1. Rasterise the same scene, with the same shading, into an intermediate image that covers the
     camera's field of view: a wide pinhole for ordinary lenses, a cubemap past 180°.
  2. Build a **ray table** on the CPU: lain's `unproject` gives a direction for each output pixel's
     sub-samples.
  3. A compute pass samples the intermediate image along each ray and averages s × s rays per pixel,
     which integrates the pixel's footprint as `libs/camera/test/texturedscene.h` does on the CPU.
- **The remap does not bias the ground truth.** A scene point still lands at `project(X)` under
  the realised model. What the remap costs is a small low-pass on pixel values, kept below the
  sensor's resolution by making the intermediate image finer than the camera's angular resolution.
  This is measured, not argued: board corners rendered through every distortion model are
  detected and compared with lain's projection of the truth. M9's CPU renderer reaches about
  0.07 px on that test, which sets the target.
- **Shading is written once, in GLSL**, for both views.
- **CI renders on the CPU through lavapipe** (Mesa's software Vulkan driver, MIT), with a Linux leg
  that installs `mesa-vulkan-drivers`. Without it, nothing that writes a capture could be tested,
  because CI has no GPU.
- **Captures are reproducible on one device, not byte-identical across devices.** M9 already lives
  with this, because OpenCV's SIFT finds different features on macOS and Windows from the same
  pixels. Tests over captures assert against ground truth with tolerances, never against pixel
  bytes. The sensor model's noise is outside this caveat: it is applied in C++ after readback
  ([ADR-0029](0029-rig-plans-are-nominal-captures-are-seeded-realisations.md)).
- **`lain::render` knows nothing about rig plans.** It draws a render scene of meshes, materials
  and lights; a rig plan's realisation at one frame is turned into one. flowview could use the same
  renderer for a 3D preview.
- **`texturedscene.h` stays test code**, so the M9 camera tests remain GPU-free. It gives up its
  procedural texture to the library, so that texture has one implementation.

## Considered options

- **A CPU ray-caster for captures, with a GPU raster viewport.** This was the first
  recommendation: exact rays, deterministic, CI-testable with no driver. Refused for duplication:
  the shading would be written twice, in C++ and GLSL, and the two would drift. It would also be
  slow at the large-rig target (100 cameras × 40 frames at 8K is hours on a CPU).
- **A GPU compute ray-tracer for captures.** Exact rays and fast, but it needs a BVH on the GPU
  plus a second visibility path beside the raster one, and still shades twice.
- **Porting the camera models to GLSL.** Refused by ADR-0016's one implementation of projection: a
  second copy is a second answer to where a point lands.
- **A CPU backend inside archimedes.** That would be a software rasteriser plus a SPIR-V
  interpreter, which is what lavapipe and SwiftShader already are. A CPU fallback for archimedes
  when no driver exists is worth its own planning pass, in archimedes, as archimedes work; it is
  recorded as a follow-up, not decided here.

## Consequences

- A capture render costs one raster pass, or six for a cubemap, plus one compute pass per camera
  frame. A camera is rendered at a time and read back, so GPU memory is bounded by the largest
  camera, whatever the rig's size.
- Shadows, area lights and physically based materials arrive as shader work in one place, for
  both views.
