// Camera node kinds and port types register whatever the backends (ADR-0016, amended): both are
// vocabulary, and a document naming one must load in a build that cannot run it. What a backend
// decides is what a menu OFFERS, availableCameraNodeKeys.
//
// Staged in ONE case from empty registries, adding a backend kind at a time, because registries only
// grow: no other case in this executable registers a backend, so the case starts with none.

#include <lain/camera/board/detection.h>
#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/flow/register.h>
#include <lain/core/factory.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lain;

namespace
{
	class NullRenderer : public camera::board::Renderer
	{
	public:
		image::Image raster(const camera::board::Pattern&, const camera::board::RenderRequest&) const override
		{
			return {};
		}
	};

	class NullDetector : public camera::board::Detector
	{
	public:
		camera::board::DetectionReport detect(const image::Image&, const media::FrameRef&,
											  const camera::board::Specification&, const camera::board::DetectionRequest&,
											  double) const override
		{
			return {};
		}
	};

	class NullEstimator : public camera::calibration::Estimator
	{
	public:
		camera::Provenance provenance() const override { return {"null", "1"}; }
		bool canEstimate(camera::DistortionModel) const override { return true; }
		camera::calibration::Estimate estimate(const camera::ImageGeometry&,
											   const std::vector<camera::board::Observation>&,
											   const camera::board::Specification&, camera::DistortionModel,
											   const std::optional<camera::CameraModelParameters>&) const override
		{
			return {};
		}
	};

	// Every camera kind, in display order: what registerCameraNodes must ALWAYS put in a factory.
	const std::vector<std::string> kAllKinds{"boardSpecification", "renderBoard", "detectBoard",
											 "calibrateCamera", "cameraModel"};

	// The kinds registerCameraNodes put in a fresh factory that it can build.
	std::vector<std::string> registered()
	{
		core::Factory<flow::Node> factory;
		camera::registerCameraNodes(factory);
		std::vector<std::string> keys;
		for (const std::string& key : kAllKinds)
		{
			if (factory.create(key) != nullptr)
				keys.push_back(key);
		}
		CHECK(factory.keys().size() == keys.size()); // and nothing else
		return keys;
	}
} // namespace

TEST_CASE("camera node kinds register always, and are offered as backends can run them", "[camera][flow]")
{
	using Keys = std::vector<std::string>;

	// No backend: every kind is still in the factory, so a camera document loads whole, but none is
	// offered, not even the board specification, which nothing could then use.
	CHECK(registered() == kAllKinds);
	CHECK(camera::availableCameraNodeKeys().empty());

	// A renderer alone: a board can be specified and drawn, and nothing else.
	camera::board::rendererRegistry().registerType<NullRenderer>("null");
	CHECK(camera::availableCameraNodeKeys() == Keys{"boardSpecification", "renderBoard"});

	// An estimator without a detector still cannot calibrate: calibration detects first.
	camera::calibration::estimatorRegistry().registerType<NullEstimator>("null");
	CHECK(camera::availableCameraNodeKeys() == Keys{"boardSpecification", "renderBoard"});

	// With a detector, detection and calibration, and the model a calibration produces.
	camera::board::detectorRegistry().registerType<NullDetector>("null");
	CHECK(camera::availableCameraNodeKeys() == kAllKinds);

	// Backends change what is offered, never what registers.
	CHECK(registered() == kAllKinds);
}

TEST_CASE("camera port types register whatever the backends", "[camera][flow]")
{
	// A port type is vocabulary: a boundary pin of one must load and save in a build that cannot
	// run a single camera node.
	camera::registerCameraPortTypes();
	CHECK(flow::portTypeRegistered("BoardSpecification"));
	CHECK(flow::portTypeRegistered("BoardDetectionReport"));
	CHECK(flow::portTypeRegistered("CalibrationReport"));
	CHECK(flow::portTypeRegistered("CameraModel"));
	CHECK(flow::portTypeKey(typeid(camera::CameraModel)) == "CameraModel");
}
