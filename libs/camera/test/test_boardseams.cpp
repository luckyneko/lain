// The detect() and render() facades over a stand-in backend: what they fill in themselves, what they
// refuse, and what they normalise, whichever backend is behind them. The stand-ins are registered
// under their own keys; a build with the OpenCV plugin tests that backend in its own suite.

#include "testboard.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>

#include <catch2/catch_test_macros.hpp>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::board;
using namespace lain::camera::testing;

namespace
{
	// Reports three corners out of order, and the scale it was handed.
	class StandInDetector : public Detector
	{
	public:
		static inline double lastScale = 0;

		DetectionReport detect(const image::Image& image, const media::FrameRef& frame, const Specification& board,
							   const DetectionRequest&, double scale) const override
		{
			lastScale = scale;
			DetectionReport report;
			report.status = DetectionStatus::Partial;
			Observation observation;
			observation.frame = frame;
			observation.image = {std::uint32_t(image.width()), std::uint32_t(image.height())};
			observation.pattern = board.pattern().fingerprint();
			observation.features = {{7, {10.0, 20.0}, {}}, {2, {30.0, 40.0}, {}}, {5, {50.0, 60.0}, {}}};
			report.observation = observation;
			report.stats.cornersFound = 3;
			report.provenance = {"stand-in", "1"};
			return report;
		}
	};

	// Draws a blank board of the requested size, or the wrong size when asked to misbehave.
	class StandInRenderer : public Renderer
	{
	public:
		static inline bool misbehave = false;

		image::Image raster(const Pattern& pattern, const RenderRequest& request) const override
		{
			const PatternParameters& p = pattern.parameters();
			const int width = int(p.squaresX * request.pixelsPerSquare + 2 * request.marginPixels);
			const int height = int(p.squaresY * request.pixelsPerSquare + 2 * request.marginPixels);
			return image::Image{misbehave ? width + 1 : width, height, image::PixelFormat::Gray8};
		}
	};

	void registerStandIns()
	{
		static const bool once = []
		{
			detectorRegistry().registerType<StandInDetector>("stand-in");
			rendererRegistry().registerType<StandInRenderer>("stand-in");
			return true;
		}();
		(void)once;
	}

	media::FrameRef someFrame()
	{
		media::FrameRef frame;
		frame.source = core::Uri{"/footage/boards"};
		frame.ordinal = 12;
		return frame;
	}
} // namespace

TEST_CASE("a scale policy resolves to a factor in (0, 1]", "[camera][board]")
{
	const ImageGeometry uhd{3840, 2160};
	CHECK(resolveScale(NativeScale{}, uhd) == 1.0);
	CHECK(resolveScale(ScaleFactor{0.5}, uhd) == 0.5);
	CHECK(resolveScale(ScaleFactor{2.0}, uhd) == 1.0);
	CHECK(resolveScale(ScaleFactor{0.0}, uhd) == 1.0);
	CHECK(resolveScale(ScaleFactor{-1.0}, uhd) == 1.0);
	CHECK(resolveScale(LongestSide{1920}, uhd) == 0.5);
	CHECK(resolveScale(LongestSide{1920}, ImageGeometry{1280, 720}) == 1.0); // never enlarged
	CHECK(resolveScale(LongestSide{0}, uhd) == 1.0);
}

TEST_CASE("detect fills what every backend reports the same way", "[camera][board]")
{
	registerStandIns();
	REQUIRE(canDetect());

	DetectionRequest request;
	request.scale = LongestSide{32};
	const image::Image image{64, 48, image::PixelFormat::Gray8};
	const DetectionReport report = detect(image, someFrame(), specification(), request);

	// The facade resolved the policy for the backend...
	CHECK(StandInDetector::lastScale == 0.5);
	// ...recorded what was asked, and how many corners the pattern has...
	CHECK(std::holds_alternative<LongestSide>(report.request.scale));
	CHECK(report.stats.cornersExpected == 24);
	// ...and put the features in canonical order, whatever order the backend found them in.
	REQUIRE(report.observation.has_value());
	REQUIRE(report.observation->features.size() == 3);
	CHECK(report.observation->features[0].id == 2);
	CHECK(report.observation->features[1].id == 5);
	CHECK(report.observation->features[2].id == 7);
	CHECK(report.observation->frame == someFrame());
	CHECK(report.provenance.backend == "stand-in");
	CHECK(report.elapsed >= core::Time{});
}

TEST_CASE("render builds the description and fingerprint and checks the raster", "[camera][board]")
{
	registerStandIns();
	REQUIRE(canRender());
	const Pattern p = pattern();

	StandInRenderer::misbehave = false;
	const RenderResult result = render(p, RenderRequest{10, 3});
	REQUIRE(result.rendering.has_value());
	CHECK(result.diagnostics.empty());
	const std::optional<Rendering>& rendering = result.rendering;
	CHECK(rendering->raster.width() == 76);
	CHECK(rendering->raster.height() == 56);
	CHECK(rendering->pattern == p.fingerprint());
	CHECK(rendering->description == p.description() + "render pixelsPerSquare 10\nrender marginPixels 3\n");

	// A raster that is not the size its description claims is refused, not passed on — and says so.
	StandInRenderer::misbehave = true;
	const RenderResult misbehaved = render(p, RenderRequest{10, 3});
	StandInRenderer::misbehave = false;
	CHECK_FALSE(misbehaved.rendering.has_value());
	REQUIRE(misbehaved.diagnostics.size() == 1);
	CHECK(misbehaved.diagnostics[0].problem == RenderProblem::BackendMisbehaved);
	CHECK(misbehaved.diagnostics[0].detail == "the stand-in renderer did not produce a 76x56 8-bit grey board");

	const RenderResult empty = render(p, RenderRequest{0, 0});
	CHECK_FALSE(empty.rendering.has_value());
	REQUIRE(empty.diagnostics.size() == 1);
	CHECK(empty.diagnostics[0].problem == RenderProblem::NoPixels);
}
