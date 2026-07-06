# gui-mode parked behind `LAIN_ENABLE_GUI` pending an acm interop seam

---
Status: resolved — the seam landed; gui-mode restored and `LAIN_ENABLE_GUI` removed
---

## Resolution (superseded by implementation)

The interop seam was built: archimedes now exposes the raw handles ImGui needs through
a single opt-in `acm::interop` header (`acmVulkanInterop.h`), with the mainline acm API
kept Vulkan-free. `lain::gui`'s `context.cpp` was rewritten onto `acm::interop` +
dynamic-rendering ImGui init (no `VkRenderPass`), gui-mode was verified opening,
rendering, and exiting cleanly on the live driver, and the temporary `LAIN_ENABLE_GUI`
gate was removed — flowview builds its gui-mode window unconditionally again. The
context below is kept as the record of why the gate existed.

## Context

Updating `extern/archimedes` to its `develop` tip (the handle-style refactor +
Vulkan 1.3 baseline) **sealed archimedes' raw Vulkan handles**. The new public API
exposes exactly one: `acm::Instance::vulkanInstance()`. Everything `lain::gui`'s
`context.cpp` fed to ImGui's official `imgui_impl_vulkan` backend is now internal —
`vkDevice`, `vkQueue`, `getGPU()` (→`VkPhysicalDevice`), `vkCommandBuffer`,
`vkSampler`, `vkImageView` — and `SwapChain::vkRenderPass()` no longer exists at all
(acm renders through dynamic rendering). So `lain::gui` can no longer be repaired by
call-site edits: the backend it drives structurally needs handles acm deliberately
keeps to itself.

## Decision

For this archimedes update we **park `lain::gui` and flowview's gui-mode behind a new
CMake switch `LAIN_ENABLE_GUI` (default OFF)** and ship the rest of the update
(engine, `lain::app`, `flow-example`, flowview **cli-mode**) repaired and verified.
The gui source is left untouched — not compiled — so re-enabling it is a clean flip,
not a rewrite. With gui off, flowview opens no window and the app harness runs its
headless graph dump (the same path as `--headless`).

When gui-mode is restored, it will be via a **narrow acm interop seam** — a handful of
accessors on `Device`/`Texture`/`Sampler`/`CommandBuffer` plus the swapchain colour
`VkFormat`, mirroring the existing `vulkanInstance()` precedent — and ImGui's
dynamic-rendering init (`UseDynamicRendering = true` +
`PipelineInfoMain.PipelineRenderingCreateInfo`, which replaces the defunct render
pass). It will **not** be an acm-native ImGui backend.

## Considered options

- **Interop seam in archimedes (the chosen path forward).** Keeps ImGui's official
  backend (so scissor/clipping, per-frame dynamic vertex buffers, and font handling
  stay ImGui's job) and honours the locked "do not hand-write an ImGui backend"
  decision. Cost: a deliberate public-API addition to archimedes. Deferred to a
  follow-up because it expands scope into a second repo.
- **acm-native ImGui renderer (rejected — dominated).** Feed `ImDrawData` through
  acm's public `Pipeline`/`Buffer`/`DescriptorSet` path. It is ~500–700 lines +
  embedded SPIR-V, **still** requires an archimedes change (acm exposes no per-draw
  scissor, which ImGui clipping needs), **stalls the queue every frame** (acm's
  vertex `Buffer::write` is a synchronous staged upload — a load-time tool, and there
  is no host-visible dynamic vertex path), needs uniform plumbing to stand in for the
  missing push constants, and directly violates the locked "do not hand-write an
  ImGui backend" decision. Worse than the seam on every axis.
- **Defer gui-mode; ship the rest (chosen for now).** Smallest, most reversible step;
  lets the update land and everything headless verify today, with the seam as a clean
  follow-up.

## Consequences

- flowview **gui-mode is dark** until the seam lands; `LAIN_ENABLE_GUI` stays OFF and
  building with it ON will not compile (the sealed handles). cli-mode is unaffected.
- The follow-up is well-scoped: add the interop seam to archimedes, switch
  `context.cpp` to the six renamed accessors + dynamic-rendering ImGui init, flip
  `LAIN_ENABLE_GUI` ON. One runtime detail to confirm then: ImGui wants
  `VK_KHR_dynamic_rendering` "explicitly enabled even on 1.3"; acm enables the
  core-1.3 `dynamicRendering` feature, which is normally sufficient.
- CLAUDE.md's descriptions of the gui/flowview Vulkan wiring (`vkRenderPass`,
  `vkInstance`, raw-handle previews) now describe the *parked* pre-seal design.
