#pragma once

#include <lain/core/factory.h>
#include <lain/flow/node.h>

#include <string>
#include <vector>

namespace lain::camera
{
	// The camera node kinds' factory keys. They are SERIALIZED: a saved document names a node by its
	// kind, and an unknown kind drops the node and its edges — which is why every build registers
	// them, backend or not (registerCameraNodes).
	inline constexpr const char* kBoardSpecificationKey = "boardSpecification";
	inline constexpr const char* kRenderBoardKey = "renderBoard";
	inline constexpr const char* kDetectBoardKey = "detectBoard";
	inline constexpr const char* kCalibrateCameraKey = "calibrateCamera";
	inline constexpr const char* kCameraModelKey = "cameraModel";
	inline constexpr const char* kRegisterCamerasKey = "registerCameras";

	// The camera node kinds this build can RUN, in display order: each kind is here only when the
	// backend it needs is registered. What a host's menu OFFERS, so nobody adds a node that can only
	// fail; it does not decide what loads (registerCameraNodes registers every kind).
	//
	// Asked of the backend registries at the time of the call, so call it after
	// registerCameraBackends().
	std::vector<std::string> availableCameraNodeKeys();

	// Register EVERY camera node kind into `factory`, whatever the backends (ADR-0016, amended). A
	// kind is vocabulary and a backend is a capability, video's rule: a document saved with camera
	// nodes loads whole in a build that cannot run them, and a node with no backend says so when it
	// runs ("this build has no board renderer", a Failed report with Rejection::NoBackend, ...)
	// rather than vanishing with its edges on load.
	void registerCameraNodes(core::Factory<flow::Node>& factory);

	// Register the camera payload types as addable port types (flow::registerPortType): a board
	// specification, a detection report, a calibration report, a camera model and a list of them (so
	// a map can lift one, ADR-0014), and a registration report. ALWAYS, whatever
	// the backends: a port type is vocabulary, and an unregistered one cannot be named on save, so a
	// boundary pin of that type would be dropped with its wiring.
	void registerCameraPortTypes();
} // namespace lain::camera
