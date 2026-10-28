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
#include <optional>
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
	// line lying on a surface facing the camera is drawn on it, by a small pull toward the camera; one on
	// a surface seen obliquely loses some of its multisampled coverage to it (shaders/lines.vert), so a
	// grid on a floor wants a pass of its own.
	struct Segment
	{
		math::Vec3d from{0.0, 0.0, 0.0};
		math::Vec3d to{0.0, 0.0, 0.0};
		image::ColorRGBf colour{1.0f, 1.0f, 1.0f};
	};

	// A reference grid on a plane, drawn into a viewport and never into a capture: a rig plan's floor,
	// say. Lines every `spacing` metres along both axes, a major one every `majorEvery`, out to `extent`
	// from the origin, each 1 px wide and anti-aliased.
	//
	// Drawn in shading, never as Segments, since a 1 px line on an obliquely seen surface loses most of
	// its multisampled coverage to it (shaders/lines.vert). A surface lying on the grid's plane draws
	// the grid's lines itself, as part of its colour; elsewhere a quad on the plane, drawn after the
	// scene, tested against its depth and pushed back a hair so a surface on the plane always wins,
	// blends the lines over whatever is behind it and nothing over what is in front.
	struct Grid
	{
		math::Vec3d origin{0.0, 0.0, 0.0};
		math::Vec3d u{1.0, 0.0, 0.0}; // the plane's two axes: unit length, at right angles
		math::Vec3d v{0.0, 1.0, 0.0};
		double spacing = 0.1;						 // metres between lines
		std::uint32_t majorEvery = 10;				 // every how many lines a major one is drawn; 0 for none
		math::Vec2d extent{10.0, 10.0};				 // metres it reaches from the origin, along u and along v
		image::ColorRGBf minor{0.25f, 0.25f, 0.25f}; // linear light
		image::ColorRGBf major{0.5f, 0.5f, 0.5f};
	};

	// Why `grid` cannot be drawn: axes that are not unit length and at right angles, a spacing or an
	// extent that is not positive and finite, a position that is not finite, or a colour that is
	// negative or not finite. Empty when it can be.
	std::vector<std::string> validate(const Grid& grid);

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

		// Records `scene` seen through `view`'s pinhole, with `grid` (if any) and `lines` over it, into
		// `cmd` for frame `frame` (the index acm::Renderer::render hands its pre-pass, below
		// MaxFramesInFlight), outside any rendering scope. That frame's texture() is then ready to
		// sample later in `cmd`. Returns why nothing was recorded, or empty. `scene` must have been
		// uploaded by this viewport's renderer, and must outlive the frame.
		std::vector<std::string> record(acm::CommandBuffer& cmd, std::uint32_t frame, const GpuScene& scene, const View& view,
										const std::vector<Segment>& lines, const std::optional<Grid>& grid);

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
