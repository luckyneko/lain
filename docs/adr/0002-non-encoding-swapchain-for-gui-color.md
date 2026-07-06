# Non-encoding swapchain: GUI colour correctness independent of surface format

---
Status: accepted
---

ImGui authors its vertex colours already in sRGB and writes them to the framebuffer
untouched. Against acm's default sRGB-format swapchain the hardware applies a *second*
linear→sRGB encode on store, so the GUI renders washed out (double encoding). We make the
swapchain **non-encoding** — everything written to it is already display-ready sRGB bytes,
and the driver does not encode on store — so ImGui's colours pass through correct. This
holds the invariant **surface-agnostic GUI correctness**: the GUI is right whatever format
acm picks.

## Decision

- **Prefer a non-encoding (UNORM-format) surface — set in archimedes' default.**
  `acm::SurfacePreferences` now ranks `B8G8R8A8_Unorm` / `R8G8B8A8_Unorm` (colorspace
  `SrgbNonlinear`, BGRA first as the native drawable format) as *the default*, so both
  `surfaceOptions` call sites in `lain::app`'s `application.cpp` (window creation *and*
  device selection) inherit it with **no lain code change**. The swapchain stores raw bytes;
  its `SrgbNonlinear` colorspace still tells the compositor to read them as sRGB. No shader
  change in lain; the stock `imgui_impl_vulkan` backend is used unmodified. The decision
  lives in archimedes because the double-encode is a general Vulkan/ImGui gotcha, not a lain
  quirk — archimedes' own UNORM-textured testbed examples were washed out too, and the flip
  fixes them (see the archimedes commit "Default to a non-encoding (UNORM) swapchain
  surface"). Only UNORM formats are listed, so a surface offering none fails loudly rather
  than degrading to washout.
- **Producers encode to display-ready sRGB themselves.** The GUI does so natively.
  **General rendering** (non-GUI, linear-lit) must encode linear→sRGB in-shader before
  writing to the swapchain — it forgoes hardware auto-encode by design.
- **Preview textures are unchanged** (`R8G8B8A8_Unorm`): sampled without decode and written
  raw, an inspector preview is a faithful byte-for-byte view of the image data — the correct
  semantic for an inspector.

## Considered options

- **Two passes on a mutable swapchain** — general rendering through the sRGB view (hardware
  auto-encode) + GUI through a UNORM alias view. The "purest" split and it keeps hardware
  encode, but it is exactly the shape of an open, unfixed MoltenVK bug
  ([#2261](https://github.com/KhronosGroup/MoltenVK/issues/2261)): two format-switched
  dynamic-rendering passes on one mutable image flicker on macOS. Rejected — the one
  combination broken on our only platform.
- **Gamma-correct ImGui in a patched backend shader** (single sRGB pass, 3D keeps hardware
  encode). Rejected: patches the ImGui backend (against lain's "don't hand-write the
  backend"), forces preview textures to sRGB views, and is not surface-agnostic (the shader
  must branch on the actual surface format).
- **Offscreen UNORM target + per-frame raw copy** to the swapchain. Rejected: an extra
  full-screen image per swapchain image and a copy each frame, for no gain over preferring a
  UNORM surface.

## Consequences

- The **surface-agnostic invariant is documented but not yet fully closed in code.** The
  fallback for a driver that offers *only* an sRGB surface — create the swapchain mutable
  (`VK_KHR_swapchain_mutable_format`) and render through a UNORM **alias view**, with
  `acm::interop::colorFormat` reporting the UNORM render format so ImGui's pipeline matches
  — is designed but deferred (dormant on MoltenVK, which offers a UNORM surface). It is an
  archimedes change when a target surface actually forces sRGB.
- Any future non-GUI content drawn into the swapchain **inherits the encode-in-shader
  contract**; it cannot rely on the framebuffer to encode.
- Verify on the live driver: the gradient inspector no longer looks washed out and the GUI
  greys match ImGui's dark theme.
