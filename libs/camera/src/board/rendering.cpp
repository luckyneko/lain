#include "lain/camera/board/rendering.h"

#include <lain/log/log.h>

#include <string>

namespace lain::camera::board
{
	core::Factory<Renderer>& rendererRegistry()
	{
		static core::Factory<Renderer> registry;
		return registry;
	}

	bool canRender()
	{
		return !rendererRegistry().keys().empty();
	}

	std::optional<Rendering> render(const Pattern& pattern, const RenderRequest& request)
	{
		const std::vector<std::string> backends = rendererRegistry().keys();
		if (backends.empty())
		{
			log::error("camera::board: cannot render a board: this build has no board renderer "
					   "(configure with -DLAIN_CAMERA_OPENCV=ON)");
			return std::nullopt;
		}
		if (request.pixelsPerSquare == 0)
		{
			log::error("camera::board: cannot render a board at 0 pixels per square");
			return std::nullopt;
		}

		image::Image raster = rendererRegistry().create(backends.front())->raster(pattern, request);

		// The backend's whole contract, checked here rather than trusted: an 8-bit grey raster of
		// exactly the requested size. A rendering whose size is not what its description says would
		// put every feature in the wrong place.
		const PatternParameters& p = pattern.parameters();
		const long long width = (long long)p.squaresX * request.pixelsPerSquare + 2LL * request.marginPixels;
		const long long height = (long long)p.squaresY * request.pixelsPerSquare + 2LL * request.marginPixels;
		if (!raster.valid() || raster.pixelFormat() != image::PixelFormat::Gray8 || raster.width() != width ||
			raster.height() != height)
		{
			log::error("camera::board: the {} renderer did not produce a {}x{} 8-bit grey board", backends.front(), width,
					   height);
			return std::nullopt;
		}

		Rendering rendering;
		rendering.raster = std::move(raster);
		rendering.description = pattern.description() + "render pixelsPerSquare " + std::to_string(request.pixelsPerSquare) +
								"\nrender marginPixels " + std::to_string(request.marginPixels) + "\n";
		rendering.pattern = pattern.fingerprint();
		rendering.request = request;
		return rendering;
	}
} // namespace lain::camera::board
