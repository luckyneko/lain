#pragma once

#include "lain/render/renderer.h"
#include "lain/render/view.h"

#include <lain/image/color.h>
#include <lain/image/image.h>
#include <lain/math/types.h>

#include <archimedes/acmCommandBuffer.h>
#include <archimedes/acmTexture.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lain::render
{
	namespace detail
	{
		struct ViewportState;
	} // namespace detail

	// A straight line drawn over a viewport, for what a designer sees but a camera never records:
	// frustums, cones, paths, the grid. In its own colour (linear light), unlit, 1 px wide whatever the
	// device (neither SwiftShader nor MoltenVK has wide lines), and hidden where a surface is nearer. A
	// line lying on a surface is drawn on it, by a small pull toward the camera (shaders/lines.vert).
	struct Segment
	{
		math::Vec3d from{0.0, 0.0, 0.0};
		math::Vec3d to{0.0, 0.0, 0.0};
		image::ColorRGBf colour{1.0f, 1.0f, 1.0f};
	};

	// The designer's own view of a scene (CONTEXT.md, "Viewport"), drawn on the device into a texture a
	// pane shows: lain's sRGB curve into 8-bit UNORM, as ImGui composes, multisampled where the device
	// allows, with overlay lines over the scene. It is recorded into a frame of a window's
	// acm::Renderer, before that frame's draw pass, and keeps what it writes per frame in flight, so
	// drawing it never waits for the device and two viewports never share a buffer. Its renderer must
	// outlive it.
	class Viewport
	{
	public:
		explicit Viewport(Renderer& renderer);
		Viewport(Viewport&&) noexcept;
		Viewport& operator=(Viewport&&) noexcept;
		~Viewport();

		// Why this viewport cannot draw (its renderer cannot, or the device refused its pipelines), or
		// empty when it can.
		const std::vector<std::string>& problems() const;

		// Records `scene` seen through `view`'s pinhole, with `lines` over it, into `cmd` for frame
		// `frame` (the index acm::Renderer::render hands its pre-pass, below MaxFramesInFlight), outside
		// any rendering scope. That frame's texture() is then ready to sample later in `cmd`. Returns
		// why nothing was recorded, or empty. `scene` must have been uploaded by this viewport's
		// renderer, and must outlive the frame.
		std::vector<std::string> record(acm::CommandBuffer& cmd, std::uint32_t frame, const GpuScene& scene, const View& view,
										const std::vector<Segment>& lines);

		// Frame `frame`'s picture, for a pane to show: the view's size, in shader-read layout. Invalid
		// before that frame's first record.
		const acm::Texture& texture(std::uint32_t frame) const;

		// Frame `frame`'s picture read back, RGBA8 tagged sRGB, waiting for the device: what a pane
		// shows of it. Invalid before that frame's first record.
		image::Image read(std::uint32_t frame);

	private:
		std::unique_ptr<detail::ViewportState> m;
	};
} // namespace lain::render
