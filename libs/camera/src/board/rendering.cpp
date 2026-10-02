#include "lain/camera/board/rendering.h"

#include <lain/string/format.h>

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

	// A result holding only why there is no rendering.
	static RenderResult refused(RenderProblem problem, std::string detail)
	{
		RenderResult result;
		result.diagnostics.push_back(RenderDiagnostic{problem, std::move(detail)});
		return result;
	}

	RenderResult render(const Pattern& pattern, const RenderRequest& request)
	{
		const std::vector<std::string> backends = rendererRegistry().keys();
		if (backends.empty())
			return refused(RenderProblem::NoBackend,
						   "this build has no board renderer (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (request.pixelsPerSquare == 0)
			return refused(RenderProblem::NoPixels, "cannot render a board at 0 pixels per square");

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
			return refused(RenderProblem::BackendMisbehaved,
						   string::format("the {} renderer did not produce a {}x{} 8-bit grey board", backends.front(),
										  width, height));
		}

		Rendering rendering;
		rendering.raster = std::move(raster);
		rendering.description = pattern.description() + "render pixelsPerSquare " + std::to_string(request.pixelsPerSquare) +
								"\nrender marginPixels " + std::to_string(request.marginPixels) + "\n";
		rendering.pattern = pattern.fingerprint();
		rendering.request = request;

		RenderResult result;
		result.rendering = std::move(rendering);
		return result;
	}
} // namespace lain::camera::board
