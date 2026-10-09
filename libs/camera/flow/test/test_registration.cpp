// Camera node kinds and port types register whatever the backends (ADR-0016, amended): both are
// vocabulary, and a document naming one must load in a build that cannot run it. What a backend
// decides is what a menu OFFERS, availableCameraNodeKeys.
//
// Staged in ONE case from empty registries, adding a backend kind at a time, because registries only
// grow: no other case in this executable registers a backend, so the case starts with none.

#include "nullbackends.h"

#include <lain/camera/flow/register.h>
#include <lain/camera/registration/refiner.h>
#include <lain/core/factory.h>
#include <lain/flow/node.h>
#include <lain/flow/porttyperegistry.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera::testing::null;

namespace
{
	// Every camera kind, in display order: what registerCameraNodes must ALWAYS put in a factory.
	// Written out rather than read from cameraNodeKeys(), which is checked against it below.
	const std::vector<std::string> kAllKinds{"boardSpecification", "renderBoard", "detectBoard",
											 "calibrateCamera", "cameraModel", "registerCameras",
											 "registerCamerasTargetless"};

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
	CHECK(camera::cameraNodeKeys() == kAllKinds);
	CHECK(camera::availableCameraNodeKeys().empty());

	// A renderer alone: a board can be specified and drawn, and nothing else.
	camera::board::rendererRegistry().registerType<NullRenderer>("null");
	CHECK(camera::availableCameraNodeKeys() == Keys{"boardSpecification", "renderBoard"});

	// An estimator without a detector still cannot calibrate: calibration detects first.
	camera::calibration::estimatorRegistry().registerType<NullEstimator>("null");
	CHECK(camera::availableCameraNodeKeys() == Keys{"boardSpecification", "renderBoard"});

	// With a detector, detection and calibration, and the model a calibration produces.
	camera::board::detectorRegistry().registerType<NullDetector>("null");
	const Keys calibrating{"boardSpecification", "renderBoard", "detectBoard", "calibrateCamera", "cameraModel"};
	CHECK(camera::availableCameraNodeKeys() == calibrating);

	// Registration also needs a pose for each view and a refinement of the rig: neither alone will do.
	camera::board::poseSolverRegistry().registerType<NullPoseSolver>("null");
	CHECK(camera::availableCameraNodeKeys() == calibrating);
	camera::registration::refinerRegistry().registerType<NullRefiner>("null");
	Keys registering = calibrating;
	registering.push_back("registerCameras");
	CHECK(camera::availableCameraNodeKeys() == registering);

	// Targetless registration extracts, matches and poses cameras from what it matched: with a
	// refiner already here, none of the three alone will do. That it needs the refiner and no board
	// backend is test_targetlesskinds.cpp's, from a process with no board backend at all.
	camera::feature::extractorRegistry().registerType<NullExtractor>("null");
	CHECK(camera::availableCameraNodeKeys() == registering);
	camera::feature::matcherRegistry().registerType<NullMatcher>("null");
	CHECK(camera::availableCameraNodeKeys() == registering);
	camera::feature::geometryRegistry().registerType<NullGeometry>("null");
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
	CHECK(flow::portTypeRegistered("RegistrationReport"));
	// The list form, so a map can lift a camera model: registerCameras takes the rig's models as one.
	CHECK(flow::portTypeKey(typeid(std::vector<camera::CameraModel>)) == "ListOfCameraModel");
}
