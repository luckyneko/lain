#pragma once

#include "lain/camera/board/pattern.h"

#include <lain/core/factory.h>
#include <lain/core/sha256.h>
#include <lain/image/image.h>

#include <cstdint>
#include <optional>
#include <string>

namespace lain::camera::board
{
	struct RenderRequest
	{
		std::uint32_t pixelsPerSquare = 100;
		std::uint32_t marginPixels = 0; // white border on every side
	};

	// A board pattern drawn: a deterministic raster plus a machine-readable description (CONTEXT.md,
	// "Board rendering"). It proves which pattern it is, and says nothing about the physical size a
	// printer will produce.
	struct Rendering
	{
		image::Image raster;		// 8-bit grey
		std::string description;	// the pattern's description, then the render's own parameters
		core::Sha256Digest pattern; // Pattern::fingerprint
		RenderRequest request;
	};

	// A board renderer backend, filling only the raster. The render() facade builds the rest, so the
	// description and the fingerprint never depend on which backend drew the board.
	class Renderer
	{
	public:
		virtual ~Renderer() = default;

		// The board, `pixelsPerSquare` per square plus the margin, 8-bit grey; an invalid image when
		// the backend cannot draw it.
		virtual image::Image raster(const Pattern& pattern, const RenderRequest& request) const = 0;
	};

	// The process-wide renderer registry, keyed by backend name ("opencv").
	core::Factory<Renderer>& rendererRegistry();

	bool canRender();

	// The pattern drawn by the registered backend, or nullopt (logged) when there is none, when the
	// request is degenerate, or when the backend fails.
	std::optional<Rendering> render(const Pattern& pattern, const RenderRequest& request = {});
} // namespace lain::camera::board
