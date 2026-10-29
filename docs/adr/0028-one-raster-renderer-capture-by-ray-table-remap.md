# One raster renderer; a capture render is a remap through lain's own camera model

---
Status: accepted (designed 2026-10-21, Milestone 15). Amended 2026-10-22: CI renders on archimedes'
CPU device (SwiftShader), not lavapipe. Amended 2026-10-24: the renderer is built (slice 3), lain's
pin carries the CPU device, and what running on it found is under *Consequences*. Amended 2026-10-24
again, planning slice 4: the ray table is a pixel-corner lattice, the intermediate image is chosen by
texel count, a capture keeps to Vulkan's guaranteed limits, and the 0.07 px target was a pinhole's.
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
  *(Amended 2026-10-28, planning M15 slice 8, decided with the repo owner: the viewport draws the rig
  plan **as planned**, meaning nominal poses, nominal models and frustums through those models, so
  that what the gizmo edits is what is drawn. A realisation is seen by looking through a camera
  (slice 9), the one view that already chooses between nominal and realised. A plan with a problem
  still draws: the viewport builds its scene from each object's own planned pose rather than through
  `realise()`, which refuses the whole plan on any problem.)* *(Amended 2026-10-28, building slice 8f:
  a room is drawn as a dollhouse, each of its surfaces the eye is outside of left out, since the
  renderer lights both faces of a surface and a room framed whole from outside otherwise hides the rig.
  A capture never cuts one: the cutaway is the Viewport's alone.)*
- **A capture render** has three steps:
  1. Rasterise the same scene, with the same shading, into an intermediate image that covers the
     camera's field of view: a wide pinhole for ordinary lenses, a cubemap past 180°.
     *(Amended 2026-10-24: no lain model reaches 180°, since every one needs Z > 0, but a pinhole's
     tan φ makes one face wasteful long before. So the intermediate image is the front face alone or
     the cube's faces, front and four sides, whichever takes fewer texels at the density below. Each
     face is cut into tiles of at most 4096 a side.)*
  2. Build a **ray table** on the CPU: lain's `unproject` gives a direction for each output pixel's
     sub-samples. *(Amended 2026-10-24, decided with the repo owner: for every pixel CORNER, and a
     sub-sample's direction is its pixel's four corner directions blended. A direction per sub-sample
     is 16× larger at s = 4 (6.4 GB at 8K), and M9's `distortedView` already interpolates on an 8 px
     grid. The blend is the one approximation of the model a capture makes. The table measures it,
     projecting each pixel centre's blended ray back through the model, and refuses a table past a
     tolerance (0.01 px by default). Measured on strongly distorted cameras of all six models at
     640×480: 2.5e-4 to 2.4e-3 px.)*
  3. A compute pass samples the intermediate image along each ray and averages s × s rays per pixel,
     which integrates the pixel's footprint as `libs/camera/test/texturedscene.h` does on the CPU.
- **The remap does not bias the ground truth.** A scene point still lands at `project(X)` under
  the realised model. What the remap costs is a small low-pass on pixel values, kept below the
  sensor's resolution by making the intermediate image finer than the camera's angular resolution.
  This is measured, not argued: board corners rendered through every distortion model are
  detected and compared with lain's projection of the truth. M9's CPU renderer reaches about
  0.07 px on that test, which sets the target. *(Corrected 2026-10-24: that 0.068 px was measured
  through a pinhole only, by an OpenCV warp of the board (`test_charuco.cpp`'s angled scene), and no
  test detected a board through the other five models. M15 slice 4 measures each model's own CPU
  baseline with `texturedscene.h` and judges both renderers against the truth, since the two
  average light differently: the CPU in 8-bit sRGB codes, the GPU in linear light.)* *(Measured
  2026-10-24, M15 sub-slice 4c: through all six models, with the board ahead and toward a corner, the
  capture's mean corner error is 0.068 to 0.086 px and the CPU baseline's 0.067 to 0.099, on Metal and
  on the CPU device alike. M9's own pinhole scene through the capture pass gives 0.067 px against
  M9's 0.068. The claim holds. The rays a pixel decide the result, one ray aliasing and four reaching
  the baseline; at four the oversampling barely matters for a board, which is texture.)*
- **"Finer than the camera's angular resolution" is a stated rule** *(added 2026-10-24)*: every step
  of one camera pixel, in any direction, spans at least m texels of the intermediate image (m = 2 by
  default). That is the least singular value of the pixel-to-face Jacobian, taken per pixel. The
  cube's faces share one focal length, so neither the density nor a texture's mip level steps at a
  seam.
- **Shading is written once, in GLSL**, for both views.
- **CI renders on archimedes' CPU device**: SwiftShader (Apache-2.0), a CPU implementation of
  Vulkan 1.3 that archimedes stages beside every runnable target and loads directly, through
  `VK_LUNARG_direct_driver_loading`, so no environment or registry is involved (archimedes PRs #1
  and #2). Without it, nothing that writes a capture could be tested, because CI has no GPU.
  *(Amended 2026-10-22. This said lavapipe, Mesa's software driver, installed on one Linux leg with
  `apt`. archimedes' device covers Linux, Windows and macOS with no install step and runs the same
  SPIR-V, so every CI leg can render. Verified against the PRs' head on macOS: lain builds
  unchanged, the default ranking still picks the hardware GPU, and with the hardware driver hidden
  `lain::app`'s headless path picks SwiftShader and reads a float texture back exactly.)*
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
  interpreter, which is what lavapipe and SwiftShader already are. *(Resolved 2026-10-22 in
  archimedes' own planning pass, its ADR-0001: archimedes stages SwiftShader as a CPU device and
  keeps one compile-time Vulkan backend.)*

## Consequences

- A capture render costs one raster pass, or six for a cubemap, plus one compute pass per camera
  frame. A camera is rendered at a time and read back, so GPU memory is bounded by the largest
  camera, whatever the rig's size.
  - *Amended 2026-10-25 (M15 slice 5d), on what that order costs.* One camera at a time means one ray
    table on the device at a time. Each holds a tile texture of up to 4096² RGBA32F (256 MB), so
    keeping every camera's table resident would grow GPU memory with the rig. So a capture streams
    **camera by camera**, and each frame's scene reaches the device once per camera.
    - `Renderer::update(GpuScene&, Scene)` makes that cheap. A mesh or texture the next scene names by
      the same `shared_ptr` keeps its device copy. Everything else (materials, instances, lights,
      descriptor sets) is rewritten, under a new serial, so a viewport rebuilds its sets as for a new
      upload.
    - `rigplan::scene` builds every mesh a plan's own geometry makes once, in `SceneAssets`, so a frame
      costs only what moved.
    - Decided with the repo owner, over re-uploading every scene whole and measuring first, and over
      every table resident.
- Shadows, area lights and physically based materials arrive as shader work in one place, for
  both views.
- **The CPU device runs its own worker pool**, beside lain's one process pool (ADR-0024). Measured
  on 14 logical cores: SwiftShader adds 15 threads beside lain's 13 workers when uncapped, and 3
  with a cap of 2. `lain::app` passes archimedes' `RuntimeConfig::cpuDeviceThreads` when it sets
  up Vulkan, after `--threads` is parsed; how the cores are split is M15's decision.
  - *Measured 2026-10-26 (M15 slice 5e), 1080p, 30 camera-frames, 14 logical cores (10 performance):*
    on the CPU device a capture takes about 260 ms a camera-frame, and the pool's recording and
    encoding hide entirely behind it, so the pool is idle for most of a capture. The device's own
    threads: 4 took 10.6 s, 7 (the half 3d chose) 8.0 s, 10 took 6.2 s, 14 took 6.3 s. On Metal a
    capture takes about 7 ms and encoding is the bottleneck instead. So giving the CPU device more
    of the cores during a capture would save about a fifth; the split is unchanged, for the repo
    owner to decide.
- **A capture keeps to Vulkan's guaranteed limits** *(added 2026-10-24, decided with the repo owner)*.
  archimedes exposes no device limits, so a tile is at most 4096 a side and a band of the ray table
  at most 2^27 bytes. A capture is then tiled and banded the same way on every device, and only
  rasterisation differs. archimedes' `Sampled` finish orders only the fragment stage, so a capture
  renders a tile with `CopySrc`, transitions it to `General`, and reads it in compute with
  `imageLoad`, blending bilinearly by hand, since linear filtering of RGBA32F is not guaranteed. Both
  archimedes changes, limit queries and a compute-visible `Sampled` finish, are deferred with a
  trigger in WORK.md's Outstanding index. *(Found building it, 2026-10-24: that chain in one command
  buffer did not order the remap's reads after the drawing on MoltenVK or SwiftShader, so a capture
  draws each tile and remaps it in two waited submissions.)*
- **A silhouette is placed to half a texel** *(measured 2026-10-24)*. The face pass takes one sample
  a texel, so a geometric edge lands within ±0.5 texel, a quarter of a camera pixel at m = 2; at a
  square's edges the coverage differs from the exact area by up to 0.34. The claim that the remap
  biases no ground truth holds for texture features, a board on a sheet among them, and not for
  silhouettes. MSAA in the face pass is deferred with a trigger.
- **archimedes' renderer shapes three choices.** Its depth attachment is discarded after a pass,
  so a camera's depth for the visibility map is rendered as distance into an R32F colour target.
  It has no cube or array-layer texture, so an intermediate image past 180° is six 2D textures in
  a descriptor array. Neither SwiftShader nor MoltenVK has `wideLines`, so overlay lines are 1 px.
- **What the CPU device found, once lain ran on it (2026-10-24, slice 3d).** Every `[gpu]` case runs
  on each kind of device the machine offers, and three differences showed:
  - its filtered sampling of 8-bit texels is about 1e-4 short of exact, so a material with no
    texture ignores what it samples rather than multiplying by a white one;
  - it draws a triangle's depth wrong when a vertex lies almost in the camera's plane (a camera
    flush on a wall puts that wall's corners there), so the vertex shader moves such a vertex a few
    millimetres along the camera's axis first (`offPlane`);
  - a 1 px line lying on a surface seen obliquely loses some of its multisampled coverage to it, on
    both devices, whatever a constant depth pull does, so overlay lines are for what is in the air,
    and a grid on the floor is drawn as its own pass. *(Built 2026-10-28, M15 sub-slice 8b, as
    `render::Grid`: drawn in shading, never as lines. A surface lying on the grid's plane mixes the
    lines into its own colour. A quad on the plane, drawn after the scene, depth-tested, pushed back a
    hair so a surface on the plane always wins, and blended, draws them elsewhere. Both mix in the
    display encoding, so on the oblique floor the two draw one line to within 0.0053 of coverage on
    Metal and 0.032 on the CPU device. A capture's frame constants carry no grid, so a capture never
    draws one.)*
  The core split is half the logical cores to the CPU device, the pool keeping its own workers, and
  `--device gpu|cpu` chooses the kind of device.
