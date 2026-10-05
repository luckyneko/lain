// The OpenCV board renderer and detector, through lain's production seams (board::render and
// board::detect) after the plugin's own registerBackend(). Expected corner positions come from lain,
// never from OpenCV: its layout for a flat rendering, its projection for a camera view.

#include "syntheticview.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/opencv/register.h>
#include <lain/camera/projection.h>
#include <lain/image/convert.h>

#include <catch2/catch_test_macros.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::board;

namespace
{
	void ensureBackend()
	{
		static const bool once = []
		{
			opencv::registerBackend();
			return true;
		}();
		(void)once;
	}

	Pattern patternOf(std::uint32_t squaresX, std::uint32_t squaresY, CharucoLayout layout = CharucoLayout::Standard)
	{
		PatternParameters p;
		p.dictionary = Dictionary::Aruco5x5_100;
		p.squaresX = squaresX;
		p.squaresY = squaresY;
		p.markerToSquare = 0.75;
		p.layout = layout;
		PatternResult result = Pattern::create(p);
		REQUIRE(result.pattern.has_value());
		return *result.pattern;
	}

	Specification specOf(const Pattern& pattern)
	{
		Instance instance;
		instance.identity = "synthetic";
		instance.squareLength.value = core::Length::from<core::Length::Millimetres>(24.0);
		SpecificationResult result = Specification::create(pattern, instance);
		REQUIRE(result.specification.has_value());
		return *result.specification;
	}

	media::FrameRef frameRef()
	{
		media::FrameRef frame;
		frame.source = core::Uri{"/synthetic/board"};
		return frame;
	}

	// How far detected corners are from where `expected` says they are, in pixels.
	struct CornerError
	{
		double mean = 0;
		double worst = 0;
	};

	template <typename Expected>
	CornerError cornerError(const Observation& observation, const Expected& expected)
	{
		CornerError error;
		for (const FeatureObservation& f : observation.features)
		{
			const double distance = math::length(f.pixel - expected(f.id));
			error.mean += distance / double(observation.features.size());
			error.worst = std::max(error.worst, distance);
		}
		return error;
	}

	// Where the camera sees corner `id`, by lain's projection.
	math::Vec2d projected(const CameraModel& camera, const Specification& spec, const math::RigidTransformd& pose,
						  std::uint32_t id)
	{
		const math::Vec3d p = pose.apply(*spec.cornerPosition(id));
		const Projection<double> pixel = project(camera, p.x, p.y, p.z);
		REQUIRE(pixel.ok());
		return {pixel.u, pixel.v};
	}

	struct Scene
	{
		Specification spec;
		CameraModel camera;
		math::RigidTransformd pose;
		image::Image image;
	};

	Scene angledScene()
	{
		ensureBackend();
		const Specification spec = specOf(patternOf(7, 5));
		const std::optional<Rendering> rendering = render(spec.pattern(), RenderRequest{80, 20}).rendering;
		REQUIRE(rendering.has_value());
		const ModelResult camera = CameraModel::create(testing::pinhole());
		REQUIRE(camera.model.has_value());
		const math::RigidTransformd pose = testing::cameraFromBoard(spec, 0.35, 0.35, -0.2);
		return {spec, *camera.model, pose, testing::view(*rendering, spec, testing::pinhole(), pose)};
	}
} // namespace

TEST_CASE("the plugin registers a renderer and a detector", "[camera][opencv]")
{
	ensureBackend();
	CHECK(canRender());
	CHECK(canDetect());
}

TEST_CASE("a rendered board is detected where lain's layout puts its corners", "[camera][opencv]")
{
	// Flat and unwarped, so the truth is exact: corner id sits where lain's board frame says, scaled
	// to pixels, with the board's top-left corner at raster coordinate margin - 0.5. This pins OpenCV's
	// corner ids to lain's layout.
	ensureBackend();
	const Specification spec = specOf(patternOf(7, 5));
	const std::optional<Rendering> rendering = render(spec.pattern(), RenderRequest{80, 20}).rendering;
	REQUIRE(rendering.has_value());
	CHECK(rendering->raster.width() == 7 * 80 + 40);

	const DetectionReport report = detect(rendering->raster, frameRef(), spec);
	REQUIRE(report.status == DetectionStatus::Detected);
	REQUIRE(report.observation->features.size() == 24);
	const double pixelsPerMetre = 80 / 0.024;
	const CornerError error = cornerError(*report.observation,
										  [&](std::uint32_t id)
										  {
											  const math::Vec3d board = *spec.cornerPosition(id);
											  return math::Vec2d{board.x * pixelsPerMetre + 19.5, board.y * pixelsPerMetre + 19.5};
										  });
	INFO("corner error: mean " << error.mean << " px, worst " << error.worst << " px");
	CHECK(error.worst < 1e-6); // measured 6e-14: a flat black-and-white junction is found exactly
	CHECK(report.provenance.backend == "opencv");
	CHECK(report.observation->pattern == spec.pattern().fingerprint());
}

TEST_CASE("a board seen at an angle is detected where lain projects it", "[camera][opencv]")
{
	const Scene scene = angledScene();
	const DetectionReport report = detect(scene.image, frameRef(), scene.spec);
	REQUIRE(report.status == DetectionStatus::Detected);
	CHECK(report.refinement == RefinementResolution::Native);
	CHECK(report.transform.scaleX == 1.0);
	const CornerError error = cornerError(*report.observation, [&](std::uint32_t id)
										  { return projected(scene.camera, scene.spec, scene.pose, id); });
	// Measured: mean 0.068 px, worst 0.136 px, on squares about 60 px across seen 20 degrees off axis.
	INFO("corner error: mean " << error.mean << " px, worst " << error.worst << " px");
	CHECK(error.mean < 0.1);
	CHECK(error.worst < 0.25);
}

TEST_CASE("detection at a reduced scale still reports source pixels", "[camera][opencv]")
{
	const Scene scene = angledScene();
	const auto truth = [&](std::uint32_t id)
	{ return projected(scene.camera, scene.spec, scene.pose, id); };

	DetectionRequest request;
	request.scale = LongestSide{480}; // half of 960

	SECTION("refined against native pixels")
	{
		const DetectionReport report = detect(scene.image, frameRef(), scene.spec, request);
		REQUIRE(report.status == DetectionStatus::Detected);
		CHECK(report.transform.scaleX == 0.5);
		CHECK(report.transform.scaleY == 0.5);
		CHECK(report.refinement == RefinementResolution::Native);
		CHECK(report.observation->image == ImageGeometry{960, 720}); // the source's, not the searched image's
		// Measured: mean 0.064 px, worst 0.100 px, which is native detection's accuracy. That is what
		// native refinement is for.
		const CornerError error = cornerError(*report.observation, truth);
		INFO("corner error: mean " << error.mean << " px, worst " << error.worst << " px");
		CHECK(error.mean < 0.1);
		CHECK(error.worst < 0.25);
	}
	SECTION("refined only at the reduced scale")
	{
		request.refineAtNativeResolution = false;
		const DetectionReport report = detect(scene.image, frameRef(), scene.spec, request);
		REQUIRE(report.status == DetectionStatus::Detected);
		CHECK(report.refinement == RefinementResolution::DetectionScale);
		// Measured: mean 0.18 px, worst 0.24 px. Coarser, and still in source pixels. Mapping pixel
		// corners rather than centres back to the source would add a 0.5 px offset per axis here, and
		// refinement against native pixels would hide it in the section above.
		const CornerError error = cornerError(*report.observation, truth);
		INFO("corner error: mean " << error.mean << " px, worst " << error.worst << " px");
		CHECK(error.mean < 0.3);
		CHECK(error.worst < 0.5);
	}
}

TEST_CASE("a frame with no board is a failed report that says why", "[camera][opencv]")
{
	ensureBackend();
	image::Image blank{640, 480, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
	std::fill(blank.data(), blank.data() + blank.byteSize(), std::uint8_t{128});
	const DetectionReport report = detect(blank, frameRef(), specOf(patternOf(7, 5)));
	CHECK(report.status == DetectionStatus::Failed);
	CHECK_FALSE(report.observation.has_value());
	CHECK(report.stats.markersFound == 0);
	REQUIRE_FALSE(report.rejections.empty());
	CHECK(report.rejections[0].reason == Rejection::NoMarkers);
}

TEST_CASE("the legacy layout is a different pattern from the standard one", "[camera][opencv]")
{
	// With an even number of rows the two layouts put the markers on different squares. A legacy
	// board detected as its own pattern is found whole; as the standard pattern, it is not.
	ensureBackend();
	const Pattern legacy = patternOf(6, 4, CharucoLayout::Legacy);
	const std::optional<Rendering> rendering = render(legacy, RenderRequest{80, 20}).rendering;
	REQUIRE(rendering.has_value());

	CHECK(detect(rendering->raster, frameRef(), specOf(legacy)).status == DetectionStatus::Detected);
	CHECK(detect(rendering->raster, frameRef(), specOf(patternOf(6, 4))).status != DetectionStatus::Detected);
	CHECK(legacy.fingerprint() != patternOf(6, 4).fingerprint());
}

TEST_CASE("a pattern's marker ids start where the pattern says", "[camera][opencv]")
{
	// A second board drawn from the same dictionary uses the next ids. Rendered and detected as
	// itself it is found; detected as the board whose ids start at 0, none of its markers belong.
	ensureBackend();
	PatternParameters p;
	p.dictionary = Dictionary::Aruco5x5_100;
	p.squaresX = 7;
	p.squaresY = 5;
	p.markerToSquare = 0.75;
	p.firstMarkerId = 17;
	const PatternResult second = Pattern::create(p);
	REQUIRE(second.pattern.has_value());
	const std::optional<Rendering> rendering = render(*second.pattern, RenderRequest{80, 20}).rendering;
	REQUIRE(rendering.has_value());

	CHECK(detect(rendering->raster, frameRef(), specOf(*second.pattern)).status == DetectionStatus::Detected);
	const DetectionReport asFirst = detect(rendering->raster, frameRef(), specOf(patternOf(7, 5)));
	CHECK(asFirst.status == DetectionStatus::Failed);
	CHECK(asFirst.stats.markersFound == 0);
}

TEST_CASE("every dictionary draws OpenCV's dictionary of that name", "[camera][opencv]")
{
	// The renderer and the detector share one mapping, so a board drawn from the wrong dictionary is
	// still detected as itself: only OpenCV's dictionary, named independently here, can tell. Each
	// lain dictionary is rendered, and its markers must be the ones that dictionary holds.
	ensureBackend();
	const std::pair<Dictionary, cv::aruco::PredefinedDictionaryType> named[] = {
		{Dictionary::Aruco4x4_50, cv::aruco::DICT_4X4_50},
		{Dictionary::Aruco4x4_100, cv::aruco::DICT_4X4_100},
		{Dictionary::Aruco4x4_250, cv::aruco::DICT_4X4_250},
		{Dictionary::Aruco4x4_1000, cv::aruco::DICT_4X4_1000},
		{Dictionary::Aruco5x5_50, cv::aruco::DICT_5X5_50},
		{Dictionary::Aruco5x5_100, cv::aruco::DICT_5X5_100},
		{Dictionary::Aruco5x5_250, cv::aruco::DICT_5X5_250},
		{Dictionary::Aruco5x5_1000, cv::aruco::DICT_5X5_1000},
		{Dictionary::Aruco6x6_50, cv::aruco::DICT_6X6_50},
		{Dictionary::Aruco6x6_100, cv::aruco::DICT_6X6_100},
		{Dictionary::Aruco6x6_250, cv::aruco::DICT_6X6_250},
		{Dictionary::Aruco6x6_1000, cv::aruco::DICT_6X6_1000},
		{Dictionary::Aruco7x7_50, cv::aruco::DICT_7X7_50},
		{Dictionary::Aruco7x7_100, cv::aruco::DICT_7X7_100},
		{Dictionary::Aruco7x7_250, cv::aruco::DICT_7X7_250},
		{Dictionary::Aruco7x7_1000, cv::aruco::DICT_7X7_1000},
	};
	for (const auto& [dictionary, opencvName] : named)
	{
		INFO(name(dictionary));
		PatternParameters p;
		p.dictionary = dictionary;
		p.squaresX = 5;
		p.squaresY = 4;
		p.markerToSquare = 0.75;
		// Ids from the top of the dictionary: the smaller dictionaries are prefixes of the larger
		// ones, so only an id beyond the smaller one's range tells DICT_4X4_50 from DICT_4X4_1000.
		p.firstMarkerId = markerCapacity(dictionary) - 10;
		const PatternResult pattern = Pattern::create(p);
		REQUIRE(pattern.pattern.has_value());
		const std::optional<Rendering> rendering = render(*pattern.pattern, RenderRequest{60, 20}).rendering;
		REQUIRE(rendering.has_value());

		const cv::Mat raster(rendering->raster.height(), rendering->raster.width(), CV_8UC1,
							 const_cast<std::uint8_t*>(rendering->raster.data()));
		std::vector<std::vector<cv::Point2f>> corners;
		std::vector<int> ids;
		cv::aruco::ArucoDetector(cv::aruco::getPredefinedDictionary(opencvName)).detectMarkers(raster, corners, ids);
		std::sort(ids.begin(), ids.end());
		std::vector<int> expected(10);
		std::iota(expected.begin(), expected.end(), int(p.firstMarkerId));
		CHECK(ids == expected);
	}
}

TEST_CASE("registering the plugin caps OpenCV's own thread pool", "[camera][opencv]")
{
	// One pool per process (ADR-0024): lain runs frames in parallel, OpenCV runs each serially.
	ensureBackend();
	CHECK(cv::getNumThreads() == 1);
}

TEST_CASE("a colour or 16-bit frame detects the same corners as grey", "[camera][opencv]")
{
	const Scene scene = angledScene();
	const DetectionReport grey = detect(scene.image, frameRef(), scene.spec);
	REQUIRE(grey.status == DetectionStatus::Detected);

	for (const image::PixelFormat format : {image::PixelFormat::RGB8, image::PixelFormat::RGBA8, image::PixelFormat::Gray16})
	{
		const image::Image converted = image::convert(scene.image, format);
		REQUIRE(converted.valid());
		const DetectionReport report = detect(converted, frameRef(), scene.spec);
		INFO("format " << int(format));
		REQUIRE(report.status == DetectionStatus::Detected);
		for (std::size_t i = 0; i < report.observation->features.size(); ++i)
			CHECK(math::length(report.observation->features[i].pixel - grey.observation->features[i].pixel) < 1e-3);
	}
}

TEST_CASE("detailed diagnostics keep rejected candidates; a summary does not", "[camera][opencv]")
{
	const Scene scene = angledScene();
	DetectionRequest request;
	CHECK_FALSE(detect(scene.image, frameRef(), scene.spec, request).evidence.has_value());
	request.detail = DetailLevel::Detailed;
	const DetectionReport detailed = detect(scene.image, frameRef(), scene.spec, request);
	REQUIRE(detailed.evidence.has_value());
	CHECK(detailed.evidence->rejectedMarkers.size() == detailed.stats.markersRejected);
}
